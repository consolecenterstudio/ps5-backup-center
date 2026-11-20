// Lancer un jeu depuis son .pkg Kraken, dans notre moteur (docs/PLAN-lancement-kraken.md, lot 3 cas 2) :
// « Installer… › Le monter là où il est » d'un .pkg. Le travail est fait par pbc-kraken (code de Kraken Mount v0.3.0) :
// cache de démarrage + index AMPR, inscription du jeu, puis, à chaque lancement, nos bibliothèques (ampr-emu-ng,
// pgo_stub — compilées par nous) posées dans son bac à sable. Avant chaque inscription : kstuff présent, Kraken Mount
// arrêté, ni ShadowMount ni la console n'ont déjà ce jeu. La liste de nos jeux est gardée (kraken-titles.json) et la
// surveillance reprend au démarrage du moteur.
#include "engine.hpp"

#include <pthread.h>

#include <atomic>
#include <thread>

#include "core/fs.hpp"
#include "core/util.hpp"
#include "images/images.hpp"
#include "images/kraken_bridge.h"

namespace pbc {

#if defined(PBCK_PS5)
namespace {
// La surveillance des lancements des jeux .pkg : un fil du moteur, démarré une fois.
void kraken_watch(const std::string& tids) {
    pbc_kraken_watch(tids.c_str());
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 2u << 20);  // code Rust : une pile large, comme un fil Rust (2 Mo)
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_t th;
    if (pthread_create(&th, &a, [](void*) -> void* { pbc_kraken_watch_loop(); return nullptr; }, nullptr) != 0) log("kraken : surveillance des lancements impossible (fil)");
    pthread_attr_destroy(&a);
}
}  // namespace
#endif

namespace {
std::mutex kraken_mu;
void kraken_log(const char* line) { log("%s", line); }
}  // namespace

Json Engine::kraken_titles() {
    std::lock_guard<std::mutex> l(kraken_mu);
    std::string t;
    Json j;
    if (!fs::read(fs::host(fs::join(dir_, "kraken-titles.json")), t, 1u << 20) || !Json::parse(t, j) || !j.is_object()) j = Json::object();
    return j;
}

void Engine::kraken_note(const std::string& tid, const Json& entry) {
    std::lock_guard<std::mutex> l(kraken_mu);
    std::string t;
    Json j;
    if (!fs::read(fs::host(fs::join(dir_, "kraken-titles.json")), t, 1u << 20) || !Json::parse(t, j) || !j.is_object()) j = Json::object();
    j.set(tid, entry);
    fs::write_atomic(fs::host(fs::join(dir_, "kraken-titles.json")), j.dump() + "\n");
}

void Engine::kraken_start() {
    static bool once = false;
    if (once) return;
    once = true;
    pbc_kraken_setup(fs::host(fs::join(dir_, "kraken")).c_str(), kraken_log);
#if defined(PBCK_PS5)
    std::string tids;
    const Json t = kraken_titles();
    for (const auto& [tid, e] : t.members()) if (e["state"].as_string() == "registered") tids += (tids.empty() ? "" : ",") + tid;
    if (!tids.empty()) { kraken_watch(tids); log("kraken : surveillance des lancements (%s)", tids.c_str()); }
#endif
}

// Les outils installés sur la console : le dossier (installé) et le processus (en marche, console seulement).
Json Engine::mounting() {
    auto tool = [](const char* dir, const char* process) {
        Json t = Json::object();
        t.set("installed", fs::is_dir(fs::host(dir)));
#if defined(PBCK_PS5)
        t.set("running", pbc_kraken_process(process) != 0);
#else
        (void)process;
        t.set("running", false);
#endif
        return t;
    };
    Json out = Json::object();
    out.set("shadowmount", tool("/data/shadowmount", "shadowmountplus.elf"));
    out.set("img_mount", settings().img_mount);
    out.set("images_uses", images_uses());
    out.set("smp_state", smp_state());  // notre ShadowMount intégré : off, starting, running, stopped, failed
    out.set("krakenmount", tool("/data/krakenmount", "kraken-mount.elf"));
    out.set("pkg_mount", settings().pkg_mount);
    out.set("pkg_uses", pkg_uses());
    // Un .pkg peut-il se lancer ici ? Il faut kstuff (Kraken Mount ou notre version intégrée s'en servent).
#if defined(PBCK_PS5)
    const bool pkg_ok = pbc_kraken_kstuff() != 0;
#else
    const bool pkg_ok = !fs::is_dir(fs::host("/data/no-kstuff"));  // sur le Mac : « kstuff absent » simulé par les tests
#endif
    out.set("pkg_ok", pkg_ok);
    if (!pkg_ok) out.set("pkg_why", "kstuff absent");
    Json ours = Json::object();  // les versions intégrées à notre moteur (engine/third_party, compilées par nous)
    ours.set("kraken_mount", "0.3.0");
    ours.set("ampr_emu_ng", "0.4.1");
    ours.set("pgo_stub", "0.6");
    ours.set("dlc_emu", "0.3");
    ours.set("shadowmountplus", "1.7beta12");
    out.set("ours", ours);
    return out;
}

