// Notification système : la demande du noyau (sceKernelSendNotificationRequest), comme les autres outils de la scène
// (Game Compressor, Garlic…). Lien faible : si la fonction manque, rien n'est envoyé.
// Disposition de la demande (0xC30 octets) : 11 entiers, puis « utiliser une image d'icône » (0x2C), le message (0x2D,
// 1024 octets), l'adresse de l'icône (0x42D, 1024 octets) — même disposition que libhijacker (OrbisNotificationRequest).
#include "core/notify.hpp"

#include <cstdio>
#include <cstring>

#include "core/util.hpp"

#if defined(PBCK_PS5)
namespace {
struct NotifyRequest {
    char head[44];        // type, req_id, priority, msg_id, target_id, user_id, unk1, unk2, app_id, error_num, unk3
    char use_icon_uri;    // 0x2C
    char message[1024];   // 0x2D
    char icon_uri[1024];  // 0x42D
    char unk[1027];       // 0x82D … 0xC30
};
static_assert(sizeof(NotifyRequest) == 0xC30, "taille de la demande de notification");
}  // namespace
extern "C" int sceKernelSendNotificationRequest(int, NotifyRequest*, size_t, int) __attribute__((weak));
#endif

namespace pbc::notify {

// Icône : aucune. Constaté à l'écran : avec « utiliser une image d'icône », la PS5
// n'affiche pas un bandeau avec notre logo mais un panneau « Debug: » (quel que soit le format d'adresse : chemin,
// file://, cxml://). Le bandeau normal reste celui sans icône.
static const char* const our_icon = "";

void send_with_icon(const std::string& title, const std::string& detail, const std::string& icon_uri) {
    log("notification: %s | %s%s%s", title.c_str(), detail.c_str(), icon_uri.empty() ? "" : " | icône ", icon_uri.c_str());
#if defined(PBCK_PS5)
    if (!sceKernelSendNotificationRequest) return;
    NotifyRequest r;
    std::memset(&r, 0, sizeof r);
    if (detail.empty()) std::snprintf(r.message, sizeof r.message, "%s", title.c_str());
    else std::snprintf(r.message, sizeof r.message, "%s\n%s", title.c_str(), detail.c_str());
    if (!icon_uri.empty()) {
        r.use_icon_uri = 1;
        std::snprintf(r.icon_uri, sizeof r.icon_uri, "%s", icon_uri.c_str());
    }
    sceKernelSendNotificationRequest(0, &r, sizeof r, 0);
#endif
}

void send(const std::string& title, const std::string& detail) { send_with_icon(title, detail, our_icon); }

}  // namespace pbc::notify
