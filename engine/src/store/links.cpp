// Liens de partage reçus de Google Drive, Dropbox et OneDrive : lecture seule, sans compte (comme mega_share).
//  - Google Drive : la page publique du dossier (« embeddedfolderview ») donne ses éléments ; un fichier se lit par
//    drive.usercontent.google.com.
//  - Dropbox : un dossier partagé se télécharge en une archive ZIP (« dl=1 »), gardée en cache sur la console et lue
//    sur place ; un fichier se lit directement.
//  - OneDrive (comptes personnels) : l'accès anonyme aux partages (« u!<lien en base64> »), éléments et adresses de
//    lecture déjà signées.
#include "store/drives.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>

#include "core/fs.hpp"
#include "core/http_client.hpp"
#include "core/util.hpp"
#include "core/zip.hpp"
#include "store/mega_crypto.hpp"

namespace pbc::store {

namespace {

std::mutex g_mu;
std::string g_cache;  // dossier du cache (chemin hôte) : archives des dossiers Dropbox

struct LNode {
    std::string name, url;   // url : adresse de lecture (vide : dans l'archive)
    bool dir = false;
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
    int zip = -1;            // Dropbox : rang dans l'archive
    std::vector<std::string> kids;  // chemins des enfants
};
struct Tree {
    std::int64_t at = 0;
    std::string service, kind = "folder", title, zip_path;
    std::map<std::string, LNode> nodes;  // « / », « /Dossier », « /Dossier/fichier »
    std::vector<zip::Entry> entries;
};
std::map<std::string, Tree> g_trees;  // lien → arbre lu (gardé quelques minutes)

std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string trim(const std::string& in) { std::string s; for (char c : in) if (!std::isspace(static_cast<unsigned char>(c))) s += c; return s; }
std::string norm(const std::string& p) {
    std::string out;
    std::string part;
    auto flush = [&] { if (!part.empty() && part != ".") out += "/" + part; part.clear(); };
    for (char c : p) { if (c == '/') flush(); else part += c; }
    flush();
    return out.empty() ? "/" : out;
}
std::string join(const std::string& dir, const std::string& name) { return dir == "/" ? "/" + name : dir + "/" + name; }
void add_node(Tree& t, const std::string& parent, LNode n) {
    const std::string p = join(parent, n.name);
    if (t.nodes.count(p)) return;
    t.nodes[parent].kids.push_back(p);
    t.nodes[p] = std::move(n);
}
std::string query(const std::string& url, const std::string& key) {
    const auto q = url.find('?');
    if (q == std::string::npos) return {};
    std::size_t i = q + 1;
    while (i < url.size()) {
        const std::size_t e = std::min(url.find('&', i), url.find('#', i));
        const std::string kv = url.substr(i, e == std::string::npos ? std::string::npos : e - i);
        const auto eq = kv.find('=');
        if (eq != std::string::npos && kv.substr(0, eq) == key) return kv.substr(eq + 1);
        if (e == std::string::npos || url[e] == '#') break;
        i = e + 1;
    }
    return {};
}
std::string html_text(std::string s) {
    static const std::pair<const char*, const char*> ents[] = {{"&amp;", "&"}, {"&#39;", "'"}, {"&quot;", "\""}, {"&lt;", "<"}, {"&gt;", ">"}, {"&#x27;", "'"}, {"&nbsp;", " "}};
    for (const auto& [a, b] : ents) for (std::size_t p; (p = s.find(a)) != std::string::npos;) s.replace(p, std::strlen(a), b);
    return s;
}
std::int64_t iso_unix(const std::string& s) {  // « 2026-11-12T08:30:00Z » (fractions ignorées)
    std::tm tm{};
    if (s.size() < 19 || std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) return 0;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return static_cast<std::int64_t>(timegm(&tm));
}
// Nom donné par « Content-Disposition » (filename*=UTF-8''… ou filename="…").
std::string disposition_name(const std::string& h) {
    auto pct = [](const std::string& v) {
        std::string o;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '%' && i + 2 < v.size()) { o += static_cast<char>(std::strtol(v.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
            else o += v[i];
        }
        return o;
    };
    auto p = h.find("filename*=");
    if (p != std::string::npos) {
        std::string v = h.substr(p + 10);
        const auto q = v.find("''");
        if (q != std::string::npos) v = v.substr(q + 2);
        v = v.substr(0, v.find(';'));
        return pct(v);
    }
    p = h.find("filename=");
    if (p == std::string::npos) return {};
    std::string v = h.substr(p + 9);
    if (!v.empty() && v[0] == '"') { v = v.substr(1); v = v.substr(0, v.find('"')); }
    else v = v.substr(0, v.find(';'));
    return v;
}
// En-têtes d'un fichier à lire (nom et taille), sans télécharger le contenu.
bool head_of(const std::string& url, std::string& name, std::uint64_t& size, std::string& type, std::string& err) {
    http::Download d;
    http::Call c;
    c.url = url;
    c.timeout_s = 60;
    c.redirects = 8;
    http::Reply h;
    if (!d.begin(c, h)) { err = h.error.empty() ? "lien refusé par le service (HTTP " + std::to_string(h.status) + ")" : h.error; return false; }
    name = disposition_name(h.header("content-disposition"));
    size = std::strtoull(h.header("content-length").c_str(), nullptr, 10);
    type = h.header("content-type");
    d.close();
    return true;
}

// ---- Google Drive ------------------------------------------------------------------------------------------------
bool gdrive_id(const std::string& url, std::string& id, bool& folder) {
    for (const char* pre : {"/folders/", "/file/d/"}) {
        const auto p = url.find(pre);
        if (p == std::string::npos) continue;
        std::size_t i = p + std::strlen(pre), b = i;
        while (i < url.size() && (std::isalnum(static_cast<unsigned char>(url[i])) || url[i] == '-' || url[i] == '_')) ++i;
        id = url.substr(b, i - b);
        folder = std::string(pre) == "/folders/";
        return id.size() >= 10;
    }
    id = query(url, "id");
    folder = url.find("/folderview") != std::string::npos || url.find("embeddedfolderview") != std::string::npos;
    return id.size() >= 10;
}
std::string gdrive_file_url(const std::string& id, const std::string& rk) {
    return "https://drive.usercontent.google.com/download?id=" + id + "&export=download&confirm=t" + (rk.empty() ? "" : "&resourcekey=" + rk);
}
bool gdrive_folder(Tree& t, const std::string& at, const std::string& id, const std::string& rk, int depth, std::string& err) {
    if (depth > 8 || t.nodes.size() > 3000) return true;
    http::Call c;
    c.url = "https://drive.google.com/embeddedfolderview?id=" + id + (rk.empty() ? "" : "&resourcekey=" + rk);
    c.timeout_s = 60;
    c.redirects = 5;
    const http::Reply r = http::call(c, {}, 8u << 20);
    if (!r.ok()) { err = r.status == 404 ? "lien Google Drive introuvable ou pas partagé à « tous ceux qui ont le lien »" : "Google Drive : " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error); return false; }
    if (r.body.find("flip-entries") == std::string::npos && r.body.find("flip-entry") == std::string::npos) {
        err = "Google Drive refuse la lecture : le dossier doit être partagé à « tous ceux qui ont le lien »";
        return false;
    }
    if (depth == 0) {
        const auto a = r.body.find("<title>"), b = r.body.find("</title>");
        if (a != std::string::npos && b != std::string::npos && b > a) t.title = html_text(r.body.substr(a + 7, b - a - 7));
    }
    std::size_t p = 0;
    while ((p = r.body.find("class=\"flip-entry\"", p)) != std::string::npos) {
        const std::size_t end = r.body.find("class=\"flip-entry\"", p + 10);
        const std::string e = r.body.substr(p, end == std::string::npos ? std::string::npos : end - p);
        p += 10;
        const auto h = e.find("href=\"");
        const auto ti = e.find("flip-entry-title\">");
        if (h == std::string::npos || ti == std::string::npos) continue;
        const std::string href = html_text(e.substr(h + 6, e.find('"', h + 6) - h - 6));
        const std::string name = html_text(e.substr(ti + 18, e.find('<', ti + 18) - ti - 18));
        std::string kid;
        bool folder = false;
        if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos || !gdrive_id(href, kid, folder)) continue;
        if (href.find("/drive/folders/") != std::string::npos) folder = true;
        const std::string krk = query(href, "resourcekey");
        LNode n;
        n.name = name;
        n.dir = folder;
        if (!folder) {
            n.url = gdrive_file_url(kid, krk);
            // La page ne donne pas les tailles : demandées au service (les copies vérifient la taille reçue).
            std::string nm, type, e2;
            if (t.nodes.size() < 400) head_of(n.url, nm, n.size, type, e2);
        }
        add_node(t, at, n);
        if (folder && !gdrive_folder(t, join(at, name), kid, krk, depth + 1, err)) return false;
    }
    return true;
}
bool load_gdrive(const std::string& url, Tree& t, std::string& err) {
    std::string id;
    bool folder = false;
    if (!gdrive_id(url, id, folder)) { err = "lien Google Drive non reconnu"; return false; }
    const std::string rk = query(url, "resourcekey");
    t.nodes["/"].dir = true;
    if (folder) { t.kind = "folder"; return gdrive_folder(t, "/", id, rk, 0, err); }
    t.kind = "file";
    LNode n;
    n.url = gdrive_file_url(id, rk);
    std::string type;
    if (!head_of(n.url, n.name, n.size, type, err)) return false;
    if (n.name.empty() || type.find("text/html") != std::string::npos) { err = "Google Drive refuse la lecture : le fichier doit être partagé à « tous ceux qui ont le lien »"; return false; }
    t.title = n.name;
    add_node(t, "/", n);
    return true;
}

// ---- Dropbox -----------------------------------------------------------------------------------------------------
std::string dropbox_dl(std::string url) {
    const auto p = url.find("dl=0");
    if (p != std::string::npos) url[p + 3] = '1';
    else if (url.find("dl=1") == std::string::npos) url += (url.find('?') == std::string::npos ? "?" : "&") + std::string("dl=1");
    return url;
}
bool load_dropbox(const std::string& url, Tree& t, std::string& err) {
    const bool folder = url.find("/scl/fo/") != std::string::npos || url.find("/sh/") != std::string::npos;
    const std::string dl = dropbox_dl(url);
    t.nodes["/"].dir = true;
    if (!folder) {
        t.kind = "file";
        LNode n;
        n.url = dl;
        std::string type;
        if (!head_of(dl, n.name, n.size, type, err)) { err = "lien Dropbox expiré, retiré ou privé (" + err + ")"; return false; }
        if (n.name.empty() || type.find("text/html") != std::string::npos) { err = "lien Dropbox expiré, retiré ou privé"; return false; }
        t.title = n.name;
        add_node(t, "/", n);
        return true;
    }
    // Dossier : une seule archive, téléchargée une fois et gardée (jusqu'au retrait du partage ou 6 h).
    std::string dir;
    { std::lock_guard<std::mutex> l(g_mu); dir = g_cache; }
    if (dir.empty()) { err = "cache des partages indisponible"; return false; }
    fs::mkdirs(dir);
    t.zip_path = dir + "/" + sha256_hex(url).substr(0, 20) + ".zip";
    fs::Entry st;
    bool fresh = false;
    if (fs::stat(t.zip_path, st) && st.size > 0 && now_unix() - st.mtime < 6 * 3600) fresh = true;
    std::string name = "Dropbox";
    if (!fresh) {
        const std::string part = t.zip_path + ".part";
        FILE* f = std::fopen(part.c_str(), "wb");
        if (!f) { err = "cache des partages impossible à écrire"; return false; }
        std::uint64_t got = 0;
        bool too_big = false;
        http::Call c;
        c.url = dl;
        c.timeout_s = 300;
        c.redirects = 8;
        const http::Reply r = http::fetch(c, [&](const char* p, std::size_t n) {
            got += n;
            if (got > (4ull << 30)) { too_big = true; return false; }
            return std::fwrite(p, 1, n, f) == n;
        });
        std::fclose(f);
        if (too_big) { ::remove(part.c_str()); err = "dossier Dropbox trop gros pour être lu d'un coup (plus de 4 Go)"; return false; }
        if (!r.ok()) { ::remove(part.c_str()); err = r.status == 404 || r.status == 410 ? "lien Dropbox expiré ou retiré" : "Dropbox : " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error); return false; }
        if (r.header("content-type").find("text/html") != std::string::npos) { ::remove(part.c_str()); err = "lien Dropbox expiré, retiré ou privé"; return false; }
        const std::string dn = disposition_name(r.header("content-disposition"));
        if (!dn.empty()) name = dn;
        if (std::rename(part.c_str(), t.zip_path.c_str()) != 0) { ::remove(part.c_str()); err = "cache des partages impossible à écrire"; return false; }
        fs::write_atomic(t.zip_path + ".name", name);
    } else {
        std::string n;
        if (fs::read(t.zip_path + ".name", n) && !n.empty()) name = n;
    }
    if (name.size() > 4 && lower(name.substr(name.size() - 4)) == ".zip") name.resize(name.size() - 4);
    t.title = name;
    if (!zip::list(t.zip_path, t.entries)) { ::remove(t.zip_path.c_str()); err = "archive Dropbox illisible (lien privé ou dossier vide ?)"; return false; }
    for (std::size_t i = 0; i < t.entries.size(); ++i) {
        const zip::Entry& e = t.entries[i];
        const std::string p = norm(e.name);
        if (p == "/") continue;
        std::string at = "/";
        std::string part;
        for (std::size_t k = 1; k <= p.size(); ++k) {  // dossiers parents, même absents de l'archive
            if (k == p.size() || p[k] == '/') {
                const std::string full = p.substr(0, k);
                const bool last = k == p.size();
                const std::string nm = full.substr(full.rfind('/') + 1);
                if (!t.nodes.count(full)) {
                    LNode n;
                    n.name = nm;
                    n.dir = !last || e.dir();
                    if (last && !e.dir()) { n.size = e.size; n.zip = static_cast<int>(i); n.mtime = st.mtime; }
                    add_node(t, at, n);
                }
                at = full;
            }
        }
    }
    return true;
}

