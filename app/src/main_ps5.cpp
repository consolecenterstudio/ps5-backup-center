// Point d'entrée sur la PS5 : affichage, manette, lancement du moteur si besoin.
#include <arpa/inet.h>
#include <cstdlib>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#include <sys/stat.h>

#include "audio.hpp"
#include "demo_renderer.hpp"
#include "fonts.hpp"
#include "heap.hpp"
#include "i18n.hpp"
#include "pbck_language.hpp"
#include "model.hpp"
#include "pbck_ui_memory.hpp"
#include "ui.hpp"

extern "C" {
int sceKernelDebugOutText(int, const char*);
std::uint64_t sceKernelGetProcessTime();
int sceUserServiceInitialize(const void*);
int sceUserServiceGetInitialUser(int*);
int scePadInit();
int scePadOpen(int, int, int, const void*);
int scePadClose(int);
int sceSystemServiceLoadExec(const char*, char* const*);
int sceNetCtlInit();
int sceNetCtlGetInfo(int, void*);
}

namespace {

using namespace app;

struct PadSample {
    std::uint32_t buttons;
    struct Stick { std::uint8_t x, y; } left_stick, right_stick;
    std::uint8_t triggers[2];
    std::uint8_t reserved0[66];
    std::int32_t connected;
    std::uint64_t timestamp_us;
    std::uint8_t reserved1[16];
    std::uint8_t connection_generation;
    std::uint8_t reserved2[15];
};
static_assert(sizeof(PadSample) == 120);
extern "C" int scePadRead(int, PadSample*, int);

void klog(const char* fmt, const char* a = "", long b = 0) {
    char line[256];
    std::snprintf(line, sizeof line, fmt, a, b);
    sceKernelDebugOutText(0, line);
}

// Manette : croix et sticks équivalents, même répétition en maintenant (350 ms puis 140 ms).
class Pad {
public:
    InputState read(std::uint64_t now_us) {
        InputState in;
        if (handle_ < 0 && now_us >= next_open_) {
            next_open_ = now_us + 1000000;
            int user = 0;
            (void)sceUserServiceInitialize(nullptr);
            if (sceUserServiceGetInitialUser(&user) == 0 && scePadInit() >= 0) handle_ = scePadOpen(user, 0, 0, nullptr);
        }
        if (handle_ < 0) { in.connected = false; return in; }
        std::array<PadSample, 16> s{};
        const int n = scePadRead(handle_, s.data(), static_cast<int>(s.size()));
        if (n < 0) { scePadClose(handle_); handle_ = -1; in.connected = false; return in; }
        std::uint32_t buttons = held_;
        bool connected = connected_;
        int sx = 128, sy = 128;
        for (int i = 0; i < n; ++i) {
            connected = s[static_cast<std::size_t>(i)].connected != 0 && !(s[static_cast<std::size_t>(i)].buttons & 0x80000000u);
            buttons = connected ? s[static_cast<std::size_t>(i)].buttons : 0;
            const auto& l = s[static_cast<std::size_t>(i)].left_stick;
            const auto& r = s[static_cast<std::size_t>(i)].right_stick;
            const bool lc = std::abs(l.x - 128) < 40 && std::abs(l.y - 128) < 40;
            sx = lc ? r.x : l.x;
            sy = lc ? r.y : l.y;
        }
        // Manette déconnectée longtemps (batterie, Remote Play renouvelé) : on rouvre.
        if (!connected) {
            if (!lost_since_) lost_since_ = now_us;
            else if (now_us - lost_since_ > 1000000) { scePadClose(handle_); handle_ = -1; lost_since_ = 0; }
            held_ = 0; dir_ = 0; connected_ = false;
            in.connected = false;
            return in;
        }
        lost_since_ = 0;
        if (!connected_) { held_ = buttons; connected_ = true; return in; }  // pas d'appui fantôme à la reconnexion
        // Stick → direction (seuil 60 %, axe dominant).
        std::uint32_t stick = 0;
        const int dx = sx - 128, dy = sy - 128;
        if (std::abs(dx) > 76 || std::abs(dy) > 76) stick = std::abs(dx) > std::abs(dy) ? (dx < 0 ? key::left : key::right) : (dy < 0 ? key::up : key::down);
        const std::uint32_t dirs = key::up | key::down | key::left | key::right;
        const std::uint32_t dir = (buttons & dirs) ? (buttons & dirs) : stick;
        std::uint32_t pressed = buttons & ~held_ & ~dirs & ~key::circle;
        if (dir != dir_) { dir_ = dir; if (dir) { pressed |= dir; next_repeat_ = now_us + 350000; } }
        else if (dir && now_us >= next_repeat_) { pressed |= dir; next_repeat_ = now_us + 140000; }
        // Rond : court = retour ; maintenu 2 s = quitter.
        if ((buttons & key::circle) && !(held_ & key::circle)) circle_since_ = now_us;
        if (buttons & key::circle) {
            const auto t = now_us - circle_since_;
            if (t > 350000) in.quit_progress = static_cast<unsigned>(std::min<std::uint64_t>(1000, t / 2000));
        } else if ((held_ & key::circle) && now_us - circle_since_ < 350000) pressed |= key::circle;
        held_ = buttons;
        in.pressed = pressed;
        in.down = buttons;
        return in;
    }
private:
    int handle_ = -1;
    std::uint64_t next_open_ = 0, next_repeat_ = 0, circle_since_ = 0, lost_since_ = 0;
    std::uint32_t held_ = 0, dir_ = 0;
    bool connected_ = false;
};

// Démarrage du moteur (/app0/pbc-engine2.elf) quand il ne tourne pas : notre module de lancement regarde quels
// chargeurs d'ELF sont présents sur cette console et envoie le moteur au premier qui répond. Une appli n'a pas les
// droits de créer le processus elle-même ; une fois lancé, le moteur se relance seul (elfldr intégré).
// Seul le chargeur du débridage (elfldr 9021, etaHEN / kstuff) : aucune appli externe. Et seulement quand aucun moteur
// ne tourne à l'ouverture (jamais pour relancer un moteur en marche : le moteur se relance seul, elfldr intégré).
namespace {
// Connexion locale avec délai court : -1 si personne n'écoute sur ce port.
int connect_local(std::uint16_t port, int timeout_ms) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    const int fl = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        if (errno != EINPROGRESS) { ::close(fd); return -1; }
        pollfd pf{fd, POLLOUT, 0};
        int err = 0;
        socklen_t el = sizeof err;
        if (::poll(&pf, 1, timeout_ms) != 1 || ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err != 0) { ::close(fd); return -1; }
    }
    ::fcntl(fd, F_SETFL, fl);
    timeval tv{20, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return fd;
}
bool send_all_fd(int fd, const std::string& data) {
    for (std::size_t sent = 0; sent < data.size();) {
        const ssize_t w = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (w <= 0) return false;
        sent += static_cast<std::size_t>(w);
    }
    return true;
}
}  // namespace

