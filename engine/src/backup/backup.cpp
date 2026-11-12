#include "backup/backup.hpp"

#include <ctime>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/fs.hpp"
#include "core/perms.hpp"
#include "core/util.hpp"

namespace pbc::backup {

namespace {

constexpr std::size_t chunk = 1u << 20;  // 1 Mio

struct FileJob {
    Part part;
    std::string source;   // chemin console
    std::string rel;      // <composant>/<relatif>
    std::uint64_t size = 0;
};

bool wanted(const std::vector<Part>& parts, Part p) {
    return parts.empty() || std::find(parts.begin(), parts.end(), p) != parts.end();
}

void expand(const Part part, const std::string& src, const std::string& rel, std::vector<FileJob>& out, int depth = 0) {
    fs::Entry e;
    if (depth > 24 || !fs::stat(fs::host(src), e)) return;
    if (!e.dir) { out.push_back({part, src, rel, e.size}); return; }
    std::vector<fs::Entry> entries;
    if (!fs::list(fs::host(src), entries)) return;
    for (const auto& c : entries) expand(part, fs::join(src, c.name), fs::join(rel, c.name), out, depth + 1);
}

// Copie d'un fichier de la console vers un appareil (disque ou réseau), avec SHA-256 au passage.
bool copy_to_store(const std::string& src_host, store::Store& st, const std::string& dst, std::uint64_t expect, std::string& sha,
                   const std::function<void(std::uint64_t)>& tick, const std::atomic<bool>& stop, std::string& err) {
    const int in = ::open(src_host.c_str(), O_RDONLY);
    if (in < 0) { err = "lecture " + std::string(std::strerror(errno)); return false; }
    auto w = st.create(dst, err);
    if (!w) { ::close(in); return false; }
    std::vector<char> buf(chunk);
    Sha256 h;
    std::uint64_t copied = 0;
    bool ok = true;
    for (;;) {
        if (stop.load()) { err = "arrêt demandé"; ok = false; break; }
        const ssize_t n = ::read(in, buf.data(), buf.size());
        if (n < 0) { if (errno == EINTR) continue; err = "lecture " + std::string(std::strerror(errno)); ok = false; break; }
        if (n == 0) break;
        h.add(buf.data(), static_cast<std::size_t>(n));
        if (!w->write(buf.data(), static_cast<std::size_t>(n))) { err = "écriture sur " + st.label(); ok = false; break; }
        copied += static_cast<std::uint64_t>(n);
        tick(static_cast<std::uint64_t>(n));
    }
    ::close(in);
    if (ok && expect != UINT64_MAX && copied != expect) { err = "taille modifiée pendant la copie"; ok = false; }
    if (!ok) { w->abort(); return false; }
    if (!w->commit(err)) return false;
    sha = h.hex();
    return true;
}

// Copie d'un fichier d'un appareil vers la console (restauration), vérifiée par SHA-256.
bool copy_from_store(store::Store& st, const std::string& src, const std::string& dst_host, std::string& sha, std::uint64_t& copied,
                     const std::function<void(std::uint64_t)>& tick, const std::atomic<bool>& stop, std::string& err) {
    auto r = st.open(src, err);
    if (!r) return false;
    if (!fs::mkdirs(std::string(fs::dirname(dst_host)))) { err = "mkdir " + std::string(std::strerror(fs::last_error())); return false; }
    const std::string tmp = dst_host + ".part";
    const int out = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) { err = "écriture " + std::string(std::strerror(errno)); return false; }
    std::vector<char> buf(chunk);
    Sha256 h;
    copied = 0;
    bool ok = true;
    for (;;) {
        if (stop.load()) { err = "arrêt demandé"; ok = false; break; }
        const long n = r->read(buf.data(), buf.size());
        if (n < 0) { err = "lecture sur " + st.label(); ok = false; break; }
        if (n == 0) break;
        h.add(buf.data(), static_cast<std::size_t>(n));
        std::size_t off = 0;
        while (ok && off < static_cast<std::size_t>(n)) {
            const ssize_t w = ::write(out, buf.data() + off, static_cast<std::size_t>(n) - off);
            if (w < 0) { if (errno == EINTR) continue; err = "écriture " + std::string(std::strerror(errno)); ok = false; break; }
            off += static_cast<std::size_t>(w);
        }
        copied += static_cast<std::uint64_t>(n);
        tick(static_cast<std::uint64_t>(n));
    }
    if (ok && ::fsync(out) != 0) { err = "fsync"; ok = false; }
    ::close(out);
    if (ok && ::rename(tmp.c_str(), dst_host.c_str()) != 0) { err = "renommage " + std::string(std::strerror(errno)); ok = false; }
    if (!ok) { ::unlink(tmp.c_str()); return false; }
    sha = h.hex();
    return true;
}

store::Store& local_store() {
    static std::unique_ptr<store::Store> s = store::local();
    return *s;
}

std::string parts_label(const Request& r) {
    if (r.parts.empty()) return "complet";
    std::string s;
    for (auto p : r.parts) { if (!s.empty()) s += '+'; s += to_string(p); }
    return s;
}

}  // namespace