// ---- OneDrive ----------------------------------------------------------------------------------------------------
void onedrive_item(Tree& t, const std::string& at, const Json& it) {
    LNode n;
    n.name = it["name"].as_string();
    if (n.name.empty() || n.name.find('/') != std::string::npos) return;
    n.dir = it["folder"].is_object();
    n.size = static_cast<std::uint64_t>(it["size"].as_int());
    n.mtime = iso_unix(it["lastModifiedDateTime"].as_string());
    n.url = it["@content.downloadUrl"].as_string();
    if (n.url.empty()) n.url = it["@microsoft.graph.downloadUrl"].as_string();
    add_node(t, at, n);
}
bool onedrive_get(const std::string& url, const std::string& auth, Json& out, std::string& err) {
    http::Call c;
    c.url = url;
    c.timeout_s = 60;
    if (!auth.empty()) c.headers = {{"Authorization", auth}, {"Prefer", "autoredeem"}};
    const http::Reply r = http::call(c, {}, 8u << 20);
    if (!r.ok() || !Json::parse(r.body, out)) { err = r.status == 404 || r.status == 403 || r.status == 401 ? "lien OneDrive introuvable, expiré ou privé" : "OneDrive : " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error); return false; }
    return true;
}
bool onedrive_folder(Tree& t, const std::string& at, const std::string& base, const std::string& auth, const std::string& drive, const std::string& item, int depth, std::string& err) {
    if (depth > 8 || t.nodes.size() > 3000) return true;
    std::string next = base + "/drives/" + drive + "/items/" + item + "/children?$top=500";
    while (!next.empty()) {
        Json j;
        if (!onedrive_get(next, auth, j, err)) return false;
        for (const auto& it : j["value"].items()) {
            onedrive_item(t, at, it);
            if (it["folder"].is_object() && it["folder"]["childCount"].as_int() > 0)
                if (!onedrive_folder(t, join(at, it["name"].as_string()), base, auth, drive, it["id"].as_string(), depth + 1, err)) return false;
        }
        next = j["@odata.nextLink"].as_string();
    }
    return true;
}
bool load_onedrive(const std::string& url, Tree& t, std::string& err) {
    if (url.find("sharepoint.com") != std::string::npos) { err = "OneDrive professionnel (SharePoint) : pas encore pris en charge, seulement les comptes personnels"; return false; }
    std::string enc = mega::b64url_encode(url);
    while (!enc.empty() && enc.back() == '=') enc.pop_back();
    const std::string share = "u!" + enc;
    // Jeton anonyme du site OneDrive (« badger »), puis l'API des contenus personnels ; sinon l'ancienne API publique.
    std::string auth, base;
    {
        http::Call c;
        c.method = "POST";
        c.url = "https://api-badgerp.svc.ms/v1.0/token";
        c.headers = {{"Content-Type", "application/json"}};
        c.timeout_s = 30;
        const http::Reply r = http::call(c, "{\"appId\":\"5cbed6ac-a083-4e14-b191-b4ba07653de2\"}");
        Json j;
        if (r.ok() && Json::parse(r.body, j) && !j["token"].as_string().empty()) { auth = "Badger " + j["token"].as_string(); base = "https://my.microsoftpersonalcontent.com/_api/v2.0"; }
    }
    Json root;
    std::string e1;
    // Un fichier seul : Microsoft refuse « $expand=children » (422) ; on redemande l'élément sans ses enfants.
    if (!auth.empty() && !onedrive_get(base + "/shares/" + share + "/driveitem?$expand=children", auth, root, e1) &&
        onedrive_get(base + "/shares/" + share + "/driveitem", auth, root, e1) && root["folder"].is_object()) root = Json();
    if (auth.empty() || root["id"].as_string().empty()) {
        auth.clear();
        base = "https://api.onedrive.com/v1.0";
        if (!onedrive_get(base + "/shares/" + share + "/root?expand=children", "", root, err)) return false;
    }
    t.nodes["/"].dir = true;
    t.title = root["name"].as_string();
    if (!root["folder"].is_object()) {  // un fichier seul
        t.kind = "file";
        onedrive_item(t, "/", root);
        return t.nodes.size() > 1 || (err = "OneDrive : fichier illisible", false);
    }
    t.kind = "folder";
    const std::string drive = root["parentReference"]["driveId"].as_string(), item = root["id"].as_string();
    for (const auto& it : root["children"].items()) onedrive_item(t, "/", it);
    for (const auto& it : root["children"].items())
        if (it["folder"].is_object() && it["folder"]["childCount"].as_int() > 0 && !drive.empty())
            if (!onedrive_folder(t, join("/", it["name"].as_string()), base, auth, drive, it["id"].as_string(), 1, err)) return false;
    (void)item;
    return true;
}