bool launch_engine() {
    // Une relance du moteur est en cours (marqueur posé par le moteur, consommé par le nouveau) : ne rien lancer.
    {
        struct stat mk{};
        if (::stat("/data/pbc-backup-center/takeover-request", &mk) == 0 && ::time(nullptr) - mk.st_mtime < 120) {
            klog("PBC2APP engine launch skipped: engine restart in progress%s%ld\n", "", 0L);
            return false;
        }
    }
    // Réglage écrit par le moteur (Paramètres › Démarrage du moteur) ; « none » : rien.
    std::vector<std::string> order;
    if (FILE* lf = std::fopen("/data/pbc-backup-center/launchers.txt", "r")) {
        char line[64];
        while (std::fgets(line, sizeof line, lf)) {
            std::string id(line);
            while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' ')) id.pop_back();
            if (!id.empty()) order.push_back(id);
        }
        std::fclose(lf);
    }
    if (order.empty()) order = {"elfldr"};
    if (order.size() == 1 && order[0] == "none") { klog("PBC2APP engine launch: all loaders disabled%s%ld\n", "", 0L); return false; }
    const int f = ::open("/app0/pbc-engine2.elf", O_RDONLY);
    if (f < 0) { klog("PBC2APP engine elf missing errno=%s%ld\n", "", errno); return false; }
    std::string elf;
    char buf[65536];
    for (ssize_t n; (n = ::read(f, buf, sizeof buf)) > 0;) elf.append(buf, static_cast<std::size_t>(n));
    ::close(f);
    for (const auto& id : order) {
        if (id == "elfldr") {  // chargeur standard des débridages : l'ELF brut sur 9021
            const int fd = connect_local(9021, 700);
            if (fd < 0) continue;
            const bool ok = send_all_fd(fd, elf);
            ::close(fd);
            klog("PBC2APP engine launch loader=elfldr-9021 sent=%s bytes=%ld\n", ok ? "1" : "0", static_cast<long>(elf.size()));
            return ok;
        }
    }
    klog("PBC2APP engine launch: no ELF loader found%s%ld\n", "", 0L);
    return false;
}

