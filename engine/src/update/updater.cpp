#include "update/updater.hpp"

#include <map>
#include <mutex>
#include <unistd.h>
#include "update/self_launch.hpp"
#include "update/autostart.hpp"
#include "core/memory.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>
#include <functional>
#include <sys/stat.h>
#include <unistd.h>

#include "core/fs.hpp"
#include "core/http_client.hpp"
#include "core/util.hpp"

#if defined(PBCK_PS5)
extern "C" {
struct PbcLaunchAppParam {
    std::uint32_t size;
    std::int32_t user_id;
    std::int32_t app_opt;
    std::uint64_t crash_report;
    std::uint64_t check_flag;
};
int sceSystemServiceLaunchApp(const char* title_id, char** argv, PbcLaunchAppParam* param);
int sceUserServiceInitialize(const void*);
int sceUserServiceGetForegroundUser(int*);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
}
#define PBC_CAN_LAUNCH 1
#endif

namespace pbc {

namespace {
constexpr const char* engine_file = "pbc-engine2.elf";

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Empreintes des fichiers installés, gardées tant que taille et date ne changent pas : les recherches automatiques
// (ouverture de l'appli, Paramètres, toutes les heures) ne relisent plus toute l'appli. Avant : 1,5 à 4 s de réponse
// pour /status pendant chaque recherche, et l'appli télé croyait le moteur injoignable.
struct HashEntry { std::uint64_t size = 0; std::int64_t mtime = 0; std::string sha; };
std::mutex g_hash_mu;
std::map<std::string, HashEntry> g_hash_cache;

// Fichier remplacé (installation, retour en arrière) : son empreinte gardée ne vaut plus. Indispensable : la date est à
// la seconde, et un fichier de même taille remplacé dans la même seconde semblait identique (test « Versions », 2.92.1).
void forget_hash(const std::string& host_path) {
    std::lock_guard<std::mutex> l(g_hash_mu);
    g_hash_cache.erase(host_path);
}

bool same_file(const std::string& host_path, std::uint64_t size, const std::string& sha) {
    fs::Entry e;
    if (!fs::stat(host_path, e) || e.dir || e.size != size) return false;
    {
        std::lock_guard<std::mutex> l(g_hash_mu);
        const auto it = g_hash_cache.find(host_path);
        if (it != g_hash_cache.end() && it->second.size == e.size && it->second.mtime == e.mtime) return it->second.sha == sha;
    }
    FILE* f = std::fopen(host_path.c_str(), "rb");
    if (!f) return false;
    Sha256 h;
    char buf[65536];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) h.add(buf, n);
    std::fclose(f);
    const std::string got = h.hex();
    {
        std::lock_guard<std::mutex> l(g_hash_mu);
        g_hash_cache[host_path] = HashEntry{e.size, e.mtime, got};
    }
    ::usleep(2000);  // laisser respirer les autres fils entre deux fichiers
    return got == sha;
}

// Chemin relatif sûr : pas d'absolu, pas de « .. ».
bool safe_rel(const std::string& p) {
    if (p.empty() || p[0] == '/' || p.find('\\') != std::string::npos) return false;
    std::size_t start = 0;
    while (start <= p.size()) {
        const auto end = p.find('/', start);
        const std::string part = p.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

// La source est soit une adresse http://, soit un dossier de la console ou d'une clé USB
// (/data/…/manifest.json, /mnt/usb0/…/manifest.json) : mise à jour sans réseau.
bool is_local(const std::string& src) { return !src.empty() && src[0] == '/'; }

std::string fetch_manifest(const std::string& src, std::string& body) {
    if (!is_local(src)) { const auto r = http::get_text(src, body); return r.error; }
    return fs::read(fs::host(src), body, 1u << 20) ? "" : "manifeste introuvable : " + src;
}

std::string fetch_file(const std::string& src, const std::string& rel, const std::function<bool(const char*, std::size_t)>& sink) {
    if (!is_local(src)) return http::get(http::resolve(src, "files/" + rel), sink, 60).error;
    const std::string p = fs::host(fs::join(fs::dirname(src), "files/" + rel));
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return "fichier absent de la source : " + rel;
    char buf[65536];
    std::string err;
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) if (!sink(buf, n)) { err = "abandon"; break; }
    if (err.empty() && std::ferror(f)) err = "lecture impossible : " + rel;
    std::fclose(f);
    return err;
}

// compare_versions : core/util (partagée avec la synchro des applis).
}  // namespace

Updater::Updater(std::string state_dir, std::string app_dir, std::string running_version)
    : dir_(std::move(state_dir)), app_dir_(std::move(app_dir)), running_(std::move(running_version)) {
    std::string s;
    if (fs::read(fs::host(fs::join(dir_, "update-skip")), s, 64)) skipped_ = s.substr(0, s.find_first_of("\r\n"));
    previous_ = previous_version();
}

std::string Updater::previous_version() const {
    std::string text;
    Json m;
    if (!fs::read(fs::host(fs::join(dir_, "update-previous/manifest.json")), text, 4u << 20) || !Json::parse(text, m)) return "";
    return m["version"].as_string();
}

namespace {
// Adresse d'un fichier voisin de la source (« …/latest.json » → « …/versions.json »), locale ou http(s).
std::string sibling(const std::string& src, const std::string& name) {
    if (src.empty()) return "";
    if (src[0] == '/') return fs::join(std::string(fs::dirname(src)), name);
    return http::resolve(src, name);
}
bool read_json(const std::string& where, Json& out, std::string& error) {
    std::string body;
    if (where.empty()) { error = "aucune source"; return false; }
    if (where[0] == '/') { if (!fs::read(fs::host(where), body, 4u << 20)) { error = "introuvable : " + where; return false; } }
    else { const auto r = http::get_text(where, body); if (!r.error.empty()) { error = r.error; return false; } }
    if (!Json::parse(body, out)) { error = "illisible : " + where; return false; }
    return true;
}
}  // namespace

Json Updater::versions(std::string& error) {
    std::string src;
    { std::lock_guard<std::mutex> l(mu_); src = configured_.empty() ? auto_source() : configured_; }
    Json list;
    if (!read_json(sibling(src, "versions.json"), list, error) || !list.is_array()) {
#if defined(PBC_UPDATE_URL)
        // Source sans liste (dossier local, clé USB) : la liste du site officiel
        std::string e2;
        if (src != PBC_UPDATE_URL && read_json(sibling(PBC_UPDATE_URL, "versions.json"), list, e2) && list.is_array()) { src = PBC_UPDATE_URL; error.clear(); }
        else
#endif
        {
            if (error.empty()) error = "liste illisible";
            error = "cette source ne propose pas de liste de versions (" + error + ")";
            return Json();
        }
    }
    Json latest, out = Json::object();
    std::string e2;
    out.set("recommended", read_json(sibling(src, "latest.json"), latest, e2) ? latest["version"] : Json(""));
    out.set("installed", installed_version());
    bool beta;
    { std::lock_guard<std::mutex> l(mu_); beta = beta_; }
    out.set("channel", beta ? "beta" : "stable");
    Json shown = Json::array();  // canal stable : les bêtas ne sont pas proposées
    for (const auto& v : list.items()) if (beta || v["channel"].as_string() != "beta") shown.push(v);
    out.set("versions", shown);
    return out;
}

bool Updater::install_version(const std::string& version, std::string& error) {
    std::string src;
    { std::lock_guard<std::mutex> l(mu_); src = configured_.empty() ? auto_source() : configured_; }
    Json list;
    if (!read_json(sibling(src, "versions.json"), list, error)) {
#if defined(PBC_UPDATE_URL)
        std::string e2;  // même repli que la liste : le site officiel
        if (src == PBC_UPDATE_URL || !read_json(sibling(PBC_UPDATE_URL, "versions.json"), list, e2)) return false;
        src = PBC_UPDATE_URL;
        error.clear();
#else
        return false;
#endif
    }
    std::string manifest;
    for (const auto& v : list.items()) if (v["version"].as_string() == version) manifest = v["manifest"].as_string();
    if (manifest.empty()) { error = "version absente de la liste : " + version; return false; }
    const std::string target = manifest.rfind("http", 0) == 0 || manifest[0] == '/' ? manifest : sibling(src, manifest);
    const std::string leaving = installed_version();
    const bool older = !leaving.empty() && compare_versions(version, leaving) < 0;
    bool clear_skip = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (busy()) { error = "une opération est déjà en cours"; return false; }
        source_ = target;  // une seule fois
        state_ = "checking";
        message_.clear();
        quiet_ = false;
        rollback_pending_ = true;  // installer aussitôt la version trouvée
        if (older) skipped_ = leaving;
        else if (version == skipped_) { skipped_.clear(); clear_skip = true; }
    }
    if (older) fs::write_atomic(fs::host(fs::join(dir_, "update-skip")), leaving + "\n");
    else if (clear_skip) ::unlink(fs::host(fs::join(dir_, "update-skip")).c_str());
    log("update install version %s (from %s)", version.c_str(), leaving.c_str());
    ++revision_;
    join();
    worker_ = std::thread([this] { run_check(); });
    return true;
}