// ---- L'appareil (lecture seule) ----------------------------------------------------------------------------------
class MemReader : public Reader {
public:
    explicit MemReader(std::string d) : d_(std::move(d)) {}
    long read(char* p, std::size_t n) override {
        const std::size_t k = std::min(n, d_.size() - at_);
        std::memcpy(p, d_.data() + at_, k);
        at_ += k;
        return static_cast<long>(k);
    }
    std::string d_;
    std::size_t at_ = 0;
};
class HttpReader : public Reader {
public:
    long read(char* p, std::size_t n) override { return dl.read(p, n); }
    http::Download dl;
};

class LinkStore : public Store {
public:
    explicit LinkStore(Tree t) : t_(std::move(t)) {}
    std::string label() const override { return t_.service; }
    bool mkdirs(const std::string&) override { return false; }
    std::unique_ptr<Writer> create(const std::string&, std::string& err) override { err = "partage en lecture seule"; return nullptr; }
    std::unique_ptr<Reader> open(const std::string& path, std::string& err) override { return open_at(path, 0, err); }
    std::unique_ptr<Reader> open_at(const std::string& path, std::uint64_t offset, std::string& err) override {
        const auto it = t_.nodes.find(norm(path));
        if (it == t_.nodes.end() || it->second.dir) { err = "fichier introuvable dans le partage"; return nullptr; }
        const LNode& n = it->second;
        if (n.zip >= 0) {
            const zip::Entry& e = t_.entries[static_cast<std::size_t>(n.zip)];
            if (e.size > (1ull << 30)) { err = "fichier trop gros dans l'archive Dropbox"; return nullptr; }
            std::string data;
            if (!zip::read(t_.zip_path, e, data, err)) return nullptr;
            auto r = std::make_unique<MemReader>(std::move(data));
            r->at_ = std::min<std::size_t>(static_cast<std::size_t>(offset), r->d_.size());
            return r;
        }
        auto r = std::make_unique<HttpReader>();
        http::Call c;
        c.url = n.url;
        c.timeout_s = 120;
        c.redirects = 8;
        if (offset) c.headers = {{"Range", "bytes=" + std::to_string(offset) + "-"}};
        http::Reply h;
        if (!r->dl.begin(c, h)) { err = "lecture refusée par " + t_.service + " (HTTP " + std::to_string(h.status) + ")"; return nullptr; }
        if (offset && h.status != 206) {  // pas de reprise côté service : on saute le début
            char buf[65536];
            for (std::uint64_t left = offset; left > 0;) { const long k = r->dl.read(buf, static_cast<std::size_t>(std::min<std::uint64_t>(left, sizeof buf))); if (k <= 0) { err = "lecture interrompue"; return nullptr; } left -= static_cast<std::uint64_t>(k); }
        }
        return r;
    }
    bool read_small(const std::string& path, std::string& out, std::uint64_t limit) override {
        std::string err;
        auto r = open(path, err);
        if (!r) return false;
        out.clear();
        char buf[65536];
        for (long n; (n = r->read(buf, sizeof buf)) > 0;) { out.append(buf, static_cast<std::size_t>(n)); if (out.size() > limit) return false; }
        return true;
    }
    bool write_small(const std::string&, std::string_view) override { return false; }
    bool stat(const std::string& path, fs::Entry& out) override {
        const auto it = t_.nodes.find(norm(path));
        if (it == t_.nodes.end()) return false;
        out = entry(it->first, it->second);
        return true;
    }
    bool list(const std::string& dir, std::vector<fs::Entry>& out) override {
        const auto it = t_.nodes.find(norm(dir));
        if (it == t_.nodes.end() || !it->second.dir) return false;
        out.clear();
        for (const auto& k : it->second.kids) { const auto n = t_.nodes.find(k); if (n != t_.nodes.end()) out.push_back(entry(k, n->second)); }
        return true;
    }
    bool space(const std::string&, fs::Space&) override { return false; }
    bool remove(const std::string&) override { return false; }
    const Tree& tree() const { return t_; }

private:
    static fs::Entry entry(const std::string&, const LNode& n) {
        fs::Entry e;
        e.name = n.name;
        e.dir = n.dir;
        e.size = n.size;
        e.mtime = n.mtime;
        return e;
    }
    Tree t_;
};