std::string console_address() {
    // L'info 14 (adresse IP) remplit l'union SceNetCtlInfo entière : 256 octets, pas 16.
    alignas(8) char info[256] = {0};
    (void)sceNetCtlInit();
    if (sceNetCtlGetInfo(14, info) != 0 || !info[0] || ::memchr(info, 0, 16) == nullptr) return std::string("http://") + tr("<adresse de la console>") + ":52370";
    return std::string("http://") + info + ":52370";
}

pbck::UiMemory memory;
std::uint32_t* frame = nullptr;
Model* model = nullptr;
Covers* covers = nullptr;
Ui* ui = nullptr;
Pad pad;
bool dirty = true;
bool engine_launched = false;

// Polices de secours (latin étendu, grec, cyrillique, arabe, CJK) : arène à part, taille exacte
// (fichiers + cache de glyphes). Sans elle l'app reste utilisable, en latin courant seulement.
void load_fallback_fonts() {
    std::size_t sizes[fonts::slot_count] = {}, total = fonts::cache_bytes;
    for (unsigned i = 0; i < fonts::slot_count; ++i) {
        const std::string path = std::string("/app0/assets/fonts/") + fonts::file_names[i];
        struct stat st {};
        if (::stat(path.c_str(), &st) == 0 && st.st_size > 0) sizes[i] = static_cast<std::size_t>(st.st_size);
        total += (sizes[i] + 63) & ~std::size_t{63};
    }
    static pbck::UiMemory arena(total);
    if (!arena.initialize()) { klog("PBC2APP fonts memory failed op=%s status=%ld\n", arena.operation(), arena.status()); return; }
    auto* cache = reinterpret_cast<unsigned char*>(arena.pixels(fonts::cache_bytes / 4));
    fonts::set_cache(cache, cache ? fonts::cache_bytes : 0);
    for (unsigned i = 0; i < fonts::slot_count; ++i) {
        if (!sizes[i]) continue;
        auto* data = reinterpret_cast<unsigned char*>(arena.pixels((sizes[i] + 3) / 4));
        const std::string path = std::string("/app0/assets/fonts/") + fonts::file_names[i];
        const int f = data ? ::open(path.c_str(), O_RDONLY) : -1;
        std::size_t got = 0;
        while (f >= 0 && got < sizes[i]) { const ssize_t n = ::read(f, data + got, sizes[i] - got); if (n <= 0) break; got += static_cast<std::size_t>(n); }
        if (f >= 0) ::close(f);
        const bool ok = got == sizes[i] && fonts::add(static_cast<fonts::Slot>(i), data, got);
        klog("PBC2APP font %s ok=%ld\n", fonts::file_names[i], ok ? 1 : 0);
    }
    klog("PBC2APP step=fonts used=%s%ld\n", "", static_cast<long>(arena.used()));
}