bool Updater::skipped(const std::string& version) const { std::lock_guard<std::mutex> l(mu_); return !version.empty() && version == skipped_; }

bool Updater::rollback(std::string& error) {
    const std::string prev = previous_version();
    if (prev.empty()) { error = "aucune version précédente gardée"; return false; }
    const std::string leaving = installed_version();  // la version qu'on quitte (installée ; le moteur en marche peut être plus ancien)
    {
        std::lock_guard<std::mutex> l(mu_);
        if (busy()) { error = "une opération est déjà en cours"; return false; }
        source_ = fs::join(dir_, "update-previous/manifest.json");  // une seule fois
        state_ = "checking";
        message_.clear();
        quiet_ = false;
        rollback_pending_ = true;
        skipped_ = leaving;  // plus proposée automatiquement
    }
    fs::write_atomic(fs::host(fs::join(dir_, "update-skip")), leaving + "\n");
    log("update rollback to %s (skip %s)", prev.c_str(), leaving.c_str());
    ++revision_;
    join();
    worker_ = std::thread([this] { run_check(); });
    return true;
}

Updater::~Updater() { join(); }

void Updater::join() { if (worker_.joinable()) worker_.join(); }

void Updater::set_channel(bool beta) { std::lock_guard<std::mutex> l(mu_); beta_ = beta; }

