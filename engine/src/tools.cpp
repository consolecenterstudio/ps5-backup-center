// Paramètres › Outils de la console (docs/maquettes/lanceur-outils.html) : les outils installés (payloads rangés par
// Payload Manager, applis outils), ceux qui tournent, et les lancer ou les arrêter d'ici. On utilise ce qui est
// installé, sans rien copier ni modifier chez eux. Façon de faire de Payload Manager (itsPLK, GPL-3.0, lue dans ses
// sources : process_mgr.c, ps5_launcher.c) : un payload en marche = un processus « *.elf » qui n'est pas une appli
// (sceKernelGetAppInfo : app_id 0), sauf mini-syscore.elf ; arrêt = SIGTERM, 1,5 s d'attente, puis SIGKILL. Le lancement,
// lui, passe par notre elfldr intégré (spawn_elf), pas par un port. La liste des processus n'est lue qu'à la demande.
#include "engine.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <strings.h>
#include <signal.h>
#include <unistd.h>

#include "core/fs.hpp"
#include "core/util.hpp"
#include "images/images.hpp"
#include "update/self_launch.hpp"

#if defined(PBCK_PS5)
#include <sys/sysctl.h>
extern "C" {
struct PbcToolAppInfo {
    std::uint32_t app_id;
    std::uint64_t unknown1;
    char title_id[14];
    char unknown2[0x3c];
};
int sceKernelGetAppInfo(pid_t pid, PbcToolAppInfo* info);
struct PbcToolLaunchParam {
    std::uint32_t size;
    std::int32_t user_id;
    std::int32_t app_opt;
    std::uint64_t crash_report;
    std::uint64_t check_flag;
};
int sceSystemServiceLaunchApp(const char* title_id, char** argv, PbcToolLaunchParam* param);
int sceUserServiceInitialize(const void*);
int sceUserServiceGetForegroundUser(int*);
}
#endif