void draw(ps5::demo::Canvas& canvas) noexcept {
    static bool first = true;
    if (first) { first = false; klog("PBC2APP step=first-frame%s%ld\n"); }
    const std::uint64_t now = sceKernelGetProcessTime();
    const InputState in = pad.read(now);
    static int marks = 0;  // repères de démarrage (diagnostic) : les trois premières images
    if (ui->update(in, now / 1000)) dirty = true;
    if (marks < 3) klog("PBC2APP step=update-done frame=%s%ld\n", "", static_cast<long>(marks));
    {   // Relevé mémoire toutes les 30 s (Ko) : réserve utilisée / pic, tas système utilisé, secours.
        static std::uint64_t last = 0;
        if (now - last > 30000000ull) {
            last = now;
            const auto h = app::heap::stats();
            char line[200];
            std::snprintf(line, sizeof line, "PBC2APP mem reserve=%zu/%zu peak=%zu system=%zu peak=%zu fallbacks=%zu (Ko)\n", h.in_use >> 10,
                          h.capacity >> 10, h.peak >> 10, h.system_in_use >> 10, h.system_peak >> 10, h.fallbacks);
            sceKernelDebugOutText(0, line);
        }
    }
    if (in.quit_progress >= 1000) {
        klog("PBC2APP quit%s%ld\n");
        sceSystemServiceLoadExec("exit", nullptr);
    }
    if (ui->quit_requested()) {
        klog("PBC2APP quit for relaunch%s%ld\n");
        sceSystemServiceLoadExec("exit", nullptr);
    }
    if (ui->start_engine_requested()) launch_engine();
    // Démarrage automatique : seulement si le moteur ne répond pas À L'OUVERTURE. Déjà vu en marche : plus jamais
    // automatiquement (une relance de mise à jour coupe le moteur 2 s ; en lancer un autre en même temps faisait deux
    // moteurs, ou un lancement raté chez le chargeur).
    if (model->snapshot()->connected) engine_launched = true;
    if (!engine_launched && now > 2000000 && !model->snapshot()->connected) { engine_launched = true; launch_engine(); }
    if (dirty) {
        Surface s(frame);
        ui->draw(s);
        dirty = false;
        if (marks < 3) klog("PBC2APP step=draw-done frame=%s%ld\n", "", static_cast<long>(marks));
    }
    if (marks < 3) ++marks;
    canvas.image({frame, static_cast<std::size_t>(screen_w * screen_h)});
}

}  // namespace