// Sur le canal bêta, un dépôt de versions (« …/latest.json ») se lit par « …/beta.json » : la bêta la plus récente,
// ou la stable si elle est plus récente. Une autre source (manifeste direct, clé USB, autre PS5) ne change pas.
std::string Updater::channel_source(const std::string& src) const {
    static const std::string kLatest = "latest.json";
    if (!beta_ || src.size() < kLatest.size() || src.compare(src.size() - kLatest.size(), kLatest.size(), kLatest) != 0) return src;
    return src.substr(0, src.size() - kLatest.size()) + "beta.json";
}

void Updater::set_source(const std::string& url) { std::lock_guard<std::mutex> l(mu_); configured_ = url; source_ = url.empty() ? auto_source() : url; }

bool Updater::busy() const { return state_ == "checking" || state_ == "downloading" || state_ == "installing"; }

void Updater::set_state(const std::string& state, const std::string& message) {
    {
        std::lock_guard<std::mutex> l(mu_);
        state_ = state;
        message_ = message;
    }
    ++revision_;
    log("update state=%s %s", state.c_str(), message.c_str());
}

std::string Updater::installed_version() const {
    std::string text;
    Json j;
    if (fs::read(fs::host(fs::join(app_dir_, "version.json")), text, 65536) && Json::parse(text, j)) return j["version"].as_string();
    return "";
}

Json Updater::changelog() const {
    std::string text;
    Json j;
    if (fs::read(fs::host(fs::join(app_dir_, "changelog.json")), text, 1u << 20) && Json::parse(text, j) && j["versions"].is_array()) return j["versions"];
    return Json::array();
}

