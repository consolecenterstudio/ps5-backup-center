#include "files/files.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <unistd.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "core/fs.hpp"
#include "core/util.hpp"

namespace pbc::files {
namespace {

struct Item { std::string rel; std::uint64_t size = 0; };

std::mutex g_stage_mu;
std::string g_stage, g_stage_file;
std::int64_t g_stage_since = 0;
void stage(const char* what, const std::string& file = "") {
    std::lock_guard<std::mutex> l(g_stage_mu);
    g_stage = what; g_stage_file = file; g_stage_since = now_unix();
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a.back() == '/' ? a + b : a + "/" + b;
}


// « dossier/nom.ext » → « dossier/nom (n).ext » (l'extension reste à la fin).
std::string numbered(const std::string& path, int n) {
    const auto slash = path.rfind('/');
    const auto dot = path.rfind('.');
    const bool ext = dot != std::string::npos && (slash == std::string::npos || dot > slash + 1);
    const std::string base = ext ? path.substr(0, dot) : path, tail = ext ? path.substr(dot) : "";
    return base + " (" + std::to_string(n) + ")" + tail;
}

// Empreinte d'un fichier d'un appareil (relecture complète) ; vide si illisible.
// on_bytes : avancée (octets lus depuis le dernier appel), signalée tous les 8 Mo au plus.
std::string sha_of(store::Store& st, const std::string& path, std::atomic<bool>& interrupt,
                   const std::function<void(std::uint64_t)>& on_bytes = nullptr) {
    std::string err;
    auto r = st.open(path, err);
    if (!r) return "";
    Sha256 h;
    std::vector<char> buf(1u << 20);  // 1 Mo : moins d'allers-retours sur un appareil réseau
    std::uint64_t pending = 0;
    for (long n; (n = r->read(buf.data(), buf.size())) > 0;) {
        h.add(buf.data(), static_cast<std::size_t>(n));
        if (interrupt) return "";
        pending += static_cast<std::uint64_t>(n);
        if (on_bytes && pending >= (8u << 20)) { on_bytes(pending); pending = 0; }
    }
    if (on_bytes && pending) on_bytes(pending);
    return h.hex();
}

// Copie d'un flux avec lecture et écriture en parallèle : un fil lit des blocs de 4 Mo (au plus 3 d'avance, ~12 Mo),
// pendant que celui-ci les écrit et calcule l'empreinte. Avant : 256 Ko lus, puis écrits, puis le suivant — chaque
// aller-retour réseau attendait le précédent (NAS en SMB : ~7 Mo/s sur un réseau gigabit).
struct Copied { bool read_ok = true, write_ok = true, stopped = false; std::uint64_t bytes = 0; std::string sha; };
Copied pump(store::Reader& r, store::Writer& w, const std::function<void(std::uint64_t)>& on_bytes, std::atomic<bool>& interrupt) {
    constexpr std::size_t block = 4u << 20;
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::vector<char>> q;
    bool eof = false, rerr = false, quit = false;
    std::thread rd([&] {
        for (;;) {
            std::vector<char> b(block);
            std::size_t got = 0;
            bool bad = false;
            while (got < block) {
                if (interrupt) break;
                const long n = r.read(b.data() + got, block - got);
                if (n < 0) { bad = true; break; }
                if (n == 0) break;
                got += static_cast<std::size_t>(n);
            }
            b.resize(got);
            std::unique_lock<std::mutex> l(m);
            cv.wait(l, [&] { return q.size() < 3 || quit; });
            if (quit) return;
            if (got) q.push_back(std::move(b));
            if (bad) rerr = true;
            if (bad || got < block || interrupt) { eof = true; cv.notify_all(); return; }
            cv.notify_all();
        }
    });
    Copied c;
    Sha256 h;
    for (;;) {
        std::vector<char> b;
        {
            std::unique_lock<std::mutex> l(m);
            cv.wait(l, [&] { return !q.empty() || eof; });
            if (q.empty() && eof) break;
            b = std::move(q.front());
            q.pop_front();
            cv.notify_all();
        }
        if (interrupt) { c.stopped = true; break; }
        if (!w.write(b.data(), b.size())) { c.write_ok = false; break; }
        h.add(b.data(), b.size());
        c.bytes += b.size();
        on_bytes(b.size());
    }
    { std::lock_guard<std::mutex> l(m); quit = true; }
    cv.notify_all();
    stage("attente de la lecture en cours");
    rd.join();
    c.read_ok = !rerr;
    c.sha = h.hex();
    return c;
}

// Tout ce qu'il y a sous les éléments cochés : fichiers (taille) et dossiers (pour les recréer, et pour le déplacement).
bool collect(store::Store& st, const std::string& root, const std::string& rel, std::vector<Item>& files,
             std::vector<std::string>& dirs, std::string& error, int depth = 0) {
    if (depth > 64) { error = "dossiers trop imbriqués : " + rel; return false; }
    fs::Entry e;
    if (!st.stat(join(root, rel), e)) { error = "introuvable : " + rel; return false; }
    if (!e.dir) { files.push_back({rel, e.size}); return true; }
    dirs.push_back(rel);
    std::vector<fs::Entry> es;
    if (!st.list(join(root, rel), es)) { error = "dossier illisible : " + rel; return false; }
    for (const auto& c : es) {
        if (!safe_name(c.name)) continue;
        if (c.dir) { if (!collect(st, root, join(rel, c.name), files, dirs, error, depth + 1)) return false; }
        else files.push_back({join(rel, c.name), c.size});
    }
    return true;
}

// Journal : lignes « C|S \t taille \t sha \t rel source \t chemin destination » et « D \t chemin ».
struct JLine { char kind = 0; std::uint64_t size = 0; std::string sha, rel, dst; };
std::vector<JLine> journal_load(const std::string& path) {
    std::vector<JLine> out;
    if (path.empty()) return out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::string line;
    for (int c; (c = std::fgetc(f)) != EOF;) {
        if (c != '\n') { line += static_cast<char>(c); continue; }
        std::vector<std::string> parts;
        std::size_t s = 0;
        for (std::size_t e; (e = line.find('\t', s)) != std::string::npos; s = e + 1) parts.push_back(line.substr(s, e - s));
        parts.push_back(line.substr(s));
        JLine j;
        if (parts.size() == 5 && (parts[0] == "C" || parts[0] == "S")) {
            j.kind = parts[0][0]; j.size = std::strtoull(parts[1].c_str(), nullptr, 10); j.sha = parts[2]; j.rel = parts[3]; j.dst = parts[4];
            out.push_back(j);
        } else if (parts.size() == 2 && parts[0] == "D") { j.kind = 'D'; j.dst = parts[1]; out.push_back(j); }
        line.clear();
    }
    std::fclose(f);
    return out;
}
void journal_add(const std::string& path, const std::string& line) {
    if (path.empty()) return;
    FILE* f = std::fopen(path.c_str(), "ab");
    if (!f) return;
    std::fwrite(line.data(), 1, line.size(), f);
    std::fputc('\n', f);
    std::fflush(f);
    ::fsync(fileno(f));
    std::fclose(f);
}

}  // namespace

Json stage_json() {
    std::lock_guard<std::mutex> l(g_stage_mu);
    Json j = Json::object();
    j.set("stage", g_stage); j.set("file", g_stage_file); j.set("since", g_stage_since);
    return j;
}

bool safe_name(const std::string& name) {
    return !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos && name.find('\0') == std::string::npos;
}

Result run(const Request& req, const backup::ProgressFn& progress, std::atomic<bool>& interrupt) {
    Result res;
    Json copied = Json::array(), renamed = Json::array(), errors = Json::array();
    int n_copied = 0, n_skipped = 0, n_removed = 0;
    if (!req.src || !req.dst) { res.error = "endroit indisponible"; return res; }
    std::vector<Item> files;
    std::vector<std::string> dirs;
    stage("liste");
    for (const auto& name : req.names) {
        if (!safe_name(name)) { res.error = "nom invalide : " + name; return res; }
        if (!collect(*req.src, req.src_dir, name, files, dirs, res.error)) return res;
    }
    backup::Progress p;
    p.files_total = static_cast<unsigned>(files.size());
    for (const auto& f : files) p.bytes_total += f.size;
    if (progress) progress(p);
    // Place libre (si l'appareil la donne).
    if (fs::Space sp; req.dst->space(req.dst_dir.empty() ? "/" : req.dst_dir, sp) && sp.free && sp.free < p.bytes_total) {
        res.error = "pas assez de place à destination";
        return res;
    }
    // Dossiers à créer : notés dans le journal s'ils n'existaient pas (pour pouvoir les retirer s'ils restent vides).
    {
        fs::Entry de;
        std::vector<std::string> to_make{req.dst_dir};
        for (const auto& d : dirs) to_make.push_back(join(req.dst_dir, d));
        for (const auto& d : to_make) {
            const bool existed = req.dst->stat(d, de) && de.dir;
            if (!req.dst->mkdirs(d)) { if (d == req.dst_dir) { res.error = "dossier de destination impossible à créer"; return res; } continue; }
            if (!existed) journal_add(req.journal, "D\t" + d);
        }
    }
    // Reprise : ce que le journal dit déjà fait (et qui est toujours là, même taille) n'est ni relu ni recopié.
    std::map<std::string, JLine> done_before;
    for (const auto& j : journal_load(req.journal)) if (j.kind == 'C' || j.kind == 'S') done_before[j.rel] = j;

    std::vector<std::string> done_src;  // originaux copiés et vérifiés (pour « Déplacer »)
    for (const auto& f : files) {
        if (interrupt) { res.error = "arrêt demandé"; return res; }
        const std::string src_path = join(req.src_dir, f.rel);
        std::string dst_path = join(req.dst_dir, f.rel);
        p.current = f.rel;
        if (progress) progress(p);
        fs::Entry de;
        if (const auto it = done_before.find(f.rel); it != done_before.end() && it->second.size == f.size &&
                                                      req.dst->stat(it->second.dst, de) && !de.dir && de.size == f.size) {
            if (it->second.kind == 'C') { ++n_copied; copied.push(f.rel); } else ++n_skipped;
            ++p.files_done;
            p.bytes_done += f.size;
            done_src.push_back(src_path);
            if (progress) progress(p);
            continue;
        }
        // Déjà là ? Identique (taille + empreinte) : sauté. Différent : on garde les deux.
        std::string src_sha;
        if (req.dst->stat(dst_path, de) && !de.dir) {
            if (de.size == f.size) {
                p.phase = "compare"; p.phase_done = 0; p.phase_total = 2 * f.size;
                const auto adv = [&](std::uint64_t n) { p.phase_done += n; if (progress) progress(p); };
                stage("comparaison", f.rel);
                src_sha = sha_of(*req.src, src_path, interrupt, adv);
                const bool same = !src_sha.empty() && src_sha == sha_of(*req.dst, dst_path, interrupt, adv);
                p.phase.clear(); p.phase_done = p.phase_total = 0;
                if (same) {
                    journal_add(req.journal, "S\t" + std::to_string(f.size) + "\t" + src_sha + "\t" + f.rel + "\t" + dst_path);
                    ++n_skipped;
                    ++p.files_done;
                    p.bytes_done += f.size;
                    done_src.push_back(src_path);
                    if (progress) progress(p);
                    continue;
                }
            }
            int k = 2;
            for (; k < 1000 && req.dst->stat(numbered(dst_path, k), de); ++k) {}
            dst_path = numbered(dst_path, k);
            Json x = Json::object();
            x.set("from", f.rel);
            x.set("to", dst_path.substr(dst_path.rfind('/') + 1));
            renamed.push(x);
        }
        // Copie : .part puis validation, empreinte au passage.
        std::string err;
        auto r = req.src->open(src_path, err);
        auto w = r ? req.dst->create(dst_path, err) : nullptr;
        if (w) w->size_hint(f.size);
        if (!r || !w) {
            Json x = Json::object(); x.set("file", f.rel); x.set("error", err.empty() ? "ouverture impossible" : err); errors.push(x);
            p.bytes_done += f.size; ++p.files_done;
            continue;
        }
        stage("copie", f.rel);
        const Copied c = pump(*r, *w, [&](std::uint64_t n) { p.bytes_done += n; if (progress) progress(p); }, interrupt);
        stage(c.stopped ? "arrêt (après lecture)" : "validation", f.rel);
        if (c.stopped || interrupt) { stage("arrêt"); w->abort(); res.error = "arrêt demandé"; stage("arrêté"); return res; }
        const bool ok = c.read_ok && c.write_ok;
        const std::uint64_t got = c.bytes;
        const std::string sha = c.sha;
        if (!ok || got != f.size || !w->commit(err)) {
            w->abort();
            Json x = Json::object(); x.set("file", f.rel); x.set("error", !ok ? "écriture impossible" : got != f.size ? "lecture incomplète" : err); errors.push(x);
            p.bytes_done += f.size > got ? f.size - got : 0; ++p.files_done;
            continue;
        }
        // Vérification : relecture de la copie.
        stage("vérification", f.rel);
        p.phase = "verify"; p.phase_done = 0; p.phase_total = f.size;
        if (progress) progress(p);
        const std::string vsha = sha_of(*req.dst, dst_path, interrupt, [&](std::uint64_t n) { p.phase_done += n; if (progress) progress(p); });
        p.phase.clear(); p.phase_done = p.phase_total = 0;
        if (vsha != sha) {
            if (interrupt) { res.error = "arrêt demandé"; return res; }
            req.dst->remove(dst_path);
            Json x = Json::object(); x.set("file", f.rel); x.set("error", "copie différente de l'original (retirée)"); errors.push(x);
            ++p.files_done;
            continue;
        }
        journal_add(req.journal, "C\t" + std::to_string(f.size) + "\t" + sha + "\t" + f.rel + "\t" + dst_path);
        ++n_copied;
        ++p.files_done;
        copied.push(f.rel);
        done_src.push_back(src_path);
        if (progress) progress(p);
    }
    // Déplacer : seulement si tout est copié et vérifié ; puis les dossiers devenus vides, du plus profond au moins profond.
    if (req.move && errors.items().empty()) {
        for (const auto& s : done_src) if (req.src->remove(s)) ++n_removed;
        std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
        for (const auto& d : dirs) {
            std::vector<fs::Entry> rest;
            if (req.src->list(join(req.src_dir, d), rest) && rest.empty()) req.src->remove(join(req.src_dir, d));
        }
    }
    stage("fin");
    res.report = Json::object();
    res.report.set("copied", n_copied);
    res.report.set("skipped", n_skipped);
    res.report.set("removed", n_removed);
    res.report.set("renamed", renamed);
    res.report.set("errors", errors);
    res.report.set("files", static_cast<std::int64_t>(files.size()));
    res.report.set("bytes", static_cast<std::int64_t>(p.bytes_total));
    res.ok = errors.items().empty();
    if (!res.ok) res.error = std::to_string(errors.items().size()) + " fichier(s) en échec (voir le rapport)";
    if (req.move && !res.ok) res.error += " ; rien n'a été supprimé à l'origine";
    return res;
}

bool cleanup(store::Store& dst, const std::string& journal, Json& report, std::string& error) {
    const auto lines = journal_load(journal);
    if (lines.empty()) { error = "rien à retirer (aucun fichier créé par cette copie)"; return false; }
    int removed = 0, kept = 0, dirs = 0;
    std::vector<std::string> made;
    for (const auto& j : lines) {
        if (j.kind == 'D') { made.push_back(j.dst); continue; }
        if (j.kind != 'C') continue;  // « S » : était déjà là, jamais touché
        fs::Entry e;
        if (dst.stat(j.dst, e) && !e.dir && e.size == j.size && dst.remove(j.dst)) ++removed;
        else ++kept;
    }
    std::sort(made.begin(), made.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    for (const auto& d : made) {
        std::vector<fs::Entry> rest;
        if (dst.list(d, rest) && rest.empty() && dst.remove(d)) ++dirs;
    }
    report = Json::object();
    report.set("removed", removed);
    report.set("kept", kept);
    report.set("dirs", dirs);
    return true;
}

Result remove_items(store::Store& st, const std::string& dir, const std::vector<std::string>& names,
                    const backup::ProgressFn& progress, std::atomic<bool>& interrupt) {
    Result res;
    std::vector<Item> files;
    std::vector<std::string> dirs;
    stage("liste (suppression)");
    for (const auto& name : names) {
        if (!safe_name(name)) { res.error = "nom invalide : " + name; return res; }
        if (!collect(st, dir, name, files, dirs, res.error)) return res;
    }
    backup::Progress p;
    p.files_total = static_cast<unsigned>(files.size());
    for (const auto& f : files) p.bytes_total += f.size;
    if (progress) progress(p);
    Json errors = Json::array();
    int n_files = 0, n_dirs = 0;
    std::uint64_t bytes = 0;
    for (const auto& f : files) {
        if (interrupt) { res.error = "arrêt demandé"; return res; }
        const std::string path = join(dir, f.rel);
        stage("suppression", f.rel);
        p.current = f.rel;
        if (st.remove(path)) { ++n_files; bytes += f.size; }
        else { Json x = Json::object(); x.set("file", f.rel); x.set("error", "suppression impossible"); errors.push(x); }
        ++p.files_done;
        p.bytes_done += f.size;
        if (progress) progress(p);
    }
    // Dossiers : du plus profond au moins profond, puis les dossiers cochés eux-mêmes.
    std::vector<std::string> all = dirs;
    for (const auto& name : names) {
        fs::Entry e;
        if (st.stat(join(dir, name), e) && e.dir && std::find(all.begin(), all.end(), name) == all.end()) all.push_back(name);
    }
    std::sort(all.begin(), all.end(), [](const std::string& a, const std::string& b) {
        return std::count(a.begin(), a.end(), '/') > std::count(b.begin(), b.end(), '/');
    });
    for (const auto& d : all) {
        if (interrupt) { res.error = "arrêt demandé"; return res; }
        if (st.remove(join(dir, d))) ++n_dirs;
        else { Json x = Json::object(); x.set("file", d + "/"); x.set("error", "dossier non vide ou protégé"); errors.push(x); }
    }
    stage("fin");
    res.report = Json::object();
    res.report.set("removed_files", n_files);
    res.report.set("removed_dirs", n_dirs);
    res.report.set("bytes", bytes);
    res.report.set("errors", errors);
    res.ok = errors.items().empty();
    if (!res.ok) res.error = std::to_string(errors.size()) + " élément(s) non supprimé(s)";
    return res;
}

}  // namespace pbc::files
