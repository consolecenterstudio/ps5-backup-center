#include "update/autostart.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>

#include "core/fs.hpp"
#include "core/util.hpp"

namespace pbc::autostart {
namespace {
// Payload Manager (itsPLK/ps5-payload-manager, GPL-3.0) : sa liste de démarrage et ses réglages.
constexpr const char* pm_dir = "/data/pldmgr";
constexpr const char* pm_autoload = "/data/pldmgr/autoload.txt";
constexpr const char* pm_config = "/data/pldmgr/pldmgr_config.txt";
constexpr const char* pm_slot_key = "local/pbc-backup-center";  // notre « emplacement » chez lui
constexpr const char* pm_slot_dir = "/data/pldmgr/payloads/local/pbc-backup-center";
constexpr const char* engine_file = "pbc-engine2.elf";

struct Launcher { const char* id; const char* name; int port; };
// Seul le chargeur du débridage lui-même (fourni par etaHEN / kstuff) : aucune appli externe. Une fois lancé, le moteur
// se relance seul (elfldr compilé dedans).
const Launcher kLaunchers[] = {{"elfldr", "elfldr (etaHEN / kstuff)", 9021}};

// Quelqu'un écoute-t-il sur ce port local ? (connexion seulement, rien n'est envoyé)
bool listening(int port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    const int fl = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool ok = ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
    if (!ok && errno == EINPROGRESS) {
        pollfd pf{fd, POLLOUT, 0};
        int err = 0;
        socklen_t el = sizeof err;
        ok = ::poll(&pf, 1, 400) == 1 && ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && err == 0;
    }
    ::close(fd);
    return ok;
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::size_t s = 0;
    while (s <= text.size()) {
        const auto e = text.find('\n', s);
        out.push_back(text.substr(s, e == std::string::npos ? std::string::npos : e - s));
        if (e == std::string::npos) break;
        s = e + 1;
    }
    return out;
}
std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}
bool is_ours(const std::string& line) {
    const std::string t = trimmed(line);
    return t == pm_slot_key || t == std::string(pm_slot_dir) + "/" + engine_file;
}
bool pm_autoload_on() {
    std::string text;
    if (!fs::read(fs::host(pm_config), text, 64 * 1024)) return false;  // pas de réglages : désactivé chez lui
    for (const auto& l : lines_of(text)) if (trimmed(l) == "AUTOLOAD_ENABLED=1") return true;
    return false;
}
bool entry_present() {
    std::string text;
    if (!fs::read(fs::host(pm_autoload), text, 64 * 1024)) return false;
    for (const auto& l : lines_of(text)) if (is_ours(l)) return true;
    return false;
}
bool same_content(const std::string& a, const std::string& b) {
    fs::Entry ea, eb;
    if (!fs::stat(a, ea) || !fs::stat(b, eb) || ea.size != eb.size) return false;
    std::string x, y;
    return fs::read(a, x, 64u << 20) && fs::read(b, y, 64u << 20) && x == y;
}
bool copy_engine(const std::string& app_dir, std::string& error) {
    std::string elf;
    if (!fs::read(fs::host(fs::join(app_dir, engine_file)), elf, 64u << 20) || elf.size() < 64) { error = "moteur installé introuvable"; return false; }
    if (!fs::mkdirs(fs::host(pm_slot_dir))) { error = "dossier de Payload Manager inaccessible"; return false; }
    if (!fs::write_atomic(fs::host(fs::join(pm_slot_dir, engine_file)), elf)) { error = "copie du moteur impossible"; return false; }
    // Sa fiche (même format que celles qu'il écrit) : il l'affiche dans sa liste comme un programme local.
    const std::string meta = std::string("{\n  \"schema\": 2,\n  \"kind\": \"local\",\n  \"filename\": \"") + engine_file +
                             "\",\n  \"installed_at\": \"" + iso_time(now_unix()) + "\"\n}\n";
    if (!fs::write_atomic(fs::host(fs::join(pm_slot_dir, "meta.json")), meta)) { error = "fiche Payload Manager impossible"; return false; }
    return true;
}
}  // namespace

