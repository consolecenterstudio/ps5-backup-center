// Images et dossiers de jeux : le ShadowMountPlus installé, ou notre version intégrée (images/smp_bridge.c) quand il
// n'est pas là (Paramètres › Montage des jeux, réglage img_mount ; docs/PLAN-integration-shadowmount.md). Jamais les
// deux en même temps : le nôtre ne démarre que si le sien ne tourne pas, et s'arrête (en démontant ce qu'il a monté) si
// le sien apparaît. Vérifié toutes les 30 s (une lecture de la liste des processus), jamais pendant une partie.
#include "engine.hpp"

#include "core/fs.hpp"
#include "images/images.hpp"
#include "images/kraken_bridge.h"
#if defined(PBCK_PS5)
#include "images/smp_bridge.h"
#endif

namespace pbc {

#if defined(PBCK_PS5)
namespace {
void smp_log(const char* line) { log("%s", line); }
}  // namespace
#endif

bool Engine::smp_installed_running() {
#if defined(PBCK_PS5)
    return pbc_kraken_process("shadowmountplus.elf") != 0;
#else
    return fs::is_dir(fs::host("/data/shadowmount/running"));  // sur le Mac : simulé par les tests
#endif
}

// « auto » : le ShadowMountPlus installé s'il tourne, sinon le nôtre ; « theirs » : seulement le sien. Pas de « toujours
// le nôtre » : on n'arrête jamais le sien, qui garde donc la main dès qu'il tourne.
std::string Engine::images_uses() {
    const std::string m = settings().img_mount;
    if (m == "theirs") return "theirs";
    if (smp_installed_running()) return "theirs";
    return "ours";
}

std::string Engine::smp_state() {
#if defined(PBCK_PS5)
    switch (pbc_smp_state()) {
        case PBC_SMP_STARTING: return "starting";
        case PBC_SMP_RUNNING: return "running";
        case PBC_SMP_STOPPED: return "stopped";
        case PBC_SMP_FAILED: return "failed";
        default: return "off";
    }
#else
    return "off";
#endif
}

void Engine::smp_tick(const std::string& running_title) {
#if defined(PBCK_PS5)
    const int st = pbc_smp_state();
    const bool want = images_uses() == "ours";
    if (st == PBC_SMP_RUNNING || st == PBC_SMP_STARTING) {
        // Le sien est apparu (ou le réglage dit « le sien ») : le nôtre s'arrête et démonte ; jamais pendant une partie.
        if (!want && (running_title.empty() || running_title == "PPSA99950")) {
            log("smp : %s — notre ShadowMount intégré s'arrête", smp_installed_running() ? "ShadowMountPlus installé en marche" : "réglage « ShadowMountPlus installé »");
            pbc_smp_stop();
        }
        return;
    }
    if (want && st == PBC_SMP_OFF) {
        log("smp : ShadowMountPlus installé absent — démarrage de notre ShadowMount intégré");
        if (pbc_smp_start(smp_log) != 0) log("smp : démarrage impossible");
    }
#else
    (void)running_title;
#endif
}

void Engine::smp_scan_now() {
#if defined(PBCK_PS5)
    pbc_smp_scan_now();
#endif
}

void Engine::smp_shutdown() {
#if defined(PBCK_PS5)
    pbc_smp_stop();
#endif
}

}  // namespace pbc