namespace pbc {

namespace {
constexpr const char* pm_payloads = "/data/pldmgr/payloads";
// Jamais arrêtés d'ici : le débridage et nous-mêmes.
bool protected_process(const std::string& name) {
    return name.rfind("kstuff", 0) == 0 || name == "pbc-engine2.elf" || name == "mini-syscore.elf";
}
bool ends_with_elf(const std::string& n) { return n.size() > 4 && strcasecmp(n.c_str() + n.size() - 4, ".elf") == 0; }
}  // namespace

// Les payloads en marche : {pid, name, memory_mb}. PS5 seulement (sur le Mac : liste simulée par les tests).
Json Engine::payload_processes() {
    Json out = Json::array();
#if defined(PBCK_PS5)
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    std::size_t len = 0;
    if (::sysctl(mib, 4, nullptr, &len, nullptr, 0) != 0 || len == 0 || len > (16u << 20)) return out;
    std::string buf(len + 4096, '\0');
    std::size_t used = buf.size();
    if (::sysctl(mib, 4, buf.data(), &used, nullptr, 0) != 0 || used > buf.size()) return out;
    // Disposition de kinfo_proc sur la PS5 (Payload Manager, process_records.h) : taille +0, pid +72, rss +264, nom +447 (20).
    std::size_t off = 0;
    const pid_t self = ::getpid();
    while (off + 4 <= used) {
        int size = 0;
        std::memcpy(&size, buf.data() + off, 4);
        if (size <= 0 || off + static_cast<std::size_t>(size) > used || static_cast<std::size_t>(size) < 467) break;
        pid_t pid = 0;
        std::int64_t rss = 0;
        char name[21] = {0};
        std::memcpy(&pid, buf.data() + off + 72, 4);
        std::memcpy(&rss, buf.data() + off + 264, 8);
        std::memcpy(name, buf.data() + off + 447, 20);
        off += static_cast<std::size_t>(size);
        const std::string n(name);
        if (pid <= 0 || !ends_with_elf(n)) continue;
        PbcToolAppInfo ai{};
        if (sceKernelGetAppInfo(pid, &ai) == 0 && ai.app_id != 0) continue;  // une appli, pas un payload
        Json p = Json::object();
        p.set("pid", static_cast<std::int64_t>(pid));
        p.set("name", n);
        p.set("memory_mb", static_cast<std::int64_t>((rss * 16384) >> 20));
        p.set("self", pid == self);
        p.set("protected", protected_process(n) || pid == self);
        out.push(p);
    }
#else
    // Mac (tests) : /data/pbc-test-processes.json = [{pid, name}].
    std::string t;
    Json j;
    if (fs::read(fs::host("/data/pbc-test-processes.json"), t, 1u << 16) && Json::parse(t, j) && j.is_array())
        for (auto p : j.items()) { p.set("protected", protected_process(p["name"].as_string())); out.push(p); }
#endif
    return out;
}

Json Engine::tools_json() {
    const Json procs = payload_processes();
    // Le processus d'un payload : même nom que le fichier, ou le même sans ce qui suit (« game-compressor-v1.1.1.elf »
    // tourne sous le nom « game-compressor.elf », « ftpsrv-ps5.elf » sous « ftpsrv.elf »).
    auto stem = [](std::string n) {
        if (n.size() > 4) n.resize(n.size() - 4);
        for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return n;
    };
    auto running = [&](const std::string& file) -> std::int64_t {
        const std::string f = stem(file);
        for (const auto& p : procs.items()) if (stem(p["name"].as_string()) == f) return p["pid"].as_int();
        std::int64_t best = 0;
        std::size_t best_len = 0;
        for (const auto& p : procs.items()) {
            const std::string n = stem(p["name"].as_string());
            if (n.size() >= 4 && n.size() > best_len && f.size() > n.size() && f.compare(0, n.size(), n) == 0 && (f[n.size()] == '-' || f[n.size()] == '_')) {
                best = p["pid"].as_int();
                best_len = n.size();
            }
        }
        return best;
    };
    // La liste de démarrage de Payload Manager (autoload.txt) : « local/<nom> » par ligne.
    std::string al;
    fs::read(fs::host("/data/pldmgr/autoload.txt"), al, 1u << 16);
    Json payloads = Json::array();
    for (const char* kind : {"local", "repo"}) {
        const std::string base = std::string(pm_payloads) + "/" + kind;
        std::vector<fs::Entry> dirs;
        fs::list(fs::host(base), dirs);
        std::sort(dirs.begin(), dirs.end(), [](const fs::Entry& a, const fs::Entry& b) { return a.name < b.name; });
        for (const auto& d : dirs) {
            if (!d.dir || d.name.empty() || d.name[0] == '.') continue;
            const std::string dir = base + "/" + d.name;
            std::string mt;
            Json meta;
            std::string file;
            if (fs::read(fs::host(dir + "/meta.json"), mt, 1u << 16) && Json::parse(mt, meta)) file = meta["filename"].as_string();
            if (file.empty()) {  // pas de fiche : le premier .elf du dossier
                std::vector<fs::Entry> fl;
                fs::list(fs::host(dir), fl);
                for (const auto& f : fl) if (!f.dir && ends_with_elf(f.name)) { file = f.name; break; }
            }
            if (file.empty() || file.find('/') != std::string::npos || !fs::exists(fs::host(dir + "/" + file))) continue;
            const std::string key = std::string(kind) + "/" + d.name;
            Json p = Json::object();
            p.set("key", key);
            p.set("name", d.name);
            p.set("file", file);
            p.set("autoload", ("\n" + al + "\n").find("\n" + key + "\n") != std::string::npos);
            const std::int64_t pid = running(file);
            p.set("pid", pid);
            for (const auto& pr : procs.items()) if (pid > 0 && pr["pid"].as_int() == pid) p.set("proc", pr["name"].as_string());  // son nom de processus
            p.set("protected", protected_process(file) || d.name == "pbc-backup-center");
            payloads.push(p);
        }
    }
    // Applis outils : Payload Manager (PLDM00001) et celles qu'on reconnaît à leur nom.
    Json apps = Json::array();
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) {
            if (it.kind != Kind::app) continue;
            std::string low = it.title;
            for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (it.title_id == "PLDM00001" || low.find("payload") != std::string::npos) {
                Json a = Json::object();
                a.set("title_id", it.title_id);
                a.set("title", it.title);
                apps.push(a);
            }
        }
    }
    Json out = Json::object();
    out.set("pldmgr", fs::is_dir(fs::host("/data/pldmgr")));
    out.set("payloads", payloads);
    out.set("processes", procs);
    out.set("apps", apps);
    return out;
}