const std::vector<std::string>& known_launchers() {
    static const std::vector<std::string> ids = [] { std::vector<std::string> v; for (const auto& l : kLaunchers) v.push_back(l.id); return v; }();
    return ids;
}

std::vector<std::string> clean_order(const Json& order) {
    std::vector<std::string> out;
    for (const auto& x : order.items()) {
        const std::string id = x.as_string();
        const auto& k = known_launchers();
        if (std::find(k.begin(), k.end(), id) != k.end() && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    }
    return out;
}

void write_launchers_file(const std::string& state_dir, const std::vector<std::string>& order) {
    std::string text;
    for (const auto& id : order) text += id + "\n";
    if (order.empty()) text = "none\n";  // tous désactivés : l'appli ne lance rien
    fs::write_atomic(fs::host(fs::join(state_dir, "launchers.txt")), text);
}

Json status(const std::string& app_dir, const std::vector<std::string>& order) {
    Json j = Json::object();
    Json arr = Json::array();
    // Activés d'abord, dans l'ordre choisi ; puis les désactivés.
    std::vector<const Launcher*> rows;
    for (const auto& id : order) for (const auto& l : kLaunchers) if (id == l.id) rows.push_back(&l);
    for (const auto& l : kLaunchers) if (std::find(order.begin(), order.end(), l.id) == order.end()) rows.push_back(&l);
    for (const Launcher* l : rows) {
        Json x = Json::object();
        x.set("id", l->id);
        x.set("name", l->name);
        x.set("port", static_cast<std::int64_t>(l->port));
        x.set("present", listening(l->port));
        x.set("enabled", std::find(order.begin(), order.end(), l->id) != order.end());
        arr.push(x);
    }
    j.set("launchers", arr);
    Json pm = Json::object();
    pm.set("present", fs::is_dir(fs::host(pm_dir)));
    pm.set("autoload_enabled", pm_autoload_on());
    const bool ours = entry_present();
    pm.set("enabled", ours);
    pm.set("copy_current", ours && same_content(fs::host(fs::join(app_dir, engine_file)), fs::host(fs::join(pm_slot_dir, engine_file))));
    j.set("pldmgr", pm);
    return j;
}

bool set_pldmgr(bool on, const std::string& app_dir, const std::string& state_dir, std::string& error) {
    if (!fs::is_dir(fs::host(pm_dir))) { error = "Payload Manager n'est pas installé sur cette console"; return false; }
    std::string text;
    const bool had = fs::read(fs::host(pm_autoload), text, 64 * 1024);
    if (!had) text.clear();
    if (text.find('\0') != std::string::npos) { error = "liste de Payload Manager illisible : rien n'a été modifié"; return false; }
    // Copie de sa liste telle qu'elle était, la première fois qu'on y touche.
    const std::string backup = fs::host(fs::join(state_dir, "pldmgr-autoload-avant.txt"));
    if (!fs::exists(backup)) fs::write_atomic(backup, had ? text : std::string("(pas de liste)\n"));
    std::string out;
    bool present = false;
    for (const auto& l : lines_of(text)) {
        if (is_ours(l)) { present = true; continue; }  // la nôtre : retirée ici, remise à la fin si « on »
        out += l + "\n";
    }
    while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();  // pas de lignes vides ajoutées
    if (out == "\n") out.clear();
    if (on) {
        if (!copy_engine(app_dir, error)) return false;
        out += std::string(pm_slot_key) + "\n";
    }
    if ((on || present) && !fs::write_atomic(fs::host(pm_autoload), out)) { error = "liste de Payload Manager non modifiable"; return false; }
    if (!on) fs::remove_tree(fs::host(pm_slot_dir));  // seulement notre dossier
    log("autostart pldmgr %s", on ? "added" : "removed");
    return true;
}

void refresh_pldmgr_copy(const std::string& app_dir) {
    if (!entry_present()) return;
    const std::string src = fs::host(fs::join(app_dir, engine_file)), dst = fs::host(fs::join(pm_slot_dir, engine_file));
    if (same_content(src, dst)) return;
    std::string err;
    if (copy_engine(app_dir, err)) log("autostart pldmgr copy refreshed");
    else log("autostart pldmgr copy: %s", err.c_str());
}

}  // namespace pbc::autostart