// « auto » : Kraken Mount s'il tourne, sinon le nôtre (pour rester fonctionnel sur une console qui ne l'a pas).
std::string Engine::pkg_uses() {
    const std::string m = settings().pkg_mount;
    if (m == "theirs" || m == "ours") return m;
#if defined(PBCK_PS5)
    return pbc_kraken_process("kraken-mount.elf") ? "theirs" : "ours";
#else
    return fs::is_dir(fs::host("/data/krakenmount/running")) ? "theirs" : "ours";  // sur le Mac : simulé par les tests
#endif
}

Json Engine::kraken_mount(const std::string& path, const std::string& title_id, std::string& error) {
    static std::atomic<bool> busy{false};
#if !defined(PBCK_PS5)
    (void)title_id;  // sur le Mac : aucune vérification de la console
#endif
#if defined(PBCK_PS5)
    if (!pbc_kraken_kstuff()) { error = "kstuff ne répond pas : il faut le débridage avec kstuff pour lancer depuis un .pkg"; return Json(); }
    if (pbc_kraken_process("kraken-mount.elf")) { error = "Kraken Mount tourne : arrêtez-le, ou choisissez « Kraken Mount installé » dans Paramètres › Montage des jeux (sinon les deux prendraient le même jeu)"; return Json(); }
    if (!title_id.empty()) {
        const Json m = images::mounts_json("/system_ex/app/" + title_id);
        for (const auto& x : m.items())
            if (x["on"].as_string() == "/system_ex/app/" + title_id && x["from"].as_string().find("/pbc-backup-center/kraken/") == std::string::npos) {
                error = "ce jeu est déjà monté par un autre outil (ShadowMount ?) : rien n'a été changé"; return Json();
            }
        if (fs::exists("/user/app/" + title_id + "/app.pkg")) { error = "ce jeu est déjà installé par la console : rien à monter"; return Json(); }
    }
#endif
    const std::string run = images::running_title();
    if (!run.empty() && run != "PPSA99950") { error = "un jeu est ouvert : fermez-le d'abord"; return Json(); }
    if (busy.exchange(true)) { error = "un autre .pkg est en préparation : un instant"; return Json(); }
    kraken_start();
    const std::string host_pkg = fs::host(path);
    std::thread([this, path, host_pkg] {
        char tid[32] = {0}, err[1024] = {0};
        Json e = Json::object();
        e.set("pkg", path);
        e.set("at", now_unix());
        log("kraken : préparation de %s", path.c_str());
        if (pbc_kraken_build(host_pkg.c_str(), tid, sizeof tid, err, sizeof err) != 0) {
            log("kraken : cache refusé (%s) : %s", path.c_str(), err);
            e.set("state", "failed"); e.set("error", std::string(err));
            kraken_note(std::string(fs::basename(path)), e);
            busy = false;
            return;
        }
        e.set("title_id", std::string(tid));
#if defined(PBCK_PS5)
        if (pbc_kraken_register(tid, err, sizeof err) != 0) {
            log("kraken : inscription refusée (%s) : %s", tid, err);
            e.set("state", "failed"); e.set("error", std::string(err));
        } else {
            e.set("state", "registered");
            log("kraken : %s inscrit depuis %s", tid, path.c_str());
        }
#else
        e.set("state", "cached");  // sur le Mac : seulement le cache (l'inscription se fait sur la console)
#endif
        kraken_note(tid, e);
#if defined(PBCK_PS5)
        std::string tids;
        const Json t = kraken_titles();
        for (const auto& [k, v] : t.members()) if (v["state"].as_string() == "registered") tids += (tids.empty() ? "" : ",") + k;
        kraken_watch(tids);
#endif
        ++revision_;
        busy = false;
    }).detach();
    Json out = Json::object();
    out.set("started", true);
    return out;
}

}  // namespace pbc
