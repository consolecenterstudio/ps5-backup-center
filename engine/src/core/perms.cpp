// Droits des fichiers d'un paquet d'appli ou de jeu : voir perms.hpp.
#include "core/perms.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "core/fs.hpp"

namespace pbc::perms {

namespace {

bool ends_with(std::string_view s, std::string_view suf) {
    if (s.size() < suf.size()) return false;
    for (std::size_t i = 0; i < suf.size(); ++i) {
        char a = s[s.size() - suf.size() + i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (a != suf[i]) return false;
    }
    return true;
}

constexpr unsigned exec_mode = 0777, dir_mode = 0777;

void walk(const std::string& root, const std::string& rel, int depth, bool fix, const std::map<std::string, unsigned>& modes, Report& rep) {
    if (depth > 32) { rep.failed.push_back(rel + " : arborescence trop profonde"); return; }
    std::vector<fs::Entry> entries;
    const std::string here = rel.empty() ? root : fs::join(root, rel);
    if (!fs::list(here, entries)) { rep.failed.push_back((rel.empty() ? "." : rel) + " : dossier illisible"); return; }
    for (const auto& e : entries) {
        const std::string r = rel.empty() ? e.name : rel + "/" + e.name, p = fs::join(root, r);
        struct ::stat st {};
        if (::lstat(p.c_str(), &st) != 0) { rep.failed.push_back(r + " : introuvable"); continue; }
        if (S_ISLNK(st.st_mode)) continue;  // un lien n'a pas de droits propres
        const unsigned cur = static_cast<unsigned>(st.st_mode & 07777);
        unsigned want = cur;
        bool must_exec = false;
        if (S_ISDIR(st.st_mode)) {
            ++rep.dirs;
            if ((cur & 0111) != 0111) want = dir_mode;  // traversée
            must_exec = true;
        } else if (S_ISREG(st.st_mode)) {
            const auto m = modes.find(r);
            if (m != modes.end()) want = m->second & 07777;          // droits d'origine (manifeste)
            if (executable_name(r) || (want & 0111)) {
                ++rep.exec_files;
                must_exec = true;
                if ((want & 0111) != 0111) want = exec_mode;
            }
        } else continue;
        if (fix && want != cur) {
            if (::chmod(p.c_str(), static_cast<mode_t>(want)) != 0) { rep.failed.push_back(r + " : droits impossibles à appliquer (" + std::strerror(errno) + ")"); continue; }
            ++rep.fixed;
        }
        // Relecture : ce qui doit s'exécuter (ou se traverser) le peut vraiment.
        if (must_exec) {
            struct ::stat again {};
            if (::lstat(p.c_str(), &again) != 0) rep.failed.push_back(r + " : relecture impossible");
            else if ((again.st_mode & 0111) != 0111) {
                char b[16];
                std::snprintf(b, sizeof b, "%04o", static_cast<unsigned>(again.st_mode & 07777));
                rep.failed.push_back(r + (S_ISDIR(again.st_mode) ? " : dossier non traversable (" : " : pas exécutable (") + b + ")");
            }
        }
        if (S_ISDIR(st.st_mode)) walk(root, r, depth + 1, fix, modes, rep);
    }
}

}  // namespace

bool executable_name(std::string_view rel) {
    const auto slash = rel.rfind('/');
    const std::string_view base = slash == std::string_view::npos ? rel : rel.substr(slash + 1);
    if (rel.rfind("sce_module/", 0) == 0 || rel.find("/sce_module/") != std::string_view::npos) return true;
    if (ends_with(base, "eboot.bin")) return base.size() == 9;
    return ends_with(base, ".elf") || ends_with(base, ".self") || ends_with(base, ".prx") || ends_with(base, ".sprx");
}

bool is_app_folder(const std::string& host_root) { return fs::exists(fs::join(host_root, "sce_sys/param.json")) || fs::exists(fs::join(host_root, "sce_sys/param.sfo")); }

std::string Report::summary() const {
    if (failed.empty()) return "";
    return failed.size() == 1 ? failed[0] : failed[0] + " (+" + std::to_string(failed.size() - 1) + ")";
}

Report apply_app(const std::string& host_root, const std::map<std::string, unsigned>& modes) {
    Report rep;
    struct ::stat st {};
    if (::stat(host_root.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 0111) != 0111) {
        if (::chmod(host_root.c_str(), dir_mode) == 0) ++rep.fixed; else rep.failed.push_back(". : droits impossibles à appliquer");
    }
    walk(host_root, "", 0, true, modes, rep);
    return rep;
}

Report check_app(const std::string& host_root) {
    Report rep;
    walk(host_root, "", 0, false, {}, rep);
    return rep;
}

}  // namespace pbc::perms