// ---- Journal ----------------------------------------------------------------

bool Journal::load() {
    lines_.clear();
    std::string text;
    if (!fs::read(path_, text)) return errno == ENOENT;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) break;  // dernière ligne incomplète : ignorée
        Json j;
        if (Json::parse(std::string_view(text).substr(start, end - start), j))
            lines_.push_back({j["rel"].as_string(), j["sha"].as_string(), static_cast<std::uint64_t>(j["size"].as_int())});
        start = end + 1;
    }
    return true;
}

bool Journal::done(const std::string& rel, std::uint64_t size, std::string* sha) const {
    for (const auto& l : lines_) if (l.rel == rel && l.size == size) { if (sha) *sha = l.sha; return true; }
    return false;
}

bool Journal::record(const std::string& rel, std::uint64_t size, const std::string& sha) {
    Json j = Json::object();
    j.set("rel", rel);
    j.set("size", size);
    j.set("sha", sha);
    std::string line = j.dump() + "\n";
    if (!fs::mkdirs(std::string(fs::dirname(path_)))) return false;
    int fd = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) return false;
    bool ok = ::write(fd, line.data(), line.size()) == static_cast<ssize_t>(line.size()) && ::fsync(fd) == 0;
    ::close(fd);
    if (ok) lines_.push_back({rel, sha, size});
    return ok;
}

// ---- Sauvegarde ---------------------------------------------------------------

std::string make_bundle_name(const Request& r) {
    std::string date = iso_time(now_unix()).substr(0, 10);
    return safe_name(r.item.title, 60) + " - " + parts_label(r) + " - " + date + " [" + random_id(4) + "].pbck";
}

std::string platform_bundle(const Item& item, const std::string& date) {
    const bool ps4 = item.kind == Kind::game_ps4;
    const std::string tag = ps4 ? "PS4" : item.platform == "Switch" ? "Switch" : "PS5";
    std::string name = safe_name(item.title, 80);
    return tag + " backup/[" + tag + "] " + name + " (" + item.title_id + ")/saves/" + date;
}