Json Updater::status() const {
    std::lock_guard<std::mutex> l(mu_);
    Json j = Json::object();
    j.set("state", state_);
    j.set("message", message_);
    j.set("restart_error", restart_error_);
    j.set("source", source_);
    j.set("channel", beta_ ? "beta" : "stable");
    j.set("running", running_);
    j.set("installed", installed_version());
    j.set("available", manifest_["version"].as_string());
    j.set("build", manifest_["build"].as_string());
    j.set("notes", manifest_["notes"].is_array() ? manifest_["notes"] : Json::array());
    // Ce qu'apporte la mise à jour : versions plus récentes que celle installée.
    Json news = Json::array();
    const std::string have = installed_version();
    for (const auto& v : manifest_["changelog"].items()) if (have.empty() || compare_versions(v["version"].as_string(), have) > 0) news.push(v);
    j.set("changelog", news);
    j.set("checked_at", checked_at_);
    j.set("bytes_total", total_);
    j.set("bytes_done", done_);
    if (state_ == "downloading") {  // avancement détaillé : utile surtout à distance (fichiers lents, attentes)
        j.set("file_index", static_cast<std::uint64_t>(file_index_));
        j.set("current_file", current_file_);
        const std::int64_t ms = now_ms() - dl_started_ms_;
        const std::uint64_t bps = ms > 1000 ? static_cast<std::uint64_t>(done_ * 1000.0 / static_cast<double>(ms)) : 0;
        j.set("bytes_per_second", bps);
        if (bps && total_ > done_) j.set("eta_seconds", static_cast<std::uint64_t>((total_ - done_) / bps));
    }
    j.set("files", static_cast<std::uint64_t>(changed_.size()));
    j.set("engine_changed", engine_changed_);
    j.set("app_changed", app_changed_);
    // Le moteur en cours est plus ancien que celui installé : un redémarrage du moteur suffit.
    const std::string inst = installed_version();
    j.set("engine_restart_needed", !inst.empty() && compare_versions(running_, inst) != 0);  // plus récente, ou plus ancienne après « Revenir »
    if (!received_from_.empty()) j.set("received_from", received_from_);
    if (!skipped_.empty()) j.set("skipped", skipped_);
    if (!previous_.empty() && previous_ != installed_version()) j.set("previous", previous_);
    return j;
}

// Sans réglage, l'app trouve seule ses mises à jour (rien à saisir) : un dossier « PBC Update » sur une clé USB, sinon
// le dossier local déposé par FTP ou par le web, sinon l'adresse officielle intégrée à la compilation (PBC_UPDATE_URL).
std::string Updater::auto_source() const {
    for (int i = 0; i < 8; ++i) {
        const std::string p = "/mnt/usb" + std::to_string(i) + "/PBC Update/manifest.json";
        if (fs::exists(fs::host(p))) return p;
    }
    if (fs::exists(fs::host("/data/pbc-update-source/manifest.json"))) return "/data/pbc-update-source/manifest.json";
#if defined(PBC_UPDATE_URL)
    return PBC_UPDATE_URL;
#else
    return "";
#endif
}

bool Updater::check(std::string& error) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (busy()) { error = "une opération est déjà en cours"; return false; }
        source_ = channel_source(configured_.empty() ? auto_source() : configured_);  // toujours la source réglée (pas celle d'une fois : autre PS5, « Revenir »)  // aucun réglage : recherche automatique à chaque vérification
        if (source_.empty()) { error = "aucune mise à jour trouvée (clé USB avec un dossier « PBC Update », ou source en ligne)"; return false; }
        state_ = "checking";
        message_.clear();
        quiet_ = false;
        attempted_at_ = now_unix();
    }
    ++revision_;
    join();
    worker_ = std::thread([this] { const auto p0 = memory::peak_inuse(); run_check(); memory::note_peak("mise à jour : recherche", p0); });
    return true;
}

bool Updater::check_auto(int min_age_s) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (busy() || state_ == "installed") return false;
        if (attempted_at_ && now_unix() - attempted_at_ < min_age_s) return false;
        source_ = channel_source(configured_.empty() ? auto_source() : configured_);  // toujours la source réglée (pas celle d'une fois : autre PS5, « Revenir »)
        if (source_.empty()) return false;
        quiet_prev_state_ = state_;
        quiet_prev_message_ = message_;
        quiet_ = true;
        state_ = "checking";
        message_.clear();
        attempted_at_ = now_unix();
    }
    ++revision_;
    join();
    worker_ = std::thread([this] { const auto p0 = memory::peak_inuse(); run_check(); memory::note_peak("mise à jour : recherche", p0); });
    return true;
}