bool load(const std::string& raw, Tree& t, std::string& err) {
    const std::string url = trim(raw), svc = link_service(url);
    {
        std::lock_guard<std::mutex> l(g_mu);
        const auto it = g_trees.find(url);
        if (it != g_trees.end() && now_unix() - it->second.at < (svc == "dropbox" ? 600 : 120)) { t = it->second; return true; }
    }
    t = Tree();
    t.service = svc == "gdrive" ? "Google Drive" : svc == "dropbox" ? "Dropbox" : "OneDrive";
    const bool ok = svc == "gdrive" ? load_gdrive(url, t, err) : svc == "dropbox" ? load_dropbox(url, t, err) : svc == "onedrive" ? load_onedrive(url, t, err) : (err = "lien de partage non reconnu", false);
    if (!ok) return false;
    t.at = now_unix();
    std::lock_guard<std::mutex> l(g_mu);
    g_trees[url] = t;
    return true;
}

}  // namespace

std::string link_service(const std::string& raw) {
    const std::string u = lower(trim(raw));
    if (u.rfind("http", 0) != 0 && u.find("drive.google.com") != 0 && u.find("www.dropbox.com") != 0 && u.find("1drv.ms") != 0) return {};
    if (u.find("drive.google.com/") != std::string::npos || u.find("docs.google.com/") != std::string::npos) return "gdrive";
    if (u.find("dropbox.com/") != std::string::npos) return "dropbox";
    if (u.find("1drv.ms/") != std::string::npos || u.find("onedrive.live.com/") != std::string::npos || u.find("sharepoint.com/") != std::string::npos) return "onedrive";
    return {};
}