Result run(Request& req, Journal& journal, const ProgressFn& progress, const std::atomic<bool>& stop) {
    Result res;
    if (req.bundle_name.empty()) req.bundle_name = make_bundle_name(req);
    const std::string bundle = fs::join(req.folder, req.bundle_name);
    res.bundle = bundle;
    store::Store& st = req.store ? *req.store : local_store();
    if (!journal.load()) { res.error = "journal illisible"; return res; }

    std::vector<FileJob> jobs;
    Json comps = Json::array();
    for (const auto& c : req.item.components) {
        if (!wanted(req.parts, c.part)) continue;
        Json cj = Json::object();
        cj.set("part", to_string(c.part));
        cj.set("label", c.label);
        cj.set("restore_to", c.restore_to);
        if (c.meta.is_object()) cj.set("meta", c.meta);  // ex. compte d'une sauvegarde PS5 : utilisateur, identifiant, nom
        comps.push(cj);
        for (const auto& s : c.sources) expand(c.part, s.source, fs::join(to_string(c.part), s.relative), jobs);
    }
    if (jobs.empty()) { res.error = "aucun fichier à sauvegarder pour ces composants"; return res; }

    Progress pr;
    pr.files_total = static_cast<unsigned>(jobs.size());
    for (const auto& j : jobs) pr.bytes_total += j.size;
    log("backup start item=%s files=%u bytes=%llu bundle=%s", req.item.key.c_str(), pr.files_total,
        static_cast<unsigned long long>(pr.bytes_total), bundle.c_str());

    Json files = Json::array();
    for (const auto& j : jobs) {
        // Rangement par plateforme : images de sauvegarde à plat (rangement classique « PS5 backup/… »),
        // tout le reste (icônes, paramètres) dans « _pbc/data/ ».
        const bool image = req.flat && j.rel.find("/images/") != std::string::npos;
        const std::string stored = !req.flat ? fs::join("data", j.rel) : image ? std::string(fs::basename(j.rel)) : fs::join("_pbc/data", j.rel);
        const std::string dst = fs::join(bundle, stored);
        std::string sha;
        pr.current = j.rel;
        fs::Entry existing;
        if (journal.done(j.rel, j.size, &sha) && st.stat(dst, existing) && existing.size == j.size) {
            ++res.skipped;  // déjà copié avant l'interruption : ni relu ni rehaché
            pr.bytes_done += j.size;
        } else {
            std::string err;
            auto tick = [&](std::uint64_t n) { pr.bytes_done += n; if (progress) progress(pr); };
            if (!copy_to_store(fs::host(j.source), st, dst, j.size, sha, tick, stop, err)) {
                res.error = j.rel + " : " + err;
                log("backup failed file=%s error=%s", j.rel.c_str(), err.c_str());
                return res;
            }
            if (!journal.record(j.rel, j.size, sha)) { res.error = "journal : écriture impossible"; return res; }
            ++res.files;
        }
        ++pr.files_done;
        res.bytes += j.size;
        if (progress) progress(pr);
        Json fj = Json::object();
        fj.set("part", to_string(j.part));
        fj.set("path", j.rel);
        fj.set("source", j.source);
        fj.set("size", j.size);
        fj.set("sha256", sha);
        fj.set("stored", stored);
        {   // droits d'origine : le SHA-256 ne les garde pas, et un transport (FTP, autre PS5) peut les perdre
            struct ::stat sm {};
            if (::stat(fs::host(j.source).c_str(), &sm) == 0) fj.set("mode", static_cast<std::int64_t>(sm.st_mode & 07777));
        }
        files.push(fj);
    }

    Json m = Json::object();
    m.set("format", "pbck");
    m.set("version", 2);
    m.set("created", iso_time(now_unix()));
    m.set("created_unix", now_unix());
    m.set("disk_id", req.disk_id);
    m.set("item", req.item.to_json());
    m.set("components", comps);
    m.set("files", files);
    m.set("bytes", res.bytes);
    const std::string meta_dir = req.flat ? fs::join(bundle, "_pbc") : bundle;
    if (!st.write_small(fs::join(meta_dir, "manifest.json"), m.dump(true) + "\n") ||
        !st.write_small(fs::join(meta_dir, "complete"), sha256_hex(m.dump()) + "\n")) {
        res.error = "manifeste : écriture impossible";
        return res;
    }
    if (!req.flat) rebuild_index(st, req.folder);  // par plateforme : pas d'index au milieu des dossiers de l'utilisateur
    res.ok = true;
    log("backup done bundle=%s files=%u skipped=%u bytes=%llu", bundle.c_str(), res.files, res.skipped,
        static_cast<unsigned long long>(res.bytes));
    return res;
}

// ---- Index du disque -----------------------------------------------------------

bool rebuild_index(const std::string& folder) { return rebuild_index(local_store(), folder); }