void Updater::note_received_from(const std::string& name) { std::lock_guard<std::mutex> l(mu_); received_from_ = name; ++revision_; }

bool Updater::check_from(const std::string& url, std::string& error) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (busy()) { error = "une opération est déjà en cours"; return false; }
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) { error = "source invalide"; return false; }
        source_ = url;  // une seule fois : la prochaine recherche normale reprend la source réglée
        state_ = "checking";
        message_.clear();
        quiet_ = false;
    }
    ++revision_;
    join();
    worker_ = std::thread([this] { const auto p0 = memory::peak_inuse(); run_check(); memory::note_peak("mise à jour : recherche", p0); });
    return true;
}

Json Updater::self_manifest() {
    // Tout le dossier de l'app installée, comme le paquet publié (« PPSA99950/… »).
    std::string vtext;
    Json vj;
    fs::read(fs::host(fs::join(app_dir_, "version.json")), vtext, 65536);
    Json::parse(vtext, vj);
    const std::string key = vj["version"].as_string() + "|" + vj["build"].as_string();
    {
        std::lock_guard<std::mutex> l(self_mu_);
        if (key == self_cache_key_ && self_cache_.is_object()) return self_cache_;
    }
    Json files = Json::array();
    std::function<void(const std::string&)> walk = [&](const std::string& rel) {
        std::vector<fs::Entry> es;
        if (!fs::list(fs::host(rel.empty() ? app_dir_ : fs::join(app_dir_, rel)), es)) return;
        for (const auto& e : es) {
            const std::string r = rel.empty() ? e.name : rel + "/" + e.name;
            if (e.name.empty() || e.name[0] == '.' || e.name.find(".pbc-") != std::string::npos) continue;  // restes d'une installation
            if (e.dir) { walk(r); continue; }
            FILE* f = std::fopen(fs::host(fs::join(app_dir_, r)).c_str(), "rb");
            if (!f) continue;
            Sha256 h;
            char buf[65536];
            std::uint64_t size = 0;
            for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) { h.add(buf, n); size += n; }
            std::fclose(f);
            Json x = Json::object();
            x.set("path", r); x.set("size", size); x.set("sha256", h.hex());
            files.push(x);
        }
    };
    walk("");
    Json m = Json::object();
    m.set("product", "PS5 Backup Center");
    m.set("version", vj["version"]); m.set("build", vj["build"]); m.set("notes", vj["notes"].is_array() ? vj["notes"] : Json::array());
    m.set("files", files);
    m.set("changelog", changelog());
    m.set("from_console", true);
    std::lock_guard<std::mutex> l(self_mu_);
    self_cache_ = m; self_cache_key_ = key;
    return m;
}

bool Updater::self_file(const std::string& rel, std::string& out) {
    if (!safe_rel(rel) || rel.find(".pbc-") != std::string::npos) return false;
    return fs::read(fs::host(fs::join(app_dir_, rel)), out, 64u << 20);
}

