#include "accounts/accounts.hpp"

#include <cstdio>
#include <cstdlib>

#include "core/fs.hpp"
#include "core/util.hpp"

#if defined(PBCK_PS5)
extern "C" {
int sceUserServiceInitialize(const void*);
int sceUserServiceGetNpAccountId(int, std::uint64_t*);
int sceUserServiceGetNpOfflineAccountId(int, std::uint64_t*);
int sceRegMgrGetStr(int, char*, std::size_t);
int sceRegMgrGetBin(int, void*, std::size_t);
int sceRegMgrGetInt(int, int*);
}
#endif

namespace pbc::accounts {

namespace {
constexpr const char* home = "/user/home";

void count_saves(const std::string& dir, bool skip_system, unsigned& count, std::uint64_t& bytes) {
    std::vector<fs::Entry> games;
    if (!fs::list(fs::host(dir), games)) return;
    for (const auto& g : games) {
        if (!g.dir || (skip_system && g.name.rfind("sce_", 0) == 0)) continue;
        ++count;
        std::vector<fs::Entry> files;
        if (fs::list(fs::host(fs::join(dir, g.name)), files))
            for (const auto& f : files) if (!f.dir) bytes += f.size;
    }
}

[[maybe_unused]] std::string hex64(std::uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "0x%016llx", static_cast<unsigned long long>(v));
    return b;
}
}  // namespace

std::vector<Account> list() {
    std::vector<Account> out;
    std::vector<fs::Entry> users;
    if (!fs::list(fs::host(home), users)) return out;
#if defined(PBCK_PS5)
    static bool init = false;
    if (!init) { init = true; (void)sceUserServiceInitialize(nullptr); }
#endif
    for (const auto& u : users) {
        if (!u.dir || u.name.size() != 8 || u.name.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) continue;
        Account a;
        a.user_id = u.name;
        const std::string dir = fs::join(home, u.name);
        std::string name;
        if (fs::read(fs::host(fs::join(dir, "username.dat")), name, 256)) a.name = std::string(name.c_str());  // terminé par des zéros
#if defined(PBCK_PS5)
        const int uid = static_cast<int>(std::strtoul(u.name.c_str(), nullptr, 16));
        std::uint64_t id = 0;
        int rc = sceUserServiceGetNpAccountId(uid, &id);
        if (rc == 0 && id) { a.account_id = hex64(id); a.account_source = "np"; }
        else {
            id = 0;
            const int rc2 = sceUserServiceGetNpOfflineAccountId(uid, &id);
            if (rc2 == 0 && id) { a.account_id = hex64(id); a.account_source = "offline"; }
            log("accounts user=%s np_rc=0x%08x offline_rc=0x%08x found=%d", u.name.c_str(), static_cast<unsigned>(rc), static_cast<unsigned>(rc2), id ? 1 : 0);
        }
#endif
        count_saves(fs::join(dir, "savedata_prospero"), false, a.ps5_saves, a.saves_bytes);
        count_saves(fs::join(dir, "savedata"), true, a.ps4_saves, a.saves_bytes);
        out.push_back(std::move(a));
    }
    return out;
}

Json registry_slots() {
    Json out = Json::array();
#if defined(PBCK_PS5)
    // Utilisateur local (dossier /user/home/<id>) de chaque nom, pour relier un emplacement à son dossier.
    std::vector<Account> users = list();
    for (int slot = 1; slot <= 16; ++slot) {
        const int off = (slot - 1) * 0x10000;
        char name[32] = {0};
        const int rn = sceRegMgrGetStr(0x07800200 + off, name, sizeof name);
        std::uint64_t aid = 0;
        const int ra = sceRegMgrGetBin(0x07800500 + off, &aid, sizeof aid);
        int flags = 0;
        const int rf = sceRegMgrGetInt(0x07800800 + off, &flags);
        char type[16] = {0};
        const int rt = sceRegMgrGetStr(0x0780B007 + off, type, sizeof type);
        if (rn < 0 || !name[0]) continue;  // emplacement vide
        Json j = Json::object();
        j.set("slot", static_cast<std::int64_t>(slot));
        j.set("name", std::string(name));
        char b[24];
        std::snprintf(b, sizeof b, "%016llX", static_cast<unsigned long long>(aid));
        j.set("account_id", ra >= 0 && aid ? std::string(b) : std::string());
        // Même identifiant, octets dans l'autre ordre (comme l'affichent Prospero Manager et Garlic SaveMgr).
        std::uint64_t rev = 0;
        for (int i = 0; i < 8; ++i) rev = (rev << 8) | ((aid >> (8 * i)) & 0xFF);
        std::snprintf(b, sizeof b, "%016llX", static_cast<unsigned long long>(rev));
        j.set("account_id_reversed", ra >= 0 && aid ? std::string(b) : std::string());
        j.set("flags", rf >= 0 ? static_cast<std::int64_t>(flags) : static_cast<std::int64_t>(-1));
        j.set("type", rt >= 0 ? std::string(type) : std::string());
        j.set("activated", rf >= 0 && flags == 0x1002 && rt >= 0 && std::string(type) == "np");
        for (const auto& u : users) if (u.name == name) j.set("user_id", u.user_id);
        out.push(j);
    }
    log("accounts registry slots=%zu", out.size());
#endif
    return out;
}

Json to_json(const std::vector<Account>& v) {
    Json a = Json::array();
    for (const auto& x : v) {
        Json j = Json::object();
        j.set("user_id", x.user_id);
        j.set("name", x.name);
        j.set("account_id", x.account_id);
        j.set("account_source", x.account_source);
        j.set("ps5_saves", static_cast<std::uint64_t>(x.ps5_saves));
        j.set("ps4_saves", static_cast<std::uint64_t>(x.ps4_saves));
        j.set("saves_bytes", x.saves_bytes);
        a.push(j);
    }
    return a;
}

}  // namespace pbc::accounts