bool rebuild_index(store::Store& st, const std::string& folder) {
    std::vector<fs::Entry> entries;
    Json idx = Json::object();
    idx.set("format", "pbck-index");
    idx.set("version", 3);
    idx.set("updated", iso_time(now_unix()));
    Json list = Json::array();
    if (st.list(folder, entries)) {
        for (const auto& e : entries) {
            if (!e.dir || e.name.size() < 5 || e.name.substr(e.name.size() - 5) != ".pbck") continue;
            const std::string b = fs::join(folder, e.name);
            std::string text;
            Json m;
            if (!st.read_small(fs::join(b, "manifest.json"), text) || !Json::parse(text, m)) continue;
            Json entry = Json::object();
            entry.set("bundle", e.name);
            entry.set("title", m["item"]["title"].as_string());
            entry.set("key", m["item"]["key"].as_string());
            entry.set("kind", m["item"]["kind"].as_string());
            entry.set("created", m["created"].as_string());
            entry.set("created_unix", m["created_unix"].as_int());
            entry.set("title_id", m["item"]["title_id"].as_string());
            // Comptes des sauvegardes PS5/PS4 contenues (vue « Sauvegardes par compte »).
            Json accounts = Json::array();
            for (const auto& c : m["components"].items()) {
                const Json& meta = c["meta"];
                if (!meta.is_object() || meta["user_id"].as_string().empty()) continue;
                Json a = Json::object();
                a.set("format", meta["format"]);
                a.set("user_id", meta["user_id"]);
                a.set("account_id", meta["account_id"]);
                a.set("account_name", meta["account_name"]);
                a.set("save_mtime", meta["mtime"]);  // heure de la sauvegarde du jeu (pas de la copie)
                if (meta["progress"].is_object()) a.set("progress", meta["progress"]);  // avancée au moment de la copie
                accounts.push(a);
            }
            entry.set("accounts", accounts);
            entry.set("bytes", m["bytes"].as_int());
            fs::Entry done;
            entry.set("complete", st.stat(fs::join(b, "complete"), done));
            Json parts = Json::array();
            for (const auto& c : m["components"].items()) parts.push(c["part"].as_string());
            entry.set("parts", parts);
            list.push(entry);
        }
    }
    idx.set("backups", list);
    return st.write_small(fs::join(folder, "index.json"), idx.dump(true) + "\n");
}

Json read_index(const std::string& folder, bool rebuild_if_needed) { return read_index(local_store(), folder, rebuild_if_needed); }

Json read_index(store::Store& st, const std::string& folder, bool rebuild_if_needed) {
    std::string text;
    Json idx;
    // Index d'une version plus ancienne : reconstruit depuis les manifestes (rien n'est perdu).
    if (st.read_small(fs::join(folder, "index.json"), text) && Json::parse(text, idx) && idx["backups"].is_array() && idx["version"].as_int() >= 3) return idx;  // v3 : heure des sauvegardes de jeu
    if (rebuild_if_needed && rebuild_index(st, folder) && st.read_small(fs::join(folder, "index.json"), text) && Json::parse(text, idx)) return idx;
    return Json::object();
}

// ---- Rangement par plateforme : lecture ------------------------------------------