void Updater::run_check() {
    std::string url;
    { std::lock_guard<std::mutex> l(mu_); url = source_; }
    std::string body;
    const std::string fetch_error = fetch_manifest(url, body);
    Json m;
    // Dépôt de versions (GitHub Pages, site https, dossier) : latest.json → {"version", "manifest": "2.90.7/manifest.json"}.
    // On suit ce pointeur une fois ; la suite (fichiers) part du vrai manifeste.
    if (fetch_error.empty()) {
        Json ptr;
        if (Json::parse(body, ptr) && !ptr["manifest"].as_string().empty() && !ptr["files"].is_array()) {
            const std::string rel = ptr["manifest"].as_string();
            const std::string next = rel.rfind("http", 0) == 0 || rel[0] == '/' ? rel
                                   : is_local(url) ? fs::join(std::string(fs::dirname(url)), rel) : http::resolve(url, rel);
            { std::lock_guard<std::mutex> l(mu_); source_ = next; }
            url = next;
            body.clear();
            const std::string e2 = fetch_manifest(url, body);
            if (!e2.empty()) { set_state("error", "source injoignable : " + e2); return; }
        }
    }
    if (!fetch_error.empty()) {
        bool quiet;
        std::string prev, prev_msg;
        { std::lock_guard<std::mutex> l(mu_); quiet = quiet_; prev = quiet_prev_state_; prev_msg = quiet_prev_message_; }
        if (quiet) {  // recherche automatique : on garde ce qui était affiché
            log("update auto-check: source injoignable : %s", fetch_error.c_str());
            set_state(prev.empty() || prev == "checking" ? "idle" : prev, prev_msg);
            return;
        }
        set_state("error", "source injoignable : " + fetch_error);
        return;
    }
    if (!Json::parse(body, m) || !m["files"].is_array() || m["version"].as_string().empty()) {
        bool quiet;
        std::string prev, prev_msg;
        { std::lock_guard<std::mutex> l(mu_); quiet = quiet_; prev = quiet_prev_state_; prev_msg = quiet_prev_message_; }
        if (quiet) {  // recherche automatique : comme une source injoignable, on garde ce qui était affiché
            log("update auto-check: manifeste illisible (%s)", url.c_str());
            set_state(prev.empty() || prev == "checking" ? "idle" : prev, prev_msg);
            return;
        }
        set_state("error", "manifeste illisible");
        return;
    }
    std::vector<File> changed;
    std::uint64_t total = 0;
    bool engine = false, app = false;
    for (const auto& f : m["files"].items()) {
        File x{f["path"].as_string(), f["sha256"].as_string(), static_cast<std::uint64_t>(f["size"].as_int())};
        if (!safe_rel(x.path) || x.sha256.size() != 64) { set_state("error", "manifeste invalide : " + x.path); return; }
        if (same_file(fs::host(fs::join(app_dir_, x.path)), x.size, x.sha256)) continue;
        total += x.size;
        (x.path == engine_file ? engine : app) = true;
        changed.push_back(std::move(x));
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        manifest_ = m;
        changed_ = std::move(changed);
        total_ = total;
        done_ = 0;
        engine_changed_ = engine;
        app_changed_ = app;
        checked_at_ = now_unix();
    }
    // version.json seul ne compte pas comme une mise à jour.
    bool only_meta = true;
    { std::lock_guard<std::mutex> l(mu_); for (const auto& f : changed_) if (f.path != "version.json") only_meta = false; }
    bool rb = false, quiet_skip = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        rb = rollback_pending_;
        rollback_pending_ = false;
        quiet_skip = quiet_ && !skipped_.empty() && manifest_["version"].as_string() == skipped_;
    }
    if (rb) {  // retour en arrière : installation aussitôt (dans ce même fil)
        if (only_meta) { set_state("up_to_date"); return; }
        { std::lock_guard<std::mutex> l(mu_); state_ = "downloading"; message_.clear(); }
        ++revision_;
        run_install();
        return;
    }
    if (quiet_skip) { set_state("up_to_date"); return; }  // version quittée par « Revenir » : pas reproposée seule
    set_state(only_meta ? "up_to_date" : "available");
    if (!only_meta) {
        std::string v;
        {
            std::lock_guard<std::mutex> l(mu_);
            v = manifest_["version"].as_string();
            if (v == announced_) v.clear();
            else announced_ = v;
        }
        if (!v.empty() && on_available) on_available(v);
    }
}

bool Updater::install(std::string& error) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (busy()) { error = "une opération est déjà en cours"; return false; }
        if (state_ != "available") { error = "recherchez d'abord une mise à jour"; return false; }
        if (manifest_["version"].as_string() == skipped_) { skipped_.clear(); ::unlink(fs::host(fs::join(dir_, "update-skip")).c_str()); }
        state_ = "downloading";
        message_.clear();
    }
    ++revision_;
    join();
    worker_ = std::thread([this] { const auto p0 = memory::peak_inuse(); run_install(); memory::note_peak("mise à jour : installation", p0); });
    return true;
}