Json Engine::tool_start(const std::string& key, std::string& error) {
    const std::string run = images::running_title();
    if (!run.empty() && run != "PPSA99950") { error = "un jeu est ouvert : fermez-le d'abord"; return Json(); }
    const Json t = tools_json();
    Json p;
    for (const auto& x : t["payloads"].items()) if (x["key"].as_string() == key) p = x;
    if (!p.is_object()) { error = "outil introuvable : " + key; return Json(); }
    if (p["protected"].as_bool()) { error = "cet outil ne se lance pas d'ici"; return Json(); }
    if (p["pid"].as_int() > 0) { error = p["file"].as_string() + " tourne déjà"; return Json(); }
    const std::string file = p["file"].as_string();
    // Jamais deux outils de montage : le nôtre s'arrête d'abord (il démonte proprement).
    if (file == "shadowmountplus.elf") smp_shutdown();
    std::string elf;
    const std::string path = std::string(pm_payloads) + "/" + key + "/" + file;
    if (!fs::read(fs::host(path), elf, 64u << 20) || elf.empty()) { error = "lecture impossible : " + path; return Json(); }
    std::string err;
    const int pid = spawn_elf(elf, file.c_str(), err);
    if (pid < 0) { error = err.empty() ? std::string("lancement impossible") : err; return Json(); }
    log("outils : %s lancé (pid %d) depuis %s", file.c_str(), pid, path.c_str());
    Json out = Json::object();
    out.set("pid", static_cast<std::int64_t>(pid));
    return out;
}

Json Engine::tool_stop(std::int64_t pid, const std::string& name, std::string& error) {
    Json found;
    const Json procs = payload_processes();
    for (const auto& p : procs.items()) if (p["pid"].as_int() == pid) found = p;
    if (!found.is_object() || found["name"].as_string() != name) { error = "ce processus n'est plus là"; return Json(); }
    if (found["protected"].as_bool()) { error = name + " est protégé : il ne s'arrête pas d'ici"; return Json(); }
    const std::string run = images::running_title();
    const bool mount_tool = name == "shadowmountplus.elf" || name == "kraken-mount.elf";
    if (mount_tool && !run.empty() && run != "PPSA99950") { error = "un jeu est ouvert : ses montages ne sont pas touchés pendant une partie"; return Json(); }
    Json out = Json::object();
    if (name == "shadowmountplus.elf") {  // sa propre façon de s'arrêter : il démonte proprement avant
        fs::write_atomic(fs::host("/data/shadowmount/STOP"), "");
        log("outils : arrêt demandé à ShadowMountPlus (fichier STOP)");
        out.set("graceful", true);
        return out;
    }
#if defined(PBCK_PS5)
    if (::kill(static_cast<pid_t>(pid), SIGTERM) != 0 && errno == ESRCH) { out.set("stopped", true); return out; }
    for (int i = 0; i < 30; ++i) {
        if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) { log("outils : %s arrêté (pid %lld)", name.c_str(), static_cast<long long>(pid)); out.set("stopped", true); return out; }
        ::usleep(50000);
    }
    if (::kill(static_cast<pid_t>(pid), SIGKILL) != 0 && errno != ESRCH) { error = "arrêt refusé par le système"; return Json(); }
    log("outils : %s arrêté de force (pid %lld)", name.c_str(), static_cast<long long>(pid));
    out.set("stopped", true);
#else
    out.set("stopped", true);  // sur le Mac : rien à arrêter
#endif
    return out;
}

Json Engine::tool_open_app(const std::string& title_id, std::string& error) {
    bool ok = false;
    const Json tools = tools_json();
    for (const auto& a : tools["apps"].items()) if (a["title_id"].as_string() == title_id) ok = true;
    if (!ok) { error = "appli inconnue : " + title_id; return Json(); }
    const std::string run = images::running_title();
    if (!run.empty() && run != "PPSA99950") { error = "un jeu est ouvert : fermez-le d'abord"; return Json(); }
#if defined(PBCK_PS5)
    (void)sceUserServiceInitialize(nullptr);
    int user = -1;
    (void)sceUserServiceGetForegroundUser(&user);
    PbcToolLaunchParam p{sizeof(PbcToolLaunchParam), user, 0, 0, 0};
    const int rc = sceSystemServiceLaunchApp(title_id.c_str(), nullptr, &p);
    log("outils : ouverture de %s rc=0x%08x", title_id.c_str(), static_cast<unsigned>(rc));
    if (rc != 0) { error = "la console a refusé d'ouvrir l'appli"; return Json(); }
#endif
    Json out = Json::object();
    out.set("opened", true);
    return out;
}

}  // namespace pbc