Json scan_platform(store::Store& st, const std::string& root) {
    Json idx = Json::object(), list = Json::array();
    idx.set("format", "pbck-index");
    idx.set("version", 2);
    idx.set("layout", "platform");
    std::vector<fs::Entry> platforms;
    if (st.list(root, platforms)) {
        for (const auto& pf : platforms) {
            if (!pf.dir || pf.name.size() < 8 || pf.name.compare(pf.name.size() - 7, 7, " backup") != 0) continue;
            const std::string pdir = fs::join(root, pf.name);
            std::vector<fs::Entry> games;
            if (!st.list(pdir, games)) continue;
            for (const auto& g : games) {
                // « [PS5] Nom (PPSA01234) », aussi avec des ajouts (« (2025) (PPSA17599) (US) v1.400 »), ou un
                // dossier sans crochets qui contient « saves » (« Split Fiction ») : plateforme tirée du dossier parent.
                if (!g.dir || g.name.empty() || g.name[0] == '_' || g.name[0] == '.') continue;
                std::string platform = pf.name.substr(0, pf.name.size() - 7), title = g.name, tid;
                if (g.name[0] == '[') {
                    const auto close = g.name.find("] ");
                    if (close == std::string::npos) continue;
                    platform = g.name.substr(1, close - 1);
                    title = g.name.substr(close + 2);
                }
                for (std::size_t i = 0; i + 9 <= g.name.size(); ++i) {  // PPSA01234 / CUSA01234 n'importe où
                    const std::string c = g.name.substr(i, 4);
                    if ((c == "PPSA" || c == "CUSA") && std::all_of(g.name.begin() + static_cast<long>(i) + 4, g.name.begin() + static_cast<long>(i) + 9, ::isdigit)) { tid = g.name.substr(i, 9); break; }
                }
                if (const auto cut = title.find(" ("); cut != std::string::npos && cut > 0) title = title.substr(0, cut);
                if (title.empty()) title = tid.empty() ? g.name : tid;
                const std::string sdir = fs::join(fs::join(pdir, g.name), "saves");
                std::vector<fs::Entry> dates;
                if (!st.list(sdir, dates)) continue;
                std::string last_date;  // la plus récente : on lit l'heure exacte de ses fichiers (comparaison avec la console)
                for (const auto& d : dates) if (d.dir && d.name.size() >= 10 && d.name > last_date) last_date = d.name;
                for (const auto& d : dates) {
                    if (!d.dir || d.name.size() < 10) continue;
                    const std::string b = fs::join(sdir, d.name);
                    Json entry = Json::object();
                    entry.set("bundle", b.substr(root == "/" ? 1 : root.size() + 1));  // relatif à la racine
                    entry.set("title", title);
                    entry.set("title_id", tid);
                    entry.set("kind", platform == "PS4" ? "game_ps4" : platform == "Switch" ? "rom" : "game_ps5");
                    entry.set("platform", platform);
                    // Date du dossier « AAAA-MM-JJ » (midi UTC), précisée par notre manifeste s'il existe.
                    std::tm tm{};
                    tm.tm_year = std::atoi(d.name.substr(0, 4).c_str()) - 1900;
                    tm.tm_mon = std::atoi(d.name.substr(5, 2).c_str()) - 1;
                    tm.tm_mday = std::atoi(d.name.substr(8, 2).c_str());
                    tm.tm_hour = 12;
                    std::int64_t when = static_cast<std::int64_t>(timegm(&tm));
                    std::string text;
                    Json m;
                    Json accounts = Json::array(), parts = Json::array();
                    parts.push("save");
                    const bool ours = st.read_small(fs::join(fs::join(b, "_pbc"), "manifest.json"), text) && Json::parse(text, m);
                    if (ours) {
                        when = m["created_unix"].as_int(when);
                        entry.set("key", m["item"]["key"].as_string());
                        entry.set("bytes", m["bytes"].as_int());
                        for (const auto& c : m["components"].items()) {
                            const Json& meta = c["meta"];
                            if (!meta.is_object() || meta["user_id"].as_string().empty()) continue;
                            Json a = Json::object();
                            a.set("format", meta["format"]);
                            a.set("user_id", meta["user_id"]);
                            a.set("account_id", meta["account_id"]);
                            a.set("account_name", meta["account_name"]);
                            a.set("save_mtime", meta["mtime"]);
                            if (meta["progress"].is_object()) a.set("progress", meta["progress"]);
                            accounts.push(a);
                        }
                    }
                    if (!ours && d.name == last_date) {  // heure exacte = fichier le plus récent du dossier
                        std::vector<fs::Entry> files;
                        std::int64_t newest = 0;
                        std::uint64_t total = 0;
                        if (st.list(b, files)) for (const auto& f : files) if (!f.dir) { newest = std::max(newest, f.mtime); total += f.size; }
                        if (newest > 0) when = newest;
                        if (total) entry.set("bytes", total);
                    }
                    entry.set("legacy", !ours);  // fait hors de l'app (script de l'utilisateur) : images seules
                    entry.set("created", iso_time(when));
                    entry.set("created_unix", when);
                    entry.set("accounts", accounts);
                    entry.set("parts", parts);
                    entry.set("complete", true);
                    list.push(entry);
                }
            }
        }
    }
    idx.set("backups", list);
    return idx;
}