void Updater::run_install() {
    std::vector<File> files;
    std::string url;
    {
        std::lock_guard<std::mutex> l(mu_);
        files = changed_;
        url = source_;
    }
    // version.json en dernier : tant qu'il n'est pas écrit, l'installation n'est pas « faite ».
    std::stable_partition(files.begin(), files.end(), [](const File& f) { return f.path != "version.json"; });
    const std::string stage = fs::join(dir_, "update-stage"), previous = fs::join(dir_, "update-previous");
    fs::remove_tree(fs::host(stage));
    if (!fs::mkdirs(fs::host(stage))) { set_state("error", "dossier d'attente impossible à créer"); return; }

    // 1. Téléchargement et vérification.
    { std::lock_guard<std::mutex> l(mu_); dl_started_ms_ = now_ms(); file_index_ = 0; }
    for (const auto& f : files) {
        { std::lock_guard<std::mutex> l(mu_); ++file_index_; current_file_ = f.path; }
        ++revision_;
        const std::string dst = fs::host(fs::join(stage, f.path));
        fs::mkdirs(std::string(fs::dirname(dst)));
        Sha256 h;
        std::uint64_t got = 0;
        // Un essai de plus si le téléchargement échoue (ex. 404 passager du cache d'un fichier fraîchement publié).
        for (int attempt = 0;; ++attempt) {
            FILE* out = std::fopen(dst.c_str(), "wb");
            if (!out) { set_state("error", "écriture impossible : " + f.path); return; }
            h = Sha256();
            { std::lock_guard<std::mutex> l(mu_); done_ -= got; }
            got = 0;
            const std::string fetch_error = fetch_file(url, f.path, [&](const char* p, std::size_t n) {
                if (std::fwrite(p, 1, n, out) != n) return false;
                h.add(p, n);
                got += n;
                { std::lock_guard<std::mutex> l(mu_); done_ += n; }
                if ((got & ((1u << 20) - 1)) < n) ++revision_;  // environ un avancement par Mo
                return true;
            });
            const bool flushed = std::fflush(out) == 0 && ::fsync(fileno(out)) == 0;
            std::fclose(out);
            if (fetch_error.empty() && flushed) break;
            if (attempt == 0 && !fetch_error.empty()) {
                log("update: %s : %s, nouvel essai", f.path.c_str(), fetch_error.c_str());
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }
            set_state("error", f.path + " : " + (fetch_error.empty() ? "écriture incomplète" : fetch_error));
            return;
        }
        if (got != f.size || h.hex() != f.sha256) { set_state("error", "fichier altéré pendant le téléchargement : " + f.path); return; }
    }
    // 2. Installation, réversible. La version en place est gardée dans update-previous (ses fichiers remplacés dans
    // files/, sa liste complète dans manifest.json) : de quoi y revenir (« Revenir à la version précédente »).
    set_state("installing");
    const Json old_manifest = self_manifest();
    fs::remove_tree(fs::host(previous));
    fs::mkdirs(fs::host(fs::join(previous, "files")));
    struct Done { std::string path; bool had_old; };
    std::vector<Done> done;
    std::string failure;
    for (const auto& f : files) {
        const std::string live = fs::host(fs::join(app_dir_, f.path)), old = fs::host(fs::join(previous, "files/" + f.path));
        const std::string fresh = fs::host(fs::join(stage, f.path));
        fs::mkdirs(std::string(fs::dirname(live)));
        fs::mkdirs(std::string(fs::dirname(old)));
        const bool had_old = fs::exists(live);
        // Mêmes droits que le fichier remplacé (0777 pour un nouveau) : un eboot sans droit
        // d'exécution empêche l'app de démarrer (CE-107750-0).
        struct ::stat st{};
        const mode_t mode = had_old && ::stat(live.c_str(), &st) == 0 ? (st.st_mode & 07777) : 0777;
        if (::chmod(fresh.c_str(), mode) != 0) { failure = "droits impossibles à régler : " + f.path; break; }
        if (had_old && std::rename(live.c_str(), old.c_str()) != 0) { failure = "remplacement impossible : " + f.path; break; }
        if (std::rename(fresh.c_str(), live.c_str()) != 0) {
            if (had_old) std::rename(old.c_str(), live.c_str());
            failure = "remplacement impossible : " + f.path;
            break;
        }
        done.push_back({f.path, had_old});
        forget_hash(live);
    }
    if (!failure.empty()) {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            const std::string live = fs::host(fs::join(app_dir_, it->path)), old = fs::host(fs::join(previous, "files/" + it->path));
            if (it->had_old) std::rename(old.c_str(), live.c_str());
            else std::remove(live.c_str());
            forget_hash(live);
        }
        set_state("error", failure + " (rien n'a changé)");
        return;
    }
    fs::remove_tree(fs::host(stage));
    {   // liste de la version quittée (sans le marqueur « servie par cette console »)
        Json m = old_manifest;
        m.set("from_console", false);
        fs::write_atomic(fs::host(fs::join(previous, "manifest.json")), m.dump() + "\n");
        std::lock_guard<std::mutex> l(mu_);
        previous_ = m["version"].as_string();
    }
    std::string version;
    {
        std::lock_guard<std::mutex> l(mu_);
        version = manifest_["version"].as_string();
        changed_.clear();
    }
    // « Démarrer avec la console » : la copie chez Payload Manager suit dès l'installation. Avant : seulement à la
    // relance du moteur ; une console plantée entre les deux redémarrait sur l'ancienne version (test console B, 2.92.2).
    autostart::refresh_pldmgr_copy(app_dir_);
    set_state("installed", "version " + version + " installée");
}

