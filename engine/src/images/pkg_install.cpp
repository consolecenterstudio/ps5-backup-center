// Installeur de la console (voir pkg_install.hpp). Les structures et l'ordre des appels suivent ce que font Alembic v0.7.0
// (src/engine/platform_ps5.cpp, GPL-3.0-or-later), ps5-web-file-manager et shsrv : tailles vérifiées (0x38, 0x2700), le
// système écrit toute la structure PlayGo. Les fonctions sont cherchées à l'exécution dans libSceAppInstUtil.sprx.
#include "images/pkg_install.hpp"

#include <cstdint>
#include <cstdio>
#include <mutex>

#if defined(PBCK_PS5)
#include <ps5/kernel.h>
#include <unistd.h>
extern "C" int sceKernelLoadStartModule(const char*, std::size_t, const void*, std::uint32_t, void*, int*);
#endif

namespace pbc::pkginst {

#if defined(PBCK_PS5)
namespace {
struct PkgMetadata {
    const char* uri;
    const char* ex_uri;
    const char* playgo_scenario_id;
    const char* content_id;
    const char* content_name;
    const char* icon_url;
    std::uint32_t slot;
    std::uint32_t is_playgo_enabled;
};
static_assert(sizeof(PkgMetadata) == 0x38, "sceAppInstUtil : métadonnées");
struct PkgInfo {
    char content_id[48];
    int type;
    int platform;
};
struct PlaygoInfo {
    char languages[30][8];
    char scenario_ids[64][3];
    char content_ids[64][48];
    long unknown[810];
};
static_assert(sizeof(PlaygoInfo) == 0x2700, "sceAppInstUtil : PlayGo");

struct Api {
    int (*initialize)(void) = nullptr;
    int (*install_by_package)(const PkgMetadata*, PkgInfo*, PlaygoInfo*) = nullptr;
    int (*user_service_initialize)(const int*) = nullptr;
    int (*uninstall)(const char*) = nullptr;
    std::string note;
};

bool handle_of(const char* name, const char* path, std::uint32_t& h, std::string& note) {
    if (kernel_dynlib_handle(-1, name, &h) == 0) return true;
    const int rc = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, nullptr);
    if (rc > 0 && kernel_dynlib_handle(-1, name, &h) == 0) { note += std::string(name) + " chargé ; "; return true; }
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", static_cast<unsigned>(rc));
    note += std::string(path) + " introuvable (" + b + ") ; ";
    return false;
}

const Api& api() {
    static Api a;
    static std::once_flag once;
    std::call_once(once, [] {
        std::uint32_t h = 0, uh = 0;
        if (handle_of("libSceAppInstUtil.sprx", "/system/common/lib/libSceAppInstUtil.sprx", h, a.note)) {
            a.initialize = reinterpret_cast<int (*)(void)>(kernel_dynlib_dlsym(-1, h, "sceAppInstUtilInitialize"));
            a.install_by_package = reinterpret_cast<int (*)(const PkgMetadata*, PkgInfo*, PlaygoInfo*)>(kernel_dynlib_dlsym(-1, h, "sceAppInstUtilInstallByPackage"));
            a.uninstall = reinterpret_cast<int (*)(const char*)>(kernel_dynlib_dlsym(-1, h, "sceAppInstUtilAppUnInstall"));
        }
        if (handle_of("libSceUserService.sprx", "/system/common/lib/libSceUserService.sprx", uh, a.note))
            a.user_service_initialize = reinterpret_cast<int (*)(const int*)>(kernel_dynlib_dlsym(-1, uh, "sceUserServiceInitialize"));
        if (!a.initialize) a.note += "sceAppInstUtilInitialize absent ; ";
        if (!a.install_by_package) a.note += "sceAppInstUtilInstallByPackage absent ; ";
    });
    return a;
}

int init_once() {
    static std::once_flag once;
    static int rc = -1;
    std::call_once(once, [] {
        const Api& a = api();
        if (a.user_service_initialize) { const int prio = 256; a.user_service_initialize(&prio); }
        rc = a.initialize ? a.initialize() : -1;
    });
    return rc;
}
}  // namespace

bool available(std::string& note) {
    const Api& a = api();
    note = a.note;
    return a.initialize && a.install_by_package;
}

bool install(const std::string& path, const std::string& content_id, const std::string& title, std::string& error) {
    std::string note;
    if (!available(note)) { error = "installeur de la console introuvable : " + note; return false; }
    char code[16];
    if (const int rc = init_once(); rc != 0) {
        std::snprintf(code, sizeof code, "0x%08x", static_cast<unsigned>(rc));
        error = std::string("installeur de la console : démarrage refusé (") + code + ")";
        return false;
    }
    // L'installeur tourne dans un autre processus, qui voit l'interne en /user/data (avec /data : refus 0x80b2150f, vu par
    // Alembic sur une console le 24/10).
    const std::string uri = path.rfind("/data/", 0) == 0 ? "/user" + path : path;
    const std::string name = title.empty() ? path.substr(path.rfind('/') + 1) : title;
    PkgMetadata meta{};
    meta.uri = uri.c_str();
    meta.ex_uri = meta.playgo_scenario_id = meta.icon_url = "";
    meta.content_id = content_id.c_str();
    meta.content_name = name.c_str();
    PkgInfo info{};
    static PlaygoInfo playgo;  // ~10 Ko ; une installation à la fois (verrou de l'appelant)
    playgo = PlaygoInfo{};
    if (const int rc = api().install_by_package(&meta, &info, &playgo); rc != 0) {
        std::snprintf(code, sizeof code, "0x%08x", static_cast<unsigned>(rc));
        error = std::string("la console a refusé le paquet (") + code + ")";
        return false;
    }
    return true;
}

// Retirer un jeu inscrit par nous (Kraken : lancé depuis son .pkg) : la console le désinstalle elle-même.
bool uninstall(const std::string& title_id, std::string& error) {
    if (!api().uninstall) { error = "désinstalleur de la console introuvable"; return false; }
    char code[16];
    if (const int rc = init_once(); rc != 0) {
        std::snprintf(code, sizeof code, "0x%08x", static_cast<unsigned>(rc));
        error = std::string("désinstalleur de la console : démarrage refusé (") + code + ")";
        return false;
    }
    if (const int rc = api().uninstall(title_id.c_str()); rc != 0) {
        std::snprintf(code, sizeof code, "0x%08x", static_cast<unsigned>(rc));
        error = std::string("la console a refusé la désinstallation (") + code + ")";
        return false;
    }
    return true;
}
#else
bool uninstall(const std::string&, std::string& error) { error = "seulement sur la console"; return false; }
bool available(std::string& note) { note = "seulement sur la console"; return false; }
bool install(const std::string&, const std::string&, const std::string&, std::string& error) { error = "l'installation d'un paquet se fait seulement sur la console"; return false; }
#endif

}  // namespace pbc::pkginst