void set_share_cache(const std::string& host_dir) { std::lock_guard<std::mutex> l(g_mu); g_cache = host_dir; }

std::unique_ptr<Store> link_share(const std::string& url, std::string& err) {
    Tree t;
    if (!load(url, t, err)) return nullptr;
    return std::make_unique<LinkStore>(std::move(t));
}

bool link_share_probe(const std::string& url, Json& out, std::string& err) {
    Tree t;
    if (!load(url, t, err)) return false;
    std::int64_t files = 0, dirs = 0, bytes = 0;
    for (const auto& [p, n] : t.nodes) { if (p == "/") continue; if (n.dir) ++dirs; else { ++files; bytes += static_cast<std::int64_t>(n.size); } }
    out = Json::object();
    out.set("service", link_service(url));
    out.set("kind", t.kind);
    out.set("name", t.title);
    out.set("files", files);
    out.set("dirs", dirs);
    out.set("bytes", bytes);
    out.set("link", trim(url));
    return true;
}

void link_share_forget(const std::string& url) {
    const std::string u = trim(url);
    std::string dir;
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_trees.erase(u);
        dir = g_cache;
    }
    if (!dir.empty() && link_service(u) == "dropbox") {
        const std::string z = dir + "/" + sha256_hex(u).substr(0, 20) + ".zip";
        ::remove(z.c_str());
        ::remove((z + ".name").c_str());
    }
}

}  // namespace pbc::store