bool Updater::restart_engine(std::string& error) {
    std::string elf;
    if (!fs::read(fs::host(fs::join(app_dir_, engine_file)), elf, 64u << 20) || elf.size() < 64) { error = "moteur installé introuvable"; return false; }
    log("update restart engine bytes=%zu", elf.size());
    // Marqueur : le nouveau moteur a le droit de remplacer celui-ci (sinon il s'en irait, croyant à un doublon).
    const std::string marker = fs::host(fs::join(dir_, "takeover-request"));
    fs::write_atomic(marker, std::to_string(static_cast<long long>(now_unix())));
    autostart::refresh_pldmgr_copy(app_dir_);  // « Démarrer avec la console » : la copie chez Payload Manager suit
    // Notre propre lanceur (elfldr compilé dans le moteur) : aucun chargeur externe, aucun port.
    if (spawn_elf(elf, engine_file, error) < 0) { ::unlink(marker.c_str()); return false; }
    { std::lock_guard<std::mutex> l(mu_); restart_error_.clear(); }
    // Le nouveau moteur consomme le marqueur dès son départ. S'il est encore là 45 s plus tard, la relance n'a pas pris :
    // on le dit (au lieu de rester muet) ; ce moteur-ci continue, la version installée sera active au prochain démarrage.
    std::thread([this, marker] {
        std::this_thread::sleep_for(std::chrono::seconds(45));
        fs::Entry e;
        if (!fs::stat(marker, e)) return;
        ::unlink(marker.c_str());
        log("update restart engine: not taken over after 45 s");
        std::lock_guard<std::mutex> l(mu_);
        restart_error_ = "la relance du moteur n'a pas pris : la nouvelle version sera active au prochain démarrage";
    }).detach();
    return true;  // le nouveau moteur demande à celui-ci de s'arrêter
}

bool Updater::relaunch_app(int delay_s, std::string& error) {
#if defined(PBC_CAN_LAUNCH)
    error.clear();
    std::thread([delay_s] {
        // L'app se ferme elle-même juste après cette demande : on attend qu'elle ait vraiment
        // disparu (plus d'app au premier plan), sinon la console refuse le lancement (0x80aa001a).
        const int before = sceSystemServiceGetAppIdOfRunningBigApp();
        int now_id = before;
        int waited = 0;
        for (; waited < 150; ++waited) {
            ::usleep(100 * 1000);
            now_id = sceSystemServiceGetAppIdOfRunningBigApp();
            if (now_id != before || now_id < 0) break;
        }
        log("update relaunch wait before=0x%x after=0x%x waited_ms=%d", static_cast<unsigned>(before), static_cast<unsigned>(now_id), waited * 100);
        ::sleep(static_cast<unsigned>(delay_s));
        (void)sceUserServiceInitialize(nullptr);
        int user = -1;
        const int u = sceUserServiceGetForegroundUser(&user);
        PbcLaunchAppParam p{sizeof(PbcLaunchAppParam), user, 0, 0, 0};
        const int rc = sceSystemServiceLaunchApp("PPSA99950", nullptr, &p);
        log("update relaunch app user_rc=%d user=%d rc=0x%08x", u, user, static_cast<unsigned>(rc));
    }).detach();
    return true;
#else
    (void)delay_s;
    error = "relance automatique indisponible ici";
    return false;
#endif
}

}  // namespace pbc