// ---- Restauration ---------------------------------------------------------------

Result restore(const RestoreRequest& req, const ProgressFn& progress, const std::atomic<bool>& stop) {
    Result res;
    res.bundle = req.bundle;
    store::Store& st = req.store ? *req.store : local_store();
    std::string text;
    Json m;
    fs::Entry done;
    // Manifeste à la racine du .pbck, ou dans « _pbc/ » pour une sauvegarde rangée par plateforme.
    std::string meta_dir = req.bundle;
    if (!st.stat(fs::join(meta_dir, "complete"), done)) meta_dir = fs::join(req.bundle, "_pbc");
    if (!st.stat(fs::join(meta_dir, "complete"), done)) { res.error = "sauvegarde incomplète"; return res; }
    if (!st.read_small(fs::join(meta_dir, "manifest.json"), text) || !Json::parse(text, m)) { res.error = "manifeste illisible"; return res; }

    Progress pr;
    for (const auto& f : m["files"].items()) {
        Part p;
        if (parse_part(f["part"].as_string(), p) && wanted(req.parts, p)) { ++pr.files_total; pr.bytes_total += static_cast<std::uint64_t>(f["size"].as_int()); }
    }
    for (const auto& f : m["files"].items()) {
        Part p;
        if (!parse_part(f["part"].as_string(), p) || !wanted(req.parts, p)) continue;
        const std::string rel = f["path"].as_string();  // <composant>/<relatif>
        // Destination : dossier neuf demandé, sinon l'emplacement d'origine du fichier.
        std::string dst;
        if (!req.target.empty()) dst = fs::join(req.target, rel);
        else dst = f["source"].as_string();
        if (rel.find("..") != std::string::npos || dst.empty()) { res.error = "chemin refusé : " + rel; return res; }
        if (!req.overwrite && fs::exists(fs::host(dst))) { res.error = "existe déjà : " + dst; return res; }
        std::string sha, err;
        std::uint64_t copied = 0;
        pr.current = rel;
        auto tick = [&](std::uint64_t n) { pr.bytes_done += n; if (progress) progress(pr); };
        const std::string stored = f["stored"].as_string().empty() ? fs::join("data", rel) : f["stored"].as_string();
        if (!copy_from_store(st, fs::join(req.bundle, stored), fs::host(dst), sha, copied, tick, stop, err)) {
            res.error = rel + " : " + err;
            return res;
        }
        if (copied != static_cast<std::uint64_t>(f["size"].as_int())) { res.error = "taille différente : " + rel; return res; }
        if (sha != f["sha256"].as_string()) { res.error = "empreinte différente : " + rel; return res; }
        {   // Droits : ceux d'origine (manifeste), sinon la règle des paquets (exécutables et modules), puis relecture.
            unsigned want = 0;
            if (f.has("mode")) want = static_cast<unsigned>(f["mode"].as_int()) & 07777;
            else if (p == Part::app && perms::executable_name(rel)) want = 0777;
            if (want) {
                const std::string h = fs::host(dst);
                struct ::stat st {};
                if (::chmod(h.c_str(), static_cast<mode_t>(want)) != 0 || ::stat(h.c_str(), &st) != 0 || ((want & 0111) && (st.st_mode & 0111) != (want & 0111))) {
                    res.error = "copie vérifiée, mais droits impossibles à appliquer : " + rel;
                    return res;
                }
            }
        }
        ++pr.files_done;
        ++res.files;
        res.bytes += copied;
        if (progress) progress(pr);
    }
    res.ok = true;
    log("restore done bundle=%s files=%u bytes=%llu target=%s", req.bundle.c_str(), res.files,
        static_cast<unsigned long long>(res.bytes), req.target.empty() ? "origine" : req.target.c_str());
    return res;
}

}  // namespace pbc::backup