int main() {
    klog("PBC2APP start%s%ld\n");
#if defined(APP2_STAGE)
    klog("PBC2APP stage=%s%ld\n", "log-only", APP2_STAGE);
    if (APP2_STAGE == 1) { for (;;) ::sleep(1); }
#endif
    if (!memory.initialize()) { klog("PBC2APP memory init failed op=%s status=%ld\n", memory.operation(), memory.status()); return 1; }
    frame = memory.pixels(static_cast<std::size_t>(screen_w) * screen_h);
    // Réserve mémoire de l'app (le tas système ne fait que ~13 Mo) : 96 Mo si possible, sinon moins.
    {
        static pbck::UiMemory r96(96u << 20), r64(64u << 20), r32(32u << 20);
        for (pbck::UiMemory* r : {&r96, &r64, &r32}) {
            if (!r->initialize()) continue;
            const bool ok = app::heap::init(r->pixels(r->capacity() / 4), r->capacity());
            klog("PBC2APP heap reserve ok=%s mb=%ld\n", ok ? "1" : "0", static_cast<long>(r->capacity() >> 20));
            if (ok) break;
        }
    }
    klog("PBC2APP step=memory ok=%s used=%ld\n", frame ? "1" : "0", static_cast<long>(memory.used()));
    // Police : en-tête 16 octets (« P5IM », largeur, hauteur, 1) puis pixels.
    Font font;
    {
        const int f = ::open("/app0/assets/font.rgba", O_RDONLY);
        std::uint32_t hdr[4] = {0, 0, 0, 0};
        if (f >= 0 && ::read(f, hdr, 16) == 16 && hdr[0] == 0x4d493550 && hdr[1] == 2048 && hdr[2] > 0 && hdr[2] <= 2048) {
            std::uint32_t* px = memory.pixels(static_cast<std::size_t>(hdr[1]) * hdr[2]);
            const std::size_t bytes = static_cast<std::size_t>(hdr[1]) * hdr[2] * 4;
            std::size_t got = 0;
            while (px && got < bytes) { const ssize_t n = ::read(f, reinterpret_cast<char*>(px) + got, bytes - got); if (n <= 0) break; got += static_cast<std::size_t>(n); }
            if (got == bytes) font.atlas = {static_cast<int>(hdr[1]), static_cast<int>(hdr[2]), px};
        }
        if (f >= 0) ::close(f);
    }
    klog("PBC2APP step=font ok=%s%ld\n", font.atlas.valid() ? "1" : "0");
    load_fallback_fonts();
    constexpr int cover_slots = 72;  // 256×256 : 18 Mo
    std::uint32_t* cover_mem = memory.pixels(static_cast<std::size_t>(Covers::size) * Covers::size * cover_slots);
    if (!frame || !font.atlas.valid() || !cover_mem) { klog("PBC2APP assets failed used=%s%ld\n", "", static_cast<long>(memory.used())); return 2; }
    klog("PBC2APP step=covers-mem%s%ld\n");
    static Endpoint ep;          // boucle locale : pas de jeton
    klog("PBC2APP step=endpoint%s%ld\n");
    static Model m(ep);
    klog("PBC2APP step=model%s%ld\n");
    static Covers c(ep, cover_mem, cover_slots);
    if (std::uint32_t* big = memory.pixels(static_cast<std::size_t>(Covers::large) * Covers::large * 2)) c.set_large_memory(big);
    klog("PBC2APP step=covers%s%ld\n");
    // Langue : celle de la console (réglage système), tant que le réglage du moteur dit « auto ».
    const auto sys = pbck::read_system_language();
    i18n::init("/app0/assets/i18n", i18n::from_console(sys.status, sys.code));
    klog("PBC2APP step=language %s code=%ld\n", i18n::current().code.c_str(), sys.code);
    const std::string addr = console_address();
    klog("PBC2APP step=address %s%ld\n", addr.c_str());
    // Thème : la dernière valeur connue (copie locale), appliquée avant la première image — le moteur peut
    // mettre longtemps à répondre au démarrage de la console ; sa valeur reste la référence ensuite.
    if (FILE* tf = std::fopen("/data/pbc-backup-center/tv-theme", "r")) {
        char id[64] = {0};
        if (std::fgets(id, sizeof id, tf)) { id[std::strcspn(id, "\r\n")] = 0; app::theme::set(id); }
        std::fclose(tf);
    }
    static Ui u(m, c, font, addr);
    // Logo et images d'appareils (assets/*.rgba, même format que la police, 256×256 au plus).
    auto load_rgba = [&](const char* path) -> Image {
        Image img;
        const int f = ::open(path, O_RDONLY);
        std::uint32_t hdr[4] = {0, 0, 0, 0};
        if (f >= 0 && ::read(f, hdr, 16) == 16 && hdr[0] == 0x4d493550 && hdr[1] > 0 && hdr[1] <= 256 && hdr[2] > 0 && hdr[2] <= 256) {
            std::uint32_t* px = memory.pixels(static_cast<std::size_t>(hdr[1]) * hdr[2]);
            const std::size_t bytes = static_cast<std::size_t>(hdr[1]) * hdr[2] * 4;
            std::size_t got = 0;
            while (px && got < bytes) { const ssize_t n = ::read(f, reinterpret_cast<char*>(px) + got, bytes - got); if (n <= 0) break; got += static_cast<std::size_t>(n); }
            if (got == bytes) img = {static_cast<int>(hdr[1]), static_cast<int>(hdr[2]), px};
        }
        if (f >= 0) ::close(f);
        return img;
    };
    u.set_logo(load_rgba("/app0/assets/logo.rgba"));
    for (int i = 0; i < Ui::device_count; ++i) {
        const std::string path = std::string("/app0/assets/device-") + Ui::device_files[i] + ".rgba";
        u.set_device(i, load_rgba(path.c_str()));
    }
    // Son : effets (~0,2 Mo) et musique d'ambiance (~1,5 Mo) dans ce qui reste de l'arène ; sans place, effets seuls.
    {
        std::size_t px = 480 * 1024;
        std::uint32_t* snd = memory.pixels(px);
        if (!snd) snd = memory.pixels(px = 64 * 1024);
        klog("PBC2APP step=audio-mem used=%s%ld\n", snd ? "" : "none ", static_cast<long>(memory.used()));
        app::audio::start("/app0/assets", snd, snd ? px * 4 : 0);
    }
    model = &m; covers = &c; ui = &u;
    klog("PBC2APP step=objects%s%ld\n");
    m.start();
    klog("PBC2APP step=model-thread%s%ld\n");
    c.start();
    klog("PBC2APP step=threads%s%ld\n");
    ps5::demo::run_frames(draw, "PS5 Backup Center");
}
