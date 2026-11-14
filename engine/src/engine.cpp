#include "update/autostart.hpp"
#include "engine.hpp"
#include "store/peer.hpp"
#include "core/memory.hpp"
#include "store/smb.hpp"
#include "store/cloud.hpp"
#include "store/drives.hpp"
#include "store/s3.hpp"
#include "store/webdav.hpp"

#include <algorithm>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <ctime>
#include <map>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <functional>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

#include "accounts/accounts.hpp"
#include "backup/backup.hpp"
#include "backup/full.hpp"
#include "saves/garlic_core.h"
#include "core/appdb.hpp"
#include "core/http_client.hpp"
#include "core/image.hpp"
#include "core/titles.hpp"
#include "core/zip.hpp"
#include "core/notify.hpp"
#include "core/power.hpp"
#include "files/files.hpp"
#include "http/web_assets.hpp"
#include "saves/console_saves.hpp"
#include "saves/inspect.hpp"
#include "saves/restore.hpp"
#include "saves/switch_import.hpp"
#include "core/fs.hpp"
#include "core/perms.hpp"
#include "core/util.hpp"
#include "places/places.hpp"
#include "images/identify.hpp"
#include "images/images.hpp"
#include "scan/recognizers/title_dir.hpp"

#if defined(PBCK_PS5)
extern "C" int sceSystemServiceParamGetString(int param_id, char* buf, std::size_t size);  // 6 : nom de la console
#endif
namespace pbc {

// Diagnostic : dernière opération longue commencée (scan, disques, liaison…) et depuis quand. Lue par l'état du
// moteur et par la sentinelle quand le verrou principal reste bloqué (accès disque coincé…).
std::atomic<const char*> g_engine_op{"idle"};
std::atomic<std::int64_t> g_engine_op_since{0};
namespace {
std::mutex g_slow_mu;
struct SlowLock { std::string where; std::int64_t ms = 0, at = 0; };
std::vector<SlowLock> g_slow;  // les 10 dernières
}  // namespace

void note_slow_lock(const char* file, int line, std::int64_t ms) {
    const char* base = std::strrchr(file, '/');
    const std::string where = std::string(base ? base + 1 : file) + ":" + std::to_string(line);
    log("verrou principal tenu %lld ms : %s", static_cast<long long>(ms), where.c_str());
    std::lock_guard<std::mutex> l(g_slow_mu);
    g_slow.push_back({where, ms, now_unix()});
    if (g_slow.size() > 10) g_slow.erase(g_slow.begin());
}

Json slow_locks_json() {
    std::lock_guard<std::mutex> l(g_slow_mu);
    Json a = Json::array();
    for (const auto& s : g_slow) { Json j = Json::object(); j.set("where", s.where); j.set("ms", s.ms); j.set("at", s.at); a.push(j); }
    return a;
}

struct OpMark {
    const char* prev;
    std::int64_t prev_since;
    const char* name;
    std::uint64_t peak0;
    explicit OpMark(const char* n) : prev(g_engine_op.load()), prev_since(g_engine_op_since.load()), name(n), peak0(memory::peak_inuse()) { g_engine_op = n; g_engine_op_since = now_unix(); }
    ~OpMark() { memory::note_peak(name, peak0); g_engine_op = prev; g_engine_op_since = prev_since; }
};

Json Settings::to_json() const {
    Json j = Json::object();
    j.set("default_place", default_place);
    j.set("custom_folder", custom_folder);
    j.set("update_source", update_source);
    j.set("update_auto_check", update_auto_check);
    j.set("update_auto_install", update_auto_install);
    j.set("update_channel", update_channel);
    j.set("covers_online", covers_online);
    j.set("tv_seen_version", tv_seen_version);
    j.set("theme", theme);
    j.set("sound_ui", sound_ui);
    j.set("music", music);
    j.set("volume", static_cast<std::int64_t>(volume));
    j.set("language", language);
    j.set("system_language", system_language);
    Json ig = Json::array();
    for (const auto& t : ignored) ig.push(t);
    j.set("ignored", ig);
    Json ma = Json::array();
    for (const auto& t : my_accounts) ma.push(t);
    j.set("my_accounts", ma);
    Json an = Json::object();
    for (const auto& [k, v] : account_names) an.set(k, v);
    j.set("account_names", an);
    j.set("wan_enabled", wan_enabled);
    j.set("wan_port", static_cast<std::int64_t>(wan_port));
    j.set("wan_public_host", wan_public_host);
    Json la = Json::array();
    for (const auto& id : launchers) la.push(id);
    j.set("launchers", la);
    return j;
}
Settings Settings::from_json(const Json& j) {
    Settings s;
    if (!j["default_place"].as_string().empty()) s.default_place = j["default_place"].as_string();
    s.custom_folder = j["custom_folder"].as_string();
    s.update_source = j["update_source"].as_string();
    s.update_auto_check = j["update_auto_check"].as_bool(true);
    s.update_auto_install = j["update_auto_install"].as_bool(false);
    s.update_channel = j["update_channel"].as_string() == "beta" ? "beta" : "stable";
    s.covers_online = j["covers_online"].as_bool(true);
    s.tv_seen_version = j["tv_seen_version"].as_string();
    // Identifiant court en minuscules ; la télé et le web retombent sur « nuit » s'ils ne le connaissent pas.
    const std::string th = j["theme"].as_string();
    if (!th.empty() && th.size() <= 32 && th.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos) s.theme = th;
    s.sound_ui = j["sound_ui"].as_bool(true);
    s.music = j["music"].as_bool(false);
    s.volume = static_cast<int>(std::clamp<std::int64_t>(j["volume"].as_int(70), 0, 100));
    if (const std::string& l = j["language"].as_string(); !l.empty() && l.size() <= 16) s.language = l;
    if (const std::string& l = j["system_language"].as_string(); l.size() <= 16) s.system_language = l;
    for (const auto& t : j["ignored"].items())  // numéros de jeux ou clés d'éléments (applis, homebrews, ROM) : identifiants seulement
        if (const std::string& id = t.as_string(); !id.empty() && id.size() <= 512) s.ignored.push_back(id);
    for (const auto& t : j["my_accounts"].items()) {  // identifiants hexadécimaux (16 chiffres), sans « 0x »
        std::string id = t.as_string();
        if (id.rfind("0x", 0) == 0 || id.rfind("0X", 0) == 0) id = id.substr(2);
        if (!id.empty() && id.size() <= 16 && id.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) s.my_accounts.push_back(id);
    }
    for (const auto& [k, v] : j["account_names"].members())
        if (k.size() <= 16 && v.is_string() && !v.as_string().empty() && v.as_string().size() <= 64) s.account_names[k] = v.as_string();
    s.wan_enabled = j["wan_enabled"].as_bool(false);
    s.wan_port = static_cast<int>(std::clamp<std::int64_t>(j["wan_port"].as_int(52373), 1024, 65535));
    if (const std::string& h = j["wan_public_host"].as_string(); h.size() <= 120 && h.find_first_of(" /:@") == std::string::npos) s.wan_public_host = h;
    if (j.has("launchers") && j["launchers"].is_array()) s.launchers = autostart::clean_order(j["launchers"]);
    return s;
}

Engine::Engine(std::string state_dir, std::string app_dir) : dir_(std::move(state_dir)) {
    app_dir_ = app_dir;
    titles::set_app_dir(app_dir);
    updater_ = std::make_unique<Updater>(dir_, std::move(app_dir), engine_version);
    fetcher_ = std::make_unique<covers::Fetcher>(dir_);
    remotes_ = std::make_unique<store::Remotes>(fs::host(fs::join(dir_, "remotes.json")));
    plans_ = std::make_unique<plans::Store>(fs::host(fs::join(dir_, "plans.json")));
    playtime_ = std::make_unique<progress::Tracker>(fs::host(fs::join(dir_, "playtime.json")));
    identities_ = std::make_unique<accounts::Identities>(fs::host(fs::join(dir_, "identities.json")));
    queue_ = std::make_unique<tasks::Queue>(fs::host(fs::join(dir_, "tasks")));
    auto title = std::make_unique<scan::TitleDirRecognizer>();
    titles_ = title.get();
    scanner_.add(std::move(title));
    auto roms = std::make_unique<scan::RomRecognizer>(catalog_);
    roms_ = roms.get();
    scanner_.add(std::move(roms));
}

Engine::~Engine() { stop(); store::cloud::set_rotation_hook(nullptr); }

bool Engine::start() {
    {   // Démarrage : fichier des chargeurs pour l'appli ; copie chez Payload Manager à jour si l'option est active.
        Settings s0;
        { MuGuard l(mu_, __FILE__, __LINE__); s0 = settings_; }
        autostart::write_launchers_file(dir_, s0.launchers);
        autostart::refresh_pldmgr_copy(app_dir_);
    }
    // Sauvegardes restées montées par une exécution précédente (plantage, arrêt brutal) : démontées tout de suite.
    gc_startup_cleanup();
    // Sentinelle : si le verrou principal reste pris plus de 10 s, le journal dit depuis quand et dans quelle opération
    // (sans rien bloquer elle-même). Sert à diagnostiquer un accès disque coincé.
    std::thread([this] {
        int stuck = 0;
        while (!quit_) {
            ::sleep(5);
            if (mu_.try_lock()) { mu_.unlock(); stuck = 0; continue; }
            if (++stuck >= 2) log("sentinelle : verrou principal pris depuis ~%d s, opération : %s (depuis %lld s)", stuck * 5, g_engine_op.load(),
                                  static_cast<long long>(now_unix() - g_engine_op_since.load()));
        }
    }).detach();
    started_at_ = now_unix();
    instance_ = random_id(4);
    if (!fs::mkdirs(fs::host(dir_))) { log("engine state dir unavailable dir=%s errno=%d uid=%d", dir_.c_str(), fs::last_error(), static_cast<int>(::getuid())); return false; }
    // Dossiers de travail d'une comparaison coupée en route (arrêt, redémarrage) : rien à garder, on vide.
    fs::remove_tree(fs::host(fs::join(dir_, "inspect")));
    // Jeton d'accès de l'API (l'app et les appareils appairés le reçoivent).
    std::string text;
    if (fs::read(fs::host(fs::join(dir_, "token")), text, 256) && text.size() >= 32) token_ = text.substr(0, 32);
    else { token_ = random_id(16); fs::write_atomic(fs::host(fs::join(dir_, "token")), token_); }
    Json j;
    if (fs::read(fs::host(fs::join(dir_, "settings.json")), text) && Json::parse(text, j)) settings_ = Settings::from_json(j);
    // Nom de la console (celui des Réglages de la PS5, montré par Remote Play) : lu une fois, ici, sur le fil principal.
#if defined(PBCK_PS5)
    {
        char nm[72] = {0};
        if (sceSystemServiceParamGetString(6, nm, sizeof nm) == 0 && nm[0]) { std::lock_guard<std::mutex> l(console_name_mu_); console_name_ = nm; console_name_system_ = true; }
    }
#else
    if (const char* n = std::getenv("PBC_CONSOLE_NAME")) { std::lock_guard<std::mutex> l(console_name_mu_); console_name_ = n; console_name_system_ = true; }  // banc de test
#endif
    // Liaison entre consoles : identifiant stable, annonce sur le réseau local, pairs jumelés.
    if (fs::read(fs::host(fs::join(dir_, "link-id")), text, 64) && text.size() >= 16) link_id_ = text.substr(0, 16);
    else { link_id_ = random_id(8); fs::write_atomic(fs::host(fs::join(dir_, "link-id")), link_id_); }
    {
        link::Self self;
        self.app = "PS5 Backup Center";
        self.version = engine_version;
        self.installed = [this] { return updater_ ? updater_->installed_version() : std::string(); };
        self.instance = link_id_;
        self.port = http_port_;
        self.caps = Json::array();
        for (const char* c : {"backups", "saves", "games", "tasks"}) self.caps.push(c);
        // Nom de la console : noté par la boucle de relevé (qui lit déjà les comptes) ; jamais d'appel aux services
        // système des comptes depuis le fil de l'annonce (ils ne sont pas faits pour être appelés en parallèle).
        self.name = [this] { std::lock_guard<std::mutex> l(console_name_mu_); return console_name_.empty() ? std::string("PS5") : console_name_; };
        self.mint_token = [this](const std::string& peer) { return issue_device_token(peer + " (console jumelée)"); };
        self.issued_tokens = [this] {  // seulement ceux délivrés à d'autres consoles (jamais ceux des navigateurs)
            std::vector<std::string> t;
            MuGuard l(mu_, __FILE__, __LINE__);
            for (const auto& d : devices_.items()) if (d["console"].as_bool() && !d["token"].as_string().empty()) t.push_back(d["token"].as_string());
            return t;
        };
        const char* lp = std::getenv("PBC_LINK_PORT");  // tests : un autre port pour ne pas gêner une vraie console
        link_ = std::make_unique<link::Link>(dir_, std::move(self), lp ? static_cast<std::uint16_t>(std::atoi(lp)) : link::kDiscoveryPort);
        link_->start();
    }
    if (fs::read(fs::host(fs::join(dir_, "settings.json")), text) && Json::parse(text, j)) {  // liaison par Internet, si activée
        const Settings s0 = Settings::from_json(j);
        link_->wan_configure(s0.wan_enabled, static_cast<std::uint16_t>(s0.wan_port), s0.wan_public_host);
    }
    if (fs::read(fs::host(fs::join(dir_, "devices.json")), text) && Json::parse(text, j) && j.is_array()) devices_ = j;
    {   // Nouveau code à chaque démarrage ; les appareils déjà appairés le restent.
        const std::string r = random_id(4);
        unsigned v = static_cast<unsigned>(std::strtoul(r.c_str(), nullptr, 16)) % 1000000u;
        char b[8];
        std::snprintf(b, sizeof b, "%06u", v);
        pair_code_ = b;
        fs::write_atomic(fs::host(fs::join(dir_, "pair-code")), pair_code_ + "\n");
    }
    load_items();
    places_ = places::detect();
    catalog_.load(fs::host(fs::join(dir_, "emulators.json")));
    catalog_.load_manual(fs::host(fs::join(dir_, "emulator-settings.json")));
    scanner_.load_cache(fs::host(fs::join(dir_, "scan-cache.json")));
    // Après une restauration de sauvegarde, la console a changé : on relit l'inventaire.
    remotes_->load();
    store::cloud::set_rotation_hook([this](const std::string& o, const std::string& n) { remotes_->rotate_secret(o, n); });
    plans_->load();
    plans_->refresh_next(now_unix(), utc_offset());
    queue_->open_place = [this](const std::string& id, std::string& err) { return open_files_place(id, err); };
    queue_->open_remote = [this](const std::string& id, std::string& err) -> std::unique_ptr<store::Store> {
        std::string inst, place;
        if (store::parse_peer_id(id, inst, place)) {  // une autre PS5 jumelée
            if (!link_) { err = "liaison indisponible"; return nullptr; }
            return store::peer(*link_, inst, place, "PS5 " + inst);
        }
        return remotes_->open(id, err);
    };
    queue_->on_finished = [this](const tasks::Task& t) {
        if (!t.request["remote"].as_string().empty()) { std::lock_guard<std::mutex> l(remote_index_mu_); remote_index_.erase(t.request["remote"].as_string()); }
        if (t.type == tasks::Type::import_switch && !t.request["check_only"].as_bool()) request_scan();
        // Rétention : garder les N dernières versions de cet élément sur ce disque (les versions protégées restent).
        if (t.type == tasks::Type::backup && t.state == tasks::State::done && t.request["keep"].as_int() > 0 && t.request["remote"].as_string().empty())
            apply_retention(t.request["folder"].as_string(), t.request["item"]["key"].as_string(), static_cast<int>(t.request["keep"].as_int()));
        ++revision_;
        if (t.type == tasks::Type::restore_save && !t.request["check_only"].as_bool()) request_scan();
    };
    if (!queue_->load()) return false;
    queue_->start();
    fetcher_->set_enabled(settings_.covers_online);
    // Nouvelle jaquette téléchargée : on la pose sur l'élément et l'affichage se met à jour.
    fetcher_->on_found = [this] {
        {
            MuGuard l(mu_, __FILE__, __LINE__);
            for (auto& it : items_) if (it.kind == Kind::rom && it.cover.empty()) it.cover = fetcher_->lookup(it.key, it.platform, it.title);
            for (auto& it : items_) official_cover(it);
        }
        save_items();
        ++revision_;
    };
    fetcher_->start();
    scan_thread_ = std::thread([this] { scan_loop(); });
    plans_thread_ = std::thread([this] { plans_loop(); });
    updater_->set_channel(settings_.update_channel == "beta");
    updater_->set_source(settings_.update_source);
    // Mise à jour trouvée : notification système dans la langue de l'interface (même si l'appli est fermée).
    updater_->on_available = [this](const std::string& v) {
        notify::send(ui_text("Backup Center : mise à jour %s disponible", v),
                     settings().update_auto_install ? ui_text("Installation automatique, jamais pendant une partie") : ui_text("Accueil de Backup Center : Croix pour l'installer"));
    };
    if (settings_.update_auto_check) updater_->check_auto(0);  // source réglée ou trouvée seule
    log("engine started version=%s items=%zu places=%zu", engine_version, items_.size(), places_.size());
    return true;
}

void Engine::stop() {
    if (quit_.exchange(true)) return;
    if (fetcher_) fetcher_->stop();
    if (link_) link_->stop();
    if (scan_thread_.joinable()) scan_thread_.join();
    if (plans_thread_.joinable()) plans_thread_.join();
    queue_->stop();
    { std::lock_guard<std::mutex> l(playtime_mu_); playtime_->save(); }
    // Aucune image de sauvegarde ne reste montée après nous (montages de Garlic intégré).
    gc_unmount();
    log("engine stopped");
}

void Engine::request_scan() { scan_requested_ = true; }

// Un texte de l'interface dans la langue réglée (« auto » : celle de la console), d'après les traductions du web
// incluses dans le moteur ; le premier %s est remplacé par `arg`. Pour les notifications système.
std::string Engine::ui_text(const std::string& fr, const std::string& arg) const {
    const Settings s = settings();
    const std::string code = s.language == "auto" ? s.system_language : s.language;
    std::string t = fr;
    if (!code.empty() && code != "fr") {
        const std::string path = "/i18n/" + code + ".json";
        for (unsigned i = 0; i < web::asset_count; ++i) {
            if (path != web::assets[i].path) continue;
            Json j;
            if (Json::parse(std::string(reinterpret_cast<const char*>(web::assets[i].data), web::assets[i].size), j) && j[fr].is_string() && !j[fr].as_string().empty())
                t = j[fr].as_string();
            break;
        }
    }
    if (const auto p = t.find("%s"); p != std::string::npos) t.replace(p, 2, arg);
    return t;
}

// Disques des PS5 jumelées et en ligne : deviennent des destinations de backup comme le NAS (« PS5-149 · USB 1 »).
void Engine::refresh_peer_places() {
    OpMark op("disques des PS5 jumelées");
    if (!link_) return;
    Json out = Json::array();
    const Json peers = link_->peers();
    for (const auto& p : peers.items()) {
        if (!p["paired"].as_bool() || !p["online"].as_bool()) continue;
        Json pl;
        std::string err;
        if (!link_->call(p["instance"].as_string(), "GET", "/api/v2/places", Json(), pl, err)) continue;
        std::string fw;
        { Json st; std::string e2; if (link_->call(p["instance"].as_string(), "GET", "/api/v2/status", Json(), st, e2)) fw = st["firmware"].as_string(); }
        for (const auto& x : pl.items()) {
            const std::string t = x["type"].as_string();
            if (t != "usb" && t != "internal") continue;
            Json j = Json::object();
            j.set("id", "peer:" + p["instance"].as_string() + ":" + x["id"].as_string());
            j.set("type", "peer");
            j.set("mount", p["host"].as_string());
            // La version du système de la console cible reste sous les yeux : « PS5-Salon (3.21) · Interne ».
            j.set("label", (p["name"].as_string().empty() ? p["host"].as_string() : p["name"].as_string()) + (fw.empty() ? std::string() : " (" + fw + ")") + " · " + x["label"].as_string());
            j.set("firmware", fw);
            j.set("free", x["free"]);
            j.set("total", x["total"]);
            j.set("marked", true);
            j.set("remote", true);
            j.set("peer", p["instance"]);
            out.push(j);
        }
    }
    MuGuard l(mu_, __FILE__, __LINE__);
    // Disques d'une PS5 jumelée apparus, disparus ou changés : la révision change, l'app télé et le web relisent
    // la liste des endroits tout de suite (sinon une app ouverte avant leur arrivée ne les voyait jamais).
    auto sig = [](const Json& a) { std::string k; for (const auto& x : a.items()) k += x["id"].as_string() + "|" + x["label"].as_string() + ";"; return k; };
    if (sig(out) != sig(peer_places_)) ++revision_;  // la place libre seule ne compte pas (elle bouge tout le temps)
    peer_places_ = out;
    peer_places_at_ = now_unix();
}

// Backups rangés sur les PS5 jumelées : leur résumé (toutes leurs copies) et leurs comptes (pour reconnaître la même
// personne PSN). Lu dans le fil de fond : la page Backups ne fait jamais attendre un appel réseau.
void Engine::refresh_peer_backups() {
    OpMark op("backups des PS5 jumelées");
    peer_backups_at_ = now_unix();
    if (!link_) return;
    Json all = Json::object();
    const Json peers = link_->peers();  // gardée : jamais de boucle sur un temporaire
    for (const auto& p : peers.items()) {
        if (!p["paired"].as_bool() || !p["online"].as_bool()) continue;
        const std::string inst = p["instance"].as_string();
        Json sum, accs;
        std::string err;
        if (!link_->call(inst, "GET", "/api/v2/backups/summary?account=&local=1", Json(), sum, err) || !sum.is_object()) continue;
        link_->call(inst, "GET", "/api/v2/accounts", Json(), accs, err);
        Json items;
        link_->call(inst, "GET", "/api/v2/items", Json(), items, err);  // ses jeux installés (Accueil › « Sur … »)
        Json x = Json::object();
        x.set("items", items.is_array() ? items : Json::array());
        x.set("name", p["name"].as_string().empty() ? p["host"].as_string() : p["name"].as_string());
        x.set("summary", sum);
        x.set("accounts", accs);
        all.set(inst, x);
    }
    MuGuard l(mu_, __FILE__, __LINE__);
    // Une PS5 jumelée hors ligne garde sa dernière liste connue (Bibliothèque de l'autre console : affichée, sans actions).
    for (const auto& [inst, x] : peer_backups_.members())
        if (!all.has(inst)) all.set(inst, x);
    peer_backups_ = all;
}

// Jeux installés sur les PS5 jumelées et absents d'ici (Accueil › « Sur PS5-Salon »), depuis le cache de fond.
Json Engine::peer_games() {
    Json pb;
    std::set<std::string> here;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        pb = peer_backups_;
        for (const auto& it : items_) if (!it.title_id.empty() && it.key.rfind("save:", 0) != 0) here.insert(it.title_id);
    }
    Json out = Json::array();
    for (const auto& [inst, x] : pb.members()) {
        Json games = Json::array();
        for (const auto& it : x["items"].items()) {
            const std::string kind = it["kind"].as_string(), tid = it["title_id"].as_string(), key = it["key"].as_string();
            if ((kind != "game_ps5" && kind != "game_ps4") || tid.empty() || key.rfind("save:", 0) == 0 || here.count(tid)) continue;
            if (settings().is_ignored(tid)) continue;
            std::int64_t bytes = 0;
            for (const auto& c : it["components"].items()) bytes += c["bytes"].as_int();
            Json g = Json::object();
            g.set("key", key);
            g.set("title", it["title"]);
            g.set("title_id", tid);
            g.set("kind", kind);
            g.set("bytes", bytes);
            games.push(g);
        }
        Json p = Json::object();
        p.set("instance", inst);
        p.set("name", x["name"]);
        p.set("games", games);
        out.push(p);
    }
    return out;
}

Json Engine::plan_peek(const std::string& kind, const std::string& peer) {
    std::lock_guard<std::mutex> l(plans_mu_);
    const auto it = plans_cache_.find(kind + "|" + peer);
    return it == plans_cache_.end() ? Json() : it->second.plan;
}

// Identité d'un élément pour savoir s'il est « aussi ici » : le numéro du jeu, sinon la plateforme et le titre.
static std::string same_thing_key(const std::string& kind, const std::string& platform, const std::string& title_id, const std::string& title) {
    if (!title_id.empty()) return (kind == "rom" ? "rom:" + platform + ":" : std::string("id:")) + title_id;
    std::string t;
    for (char c : title) if (std::isalnum(static_cast<unsigned char>(c))) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return kind + ":" + platform + ":" + t;
}

Json Engine::peer_library(const std::string& instance, bool fresh) {
    Json out = Json::object();
    out.set("instance", instance);
    bool paired = false, online = false;
    std::string name;
    if (link_) {
        const Json ps = link_->peers();
        for (const auto& p : ps.items())
            if (p["instance"].as_string() == instance) {
                paired = p["paired"].as_bool(); online = p["online"].as_bool();
                name = p["name"].as_string().empty() ? p["host"].as_string() : p["name"].as_string();
                out.set("remote", p["remote"]);
            }
    }
    if (!paired) { out.set("error", "PS5 inconnue ou pas jumelée"); return out; }
    out.set("name", name);
    out.set("online", online);
    if (fresh && online) {  // Relire : ses éléments seulement (un appel), pas ses sauvegardes ni ses comptes
        Json items;
        std::string err;
        if (link_->call(instance, "GET", "/api/v2/items", Json(), items, err) && items.is_array()) {
            MuGuard l(mu_, __FILE__, __LINE__);
            Json x = peer_backups_[instance].is_object() ? peer_backups_[instance] : Json::object();
            x.set("items", items);
            x.set("items_at", now_unix());
            if (!x["name"].is_string()) x.set("name", name);
            peer_backups_.set(instance, x);
        } else out.set("read_error", err.empty() ? std::string("lecture impossible") : err);
    }
    Json cache;
    std::set<std::string> here;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        cache = peer_backups_[instance];
        for (const auto& it : items_)
            if (it.key.rfind("save:", 0) != 0) here.insert(same_thing_key(to_string(it.kind), it.platform, it.title_id, it.title));
    }
    out.set("read_at", cache["items_at"].is_number() ? cache["items_at"].as_int() : peer_backups_at_);
    // Parties : le plan déjà calculé (préparé en fond toutes les 15 s quand une synchro existe) ; jamais calculé ici.
    std::map<std::string, std::string> save_state;
    const Json plan = plan_peek("parties", instance);
    for (const auto& t : plan["titles"].items()) {
        const std::string st = t["state"].as_string();
        save_state[t["title_id"].as_string()] = st == "here_newer" || st == "only_here" ? "here" : st == "there_newer" || st == "only_there" ? "there" : st;
    }
    Json items = Json::array();
    int missing = 0;
    for (const auto& it : cache["items"].items()) {
        if (it["ignored"].as_bool()) continue;
        Json x = it;
        const std::string kind = it["kind"].as_string(), key = it["key"].as_string();
        const bool save_only = key.rfind("save:", 0) == 0;
        const bool both = here.count(same_thing_key(kind, it["platform"].as_string(), it["title_id"].as_string(), it["title"].as_string())) > 0;
        x.set("here", save_only ? "save_only" : both ? "both" : "missing");
        if (!save_only && !both) ++missing;
        const auto s = save_state.find(it["title_id"].as_string());
        if (s != save_state.end()) x.set("save", s->second);
        // Jaquette : par le numéro du jeu (cache d'ici, sinon chez elle) ; sinon la sienne, via elle.
        const std::string tid = it["title_id"].as_string();
        x.set("cover_key", !tid.empty() && (kind == "game_ps5" || kind == "app") ? "tid:ps5:" + tid
                           : !tid.empty() && kind == "game_ps4" ? "tid:ps4:" + tid
                           : !tid.empty() && it["platform"].as_string().rfind("Switch", 0) == 0 ? "tid:switch:" + tid
                           : "peer:" + instance + ":" + key);
        items.push(x);
    }
    out.set("items", items);
    out.set("missing_here", missing);
    return out;
}

Json Engine::peer_action(const Json& body, std::string& error) {
    const std::string inst = body["instance"].as_string(), action = body["action"].as_string();
    if (!link_) { error = "liaison indisponible"; return Json(); }
    Json req = Json::object(), out;
    if (action == "backup" || action == "backup_saves") {
        req.set("type", "backup");
        req.set("item", body["item"]);
        if (action == "backup_saves") { Json p = Json::array(); p.push("save"); req.set("parts", p); }
        if (!link_->call(inst, "POST", "/api/v2/tasks", req, out, error)) { if (error.empty()) error = "autre PS5 injoignable"; return Json(); }
        return out;
    }
    error = "action inconnue";
    return Json();
}

// ---- Synchro des copies avec une autre PS5 ------------------------------------------------------------------------
// Une copie = un dossier .pbck dont le nom porte un identifiant unique : même nom des deux côtés = même copie.
// Côté ici : chaque disque (interne, USB). Côté là-bas : ses disques (pas son NAS, qu'on lit déjà s'il est ajouté ici).
Json Engine::sync_copies_plan(const std::string& peer, bool include_full) {
    Json out = Json::object();
    out.set("peer", peer);
    // L'autre côté est une autre PS5 jumelée, ou un NAS de cette console (« remote:… ») : mêmes règles, même écran.
    const bool nas = peer.rfind("remote:", 0) == 0;
    Json pb;
    if (nas) {
        std::string label;
        const Json remotes = remotes_json();  // gardée en vie pendant la boucle
        for (const auto& r : remotes.items()) if (r["id"].as_string() == peer) label = r["name"].as_string();  // appareils enregistrés
        if (label.empty()) { out.set("error", "NAS inconnu"); return out; }
        out.set("name", label);
        out.set("nas", true);
    } else {
        { MuGuard l(mu_, __FILE__, __LINE__); pb = peer_backups_[peer]; }
        if (!pb.is_object()) { out.set("error", "autre PS5 pas encore lue (hors ligne ?)"); return out; }
        out.set("name", pb["name"]);
    }
    auto is_full = [](const Json& parts) { for (const auto& p : parts.items()) if (p.as_string() != "save") return true; return false; };
    // Ici
    std::map<std::string, Json> here;  // bundle → copie
    std::vector<std::pair<std::string, std::string>> plist;
    { MuGuard l(mu_, __FILE__, __LINE__); for (const auto& p : places_) plist.push_back({p.id, p.label}); }
    auto collect = [&](const std::string& pid, const std::string& label, std::map<std::string, Json>& into) {
        const Json idx = backups_json(pid);
        for (const auto& b : idx["backups"].items()) {
            if (!b["complete"].as_bool() || b["legacy"].as_bool()) continue;
            Json c = Json::object();
            c.set("place", pid); c.set("place_label", label); c.set("bundle", b["bundle"]); c.set("title", b["title"]);
            c.set("title_id", b["title_id"]); c.set("bytes", b["bytes"]); c.set("full", is_full(b["parts"]));
            c.set("kind", b["kind"]);  // une appli reste une appli
            // Détails pour choisir : date de la copie, heure de la partie copiée, avancée et temps de jeu notés à la copie.
            c.set("created_unix", b["created_unix"]);
            for (const auto& a : b["accounts"].items()) {
                if (a["save_mtime"].as_int()) c.set("saved_at", a["save_mtime"]);
                if (a["progress"].is_object()) { c.set("progress_text", a["progress"]["text"]); c.set("play_time", a["progress"]["play_time_seconds"]); }
            }
            // Une copie se reconnaît par son nom, sans son dossier (sur un NAS : « PBC Backup/<nom> ») — sinon elle
            // repartirait à chaque synchro.
            into.emplace(std::string(fs::basename(b["bundle"].as_string())), c);
        }
    };
    for (const auto& [pid, label] : plist) if (!(nas && pid == peer) && pid.rfind("remote:", 0) != 0) collect(pid, label, here);  // ici : les disques de la console
    // Là-bas
    std::map<std::string, Json> there;
    if (nas) collect(peer, out["name"].as_string(), there);
    if (!nas) for (const auto& g : pb["summary"]["games"].items())
        for (const auto& c : g["copies"].items()) {
            const std::string pl = c["place"].as_string();
            if (pl.rfind("remote:", 0) == 0 || pl.rfind("peer:", 0) == 0 || !c["complete"].as_bool(true) || c["legacy"].as_bool()) continue;
            Json x = Json::object();
            x.set("place", pl); x.set("place_label", c["place_label"]); x.set("bundle", c["bundle"]); x.set("title", g["title"]);
            x.set("title_id", g["title_id"]); x.set("bytes", c["bytes"].as_int() ? c["bytes"] : c["content_bytes"]);
            x.set("full", g["full"].as_bool());
            x.set("kind", g["kind"]);
            x.set("created_unix", c["created_unix"]);
            if (const std::int64_t sv = std::max(c["save_mtime"].as_int(), c["game_saved_at"].as_int())) x.set("saved_at", sv);
            if (!c["progress_text"].as_string().empty()) x.set("progress_text", c["progress_text"]);
            if (c["play_time_seconds"].as_int()) x.set("play_time", c["play_time_seconds"]);
            there.emplace(c["bundle"].as_string(), x);
        }
    // Jeux et applis complets : JAMAIS de versions — la copie complète la plus récente part une
    // seule fois, si l'autre côté n'en a aucune. Les sauvegardes, elles, gardent leurs versions.
    auto newest_full = [](const std::map<std::string, Json>& side) {
        std::map<std::string, std::int64_t> m;  // title_id → date de la copie complète la plus récente
        for (const auto& [b, c] : side)
            if (c["full"].as_bool()) { auto& v = m[c["title_id"].as_string()]; v = std::max(v, c["created_unix"].as_int()); }
        return m;
    };
    const auto full_here = newest_full(here), full_there = newest_full(there);
    auto full_wanted = [](const Json& c, const std::map<std::string, std::int64_t>& mine, const std::map<std::string, std::int64_t>& other) {
        const std::string tid = c["title_id"].as_string();
        const auto me = mine.find(tid);
        if (me != mine.end() && c["created_unix"].as_int() < me->second) return false;  // une plus récente existe de ce côté
        // Envoi « une seule fois » : le jeu ne part que si l'autre côté n'en a AUCUNE copie complète. Une fois là-bas,
        // seules ses sauvegardes suivent — jamais 150 Go renvoyés parce qu'une partie a bougé.
        return other.find(tid) == other.end();
    };
    Json pull = Json::array(), push = Json::array();
    std::int64_t pull_b = 0, push_b = 0, pull_full = 0, push_full = 0;
    std::set<std::string> seen_full;  // une même copie complète rangée sur deux disques ne part qu'une fois
    for (const auto& [b, c] : there) if (!here.count(b)) {
        if (c["full"].as_bool()) { if (!full_wanted(c, full_there, full_here) || !seen_full.insert("p" + c["title_id"].as_string()).second) continue; ++pull_full; if (!include_full) continue; }
        pull.push(c); pull_b += c["bytes"].as_int();
    }
    for (const auto& [b, c] : here) if (!there.count(b)) {
        if (c["full"].as_bool()) { if (!full_wanted(c, full_here, full_there) || !seen_full.insert("h" + c["title_id"].as_string()).second) continue; ++push_full; if (!include_full) continue; }
        push.push(c); push_b += c["bytes"].as_int();
    }
    // Jeux suivis (préférences) : seuls ceux-là comptent pour la bulle et la synchro « des jeux suivis ».
    const Json prefs = sync_prefs(peer);
    std::int64_t fpull = 0, fpush = 0, fbytes = 0, ffull_skipped = 0;
    std::set<std::string> fgames;
    // Par défaut, les titres suivis n'envoient que leurs sauvegardes ; les copies complètes seulement si « with_full ».
    const bool with_full = prefs["with_full"].as_bool();
    std::set<std::string> full_titles;  // jeux dont le jeu complet part aussi (une seule fois), choisi jeu par jeu
    for (const auto& t : prefs["full_titles"].items()) full_titles.insert(t.as_string());
    auto mark = [&](Json& list, std::int64_t& n) {
        Json o = Json::array();
        for (const auto& c : list.items()) {
            Json x = c;
            bool f = sync_followed(prefs, c["title_id"].as_string());
            if (f && c["full"].as_bool() && !with_full && !full_titles.count(c["title_id"].as_string())) { f = false; ++ffull_skipped; }
            x.set("followed", f);
            if (f) { ++n; fbytes += c["bytes"].as_int(); fgames.insert(c["title_id"].as_string()); }
            o.push(x);
        }
        list = o;
    };
    mark(pull, fpull);
    mark(push, fpush);
    out.set("mode", prefs["mode"]);
    out.set("followed_games", static_cast<std::int64_t>(prefs["mode"].as_string() == "all" ? -1 : static_cast<std::int64_t>(prefs["games"].size())));
    out.set("followed_pull", fpull); out.set("followed_push", fpush);
    out.set("followed_bytes", fbytes); out.set("with_full", with_full); out.set("followed_full_skipped", ffull_skipped);
    out.set("auto_new", prefs["mode"].as_string() == "all");
    // Un jeu par ligne, même ceux déjà au même niveau : l'écran « Choisir les jeux » et la page Synchro partent de là.
    {
        struct T { Json t = Json::object(); std::int64_t here_at = 0, there_at = 0, pull = 0, push = 0, bytes = 0, full_push = 0, full_pull = 0; };
        std::map<std::string, T> by;
        auto when = [](const Json& c) { return c["saved_at"].as_int() ? c["saved_at"].as_int() : c["created_unix"].as_int(); };
        auto seen = [&](const Json& c, bool here_side) {
            const std::string tid = c["title_id"].as_string();
            if (tid.empty()) return;
            T& x = by[tid];
            if (!x.t.has("title")) { x.t.set("title_id", tid); x.t.set("title", c["title"]); x.t.set("kind", c["kind"]); }
            if (c["full"].as_bool()) return;  // les dates et le temps de jeu viennent des sauvegardes
            std::int64_t& at = here_side ? x.here_at : x.there_at;
            if (when(c) >= at) {
                at = when(c);
                if (c["play_time"].as_int()) x.t.set(here_side ? "here_play" : "there_play", c["play_time"]);
            }
        };
        for (const auto& [b, c] : here) seen(c, true);
        for (const auto& [b, c] : there) seen(c, false);
        for (const auto& c : pull.items()) {
            T& x = by[c["title_id"].as_string()];
            if (c["full"].as_bool()) x.full_pull = c["bytes"].as_int(); else { ++x.pull; x.bytes += c["bytes"].as_int(); }
        }
        for (const auto& c : push.items()) {
            T& x = by[c["title_id"].as_string()];
            if (c["full"].as_bool()) x.full_push = c["bytes"].as_int(); else { ++x.push; x.bytes += c["bytes"].as_int(); }
        }
        Json titles = Json::array();
        for (auto& [tid, x] : by) {
            if (!x.t.has("title")) continue;
            x.t.set("here_at", x.here_at); x.t.set("there_at", x.there_at);
            x.t.set("pull", x.pull); x.t.set("push", x.push); x.t.set("bytes", x.bytes);
            x.t.set("full_push_bytes", x.full_push); x.t.set("full_pull_bytes", x.full_pull);  // jeu complet absent de l'autre côté
            x.t.set("state", x.pull && x.push ? "both" : x.push ? "push" : x.pull ? "pull" : "same");
            x.t.set("followed", sync_followed(prefs, tid));
            x.t.set("send_full", with_full || full_titles.count(tid) > 0);
            titles.push(x.t);
        }
        out.set("titles", titles);
    }
    out.set("pull", pull); out.set("push", push);
    out.set("pull_bytes", pull_b); out.set("push_bytes", push_b);
    out.set("pull_full", pull_full); out.set("push_full", push_full);  // jeux complets non comptés (sauf include_full)
    // Disques proposés : ici, le premier USB sinon l'interne ; là-bas, son disque le plus libre.
    Place hp;
    std::string err;
    if (resolve_place("auto", hp, err)) { out.set("here_place", hp.id); out.set("here_label", hp.label); fs::Space sp; if (fs::space(fs::host(hp.mount), sp)) out.set("here_free", static_cast<std::int64_t>(sp.free)); }
    if (nas) { out.set("peer_place", peer); out.set("peer_label", out["name"]); }
    else {
        MuGuard l(mu_, __FILE__, __LINE__);
        std::int64_t best = -1;
        for (const auto& p : peer_places_.items())
            if (p["peer"].as_string() == peer && p["free"].as_int() > best) { best = p["free"].as_int(); out.set("peer_place", p["id"]); out.set("peer_label", p["label"]); out.set("peer_free", p["free"]); }
    }
    return out;
}

namespace {
// Copie récursive d'un dossier d'un stockage à l'autre ; « complete » en dernier (une copie interrompue reste incomplète).
bool copy_tree(store::Store& src, const std::string& from, store::Store& dst, const std::string& to, std::atomic<std::int64_t>& done,
               const std::atomic<bool>& stop, std::string& err) {
    std::vector<fs::Entry> es;
    if (!src.list(from, es)) { err = "lecture impossible : " + from; return false; }
    if (!dst.mkdirs(to)) { err = "écriture impossible : " + to; return false; }
    std::stable_sort(es.begin(), es.end(), [](const fs::Entry& a, const fs::Entry& b) { return (a.name == "complete") < (b.name == "complete"); });
    for (const auto& e : es) {
        if (stop) { err = "arrêtée"; return false; }
        const std::string a = fs::join(from, e.name), b = fs::join(to, e.name);
        if (e.dir) { if (!copy_tree(src, a, dst, b, done, stop, err)) return false; continue; }
        auto r = src.open(a, err);
        if (!r) return false;
        auto w = dst.create(b, err);
        if (!w) return false;
        w->size_hint(e.size);
        std::vector<char> buf(1u << 20);
        for (;;) {
            if (stop) { w->abort(); err = "arrêtée"; return false; }
            const long n = r->read(buf.data(), buf.size());
            if (n < 0) { w->abort(); err = "lecture interrompue : " + a; return false; }
            if (n == 0) break;
            if (!w->write(buf.data(), static_cast<std::size_t>(n))) { w->abort(); err = "écriture interrompue : " + b; return false; }
            done += n;
        }
        if (!w->commit(err)) return false;
    }
    return true;
}
}  // namespace

Json Engine::sync_prefs(const std::string& peer) {
    std::lock_guard<std::mutex> l(sync_mu_);
    std::string text;
    Json all;
    if (!fs::read(fs::host(fs::join(dir_, "sync-prefs.json")), text) || !Json::parse(text, all) || !all.is_object()) all = Json::object();
    Json p = all[peer];
    if (!p.is_object()) { p = Json::object(); p.set("mode", "chosen"); p.set("games", Json::array()); p.set("excluded", Json::array()); }
    return p;
}

// Une synchro est-elle configurée avec cet appareil (au moins un jeu, une partie ou une appli suivis, ou « tout ») ?
bool Engine::sync_configured(const std::string& peer) {
    const Json p = sync_prefs(peer);
    if (p["mode"].as_string() == "all") return true;
    for (const char* k : {"games", "parties_games", "apps_games", "emu_games"}) if (!p[k].items().empty()) return true;
    return false;
}

bool Engine::sync_followed(const Json& prefs, const std::string& title_id) const {
    auto in = [&](const char* k) { for (const auto& t : prefs[k].items()) if (t.as_string() == title_id) return true; return false; };
    return prefs["mode"].as_string() == "all" ? !in("excluded") : in("games");
}

Json Engine::set_sync_prefs(const Json& body, std::string& error) {
    const std::string peer = body["peer"].as_string();
    if (peer.empty()) { error = "PS5 inconnue"; return Json(); }
    Json p = sync_prefs(peer);
    if (body["mode"].as_string() == "all" || body["mode"].as_string() == "chosen") p.set("mode", body["mode"]);
    if (body["reset"].as_bool()) { p.set("games", Json::array()); p.set("excluded", Json::array()); }  // « Tout suivre » / « Ne rien suivre »
    if (body.has("with_full")) p.set("with_full", body["with_full"].as_bool());  // titres suivis : copies complètes aussi (non par défaut)
    for (const auto& [tid, on] : body["send_full"].members()) {  // jeu par jeu : envoyer aussi le jeu complet, une seule fois
        Json kept = Json::array();
        for (const auto& t : p["full_titles"].items()) if (t.as_string() != tid) kept.push(t);
        if (on.as_bool()) kept.push(tid);
        p.set("full_titles", kept);
    }
    if (body.has("auto_new") || body.has("follow_all")) {
        // Il faut la liste des jeux connus des deux côtés : « nouveaux jeux automatiquement » garde les choix actuels,
        // « tout choisir / tout retirer » s'applique à tous les jeux affichés.
        const Json plan = sync_copies_plan(peer, true);
        std::vector<std::pair<std::string, bool>> known;  // jeu → suivi aujourd'hui
        for (const auto& t : plan["titles"].items()) known.push_back({t["title_id"].as_string(), sync_followed(p, t["title_id"].as_string())});
        if (body.has("follow_all")) for (auto& k : known) k.second = body["follow_all"].as_bool();
        const bool auto_new = body.has("auto_new") ? body["auto_new"].as_bool() : p["mode"].as_string() == "all";
        Json games = Json::array(), excluded = Json::array();
        for (const auto& [tid, on] : known) (on ? games : excluded).push(tid);
        p.set("mode", auto_new ? "all" : "chosen");
        p.set("games", auto_new ? Json::array() : games);
        p.set("excluded", auto_new ? excluded : Json::array());
    }
    for (const auto& [tid, on] : body["follow"].members()) {
        // « chosen » : la liste des suivis ; « all » : la liste des exclus (l'inverse).
        const bool all = p["mode"].as_string() == "all";
        const char* key = all ? "excluded" : "games";
        const bool want_in = all ? !on.as_bool() : on.as_bool();
        Json kept = Json::array();
        for (const auto& t : p[key].items()) if (t.as_string() != tid) kept.push(t);
        if (want_in) kept.push(tid);
        p.set(key, kept);
    }
    std::lock_guard<std::mutex> l(sync_mu_);
    std::string text;
    Json all;
    if (!fs::read(fs::host(fs::join(dir_, "sync-prefs.json")), text) || !Json::parse(text, all) || !all.is_object()) all = Json::object();
    all.set(peer, p);
    fs::write_atomic(fs::host(fs::join(dir_, "sync-prefs.json")), all.dump(true));
    return p;
}

Json Engine::sync_copies_start(const Json& body, std::string& error) {
    if (!link_) { error = "liaison indisponible"; return Json(); }
    if (sync_running_.exchange(true)) { error = "une synchro des copies est déjà en cours"; return Json(); }
    const std::string peer = body["peer"].as_string();
    Json plan = sync_copies_plan(peer, body["include_full"].as_bool());
    if (plan.has("error")) { sync_running_ = false; error = plan["error"].as_string(); return Json(); }
    if (body["followed"].as_bool()) {  // « Synchroniser les jeux suivis » : seulement ceux-là
        for (const char* side : {"pull", "push"}) {
            Json kept = Json::array();
            std::int64_t bytes = 0;
            for (const auto& c : plan[side].items()) if (c["followed"].as_bool()) { kept.push(c); bytes += c["bytes"].as_int(); }
            plan.set(side, kept);
            plan.set(std::string(side) + "_bytes", bytes);
        }
    }
    if (body["bundles"].is_array() && body["bundles"].size()) {  // « Choisir les jeux » : seulement les copies cochées
        std::set<std::string> want;
        for (const auto& b : body["bundles"].items()) want.insert(b.as_string());
        for (const char* side : {"pull", "push"}) {
            Json kept = Json::array();
            std::int64_t bytes = 0;
            for (const auto& c : plan[side].items()) if (want.count(c["bundle"].as_string())) { kept.push(c); bytes += c["bytes"].as_int(); }
            plan.set(side, kept);
            plan.set(std::string(side) + "_bytes", bytes);
        }
    }
    const std::string here_place = body["here_place"].as_string().empty() ? plan["here_place"].as_string() : body["here_place"].as_string();
    const std::string peer_place = body["peer_place"].as_string().empty() ? plan["peer_place"].as_string() : body["peer_place"].as_string();
    std::string inst, ppl;
    const bool nas = peer.rfind("remote:", 0) == 0;  // le NAS de cette console : copies entre ses disques et lui
    if (here_place.empty() || (!nas && (!store::parse_peer_id(peer_place, inst, ppl) || inst != peer))) { sync_running_ = false; error = "disque de destination introuvable"; return Json(); }
    const std::size_t total = plan["pull"].size() + plan["push"].size();
    {
        std::lock_guard<std::mutex> l(sync_mu_);
        sync_status_ = Json::object();
        sync_status_.set("state", "running"); sync_status_.set("peer", peer); sync_status_.set("name", plan["name"]);
        sync_status_.set("total", static_cast<std::int64_t>(total)); sync_status_.set("done", static_cast<std::int64_t>(0));
        sync_status_.set("bytes_total", plan["pull_bytes"].as_int() + plan["push_bytes"].as_int()); sync_status_.set("bytes_done", static_cast<std::int64_t>(0));
        sync_status_.set("started", now_unix()); sync_status_.set("errors", Json::array());
    }
    sync_stop_ = false;
    std::thread([this, plan, peer, here_place, peer_place, ppl, nas] {
        OpMark op("synchro des copies");
        std::atomic<std::int64_t> bytes{0};
        std::int64_t done = 0, failed = 0;
        auto set = [&](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); sync_status_.set(k, v); };
        auto one = [&](const Json& c, bool pulling) {
            std::string err, src_path, dst_path;
            std::unique_ptr<store::Store> src, dst;
            const std::string bundle = c["bundle"].as_string(), name(fs::basename(bundle));  // nom seul à l'arrivée
            if (pulling) {  // de là-bas vers ici
                if (nas) src = version_store(peer, bundle, src_path, err);
                else { src = store::peer(*link_, peer, c["place"].as_string(), "PS5"); src_path = "/" + bundle; }
                dst = version_store(here_place, name, dst_path, err);
            } else {
                src = version_store(c["place"].as_string(), bundle, src_path, err);
                if (nas) dst = version_store(peer, name, dst_path, err);
                else { dst = store::peer(*link_, peer, ppl, "PS5"); dst_path = "/" + bundle; }
            }
            fs::Entry x;
            if (src && dst && dst->stat(fs::join(dst_path, "complete"), x)) return true;  // déjà là (jamais écrasée)
            const bool ok = src && dst && copy_tree(*src, src_path, *dst, dst_path, bytes, sync_stop_, err);
            if (ok) {  // l'index du dossier d'arrivée la connaît tout de suite (Backups, état des parties)
                const std::string folder = dst_path.substr(0, dst_path.rfind('/'));
                backup::rebuild_index(*dst, folder.empty() ? std::string("/") : folder);
            }
            if (!ok) {
                log("sync copies %s %s : %s", pulling ? "pull" : "push", bundle.c_str(), err.c_str());
                std::lock_guard<std::mutex> l(sync_mu_);
                Json es = sync_status_["errors"];
                Json e = Json::object(); e.set("bundle", bundle); e.set("title", c["title"]); e.set("error", err);
                es.push(e);
                sync_status_.set("errors", es);
            }
            return ok;
        };
        for (const char* side : {"pull", "push"})
            for (const auto& c : plan[side].items()) {
                if (sync_stop_) break;
                set("current", c["title"]);
                set("direction", std::string(side));
                if (!one(c, std::string(side) == "pull")) ++failed;
                ++done;
                set("done", done);
                set("bytes_done", static_cast<std::int64_t>(bytes));
            }
        set("state", sync_stop_ ? std::string("cancelled") : failed ? std::string("failed") : std::string("done"));
        set("failed", failed);
        set("finished", now_unix());
        set("current", std::string());
        { std::lock_guard<std::mutex> l(remote_index_mu_); remote_index_.clear(); }
        peer_backups_at_ = 0;  // relire l'autre PS5 au prochain tour
        sync_running_ = false;
    }).detach();
    return sync_copies_status();
}

Json Engine::sync_copies_status() { std::lock_guard<std::mutex> l(sync_mu_); return sync_status_; }
void Engine::sync_copies_cancel() { sync_stop_ = true; }

// {peer, item, place, format, user_id} : demande à l'autre PS5 d'envoyer un de ses jeux ici et de l'y installer.
Json Engine::pull_peer_game(const Json& body, std::string& error) {
    if (!link_) { error = "liaison indisponible"; return Json(); }
    Json req = Json::object();
    req.set("place", "peer:" + link_id_ + ":" + body["place"].as_string());  // ce disque-ci, vu de l'autre PS5
    req.set("item", body["item"]);
    req.set("format", body["format"]);
    req.set("user_id", body["user_id"]);
    Json out;
    if (!link_->call(body["peer"].as_string(), "POST", "/api/v2/link/send-game", req, out, error)) return Json();
    return out;
}

Json Engine::pull_peer_save(const Json& body, std::string& error) {
    if (!link_) { error = "liaison indisponible"; return Json(); }
    const std::string peer = body["peer"].as_string();
    std::string place = body["place"].as_string();
    const std::string pre = "peer:" + peer + ":";
    if (place.rfind(pre, 0) == 0) place = place.substr(pre.size());
    Json req = Json::object();
    req.set("instance", link_id_);  // identifiant stable de liaison (celui que l'autre PS5 connaît)
    req.set("place", place);
    req.set("bundle", body["bundle"]);
    req.set("title_id", body["title_id"]);
    req.set("user_id", body["user_id"]);
    Json out;
    if (!link_->call(peer, "POST", "/api/v2/link/send-save", req, out, error)) return Json();
    return out;
}

// Copie d'un dossier entre deux appareils (disque, NAS, autre PS5), vérifiée, réutilisée par la synchro avec le NAS.
bool copy_store_tree(store::Store& src, const std::string& from, store::Store& dst, const std::string& to, std::atomic<std::int64_t>& done,
                     const std::atomic<bool>& stop, std::string& err) { return copy_tree(src, from, dst, to, done, stop, err); }

Json Engine::plan_cached(const std::string& kind, const std::string& peer, bool full, bool fresh) {
    const std::string key = kind + "|" + peer + (full ? "|full" : "");
    const std::uint64_t gen = plans_gen_.load(), rev = revision_.load();
    // Pendant une mise à niveau, l'état bouge : jamais de plan gardé, toujours le vrai.
    const bool busy = sync_running_.load() || parties_running_.load();
    if (busy) {
        Json j = kind == "copies" ? sync_copies_plan(peer, full) : kind == "parties" ? parties_plan(peer) : kind == "apps" ? apps_plan(peer) : kind == "emu" ? emu_plan(peer) : dlc_plan(peer);
        j.set("cached_at", now_unix());
        return j;
    }
    // Une PS5 jumelée hors ligne (éteinte, plantée) : réponse immédiate, sans essayer. Avant : chaque plan attendait la fin
    // des délais de connexion (jusqu'à 3 min), l'appli télé redemandait toutes les ~20 s, les demandes s'empilaient
    // jusqu'à remplir le serveur (« serveur occupé » tant que la PS5 jumelée restait éteinte).
    if (link_ && peer.rfind("remote:", 0) != 0) {
        const Json ps = link_->peers();
        for (const auto& p : ps.items())
            if (p["instance"].as_string() == peer && p["paired"].as_bool() && !p["online"].as_bool()) {
                Json j = Json::object();
                j.set("peer", peer);
                j.set("error", "autre PS5 injoignable : hors ligne");
                j.set("offline", true);
                j.set("cached_at", now_unix());
                return j;
            }
    }
    const bool configured = sync_configured(peer);
    std::unique_lock<std::mutex> l(plans_mu_);
    auto valid = [&](const CachedPlan& c) {
        // Un plan en erreur (appareil injoignable) ne sert que 5 s ; un plan long à calculer (NAS : 2 min) sert plus
        // longtemps, sinon on le recalculerait sans arrêt.
        // Sans synchro configurée avec cet appareil : rien ne bouge de notre fait, 5 min (refait dès qu'un choix change).
        const std::int64_t ttl = c.plan.has("error") ? std::max<std::int64_t>(5, c.took_s) : configured ? std::max<std::int64_t>(20, 3 * c.took_s) : std::max<std::int64_t>(300, 3 * c.took_s);
        return c.gen == gen && c.rev == rev && now_unix() - c.at < ttl;
    };
    if (!fresh) {
        auto it = plans_cache_.find(key);
        if (it != plans_cache_.end() && valid(it->second)) { Json j = it->second.plan; j.set("cached_at", it->second.at); return j; }
    }
    // Un seul calcul à la fois par plan. Avant : préparation d'avance (toutes les 15 s) + appli (toutes les 10 s) lançaient
    // le même calcul de 2 min avec le NAS en parallèle ; ils s'empilaient jusqu'à remplir le serveur (« serveur occupé »,
    // appli déconnectée). Si un calcul tourne : l'ancien plan s'il existe, sinon on attend celui-là.
    // L'ancien plan ne sert que s'il a été calculé avec les mêmes choix et le même état (seul son âge a expiré).
    auto same_inputs = [&](const CachedPlan& c) { return c.gen == gen && c.rev == rev; };
    if (plans_running_.count(key)) {
        auto it = plans_cache_.find(key);
        if (!fresh && it != plans_cache_.end() && same_inputs(it->second)) { Json j = it->second.plan; j.set("cached_at", it->second.at); j.set("refreshing", true); return j; }
        // Attente bornée : une demande ne bloque jamais un fil du serveur plus de 8 s pour un calcul déjà lancé.
        auto busy_answer = [&] {
            Json j = Json::object();
            j.set("peer", peer);
            j.set("error", "calcul en cours, réessayez dans un instant");
            j.set("computing", true);
            j.set("cached_at", now_unix());
            return j;
        };
        if (!plans_cv_.wait_for(l, std::chrono::seconds(8), [&] { return !plans_running_.count(key) || quit_.load(); })) return busy_answer();
        it = plans_cache_.find(key);
        if (it != plans_cache_.end() && same_inputs(it->second)) { Json j = it->second.plan; j.set("cached_at", it->second.at); return j; }
        if (plans_running_.count(key)) return busy_answer();  // un autre a déjà relancé entre-temps
    }
    plans_running_.insert(key);
    l.unlock();
    const std::int64_t t0 = now_unix();
    Json j = kind == "copies" ? sync_copies_plan(peer, full) : kind == "parties" ? parties_plan(peer) : kind == "apps" ? apps_plan(peer) : kind == "emu" ? emu_plan(peer) : dlc_plan(peer);
    l.lock();
    plans_cache_[key] = {j, now_unix(), gen, rev, now_unix() - t0};
    plans_running_.erase(key);
    l.unlock();
    plans_cv_.notify_all();
    j.set("cached_at", now_unix());
    return j;
}

void Engine::plans_loop() {
    // Laisse le moteur démarrer (scan, liaison), puis toutes les 15 s : les plans de chaque appareil en avance.
    for (int i = 0; i < 100 && !quit_; ++i) ::usleep(100 * 1000);
    while (!quit_) {
        const bool busy = sync_running_.load() || parties_running_.load();  // pendant une mise à niveau : pas de calcul en plus
        if (!busy && link_) {
            const Json peers = link_->peers();  // gardé : jamais de boucle sur un temporaire
            for (const auto& p : peers.items()) {
                if (quit_) break;
                if (!p["paired"].as_bool() || !p["online"].as_bool()) continue;
                const std::string inst = p["instance"].as_string();
                // Préparé d'avance seulement si une synchro est configurée avec cette PS5 ; sinon, à la demande.
                if (!sync_configured(inst)) continue;
                for (const char* k : {"copies", "parties", "dlc", "apps", "emu"}) { if (quit_) break; OpMark op("synchro préparée d'avance"); plan_cached(k, inst, std::string(k) == "copies"); }
            }
            for (const auto& r : remotes_->list()) {
                if (quit_) break;
                if (r.type == "switch" || !r.for_sync()) continue;  // NAS qui ne sert pas à la synchro
                if (!remotes_->same_as(r.id, "sync").empty()) continue;  // même NAS par un autre protocole : déjà préparé
                if (!sync_configured(r.id)) continue;  // NAS sans synchro configurée : à la demande
                for (const char* k : {"copies", "parties", "apps"}) { if (quit_) break; plan_cached(k, r.id, std::string(k) == "copies"); }  // NAS : sauvegardes, parties, dépôt d'applis
            }
        }
        memory::purge();  // la mémoire libérée retourne au système (jeux et applis doivent pouvoir se lancer)
        for (int i = 0; i < 150 && !quit_; ++i) ::usleep(100 * 1000);
    }
}

void Engine::scan_loop() {
    using namespace std::chrono;
    auto last = steady_clock::now();
    while (!quit_) {
        // Disques des PS5 jumelées : toutes les 30 s, et tout de suite quand l'une d'elles apparaît ou disparaît
        // (démarrage, autre console rallumée) — Appareils la montre avec son stockage sans attendre.
        {
            std::string sig;
            if (link_) { const Json ps = link_->peers(); for (const auto& p : ps.items()) if (p["paired"].as_bool() && p["online"].as_bool()) sig += p["instance"].as_string() + ","; }
            const bool changed = sig != peer_online_sig_;
            // Pas d'appels pour rien : tout de suite quand une PS5 arrive ou part, sinon disques toutes les 2 min et
            // sauvegardes / comptes / jeux toutes les 10 min (avant : 30 s et 2 min, les deux PS5 se chargeaient l'une l'autre).
            if (changed || now_unix() - peer_places_at_ >= 120) { peer_online_sig_ = sig; refresh_peer_places(); }
            if (changed || now_unix() - peer_backups_at_ >= 600) refresh_peer_backups();
        }
        // Rescan à la demande, ou toutes les 60 s (le cache rend un scan sans changement quasi gratuit).
        if (scan_requested_.exchange(false) || steady_clock::now() - last > seconds(60)) {
            { MuGuard l(mu_, __FILE__, __LINE__); scanning_ = true; }
            ++revision_;
            OpMark op_places("détection des disques");
            auto places = places::detect();
            // Émulateurs installés et leurs dossiers de jeux (réglages d'abord, défauts ensuite).
            std::vector<std::string> usb, tids;
            for (const auto& p : places) if (p.type == Place::Type::usb) usb.push_back(p.mount);
            for (const char* d : {"/data/homebrew", "/user/app"}) {
                std::vector<fs::Entry> entries;
                if (fs::list(fs::host(d), entries)) for (const auto& e : entries) if (e.dir) tids.push_back(e.name);
            }
            scan::Stats st;
            std::vector<Item> items;
            {
                std::lock_guard<std::mutex> l(catalog_mu_);
                catalog_.resolve(usb, tids);
                titles_->clear_config();
                for (const auto& em : catalog_.all())
                    for (const auto& t : em.title_ids) titles_->set_config_files(t, em.config_files);
                OpMark op_scan("scan des jeux");
                items = scanner_.run(places, roms_->roots(), &st);
                if (st.reused < st.dirs) scanner_.save_cache(fs::host(fs::join(dir_, "scan-cache.json")));
            }
            // Sauvegardes PS5/PS4 de chaque compte : sur le jeu installé, ou « sauvegarde seule ».
            {
                const auto apps = appdb::read();
                // Titre installé mais type « application » dans la base système (YouTube, Netflix…) : pas un jeu.
                for (auto& it : items) {
                    auto a = apps.find(it.title_id);
                    if (it.kind == Kind::game_ps5 && a != apps.end() && a->second.is_app()) it.kind = Kind::app;
                }
                const auto accs_now = accounts::list();
                saves::attach(items, accs_now, apps);
                // Registre des identités : les comptes de la console, puis les registres des disques USB (mis en commun,
                // et réécrits sur chaque disque pour qu'un formatage de la console ne fasse pas oublier les noms).
                for (const auto& a : accs_now) identities_->note(a.account_id, a.name, a.user_id, now_unix());
                for (const auto& a : accs_now) if (!a.name.empty()) { std::lock_guard<std::mutex> l(console_name_mu_); if (!console_name_system_) console_name_ = "PS5 · " + a.name; break; }
                for (const auto& pl : places)
                    if (pl.type == Place::Type::usb) {
                        std::string text;
                        const std::string folder = settings().custom_folder.empty() ? fs::join(pl.mount, places::default_folder) : settings().custom_folder;
                        Json other;
                        const std::string path = fs::host(fs::join(folder, "identities.json"));
                        if (fs::read(path, text) && Json::parse(text, other)) identities_->merge(other);
                    }
                const std::string reg = identities_->to_json().dump(true) + "\n";
                for (const auto& pl : places)
                    if (pl.type == Place::Type::usb) {
                        std::string text;
                        const std::string folder = settings().custom_folder.empty() ? fs::join(pl.mount, places::default_folder) : settings().custom_folder;
                        if (!fs::is_dir(fs::host(folder))) continue;  // jamais créer « PBC Backup » juste pour le registre
                        const std::string path = fs::host(fs::join(folder, "identities.json"));
                        if (!fs::read(path, text) || text != reg) fs::write_atomic(path, reg);
                    }
                identities_->save();
                for (auto& it : items) if (it.kind == Kind::rom && it.cover.empty()) it.cover = fetcher_->lookup(it.key, it.platform, it.title);
                for (auto& it : items) official_cover(it);
            }
            {
                MuGuard l(mu_, __FILE__, __LINE__);
                places_ = std::move(places);
                items_ = std::move(items);
                last_scan_ = st;
                scanned_at_ = now_unix();
                scanning_ = false;
            }
            save_items();
            ++revision_;
            last = steady_clock::now();
        }
        // Plans dus : on crée leurs tâches (au plus une fois par minute de vérification).
        static std::int64_t last_plan_check = 0, last_play_tick = 0, last_update_check = 0;
        const std::int64_t now = now_unix();
        // Mises à jour : une recherche toutes les heures (si « Rechercher automatiquement » est à Oui).
        if (!last_update_check) last_update_check = now;  // celle du démarrage compte pour la première
        if (now - last_update_check >= 3600) {
            last_update_check = now;
            if (settings().update_auto_check) updater_->check_auto(600);
        }
        // Temps de jeu : le jeu en cours, toutes les 30 s (sessions additionnées par jeu).
        if (now - last_play_tick >= 30) {
            last_play_tick = now;
            const std::string run = images::running_title();
            {
                std::lock_guard<std::mutex> l(playtime_mu_);
                playtime_->tick(run, now);
            }
            // Jamais pendant une partie ; Backup Center lui-même au premier plan n'est pas une partie.
            if ((run.empty() || run == "PPSA99950") && settings().update_auto_install) auto_install_step();
        }
        if (now - last_plan_check >= 20) {
            last_plan_check = now;
            for (const auto& p : plans_->due(now)) {
                std::string err;
                const int n = run_plan(p.id, err);
                log("plan due id=%s queued=%d error=%s", p.id.c_str(), n, err.c_str());
            }
        }
        ::usleep(200 * 1000);
    }
}

// Alimentation : refusée pendant une partie ou une copie, sauf `force` (l'utilisateur a confirmé).
bool Engine::power(const std::string& action, bool force, std::string& error) {
    // Le mode repos plante les consoles débridées (réseau coupé, console à rallumer à la main).
    if (action == "rest") { error = "mode repos désactivé : il peut planter une console débridée (redémarrer ou éteindre)"; return false; }
    if (!force) {
        const std::string run = images::running_title();
        if (!run.empty() && run != "PPSA99950") { error = "une partie est en cours (" + run + ")"; return false; }
        if (std::string tid, ttl; queue_->running(tid, ttl)) { error = "une copie est en cours"; return false; }
        for (const auto& [id, s] : link_sends().members()) if (s["state"].as_string() == "running") { error = "un envoi est en cours"; return false; }
    }
    log("power request action=%s force=%d", action.c_str(), force ? 1 : 0);
    return power::request(action, 1500, error);
}

// ---- « Fichiers » : copier / déplacer n'importe quels fichiers ou dossiers (files/files.hpp) ----------------------
namespace {
// Chemin sûr : absolu, sans « . » ni « .. », sans octet nul.
bool clean_abs(const std::string& p) {
    if (p.empty() || p[0] != '/' || p.size() > 2048 || p.find('\0') != std::string::npos) return false;
    for (std::size_t s = 1; s <= p.size();) {
        const auto e = p.find('/', s);
        const std::string seg = p.substr(s, e == std::string::npos ? std::string::npos : e - s);
        if (seg == ".." || seg == ".") return false;
        if (e == std::string::npos) break;
        s = e + 1;
    }
    return true;
}
// Dossier d'état du moteur (jetons, liaison) : jamais lu ni écrit par « Fichiers », ici ou depuis l'autre PS5.
bool forbidden_local(const std::string& p) {
    return p == "/data/pbc-backup-center" || p.rfind("/data/pbc-backup-center/", 0) == 0;
}
}  // namespace

Json Engine::files_places() {
    Json a = Json::array();
    std::vector<Place> ps;
    { MuGuard l(mu_, __FILE__, __LINE__); ps = places_; }
    for (const auto& p : ps) {
        Json j = Json::object();
        j.set("id", p.id); j.set("kind", "here"); j.set("label", p.label); j.set("root", p.mount);
        j.set("free", p.free); j.set("total", p.total);
        a.push(j);
    }
    for (const auto& r : remotes_->list()) {
        Json j = Json::object();
        j.set("id", r.id); j.set("kind", r.type == "switch" ? "switch" : "nas"); j.set("label", r.name);
        j.set("root", r.ftp.root.empty() ? std::string("/") : r.ftp.root);
        a.push(j);
    }
    Json pp;
    { MuGuard l(mu_, __FILE__, __LINE__); pp = peer_places_; }
    for (const auto& p : pp.items()) {
        std::string inst, place;
        if (!store::parse_peer_id(p["id"].as_string(), inst, place)) continue;
        Json j = Json::object();
        j.set("id", "peerfiles:" + inst + ":" + place); j.set("kind", "peer"); j.set("label", p["label"]); j.set("root", "/");
        j.set("free", p["free"]); j.set("total", p["total"]);
        a.push(j);
    }
    return a;
}

std::unique_ptr<store::Store> Engine::open_files_place(const std::string& id, std::string& error) {
    if (id.rfind("peerfiles:", 0) == 0) {
        std::string inst, place;
        if (!store::parse_peer_id("peer:" + id.substr(10), inst, place) || !link_) { error = "PS5 inconnue"; return nullptr; }
        return store::peer(*link_, inst, "files:" + place, id);
    }
    if (id.rfind("remote:", 0) == 0) return remotes_->open(id, error);
    Place p;
    if (!resolve_place(id, p, error)) return nullptr;
    return store::local();
}

bool Engine::files_list(const std::string& place, const std::string& path, Json& out, std::string& error) {
    if (!clean_abs(path)) { error = "chemin invalide"; return false; }
    const bool here = place.rfind("peerfiles:", 0) != 0 && place.rfind("remote:", 0) != 0;
    if (here && forbidden_local(path)) { error = "dossier réservé au moteur"; return false; }
    auto st = open_files_place(place, error);
    if (!st) return false;
    std::vector<fs::Entry> es;
    if (!st->list(path, es)) { error = "dossier illisible"; return false; }
    std::sort(es.begin(), es.end(), [](const fs::Entry& a, const fs::Entry& b) { return a.dir != b.dir ? a.dir : a.name < b.name; });
    Json a = Json::array();
    for (const auto& e : es) {
        if (!files::safe_name(e.name) || (e.name.size() > 5 && e.name.compare(e.name.size() - 5, 5, ".part") == 0)) continue;
        if (here && forbidden_local(fs::join(path, e.name))) continue;
        Json x = Json::object();
        x.set("name", e.name); x.set("dir", e.dir); x.set("size", static_cast<std::int64_t>(e.size)); x.set("mtime", e.mtime);
        a.push(x);
    }
    out = Json::object();
    out.set("path", path);
    out.set("entries", a);
    if (fs::Space sp; st->space(path, sp)) { out.set("free", static_cast<std::int64_t>(sp.free)); out.set("total", static_cast<std::int64_t>(sp.total)); }
    return true;
}

bool Engine::files_mkdir(const std::string& place, const std::string& path, std::string& error) {
    const bool here = place.rfind("peerfiles:", 0) != 0 && place.rfind("remote:", 0) != 0;
    if (!clean_abs(path) || (here && forbidden_local(path))) { error = "chemin invalide"; return false; }
    auto st = open_files_place(place, error);
    if (!st) return false;
    if (!st->mkdirs(path)) { error = "dossier impossible à créer"; return false; }
    return true;
}

namespace {
// « Supprimer » sur cette PS5 : jamais le système, les jeux et sauvegardes gérés par la console, le moteur, la racine
// d'un disque ni un dossier de premier niveau (/data, /mnt/usb1…).
bool delete_protected_local(const std::string& p, const std::vector<std::string>& mounts) {
    if (forbidden_local(p) || std::count(p.begin(), p.end(), '/') < 2) return true;
    for (const char* bad : {"/system", "/system_ex", "/preinst", "/user", "/dev", "/proc", "/mnt/sandbox", "/data/homebrew", "/update"})
        if (p == bad || p.rfind(std::string(bad) + "/", 0) == 0) return true;  // /data/homebrew : les applis installées (dont celle-ci)
    for (const auto& m : mounts) if (!m.empty() && (p == m || m.rfind(p + "/", 0) == 0)) return true;  // un disque ou ce qui le contient
    return false;
}
}  // namespace

std::string Engine::files_delete(const Json& b, std::string& error) {
    const std::string place = b["place"].as_string(), dir = b["dir"].as_string();
    if (b["confirm"].as_string() != "SUPPRIMER") { error = "confirmation manquante"; return ""; }
    if (place.rfind("peerfiles:", 0) == 0) { error = "suppression sur l'autre PS5 : pas encore possible"; return ""; }
    if (!clean_abs(dir)) { error = "chemin invalide"; return ""; }
    if (!b["names"].is_array() || b["names"].items().empty()) { error = "rien de choisi"; return ""; }
    const bool here = place.rfind("remote:", 0) != 0;
    std::vector<std::string> mounts;
    if (here) { MuGuard l(mu_, __FILE__, __LINE__); for (const auto& p : places_) mounts.push_back(p.mount); }
    for (const auto& n : b["names"].items()) {
        if (!files::safe_name(n.as_string())) { error = "nom invalide"; return ""; }
        const std::string path = fs::join(dir, n.as_string());
        if (here && delete_protected_local(path, mounts)) { error = "élément protégé : " + path; return ""; }
        if (!here && path.find('/', 1) == std::string::npos && dir == "/") { error = "élément protégé (racine du partage) : " + path; return ""; }
    }
    std::string e1;
    if (!open_files_place(place, e1)) { error = "endroit injoignable : " + e1; return ""; }
    Json r = Json::object();
    r.set("op", "delete");
    r.set("place", place);
    r.set("dir", dir);
    r.set("names", b["names"]);
    const std::size_t n = b["names"].items().size();
    const std::string title = "Supprimer · " + (n == 1 ? b["names"].items()[0].as_string() : std::to_string(n) + " éléments");
    log("files: suppression demandée place=%s dir=%s n=%d", place.c_str(), dir.c_str(), static_cast<int>(n));
    return queue_->add_files(r, title);
}

Json Engine::uninstall_status() const {
    std::lock_guard<std::mutex> l(uninstall_mu_);
    return uninstall_status_.is_object() ? uninstall_status_ : Json::object();
}

bool Engine::uninstall_game(const Json& body, std::string& error) {
    const std::string tid = body["title_id"].as_string();
    const bool delete_source = body["delete_source"].as_bool();
    if (body["confirm"].as_string() != "DESINSTALLER") { error = "confirmation manquante"; return false; }
    if (tid.size() != 9 || (tid.rfind("PPSA", 0) != 0 && tid.rfind("CUSA", 0) != 0)) { error = "numéro de jeu invalide"; return false; }
    if (tid == "PPSA99950") { error = "Backup Center ne se désinstalle pas lui-même"; return false; }
    if (images::running_title() == tid) { error = "le jeu est ouvert : fermez-le d'abord"; return false; }
    // Ce qu'on sait du jeu ici : son contenu (image ou dossier) et ses sauvegardes sur la console.
    Item game, saves_item;
    bool has_game = false, has_saves = false;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) {
            if (it.title_id != tid || (it.kind != Kind::game_ps5 && it.kind != Kind::game_ps4)) continue;
            for (const auto& c : it.components) {
                if (c.part == Part::game && !c.sources.empty() && !has_game) { game = it; has_game = true; }
                if (c.part == Part::save && !c.sources.empty() && !has_saves) { saves_item = it; has_saves = true; }
            }
        }
    }
    std::vector<std::string> sources = images::manual_sources(tid);
    const std::vector<std::string> listed = sources;  // à remettre dans la liste si la suite échoue
    for (const auto& s : images::smp_game_sources(tid))
        if (std::find(sources.begin(), sources.end(), s) == sources.end()) sources.push_back(s);
    if (has_game)
        for (const auto& c : game.components)
            if (c.part == Part::game)
                for (const auto& s : c.sources)
                    if (std::find(sources.begin(), sources.end(), s.source) == sources.end()) sources.push_back(s.source);
    if (!has_game && sources.empty() && !fs::is_dir(fs::host("/user/app/" + tid))) { error = "jeu introuvable sur cette PS5"; return false; }
    {
        std::lock_guard<std::mutex> l(uninstall_mu_);
        if (uninstall_running_) { error = "une désinstallation est déjà en cours"; return false; }
        uninstall_running_ = true;
        uninstall_status_ = Json::object();
        uninstall_status_.set("state", "running");
        uninstall_status_.set("title_id", tid);
        uninstall_status_.set("title", has_game ? game.title : (has_saves ? saves_item.title : tid));
        uninstall_status_.set("step", "start");
        uninstall_status_.set("delete_source", delete_source);
        uninstall_status_.set("started", now_unix());
    }
    log("uninstall: %s demandé (supprimer la source : %d, %d source(s))", tid.c_str(), delete_source ? 1 : 0, static_cast<int>(sources.size()));
    std::thread([this, tid, delete_source, sources, listed, has_saves, saves_item] {
        auto set = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(uninstall_mu_); uninstall_status_.set(k, v); };
        auto finish = [this, tid](const std::string& err) {
            std::lock_guard<std::mutex> l(uninstall_mu_);
            uninstall_status_.set("state", err.empty() ? "done" : "failed");
            if (!err.empty()) uninstall_status_.set("error", err);
            uninstall_status_.set("finished", now_unix());
            uninstall_running_ = false;
            log("uninstall: %s %s %s", tid.c_str(), err.empty() ? "terminé" : "échec", err.c_str());
        };
        // 1. Sauvegardes du jeu d'abord (sauvegarde normale, restaurable) : la désinstallation peut les effacer.
        if (has_saves) {
            set("step", "backup_saves");
            std::string task, err;
            if (!backup_item(saves_item, {Part::save}, "", task, err)) { finish("sauvegarde des parties impossible : " + err + " — rien n'a été désinstallé"); return; }
            set("backup_task", task);
            for (int i = 0;; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                tasks::Task t;
                if (!queue_->get(task, t)) { finish("sauvegarde des parties introuvable — rien n'a été désinstallé"); return; }
                if (t.state == tasks::State::done) break;
                if (t.state == tasks::State::failed || t.state == tasks::State::cancelled || i > 900) {
                    finish("sauvegarde des parties pas terminée (" + (t.error.empty() ? std::string("interrompue") : t.error) + ") — rien n'a été désinstallé");
                    return;
                }
            }
        }
        if (images::running_title() == tid) { finish("le jeu a été ouvert entre-temps — rien n'a été désinstallé"); return; }
        // 2. Retiré de la liste de ShadowMountPlus : sinon son prochain scan le réinstalle.
        set("step", "unlist");
        // En cas d'échec plus loin, la liste est remise comme avant (le jeu reste inscrit, rien n'est perdu).
        auto relist = [&listed] { for (const auto& s : listed) images::smp_manual_add(s); };
        for (const auto& s : images::manual_sources(tid)) {
            std::string err;
            if (!images::smp_manual_remove(s, err)) { relist(); finish(err); return; }
        }
        // 3. Désinstallation par ShadowMountPlus (le système l'accepte, puis retire le jeu en arrière-plan).
        set("step", "uninstall");
        if (fs::is_dir(fs::host("/user/app/" + tid))) {
            std::string err;
            if (!images::smp_uninstall(tid, err)) { relist(); finish(err + " — le jeu reste installé"); return; }
            for (int i = 0; i < 60 && fs::is_dir(fs::host("/user/app/" + tid)); ++i) std::this_thread::sleep_for(std::chrono::seconds(2));
            if (fs::is_dir(fs::host("/user/app/" + tid))) set("note", "désinstallation acceptée, encore en cours côté système");
        }
        // 4. Image ou dossier du jeu : seulement si demandé, et seulement ce qui porte le numéro du jeu.
        if (delete_source) {
            set("step", "delete_source");
            Json removed = Json::array();
            for (const auto& s : sources) {
                const std::string base(fs::basename(s));
                if (base.compare(0, 9, tid) != 0 || s.rfind("/user/", 0) == 0 || s.rfind("/system", 0) == 0 || s.find("/../") != std::string::npos) continue;
                if (images::mounted(s, images::kind_of(s), tid)) continue;  // encore monté : on n'y touche pas
                const bool ok = fs::is_dir(fs::host(s)) ? fs::remove_tree(fs::host(s)) : ::unlink(fs::host(s).c_str()) == 0;
                ::unlink(fs::host(s + ".vhash").c_str());
                if (ok) removed.push(s);
            }
            set("removed", removed);
        }
        request_scan();
        finish("");
    }).detach();
    return true;
}

std::string Engine::files_copy(const Json& b, std::string& error) {
    const std::string sp = b["src_place"].as_string(), dp = b["dst_place"].as_string();
    const std::string sd = b["src_dir"].as_string(), dd = b["dst_dir"].as_string();
    auto here = [](const std::string& p) { return p.rfind("peerfiles:", 0) != 0 && p.rfind("remote:", 0) != 0; };
    if (!clean_abs(sd) || !clean_abs(dd) || (here(sp) && forbidden_local(sd)) || (here(dp) && forbidden_local(dd))) { error = "chemin invalide"; return ""; }
    if (!b["names"].is_array() || b["names"].items().empty()) { error = "rien de choisi"; return ""; }
    for (const auto& n : b["names"].items()) if (!files::safe_name(n.as_string())) { error = "nom invalide"; return ""; }
    if (sp == dp) {  // pas de copie d'un dossier dans lui-même
        for (const auto& n : b["names"].items()) {
            const std::string src = fs::join(sd, n.as_string());
            if (dd == src || dd.rfind(src + "/", 0) == 0) { error = "la destination est dans un dossier copié"; return ""; }
        }
    }
    std::string e1, e2;
    if (!open_files_place(sp, e1)) { error = "départ injoignable : " + e1; return ""; }
    if (!open_files_place(dp, e2)) { error = "destination injoignable : " + e2; return ""; }
    Json r = Json::object();
    for (const char* k : {"src_place", "src_dir", "dst_place", "dst_dir"}) r.set(k, b[k]);
    r.set("names", b["names"]);
    r.set("move", b["move"].as_bool());
    const std::size_t n = b["names"].items().size();
    const std::string title = n == 1 ? b["names"].items()[0].as_string() : std::to_string(n) + " éléments";
    return queue_->add_files(r, title);
}

// « Installer automatiquement » : une mise à jour trouvée est installée, puis le moteur est relancé quand aucune tâche
// ne tourne (ses tâches reprendraient, mais on évite de couper une copie). Appelé toutes les 30 s, hors partie.
void Engine::auto_install_step() {
    const Json u = updater_->status();
    const std::string st = u["state"].as_string();
    if (st == "available" && !updater_->skipped(u["available"].as_string())) {  // jamais la version quittée par « Revenir »
        std::string err;
        const bool ok = updater_->install(err);
        log("update auto-install version=%s ok=%d %s", u["available"].as_string().c_str(), ok ? 1 : 0, err.c_str());
        return;
    }
    static std::string restarted_for;  // une seule tentative de relance par version installée
    const std::string inst = u["installed"].as_string();
    if (!u["engine_restart_needed"].as_bool() || inst.empty() || inst == restarted_for || st == "downloading" || st == "installing") return;
    if (queue_->has_work()) return;
    for (const auto& [id, s] : link_sends().members()) if (s["state"].as_string() == "running") return;
    restarted_for = inst;
    std::string err;
    const bool ok = updater_->restart_engine(err);
    log("update auto-restart installed=%s ok=%d %s", inst.c_str(), ok ? 1 : 0, err.c_str());
}

void Engine::save_items() {
    Json j = Json::object();
    Json a = Json::array();
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) a.push(it.to_json());
        j.set("scanned_at", scanned_at_);
    }
    j.set("items", a);
    fs::write_atomic(fs::host(fs::join(dir_, "items.json")), j.dump() + "\n");
}

void Engine::load_items() {
    std::string text;
    Json j;
    if (!fs::read(fs::host(fs::join(dir_, "items.json")), text, 64u << 20) || !Json::parse(text, j)) return;
    MuGuard l(mu_, __FILE__, __LINE__);
    items_.clear();
    for (const auto& ij : j["items"].items()) { Item it; if (Item::from_json(ij, it)) items_.push_back(std::move(it)); }
    scanned_at_ = j["scanned_at"].as_int();
}

std::int64_t Engine::now_unix_engine() { return now_unix(); }

namespace {
std::mutex g_tv_mu;
std::string g_tv_version;
std::int64_t g_tv_seen = 0;
}
std::vector<std::pair<std::int64_t, std::string>> g_local_reqs;
std::mutex g_upd_mu;
std::map<std::string, std::int64_t> g_upd_tokens;   // jeton → expiration
Json g_upd_status = Json::object();
std::atomic<bool> g_upd_running{false};

bool Engine::update_token_ok(const std::string& token) {
    std::lock_guard<std::mutex> l(g_upd_mu);
    const auto it = g_upd_tokens.find(token);
    return it != g_upd_tokens.end() && it->second > now_unix();
}

Json Engine::update_peer_status() { std::lock_guard<std::mutex> l(g_upd_mu); return g_upd_status; }

bool Engine::update_from(const std::string& source, std::string& error) {
    if (!updater_->check_from(source, error)) return false;
    std::thread([this] {  // dès que c'est trouvé : installer (mêmes vérifications qu'une mise à jour normale)
        for (int i = 0; i < 120; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const std::string st = updater_->status()["state"].as_string();
            if (st == "available") { std::string e2; updater_->install(e2); return; }
            if (st == "error" || st == "up_to_date" || st == "installed") return;
        }
    }).detach();
    return true;
}

bool Engine::update_peer(const std::string& instance, std::string& error) {
    if (!link_) { error = "liaison indisponible"; return false; }
    std::string host, name;
    {
        const Json peers = link_->peers();
        for (const auto& p : peers.items()) if (p["instance"].as_string() == instance && p["paired"].as_bool()) { host = p["host"].as_string(); name = p["name"].as_string(); }
    }
    if (host.empty()) { error = "PS5 inconnue ou pas jumelée"; return false; }
    // Notre adresse vue de l'autre PS5 : l'interface réseau qui mène à elle.
    std::string me;
    {
        const int sfd = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET; a.sin_port = htons(9);
        if (sfd >= 0 && ::inet_pton(AF_INET, host.c_str(), &a.sin_addr) == 1 && ::connect(sfd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
            sockaddr_in l{}; socklen_t ll = sizeof l;
            char buf[INET_ADDRSTRLEN] = {0};
            if (::getsockname(sfd, reinterpret_cast<sockaddr*>(&l), &ll) == 0 && ::inet_ntop(AF_INET, &l.sin_addr, buf, sizeof buf)) me = buf;
        }
        if (sfd >= 0) ::close(sfd);
    }
    if (me.empty() && !link_->via_wan(instance)) { error = "adresse de cette PS5 introuvable"; return false; }
    if (g_upd_running.exchange(true)) { error = "une mise à jour d'une autre PS5 est déjà en cours"; return false; }
    const std::string token = random_id(16);
    { std::lock_guard<std::mutex> l(g_upd_mu); g_upd_tokens[token] = now_unix() + 7200; }
    const std::string want = updater_->installed_version();
    const std::string source = (link_->via_wan(instance) ? "http://wan-" + link_id_ : "http://" + me + ":" + std::to_string(http_port_)) + "/api/v2/update/self/" + token + "/manifest.json";
    auto set = [](const std::string& st, const std::string& msg) { std::lock_guard<std::mutex> l(g_upd_mu); g_upd_status.set("state", st); g_upd_status.set("message", msg); };
    {
        std::lock_guard<std::mutex> l(g_upd_mu);
        g_upd_status = Json::object();
        g_upd_status.set("peer", instance); g_upd_status.set("name", name); g_upd_status.set("version", want);
        g_upd_status.set("state", "starting"); g_upd_status.set("started", now_unix());
    }
    std::thread([this, instance, source, want, token, set] {
        Json o, st;
        std::string err;
        bool ok = link_->call(instance, "POST", "/api/v2/update/from", [&] { Json b = Json::object(); b.set("source", source); { std::lock_guard<std::mutex> cl(console_name_mu_); b.set("from", console_name_.empty() ? std::string("PS5") : console_name_); } return b; }(), o, err, 30);
        std::string restore_source;  // version plus ancienne (sans « from ») : son réglage de source, remis à la fin
        bool legacy = false;
        if (!ok && err.find("action inconnue") != std::string::npos) {
            // Version d'avant 2.88 : elle sait se mettre à jour depuis la source réglée dans ses paramètres. On règle
            // notre adresse le temps de la mise à jour, puis on remet SON réglage (même vide).
            Json cur, r1, r2, r3;
            std::string e1;
            ok = link_->call(instance, "GET", "/api/v2/settings", Json(), cur, e1, 15);
            restore_source = cur["update_source"].as_string();
            Json b = Json::object(); b.set("update_source", source);
            ok = ok && link_->call(instance, "POST", "/api/v2/settings", b, r1, e1, 15);
            ok = ok && link_->call(instance, "POST", "/api/v2/update/check", Json::object(), r2, e1, 15);
            for (int i = 0; ok && i < 60; ++i) {  // recherche, puis installation
                std::this_thread::sleep_for(std::chrono::seconds(2));
                Json st0; std::string e2;
                if (!link_->call(instance, "GET", "/api/v2/update", Json(), st0, e2, 10)) continue;
                if (st0["state"].as_string() == "available") { ok = link_->call(instance, "POST", "/api/v2/update/install", Json::object(), r3, e1, 15); break; }
                if (st0["state"].as_string() == "error") { ok = false; e1 = st0["message"].as_string(); break; }
                if (st0["state"].as_string() == "up_to_date") break;
            }
            legacy = true;
            err = e1;
        }
        if (!ok) set("failed", "l'autre PS5 refuse : " + err);
        // Attente de l'installation là-bas (téléchargement + vérification + remplacement), 20 min au plus.
        std::string last;
        for (int i = 0; ok && i < 600; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!link_->call(instance, "GET", "/api/v2/update", Json(), st, err, 10)) continue;
            const std::string s = st["state"].as_string();
            if (st["installed"].as_string() == want) break;
            if (s == "error") { ok = false; set("failed", st["message"].as_string()); break; }
            if (s != last) { last = s; set(s == "downloading" ? "downloading" : s == "installing" ? "installing" : "checking", st["message"].as_string()); }
            if (s == "up_to_date") break;
        }
        if (ok && st["installed"].as_string() != want && st["running"].as_string() != want) { ok = false; set("failed", "installation non confirmée là-bas"); }
        if (legacy) {  // son réglage de source, tel qu'il était
            Json b = Json::object(), r4; std::string e4;
            b.set("update_source", restore_source);
            link_->call(instance, "POST", "/api/v2/settings", b, r4, e4, 15);
        }
        if (ok) {  // relance du moteur là-bas, par SA voie normale ; refus = on s'arrête et on le dit
            set("restarting", "");
            Json r2;
            std::string e2;
            const bool asked = link_->call(instance, "POST", "/api/v2/update/restart-engine", Json::object(), r2, e2, 15);
            // Refus clair (chargeur injoignable, moteur introuvable…) : on s'arrête et on le dit. Connexion coupée : normal
            // pendant une relance, on attend la nouvelle version.
            const bool cut = e2.find("connexion") != std::string::npos || e2.find("délai") != std::string::npos || e2.find("réponse") == 0 || e2.empty();
            if (!asked && !cut) {
                set("installed_not_restarted", want + " installée ; relance refusée là-bas (" + e2 + ") : active au prochain démarrage de cette console");
            } else {
                bool up = false;
                for (int i = 0; i < 45 && !up; ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    Json s3; std::string e3;
                    if (link_->call(instance, "GET", "/api/v2/update", Json(), s3, e3, 5) && s3["running"].as_string() == want) up = true;
                }
                set(up ? "done" : "installed_not_restarted", up ? want + " en marche" : want + " installée ; le moteur ne répond pas encore avec cette version");
            }
        }
        { std::lock_guard<std::mutex> l(g_upd_mu); g_upd_tokens.erase(token); g_upd_status.set("finished", now_unix()); }
        g_upd_running = false;
        log("update peer %s : %s", instance.c_str(), update_peer_status()["state"].as_string().c_str());
    }).detach();
    return true;
}
void Engine::note_local_request(const std::string& what) {
    std::lock_guard<std::mutex> l(g_tv_mu);
    g_local_reqs.push_back({now_unix(), what});
    if (g_local_reqs.size() > 80) g_local_reqs.erase(g_local_reqs.begin());
}
void Engine::note_tv_app(const std::string& version) {
    std::lock_guard<std::mutex> l(g_tv_mu);
    g_tv_version = version; g_tv_seen = now_unix();
}

Json Engine::status() const {
    // Jamais bloqué : si le verrou principal ne se libère pas en 1 s, on répond quand même (occupé, et à quoi).
    // 1 s et pas 2 : l'appli télé abandonne au bout de 1,5 s et se croyait alors sans moteur.
    std::unique_lock<std::mutex> l(mu_, std::try_to_lock);
    // Essai toutes les 5 ms (et pas 100) : pendant une préparation de synchro, le verrou est pris et rendu des milliers de
    // fois de suite ; en ne regardant que toutes les 100 ms, on tombait toujours dessus (« occupé » sans vraie attente).
    for (int i = 0; i < 200 && !l.owns_lock(); ++i) { ::usleep(5 * 1000); l.try_lock(); }
    if (!l.owns_lock()) {
        Json b = Json::object();
        b.set("engine", "PS5 Backup Center");
        b.set("version", engine_version);
        b.set("link_id", link_id_);
        b.set("busy", true);
        b.set("started_at", started_at_);           // l'appli voit quand même un nouveau moteur (fenêtre « rouvrir »)
        b.set("update", updater_->status());        // et l'état des mises à jour (verrou à part)
        b.set("busy_in", std::string(g_engine_op.load()));
        b.set("busy_for", now_unix() - g_engine_op_since.load());
        b.set("slow_locks", slow_locks_json());
        return b;
    }
    Json j = Json::object();
    j.set("engine", "PS5 Backup Center");
    j.set("version", engine_version);
    j.set("started_at", started_at_);
    // Fiche d'identité : pour savoir quel moteur répond, et s'il est en train de s'arrêter.
    j.set("pid", static_cast<std::int64_t>(::getpid()));
    j.set("ppid", static_cast<std::int64_t>(::getppid()));
    j.set("instance", instance_);
    j.set("link_id", link_id_);  // identifiant stable (liaison entre consoles)
    j.set("firmware", firmware());  // système de la console (format conseillé pour un jeu reçu)
    j.set("role", stopping_since_.load() ? "stopping" : "active");
    j.set("slow_locks", slow_locks_json());
    j.set("files_stage", files::stage_json());
    if (stopping_since_.load()) j.set("stopping_since", stopping_since_.load());
    if (std::string tid, ttl; queue_->running(tid, ttl)) {
        Json a = Json::object();
        a.set("id", tid);
        a.set("title", ttl);
        j.set("active_task", a);
    }
    j.set("revision", revision());
    j.set("items", static_cast<std::uint64_t>(items_.size()));
    j.set("scanning", scanning_);
    j.set("scanned_at", scanned_at_);
    Json s = Json::object();
    s.set("dirs", static_cast<std::uint64_t>(last_scan_.dirs));
    s.set("reused", static_cast<std::uint64_t>(last_scan_.reused));
    s.set("ms", last_scan_.ms);
    j.set("last_scan", s);
    j.set("update", updater_->status());
    j.set("memory", memory::stats());  // mémoire du moteur (console) : allouée / active / réservée
    {   // l'appli télé qui nous parle : sa version et quand elle a parlé pour la dernière fois
        std::lock_guard<std::mutex> tl(g_tv_mu);
        Json tv = Json::object();
        tv.set("version", g_tv_version); tv.set("seen", g_tv_seen);
        Json rq = Json::array();
        for (const auto& [at, w] : g_local_reqs) { Json x = Json::object(); x.set("at", at); x.set("what", w); rq.push(x); }
        tv.set("recent", rq);
        j.set("tv_app", tv);
    }
    return j;
}

Json Engine::emulators_json() const { std::lock_guard<std::mutex> l(catalog_mu_); return catalog_.to_json(); }

bool Engine::set_emulator_settings(const std::string& id, const Json& settings) {
    {
        std::lock_guard<std::mutex> l(catalog_mu_);
        if (!catalog_.set_manual(id, settings, fs::host(fs::join(dir_, "emulator-settings.json")))) return false;
    }
    request_scan();  // les dossiers changent : on relit la collection
    ++revision_;
    return true;
}

// Dossiers et fichiers d'un appareil réseau (Switch, NAS…), pour l'explorateur de Destinations.
// Parcourir un appareil AVANT de l'enregistrer : on se connecte avec les réglages saisis (mot de passe
// compris ; pour un appareil déjà enregistré, celui gardé sur la console s'il n'est pas redonné), pour
// choisir le dossier sans le taper. Rien n'est gardé.
Json Engine::browse_config(const Json& body, std::string& error) {
    store::FtpConfig cfg = store::FtpConfig::from_json(body);
    if (body["type"].as_string() == "s3" && cfg.service != "other" && cfg.service != "r2") cfg.host = store::s3_endpoint(cfg);
    if (body["type"].as_string() == "gdrive" || body["type"].as_string() == "onedrive" || body["type"].as_string() == "dropbox") {
        store::Remote linked;
        linked.type = body["type"].as_string();
        if (!body["link"].as_string().empty() && !take_link(body, linked, error)) return Json();
        cfg.password = linked.ftp.password;
        cfg.host = linked.type;
    }
    if (cfg.host.empty()) { error = "adresse manquante"; return Json(); }
    const std::string id = body["id"].as_string();
    store::Remote known;
    if (cfg.password.empty() && !id.empty() && remotes_->find(id, known)) cfg.password = known.ftp.password;
    if (body["type"].as_string() == "webdav" && cfg.pin.empty() && !id.empty() && remotes_->find(id, known)) cfg.pin = known.ftp.pin;
    if (body["type"].as_string() == "webdav" && cfg.pin.empty() && !store::webdav_trust(cfg, error)) return Json();
    auto st = store::connect(body["type"].as_string(), cfg, error);
    if (!st) return Json();
    return list_store(*st, body["path"].as_string(), error);
}

// Tester des réglages pas encore enregistrés : connexion, puis écriture d'un témoin dans le dossier choisi
// (lecture seule du dossier JKSV pour une Switch). Rien n'est gardé.
bool Engine::test_config(const Json& body, std::string& error) {
    store::Remote r;
    r.type = body["type"].as_string().empty() ? "ftp" : body["type"].as_string();
    r.ftp = store::FtpConfig::from_json(body);
    const std::string id = body["id"].as_string();
    store::Remote known;
    if (!body["link"].as_string().empty() && !take_link(body, r, error)) return false;
    if (r.ftp.password.empty() && !id.empty() && remotes_->find(id, known)) r.ftp.password = known.ftp.password;
    if (r.type == "webdav" && !store::webdav_trust(r.ftp, error)) return false;  // certificat maison du NAS accepté pour ce test
    return store::Remotes::test(r, error);
}

Json Engine::browse_remote(const std::string& id, const std::string& path, std::string& error) {
    auto st = remotes_->open(id, error);
    if (!st) return Json();
    return list_store(*st, path, error);
}

Json Engine::list_store(store::Store& st0, const std::string& path, std::string& error) {
    store::Store* st = &st0;
    std::string p = path.empty() ? "/" : path;
    if (p != "/" && p.back() == '/') p.pop_back();
    std::vector<fs::Entry> entries;
    if (!st->list(p, entries)) { error = "dossier illisible : " + p; return Json(); }
    std::sort(entries.begin(), entries.end(), [](const fs::Entry& a, const fs::Entry& b) { return a.dir != b.dir ? a.dir : a.name < b.name; });
    Json out = Json::object(), list = Json::array();
    out.set("path", p);
    for (const auto& e : entries) {
        if (e.name.empty() || e.name[0] == '.') continue;
        Json j = Json::object();
        j.set("name", e.name);
        j.set("dir", e.dir);
        j.set("size", e.size);
        list.push(j);
        if (list.size() >= 500) break;
    }
    out.set("entries", list);
    return out;
}

// Dossiers et fichiers d'un dossier de la console (explorateur de l'app, pour choisir un dossier).
Json Engine::browse(const std::string& path, std::string& error) const {
    std::string p = path.empty() ? "/" : path;
    if (p != "/" && p.back() == '/') p.pop_back();
    Json out = Json::object(), list = Json::array();
    out.set("path", p);
    if (p == "/") {  // racines utiles : disques USB, données des applications
        for (const char* r : {"/mnt", "/data", "/user"}) { Json e = Json::object(); e.set("name", std::string(r).substr(1)); e.set("dir", true); list.push(e); }
        out.set("entries", list);
        return out;
    }
    std::vector<fs::Entry> entries;
    if (!fs::list(fs::host(p), entries)) { error = "dossier illisible : " + p; return Json(); }
    std::sort(entries.begin(), entries.end(), [](const fs::Entry& a, const fs::Entry& b) { return a.dir != b.dir ? a.dir : a.name < b.name; });
    for (const auto& e : entries) {
        if (e.name.empty() || e.name[0] == '.') continue;
        Json j = Json::object();
        j.set("name", e.name);
        j.set("dir", e.dir);
        j.set("size", e.size);
        list.push(j);
        if (list.size() >= 500) break;
    }
    out.set("entries", list);
    return out;
}

Json Engine::places_json() {
    MuGuard l(mu_, __FILE__, __LINE__);
    Json a = Json::array();
    for (const auto& p : places_) {
        Json j = Json::object();
        j.set("id", p.id);
        j.set("type", p.type == Place::Type::usb ? "usb" : "internal");
        j.set("mount", p.mount);
        j.set("label", p.label);
        j.set("free", p.free);
        j.set("total", p.total);
        j.set("marked", p.id.rfind("mount:", 0) != 0);
        a.push(j);
    }
    for (const auto& r : remotes_->list()) {
        Json j = Json::object();
        j.set("id", r.id);
        j.set("type", r.type == "smb" || r.type == "webdav" || r.type == "s3" || r.type == "gdrive" || r.type == "onedrive" || r.type == "dropbox" ? std::string("ftp") : r.type);  // NAS FTP, SMB, WebDAV ou stockage S3 : même rôle partout (sauvegardes, synchro, transferts)
        j.set("protocol", r.type);
        j.set("use", r.use.empty() ? std::string("both") : r.use);  // Sauvegardes et Synchro, ou l'un des deux
        j.set("same_as", remotes_->same_as(r.id, "sync"));  // Synchro : même dossier du même NAS qu'un autre appareil, vide sinon
        j.set("mount", r.address());
        j.set("label", r.name);
        j.set("free", static_cast<std::uint64_t>(0));
        j.set("total", static_cast<std::uint64_t>(0));
        j.set("marked", true);
        j.set("remote", true);
        a.push(j);
    }
    for (const auto& j : peer_places_.items()) a.push(j);  // disques des PS5 jumelées
    return a;
}

Json Engine::items_json(const std::string& kind, const std::string& place) const {
    // Copie sous le verrou (rapide), fiches construites ensuite sans bloquer le moteur. Avant : 564 ms sous le verrou
    // principal à chaque liste (appli télé, autre PS5), et /status attendait.
    std::vector<Item> items;
    std::vector<bool> ignored;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) {
            if (!kind.empty() && kind != to_string(it.kind)) continue;
            if (!place.empty() && place != it.place_id) continue;
            items.push_back(it);
            ignored.push_back(settings_.is_ignored(it.title_id) || settings_.is_ignored(it.key));
        }
    }
    Json a = Json::array();
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        Json j = it.to_json();
        if (ignored[i]) j.set("ignored", true);  // la télé et le web le montrent à part
        // Le client n'a pas besoin des listes de fichiers : seulement composants, libellés et tailles.
        Json comps = Json::array();
        for (const auto& c : j["components"].items()) {
            Json cj = Json::object();
            cj.set("part", c["part"]);
            cj.set("label", c["label"]);
            cj.set("bytes", c["bytes"]);
            if (c["meta"].is_object()) cj.set("meta", c["meta"]);
            comps.push(cj);
        }
        j.set("components", comps);
        j.set("has_cover", !it.cover.empty());
        a.push(j);
    }
    return a;
}

bool Engine::find_item(const std::string& key, Item& out) const {
    MuGuard l(mu_, __FILE__, __LINE__);
    for (const auto& it : items_) if (it.key == key) { out = it; return true; }
    return false;
}

// Icône d'un jeu Switch par son numéro (title ID), pour les jeux absents de la bibliothèque
// (sauvegarde sans ROM sur cette PS5). Téléchargée une fois depuis tinfoil.media, gardée en cache ;
// un jeu inconnu n'est pas redemandé avant une semaine.
static std::string switch_title_cover(const std::string& dir, std::string tid) {
    if (tid.size() != 16) return {};
    for (auto& c : tid) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const std::string folder = fs::host(fs::join(dir, "jaquettes-switch"));
    const std::string jpg = folder + "/" + tid + ".jpg", none = folder + "/" + tid + ".absent";
    fs::Entry e;
    if (fs::stat(jpg, e) && e.size > 0) return jpg;
    if (fs::stat(none, e) && std::time(nullptr) - static_cast<std::time_t>(e.mtime) < 7 * 86400) return {};
    fs::mkdirs(folder);
    std::string body;
    // 6 s au plus : tinfoil ne sert vite que les jeux qu'il a déjà en cache.
    const auto r = http::get("http://tinfoil.media/ti/" + tid + "/256/256", [&](const char* d, std::size_t n) { body.append(d, n); return body.size() < (2u << 20); }, 6);
    const bool image = body.size() > 3 && static_cast<unsigned char>(body[0]) == 0xFF && static_cast<unsigned char>(body[1]) == 0xD8;
    if (r.status == 200 && image) { fs::write_atomic(jpg, body); return jpg; }
    if (r.status == 404 || (r.status == 200 && !image)) fs::write_atomic(none, "");
    return {};
}

// Jeu dont il ne reste que la sauvegarde : l'icône officielle du jeu remplace la petite image de la
// sauvegarde dès qu'elle est téléchargée (en arrière-plan).
void Engine::official_cover(Item& it) {
    if (it.key.rfind("save:", 0) != 0 || it.title_id.empty() || (it.kind != Kind::game_ps5 && it.kind != Kind::game_ps4)) return;
    const std::string official = fetcher_->lookup("pspatches:" + it.title_id, "pspatches", it.title_id);
    if (!official.empty()) it.cover = official;
}

std::string Engine::local_cover_key(const std::string& key) const {
    std::string tid;
    if (key.rfind("tid:switch:", 0) == 0) tid = key.substr(11);
    else if (key.rfind("tid:ps5:", 0) == 0 || key.rfind("tid:ps4:", 0) == 0) tid = key.substr(8);
    else return {};
    MuGuard l(mu_, __FILE__, __LINE__);
    for (const auto& it : items_) if (it.title_id == tid && !it.cover.empty()) return it.key;
    return {};
}

std::string Engine::cover_path(const std::string& key) const {
    if (key.rfind("tid:ps5:", 0) == 0 || key.rfind("tid:ps4:", 0) == 0) {  // jeu PS absent de la console : icône officielle
        const std::string tid = key.substr(8);
        // Si le jeu est quelque part dans la bibliothèque, sa jaquette ; sinon prosperopatches / orbispatches.
        {
            MuGuard l(mu_, __FILE__, __LINE__);
            for (const auto& it : items_) if (it.title_id == tid && !it.cover.empty()) return fs::host(it.cover);
        }
        const std::string official = fetcher_->lookup("pspatches:" + tid, "pspatches", tid);
        if (!official.empty()) return fs::host(official);
        // Pas d'icône officielle ici (console hors ligne…) : la jaquette d'une PS5 jumelée qui a ce jeu, gardée en cache.
        const std::string cached = fs::host(fs::join(fs::join(dir_, "peer-covers"), key.substr(4, 3) + "-" + tid + ".png"));
        if (fs::exists(cached)) return cached;
        static std::mutex miss_mu;
        static std::map<std::string, std::int64_t> miss;  // demandé récemment sans succès : pas de nouvel appel avant 10 min
        { std::lock_guard<std::mutex> g(miss_mu); auto m = miss.find(key); if (m != miss.end() && now_unix() - m->second < 600) return {}; }
        if (link_) {
            const Json peers = link_->peers();
            for (const auto& p : peers.items()) {
                if (!p["paired"].as_bool() || !p["online"].as_bool()) continue;
                std::string png;
                if (link_->get_raw(p["instance"].as_string(), "/api/v2/cover?key=" + key + "&size=512&local=1", png, 5) && png.size() > 8) {
                    fs::mkdirs(fs::host(fs::join(dir_, "peer-covers")));
                    if (fs::write_atomic(cached, png)) return cached;
                }
            }
        }
        { std::lock_guard<std::mutex> g(miss_mu); miss[key] = now_unix(); }
        return {};
    }
    if (key.rfind("tid:switch:", 0) == 0) {
        const std::string tid = key.substr(11);
        {   // le jeu est ici (jaquette d'Eden) : elle d'abord
            MuGuard l(mu_, __FILE__, __LINE__);
            for (const auto& it : items_) if (it.title_id == tid && !it.cover.empty()) return fs::host(it.cover);
        }
        // Icône officielle (CDN Nintendo, en arrière-plan) ; sinon tinfoil.
        const std::string official = fetcher_->lookup("eshop:" + tid, "eshop", tid);
        if (!official.empty()) return fs::host(official);
        const std::string tin = switch_title_cover(dir_, tid);
        if (!tin.empty()) return tin;
        // Hors ligne : la jaquette d'une PS5 jumelée qui a ce jeu (Eden), gardée en cache — comme pour les jeux PS.
        const std::string cached = fs::host(fs::join(fs::join(dir_, "peer-covers"), "sw-" + tid + ".png"));
        if (fs::exists(cached)) return cached;
        static std::mutex miss_mu;
        static std::map<std::string, std::int64_t> miss;
        { std::lock_guard<std::mutex> g(miss_mu); auto m = miss.find(key); if (m != miss.end() && now_unix() - m->second < 600) return {}; }
        if (link_) {
            const Json peers = link_->peers();
            for (const auto& p : peers.items()) {
                if (!p["paired"].as_bool() || !p["online"].as_bool()) continue;
                std::string img;
                if (link_->get_raw(p["instance"].as_string(), "/api/v2/cover?key=" + key + "&size=512&local=1", img, 5) && img.size() > 8) {
                    fs::mkdirs(fs::host(fs::join(dir_, "peer-covers")));
                    if (fs::write_atomic(cached, img)) return cached;
                }
            }
        }
        { std::lock_guard<std::mutex> g(miss_mu); miss[key] = now_unix(); }
        return {};
    }
    if (key.rfind("peer:", 0) == 0) {  // « peer:<instance>:<clé chez elle> » (vue « autre console »)
        const auto c = key.find(':', 5);
        if (c == std::string::npos || !link_) return {};
        const std::string inst = key.substr(5, c - 5), remote_key = key.substr(c + 1);
        const std::string cached = fs::host(fs::join(fs::join(dir_, "peer-covers"), "k-" + sha256_hex(key).substr(0, 24) + ".png"));
        if (fs::exists(cached)) return cached;
        static std::mutex miss_mu;
        static std::map<std::string, std::int64_t> miss;
        { std::lock_guard<std::mutex> g(miss_mu); auto m = miss.find(key); if (m != miss.end() && now_unix() - m->second < 600) return {}; }
        bool online = false;
        { const Json ps = link_->peers(); for (const auto& p : ps.items()) if (p["instance"].as_string() == inst) online = p["paired"].as_bool() && p["online"].as_bool(); }
        std::string enc;
        for (char ch : remote_key) {
            const auto u = static_cast<unsigned char>(ch);
            if (std::isalnum(u) || ch == '-' || ch == '_' || ch == '.') enc += ch;
            else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", u); enc += b; }
        }
        std::string img;
        if (online && link_->get_raw(inst, "/api/v2/cover?key=" + enc + "&size=512", img, 5) && img.size() > 8) {
            fs::mkdirs(fs::host(fs::join(dir_, "peer-covers")));
            if (fs::write_atomic(cached, img)) return cached;
        }
        { std::lock_guard<std::mutex> g(miss_mu); miss[key] = now_unix(); }
        return {};
    }
    Item it;
    if (!find_item(key, it) || it.cover.empty()) return {};
    return fs::host(it.cover);
}

bool Engine::cover_thumbnail(const std::string& key, int max, std::string& png) {
    const std::string src = cover_path(key);
    fs::Entry e;
    if (src.empty() || !fs::stat(src, e)) return false;
    // Clé du cache : fichier source, sa date, sa taille et la taille demandée — une jaquette modifiée est refaite.
    const std::string id = sha256_hex(src + "|" + std::to_string(e.mtime) + "|" + std::to_string(e.size) + "|" + std::to_string(max)).substr(0, 32);
    const std::string cached = fs::host(fs::join(fs::join(dir_, "vignettes"), id + ".png"));
    if (fs::read(cached, png, 4u << 20)) return true;
    static std::mutex decode_mu;  // une seule image décodée à la fois : sinon des dizaines d'images en grand en mémoire
    std::lock_guard<std::mutex> dl(decode_mu);
    if (fs::read(cached, png, 4u << 20)) return true;  // faite pendant l'attente
    std::string data;
    std::vector<std::uint32_t> rgba, small;
    int w = 0, h = 0, sw = 0, sh = 0;
    if (!fs::read(src, data, 16u << 20) || !image::decode(data, rgba, w, h)) return false;
    image::fit(rgba, w, h, max, small, sw, sh);
    image::encode_png(small.data(), static_cast<unsigned>(sw), static_cast<unsigned>(sh), png);
    fs::write_atomic(cached, png);
    return true;
}

bool Engine::resolve_place(const std::string& place_id, Place& out, std::string& error) {
    MuGuard l(mu_, __FILE__, __LINE__);
    std::string want = place_id.empty() ? settings_.default_place : place_id;
    const Place* p = nullptr;
    if (want == "auto") {
        for (const auto& x : places_) if (x.type == Place::Type::usb) { p = &x; break; }
        if (!p && !places_.empty()) p = &places_[0];
    } else {
        p = places::find(places_, want);
    }
    if (!p) { error = "destination introuvable (disque débranché ?)"; return false; }
    out = *p;
    return true;
}

bool Engine::backup(const std::string& key, const std::vector<Part>& parts, const std::string& place_id,
                    std::string& task_id, std::string& error, const std::string& account, int keep) {
    Item item;
    if (!find_item(key, item)) { error = "élément introuvable"; return false; }
    return backup_item(std::move(item), parts, place_id, task_id, error, account, keep);
}

// Sauvegarde complète de la console : un élément « Cette PS5 » dont chaque catégorie cochée est
// un composant, sauvegardé comme les autres (vers un disque ou le NAS).
bool Engine::full_backup(const std::vector<std::string>& cats, const std::string& place_id, std::string& task_id, std::string& error) {
    std::vector<Item> items;
    { MuGuard l(mu_, __FILE__, __LINE__); items = items_; }
    Item item;
    if (!full::make_item(cats, items, item, error)) return false;
    if (place_id.empty() || place_id == "internal") { error = "choisissez un disque USB ou le NAS : une sauvegarde complète ne reste pas sur la console"; return false; }
    return backup_item(std::move(item), {}, place_id, task_id, error, "", 0);
}

Json Engine::full_plan() {
    std::lock_guard<std::mutex> l(full_mu_);
    const std::int64_t now = now_unix();
    if (!full_busy_ && (full_at_ == 0 || now - full_at_ > 120)) {
        full_busy_ = true;
        std::vector<Item> items;
        { MuGuard m(mu_, __FILE__, __LINE__); items = items_; }
        std::thread([this, items = std::move(items)] {
            Json j = full::plan(items);
            std::lock_guard<std::mutex> g(full_mu_);
            full_cache_ = std::move(j);
            full_at_ = now_unix();
            full_busy_ = false;
        }).detach();
    }
    Json out = full_cache_.is_object() ? full_cache_ : Json::object();
    out.set("computing", full_busy_);
    return out;
}

bool Engine::backup_item(Item item, const std::vector<Part>& parts, const std::string& place_id,
                         std::string& task_id, std::string& error, const std::string& account, int keep) {
    if (!account.empty()) {
        std::vector<Component> kept;
        for (auto& c : item.components)
            if (c.part != Part::save || !c.meta.is_object() || c.meta["user_id"].as_string().empty() || c.meta["user_id"].as_string() == account) kept.push_back(std::move(c));
        item.components = std::move(kept);
    }
    {   // une autre PS5 jumelée : « peer:<instance>:<disque chez elle> »
        std::string inst, pl;
        if (store::parse_peer_id(place_id, inst, pl)) {
            if (!link_) { error = "liaison indisponible"; return false; }
            auto st = store::peer(*link_, inst, pl, "PS5");
            if (!st->mkdirs("/")) { error = "l'autre PS5 ne répond pas ou refuse ce disque"; return false; }
            backup::Request req;
            req.item = item;
            req.parts = parts;
            req.folder = "/";
            req.disk_id = place_id;
            req.remote_id = place_id;
            req.keep = keep;
            task_id = queue_->add_backup(req);
            return true;
        }
    }
    if (place_id.rfind("remote:", 0) == 0) {  // appareil réseau : connexion vérifiée tout de suite
        store::Remote r;
        if (!remotes_->find(place_id, r)) { error = "appareil inconnu"; return false; }
        auto st = remotes_->open(place_id, error);
        if (!st) return false;
        if (!st->mkdirs(r.folder())) { error = "dossier impossible à créer sur " + r.name; return false; }
        backup::Request req;
        req.item = item;
        req.parts = parts;
        req.folder = r.folder();
        req.disk_id = r.id;
        req.remote_id = r.id;
        req.keep = keep;
        // Rangement par plateforme : seulement pour une sauvegarde (save) d'un jeu PS5 / PS4.
        const bool saves_only = !parts.empty() && std::all_of(parts.begin(), parts.end(), [](Part p) { return p == Part::save; });
        if (r.ftp.layout == "platform" && saves_only && (item.kind == Kind::game_ps5 || item.kind == Kind::game_ps4) && !item.title_id.empty()) {
            req.flat = true;
            const std::string date = iso_time(now_unix()).substr(0, 10);
            std::string name = backup::platform_bundle(item, date);
            fs::Entry e;
            for (int n = 2; st->stat(fs::join(fs::join(req.folder, name), "_pbc/complete"), e) || st->stat(fs::join(req.folder, name), e); ++n)
                name = backup::platform_bundle(item, date + " (" + std::to_string(n) + ")");  // déjà une sauvegarde ce jour-là
            req.bundle_name = name;
        }
        task_id = queue_->add_backup(req);
        return true;
    }
    Place place;
    if (!resolve_place(place_id, place, error)) return false;
    if (!places::ensure_marker(place)) { error = "disque en lecture seule ?"; return false; }
    // Place disponible : on refuse d'emblée une sauvegarde qui ne tiendrait pas (marge de 1 %).
    std::uint64_t need = 0;
    for (const auto& c : item.components)
        if (parts.empty() || std::find(parts.begin(), parts.end(), c.part) != parts.end()) need += c.bytes;
    fs::Space space;
    if (fs::space(fs::host(place.mount), space) && need + need / 100 > space.free) {
        error = "place insuffisante sur " + place.label + " : il faut " + std::to_string(need / 1000000) + " Mo, " +
                std::to_string(space.free / 1000000) + " Mo libres";
        return false;
    }
    std::string folder;
    if (!places::backup_folder(place, settings().custom_folder, folder)) { error = "dossier de sauvegarde impossible à créer"; return false; }
    {
        MuGuard l(mu_, __FILE__, __LINE__);  // le témoin a pu changer l'identifiant du disque
        for (auto& p : places_) if (p.mount == place.mount) p.id = place.id;
    }
    backup::Request req;
    req.item = item;
    req.parts = parts;
    req.folder = folder;
    req.disk_id = place.id;
    req.keep = keep;
    task_id = queue_->add_backup(req);
    return true;
}

bool Engine::restore(const std::string& place_id, const std::string& bundle_name, const std::vector<Part>& parts,
                     const std::string& target, std::string& task_id, std::string& error, bool overwrite) {
    if (place_id.rfind("remote:", 0) == 0) {
        store::Remote r;
        if (!remotes_->find(place_id, r)) { error = "appareil inconnu"; return false; }
        // Le nom est un chemin relatif à la racine de l'appareil (« PS5 backup/[PS5] …/saves/<date> » ou
        // « PBC Backup/<nom>.pbck »). Ancien format (nom seul) : dans le dossier « PBC Backup ».
        if (bundle_name.empty() || bundle_name.find("..") != std::string::npos) { error = "sauvegarde invalide"; return false; }
        std::string root = r.ftp.root.empty() ? "/" : r.ftp.root;
        if (root.size() > 1 && root.back() == '/') root.pop_back();
        backup::RestoreRequest req;
        req.bundle = bundle_name.find('/') == std::string::npos ? fs::join(fs::join(root, "PBC Backup"), bundle_name) : fs::join(root, bundle_name);
        req.parts = parts;
        req.target = target;
        req.overwrite = overwrite;
        req.remote_id = r.id;
        task_id = queue_->add_restore(req, bundle_name);
        return true;
    }
    Place place;
    if (!resolve_place(place_id, place, error)) return false;
    if (bundle_name.empty() || bundle_name.find("..") != std::string::npos) { error = "sauvegarde invalide"; return false; }
    std::string folder;
    if (!places::backup_folder(place, settings().custom_folder, folder)) { error = "dossier introuvable"; return false; }
    backup::RestoreRequest req;
    req.bundle = fs::join(folder, bundle_name);
    req.parts = parts;
    req.target = target;
    req.overwrite = overwrite;  // remise à l'emplacement d'origine : écrase seulement sur demande explicite
    std::string text;
    Json m;
    std::string title = bundle_name;
    if (fs::read(fs::host(fs::join(req.bundle, "manifest.json")), text) && Json::parse(text, m)) { title = m["item"]["title"].as_string(); req.item_key = m["item"]["key"].as_string(); }
    task_id = queue_->add_restore(req, title);
    return true;
}

bool Engine::restore_save(const std::string& place_id, const std::string& bundle_name, const std::string& user_id, const std::string& title_id,
                          bool allow_resign, std::string& task_id, std::string& error, bool check_only) {
    if (user_id.size() != 8 || user_id.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) { error = "compte invalide"; return false; }
    if (place_id.rfind("remote:", 0) == 0) {  // directement depuis le NAS : les images sont téléchargées par la tâche
        store::Remote rm;
        if (!remotes_->find(place_id, rm)) { error = "appareil inconnu"; return false; }
        if (bundle_name.empty() || bundle_name.find("..") != std::string::npos) { error = "sauvegarde invalide"; return false; }
        if (title_id.empty()) { error = "jeu inconnu pour cette sauvegarde"; return false; }
        std::string root = rm.ftp.root.empty() ? "/" : rm.ftp.root;
        if (root.size() > 1 && root.back() == '/') root.pop_back();
        Json r = Json::object();
        r.set("remote", place_id);
        r.set("bundle", bundle_name.find('/') == std::string::npos ? fs::join(fs::join(root, "PBC Backup"), bundle_name) : fs::join(root, bundle_name));
        r.set("user_id", user_id);
        r.set("title_id", title_id);
        r.set("allow_resign", allow_resign);
        r.set("state_dir", dir_);
        r.set("check_only", check_only);
        std::string title = title_id, item_key = "title:" + title_id;
        {
            MuGuard l(mu_, __FILE__, __LINE__);
            for (const auto& it : items_) if (it.title_id == title_id) { title = it.title; item_key = it.key; break; }
        }
        r.set("item_key", item_key);
        task_id = queue_->add_restore_save(r, std::string(check_only ? "Vérifier · " : "Restaurer · ") + title + " (" + rm.name + ")");
        return true;
    }
    Place place;
    if (!resolve_place(place_id, place, error)) return false;
    if (bundle_name.empty() || bundle_name.find("..") != std::string::npos) { error = "sauvegarde invalide"; return false; }
    std::string folder;
    if (!places::backup_folder(place, settings().custom_folder, folder)) { error = "dossier introuvable"; return false; }
    const std::string bundle = fs::join(folder, bundle_name);
    std::string text;
    Json m;
    Json r = Json::object();
    if (!fs::read(fs::host(fs::join(bundle, "manifest.json")), text) || !Json::parse(text, m)) {
        // Dossier daté par plateforme (ancienne sauvegarde, images seules) : restauration image par image.
        if (title_id.empty()) { error = "sauvegarde illisible"; return false; }
        r.set("images_dir", bundle);
        m = Json::object();
        Json item = Json::object();
        const std::string n = titles::name(title_id);
        item.set("title", n.empty() ? title_id : n);
        item.set("key", "tid:ps5:" + title_id);
        m.set("item", item);
    }
    r.set("bundle", bundle);
    r.set("user_id", user_id);
    r.set("title_id", title_id);
    r.set("allow_resign", allow_resign);
    r.set("state_dir", dir_);
    r.set("check_only", check_only);
    r.set("item_key", m["item"]["key"].as_string());
    task_id = queue_->add_restore_save(r, std::string(check_only ? "Vérifier · " : "Restaurer · ") + m["item"]["title"].as_string());
    return true;
}

bool Engine::inspect_save(const Json& body, std::string& task_id, std::string& error) {
    const std::string title_id = body["title_id"].as_string(), user_id = body["user_id"].as_string();
    if (title_id.empty() || title_id.find_first_of("/.") != std::string::npos) { error = "jeu inconnu"; return false; }
    if (!user_id.empty() && (user_id.size() != 8 || user_id.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)) { error = "compte invalide"; return false; }
    if (!body["sources"].is_array() || !body["sources"].size()) { error = "aucune version à comparer"; return false; }
    Json sources = Json::array();
    for (const auto& s : body["sources"].items()) {
        Json o = Json::object();
        const std::string place_id = s["place"].as_string(), bundle = s["bundle"].as_string();
        if (s["kind"].as_string() == "console") {
            if (user_id.empty()) { error = "compte manquant pour la sauvegarde de la console"; return false; }
            o.set("kind", "console");
        } else if (bundle.empty() || bundle.find("..") != std::string::npos) { error = "sauvegarde invalide"; return false; }
        else if (place_id.rfind("remote:", 0) == 0) {  // NAS : chemin relatif à la racine (ancien format : nom seul dans « PBC Backup »)
            store::Remote rm;
            if (!remotes_->find(place_id, rm)) { error = "appareil inconnu"; return false; }
            std::string root = rm.ftp.root.empty() ? "/" : rm.ftp.root;
            if (root.size() > 1 && root.back() == '/') root.pop_back();
            o.set("kind", "remote");
            o.set("remote", place_id);
            o.set("path", bundle.find('/') == std::string::npos ? fs::join(fs::join(root, "PBC Backup"), bundle) : fs::join(root, bundle));
            o.set("label", rm.name + " · " + bundle);
        } else {
            Place place;
            if (!resolve_place(place_id, place, error)) return false;
            std::string folder;
            if (!places::backup_folder(place, settings().custom_folder, folder)) { error = "dossier introuvable"; return false; }
            o.set("kind", "local");
            o.set("path", fs::join(folder, bundle));
            o.set("label", bundle);
        }
        if (!place_id.empty()) o.set("place", place_id);
        if (!bundle.empty()) o.set("bundle", bundle);
        sources.push(o);
    }
    Json r = Json::object();
    r.set("title_id", title_id);
    r.set("user_id", user_id);
    r.set("sources", sources);
    r.set("force", body["force"].as_bool());
    r.set("state_dir", dir_);
    // Diagnostic : noms de fichiers intérieurs à copier dans <état>/diag/<jeu>/ (jamais pour la sauvegarde de la console).
    if (body["extract"].is_array()) {
        for (const auto& sname : body["sources"].items()) if (sname["kind"].as_string() == "console") { error = "extraction : copies de backups seulement"; return false; }
        r.set("extract", body["extract"]);
    }
    std::string title = title_id, item_key = "title:" + title_id;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) if (it.title_id == title_id) { title = it.title; item_key = it.key; break; }
    }
    r.set("item_key", item_key);
    task_id = queue_->add_inspect_save(r, "Comparer · " + title);
    return true;
}

Json Engine::save_inspections(const std::string& title_id, const std::string& user_id) { return saves::cached_inspections(dir_, title_id, user_id); }

Json Engine::remote_index(const std::string& id, bool fresh) {
    // Une seule lecture d'un même appareil à la fois : pendant qu'elle tourne, les autres demandes reçoivent
    // la dernière liste connue (ou attendent la première), au lieu d'ouvrir chacune leur connexion FTP.
    {
        std::unique_lock<std::mutex> l(remote_index_mu_);
        auto it = remote_index_.find(id);
        if (!fresh && it != remote_index_.end() && now_unix() - it->second.first < 120) return it->second.second;
        if (remote_fetching_.count(id)) {
            if (it != remote_index_.end()) return it->second.second;
            remote_index_cv_.wait_for(l, std::chrono::seconds(90), [&] { return !remote_fetching_.count(id); });
            auto again = remote_index_.find(id);
            return again != remote_index_.end() ? again->second.second : Json::object();
        }
        remote_fetching_.insert(id);
    }
    struct Done {  // fin de lecture signalée quoi qu'il arrive
        Engine* e; std::string id;
        ~Done() { { std::lock_guard<std::mutex> l(e->remote_index_mu_); e->remote_fetching_.erase(id); } e->remote_index_cv_.notify_all(); }
    } done{this, id};
    store::Remote r;
    std::string err;
    Json idx = Json::object();
    if (remotes_->find(id, r)) {
        auto st = remotes_->open(id, err);
        // On lit TOUT ce qui est sous le dossier de l'appareil, quel que soit le rangement choisi (qui ne
        // décide que de l'endroit où vont les nouvelles sauvegardes) : dossiers par plateforme
        // (« PS5 backup/… », y compris ceux faits hors de l'app) et dossier « PBC Backup ».
        // Les noms sont relatifs à la racine de l'appareil.
        if (st) {
            std::string root = r.ftp.root.empty() ? "/" : r.ftp.root;
            if (root.size() > 1 && root.back() == '/') root.pop_back();
            // Par plateforme hors de « PBC Backup » (anciennes, pas encore rangées) et dedans.
            idx = backup::scan_platform(*st, root);
            Json all = idx["backups"].is_array() ? idx["backups"] : Json::array();
            std::set<std::string> seen;
            for (const auto& b : all.items()) seen.insert(b["bundle"].as_string());
            const std::string pbc = fs::join(root, "PBC Backup");
            fs::Entry pe;
            if (fs::basename(root) == "PBC Backup")  // dossier de l'appareil = « PBC Backup » lui-même : son index est là
                for (const auto& b : backup::read_index(*st, root)["backups"].items())
                    if (seen.insert(b["bundle"].as_string()).second) all.push(b);
            if (st->stat(pbc, pe) && pe.dir) {
                auto add = [&](const Json& list) {
                    for (const auto& b : list.items()) {
                        const std::string name = "PBC Backup/" + b["bundle"].as_string();
                        if (!seen.insert(name).second) continue;
                        Json c = b;
                        c.set("bundle", name);
                        all.push(c);
                    }
                };
                add(backup::scan_platform(*st, pbc)["backups"]);
                add(backup::read_index(*st, pbc)["backups"]);
            }
            idx.set("backups", all);
            idx.set("layout", r.ftp.layout);
        }
        else idx.set("error", "appareil injoignable : " + err);
    }
    std::lock_guard<std::mutex> l(remote_index_mu_);
    remote_index_[id] = {now_unix(), idx};
    return idx;
}

// Ranger sous « PBC Backup » les dossiers par plateforme qui sont à la racine de l'appareil
// (« PS5 backup », « Switch backup »…). Renommage sur place (sans copie, réversible) ; si la cible
// existe déjà, on fusionne jeu par jeu puis date par date, sans jamais rien écraser.
Json Engine::adopt_backups(const std::string& id, std::string& error) {
    store::Remote r;
    if (!remotes_->find(id, r)) { error = "appareil inconnu"; return Json(); }
    auto st = remotes_->open(id, error);
    if (!st) return Json();
    std::string root = r.ftp.root.empty() ? "/" : r.ftp.root;
    if (root.size() > 1 && root.back() == '/') root.pop_back();
    std::string pbc = fs::join(root, "PBC Backup");
    if (fs::basename(root) == "PBC Backup") {  // le dossier de l'appareil est déjà « PBC Backup » : on range depuis son parent
        pbc = root;
        root = std::string(fs::dirname(root));
        if (root.empty()) root = "/";
    }
    if (!st->mkdirs(pbc)) { error = "impossible de créer « " + pbc + " »"; return Json(); }
    unsigned moved = 0, merged = 0, skipped = 0;
    Json log = Json::array();
    auto exists = [&](const std::string& p) { fs::Entry e; return st->stat(p, e); };
    std::function<void(const std::string&, const std::string&, int)> move = [&](const std::string& from, const std::string& to, int depth) {
        if (!exists(to)) {
            if (st->rename(from, to)) { ++moved; log.push(from + " → " + to); }
            else { ++skipped; log.push("refusé : " + from); }
            return;
        }
        if (depth >= 3) { ++skipped; log.push("déjà présent, laissé en place : " + from); return; }  // même date des deux côtés
        std::vector<fs::Entry> kids;
        if (!st->list(from, kids)) { ++skipped; return; }
        for (const auto& k : kids) if (k.dir) move(fs::join(from, k.name), fs::join(to, k.name), depth + 1);
        std::vector<fs::Entry> left;
        if (st->list(from, left) && left.empty()) { st->remove(from); ++merged; }  // dossier vide après fusion
    };
    std::vector<fs::Entry> top;
    if (!st->list(root, top)) { error = "dossier illisible : " + root; return Json(); }
    for (const auto& d : top) {
        if (!d.dir || d.name == "PBC Backup" || d.name.size() < 8 || d.name.compare(d.name.size() - 7, 7, " backup") != 0) continue;
        move(fs::join(root, d.name), fs::join(pbc, d.name), 0);
    }
    { std::lock_guard<std::mutex> l(remote_index_mu_); remote_index_.erase(id); }
    Json out = Json::object();
    out.set("moved", static_cast<std::uint64_t>(moved));
    out.set("merged", static_cast<std::uint64_t>(merged));
    out.set("skipped", static_cast<std::uint64_t>(skipped));
    out.set("log", log);
    return out;
}

Json Engine::remotes_json() const {
    Json a = Json::array();
    for (const auto& r : remotes_->list()) a.push(r.to_json(false));
    return a;
}

bool Engine::add_remote(const Json& body, std::string& id, std::string& error) {
    store::Remote r;
    r.type = body["type"].as_string().empty() ? "ftp" : body["type"].as_string();
    r.name = body["name"].as_string();
    r.ftp = store::FtpConfig::from_json(body);
    const std::string use = body["use"].as_string();  // « both » (défaut), « backups » ou « sync »
    if (use == "backups" || use == "sync") r.use = use;
    if (!body["link"].as_string().empty() && !take_link(body, r, error)) return false;  // Google Drive, OneDrive
    if (!remotes_->add(r, id, error)) return false;
    ++revision_;
    return true;
}

bool Engine::update_remote(const std::string& id, const Json& body, std::string& error) {
    store::Remote r;
    r.type = body["type"].as_string();
    r.name = body["name"].as_string();
    r.ftp = store::FtpConfig::from_json(body);
    const std::string use = body["use"].as_string();  // « both » (défaut), « backups » ou « sync »
    if (use == "backups" || use == "sync") r.use = use;
    if (!body["link"].as_string().empty() && !take_link(body, r, error)) return false;  // compte reconnecté
    if (!remotes_->update(id, r, error)) return false;
    ++revision_;
    return true;
}

bool Engine::test_remote(const std::string& id, std::string& error) {
    store::Remote r;
    if (!remotes_->find(id, r)) { error = "appareil inconnu"; return false; }
    return store::Remotes::test(r, error);
}

Json Engine::switch_saves(const std::string& id, std::string& error) {
    store::Remote r;
    if (!remotes_->find(id, r) || r.type != "switch") { error = "Switch inconnue"; return Json(); }
    auto st = remotes_->open(id, error);
    if (!st) return Json();
    // Jeux Switch connus (ROM d'Eden) : nom simplifié → identifiant.
    std::map<std::string, std::pair<std::string, std::string>> known;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) if (it.platform == "Switch" && !it.title_id.empty()) known[covers::normalize(it.title)] = {it.title_id, it.title};
    }
    std::vector<fs::Entry> games;
    if (!st->list("/JKSV", games)) { error = "dossier /JKSV illisible"; return Json(); }
    Json out = Json::array();
    for (const auto& g : games) {
        if (!g.dir || g.name == "_TRASH_") continue;
        std::vector<fs::Entry> saves;
        if (!st->list("/JKSV/" + g.name, saves)) continue;
        // Dossier nommé par l'identifiant du jeu (JKSV quand il ne connaît pas le nom) : on le prend tel quel.
        const bool by_id = g.name.size() == 16 && g.name.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
        auto k = known.find(covers::normalize(g.name));
        std::string tid = by_id ? g.name : k == known.end() ? "" : k->second.first, eden_title = k == known.end() ? "" : k->second.second;
        if (by_id) for (auto& c : tid) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (by_id) for (const auto& [n, v] : known) if (v.first == tid) eden_title = v.second;
        Json game = Json::object();
        game.set("game", g.name);
        game.set("title_id", tid);
        game.set("eden_title", eden_title);
        Json list = Json::array();
        for (const auto& s : saves) {
            // « <profil> - AAAA-MM-JJ_HH-MM-SS[.zip] »
            std::string name = s.name;
            const bool zip = name.size() > 4 && name.compare(name.size() - 4, 4, ".zip") == 0;
            if (zip) name.resize(name.size() - 4);
            const auto dash = name.rfind(" - ");
            Json e = Json::object();
            e.set("file", s.name);
            e.set("profile", dash == std::string::npos ? name : name.substr(0, dash));
            e.set("date", dash == std::string::npos ? "" : name.substr(dash + 3));
            e.set("zip", zip);
            e.set("bytes", s.size);
            e.set("device", dash != std::string::npos && name.substr(0, dash) == "Device");
            list.push(e);
        }
        game.set("saves", list);
        out.push(game);
    }
    return out;
}

namespace {
const std::vector<std::string> eden_roots = {"/data/prosperoeden", "/data/eden"};
}

Json Engine::eden_profiles_json() const {
    Json a = Json::array();
    for (const auto& p : saves::eden_profiles(eden_roots)) {
        Json j = Json::object();
        j.set("eden", p.root == "/data/prosperoeden" ? "ProsperoEden" : "ProsperoEden (/data/eden)");
        j.set("root", p.root);
        j.set("profile", p.folder);
        j.set("name", p.name);
        a.push(j);
    }
    return a;
}

bool Engine::import_switch(const Json& body, std::string& task_id, std::string& error) {
    store::Remote r;
    if (!remotes_->find(body["remote"].as_string(), r) || r.type != "switch") { error = "Switch inconnue"; return false; }
    const std::string root = body["eden_root"].as_string();
    bool known = false;
    for (const auto& p : saves::eden_profiles(eden_roots)) if (p.root == root && p.folder == body["profile"].as_string()) known = true;
    if (!known) { error = "profil Eden inconnu"; return false; }
    Json req = Json::object();
    for (const char* k : {"remote", "game", "file", "title_id", "eden_root", "profile"}) req.set(k, body[k]);
    req.set("check_only", body["check_only"].as_bool());
    req.set("force_older", body["force_older"].as_bool());
    req.set("state_dir", dir_);
    Json apps = Json::array();
    for (const char* a : {"PPSA99008", "PPSA99186"}) apps.push(a);  // ProsperoEden et son interface Switch
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) if (it.kind == Kind::app && it.title.find("Eden") != std::string::npos) apps.push(it.title_id);
    }
    req.set("eden_apps", apps);
    task_id = queue_->add_import_switch(req, std::string(body["check_only"].as_bool() ? "Vérifier · " : "Importer · ") + body["game"].as_string());
    return true;
}

bool Engine::export_switch(const Json& body, std::string& task_id, std::string& error) {
    store::Remote r;
    if (!remotes_->find(body["remote"].as_string(), r) || r.type != "switch") { error = "Switch inconnue"; return false; }
    bool known = false;
    for (const auto& p : saves::eden_profiles(eden_roots)) if (p.root == body["eden_root"].as_string() && p.folder == body["profile"].as_string()) known = true;
    if (!known) { error = "profil Eden inconnu"; return false; }
    Json req = Json::object();
    for (const char* k : {"remote", "eden_root", "profile", "title_id", "game_folder", "profile_name"}) req.set(k, body[k]);
    req.set("check_only", body["check_only"].as_bool());
    task_id = queue_->add_export_switch(req, std::string(body["check_only"].as_bool() ? "Vérifier · " : "Envoyer vers la Switch · ") + body["game_folder"].as_string());
    return true;
}

namespace {
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string upper(std::string s) { for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }
}

Json Engine::sync_compare(const std::string& a, const std::string& b, std::string& error) {
    struct Ref { std::string title_id, title, profile_key, profile_name; std::int64_t time = 0; std::uint64_t bytes = 0; Json where; };
    std::map<std::string, std::string> names;  // identifiant Switch → titre (jeux connus d'Eden)
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) if (it.platform == "Switch" && !it.title_id.empty()) names[upper(it.title_id)] = it.title;
    }
    // Un « côté » : liste de sauvegardes (jeu, profil, date). D'autres formats viendront s'ajouter ici.
    auto collect = [&](const std::string& spec, std::vector<Ref>& out, Json& info) -> bool {
        const auto colon = spec.find(':');
        const std::string kind = spec.substr(0, colon), arg = colon == std::string::npos ? "" : spec.substr(colon + 1);
        info = Json::object();
        info.set("spec", spec);
        info.set("kind", kind);
        if (kind == "switch") {
            store::Remote r;
            if (!remotes_->find(arg, r)) { error = "Switch inconnue"; return false; }
            info.set("name", r.name);
            const Json games = switch_saves(arg, error);
            if (!error.empty()) return false;
            for (const auto& g : games.items()) {
                const std::string tid = upper(g["title_id"].as_string());
                if (tid.empty()) continue;
                std::map<std::string, Ref> newest;
                for (const auto& s : g["saves"].items()) {
                    const std::string prof = s["device"].as_bool() ? "device" : lower(s["profile"].as_string());
                    const std::int64_t t = saves::jksv_time(s["file"].as_string());
                    Ref& ref = newest[prof];
                    if (!ref.title_id.empty() && ref.time >= t) continue;
                    ref.title_id = tid;
                    ref.title = g["eden_title"].as_string().empty() ? g["game"].as_string() : g["eden_title"].as_string();
                    ref.profile_key = prof;
                    ref.profile_name = s["profile"].as_string();
                    ref.time = t;
                    ref.bytes = static_cast<std::uint64_t>(s["bytes"].as_int());
                    Json w = Json::object();
                    w.set("game", g["game"]);
                    w.set("file", s["file"]);
                    ref.where = w;
                }
                for (auto& [k, ref] : newest) out.push_back(std::move(ref));
            }
            return true;
        }
        if (kind == "eden") {
            bool any = false;
            for (const auto& p : saves::eden_profiles(eden_roots)) {
                if (p.root != arg) continue;
                any = true;
                const std::string dir = fs::join(p.root, "user/nand/user/save/0000000000000000/" + p.folder);
                std::vector<fs::Entry> games;
                if (!fs::list(fs::host(dir), games)) continue;
                for (const auto& g : games) {
                    if (!g.dir || g.name.size() != 16) continue;
                    Ref ref;
                    ref.title_id = upper(g.name);
                    auto n = names.find(ref.title_id);
                    if (n != names.end()) ref.title = n->second;
                    else { ref.title = titles::name(ref.title_id); if (ref.title.empty()) ref.title = ref.title_id; }
                    ref.profile_key = p.folder == "00000000000000000000000000000000" ? "device" : lower(p.name);
                    ref.profile_name = p.name;
                    std::vector<fs::Entry> files;
                    if (fs::list(fs::host(fs::join(dir, g.name)), files))
                        for (const auto& f : files) { ref.time = std::max(ref.time, f.mtime); ref.bytes += f.size; }
                    Json w = Json::object();
                    w.set("eden_root", p.root);
                    w.set("profile", p.folder);
                    ref.where = w;
                    out.push_back(std::move(ref));
                }
            }
            info.set("name", arg == "/data/prosperoeden" ? "ProsperoEden" : "ProsperoEden (/data/eden)");
            if (!any) { error = "Eden introuvable : " + arg; return false; }
            return true;
        }
        error = "côté inconnu : " + kind + " (bientôt : PS5, PS4, cartes mémoire PS1/PS2…)";
        return false;
    };
    std::vector<Ref> ra, rb;
    Json ia, ib;
    if (!collect(a, ra, ia) || !collect(b, rb, ib)) return Json();
    struct Pair { const Ref* a = nullptr; const Ref* b = nullptr; };
    std::map<std::string, Pair> pairs;
    for (const auto& r : ra) pairs[r.title_id + "|" + r.profile_key].a = &r;
    for (const auto& r : rb) pairs[r.title_id + "|" + r.profile_key].b = &r;
    // Contenu identique ? Pour une sauvegarde JKSV, la fin du zip donne nom, taille et CRC de chaque
    // fichier : comparés aux fichiers d'Eden, sans télécharger la sauvegarde.
    std::unique_ptr<store::Store> sw;
    std::string sw_err;
    const bool a_switch = a.rfind("switch:", 0) == 0, b_switch = b.rfind("switch:", 0) == 0;
    if (a_switch != b_switch) sw = remotes_->open((a_switch ? a : b).substr(7), sw_err);
    auto identical = [&](const Ref& s, const Ref& e) -> int {  // 1 identique, 0 différent, -1 inconnu
        if (!sw) return -1;
        const std::string path = "/JKSV/" + s.where["game"].as_string() + "/" + s.where["file"].as_string();
        if (s.bytes < 22) return -1;
        const std::uint64_t want = std::min<std::uint64_t>(s.bytes, 65557);
        std::string err, tail;
        auto r = sw->open_at(path, s.bytes - want, err);
        if (!r) return -1;
        char buf[16384];
        for (long n; tail.size() < want && (n = r->read(buf, sizeof buf)) > 0;) tail.append(buf, static_cast<std::size_t>(n));
        r.reset();
        std::vector<zip::Entry> es;
        if (!zip::list_tail(tail, s.bytes, es)) return -1;
        const std::string dir = fs::join(e.where["eden_root"].as_string(), "user/nand/user/save/0000000000000000/" + e.where["profile"].as_string() + "/" + e.title_id);
        std::vector<fs::Entry> files;
        if (!fs::list(fs::host(dir), files)) return -1;
        unsigned count = 0;
        for (const auto& z : es) {
            if (z.dir() || z.name == ".nx_save_meta.bin") continue;
            ++count;
            std::string data;
            if (!fs::read(fs::host(fs::join(dir, z.name)), data, 256u << 20) || data.size() != z.size || zip::crc32(data) != z.crc) return 0;
        }
        unsigned local = 0;
        for (const auto& f : files) if (!f.dir) ++local;
        return local == count ? 1 : 0;
    };
    Json entries = Json::array();
    for (const auto& [key, p] : pairs) {
        const Ref& any = p.a ? *p.a : *p.b;
        Json e = Json::object();
        e.set("title_id", any.title_id);
        e.set("title", p.a && !p.a->title.empty() && p.a->title != p.a->title_id ? p.a->title : any.title);
        e.set("profile", any.profile_key);
        auto side = [](const Ref* r) {
            if (!r) return Json();
            Json j = Json::object();
            j.set("profile_name", r->profile_name);
            j.set("time", r->time);
            j.set("bytes", r->bytes);
            j.set("where", r->where);
            return j;
        };
        e.set("a", side(p.a));
        e.set("b", side(p.b));
        std::string st;
        if (!p.b) st = "only_a";
        else if (!p.a) st = "only_b";
        else {
            const int same = sw ? identical(a_switch ? *p.a : *p.b, a_switch ? *p.b : *p.a) : -1;
            if (same == 1) st = "identical";
            else if (std::llabs(p.a->time - p.b->time) < 86400) st = "same_day";
            else st = p.a->time > p.b->time ? "a_newer" : "b_newer";
        }
        e.set("status", st);
        entries.push(e);
    }
    Json out = Json::object();
    out.set("a", ia);
    out.set("b", ib);
    out.set("entries", entries);
    return out;
}

std::int64_t Engine::utc_offset() const {
    const std::time_t now = std::time(nullptr);
    std::tm local{}, utc{};
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
    return static_cast<std::int64_t>(timegm(&local) - timegm(&utc));
}

std::vector<std::string> Engine::plan_items(const plans::Plan& p) {
    std::vector<std::string> keys = p.items;
    MuGuard l(mu_, __FILE__, __LINE__);
    for (const auto& it : items_) {
        bool take = false;
        if (p.rule == "all_games") take = it.kind == Kind::game_ps5 || it.kind == Kind::game_ps4;
        else if (p.rule == "apps") take = it.kind == Kind::app;
        else if (p.rule == "roms") take = it.kind == Kind::rom && (p.rule_arg.empty() || it.platform == p.rule_arg);
        else if (p.rule == "ps5_saves")
            for (const auto& c : it.components)
                if (c.part == Part::save && c.meta.is_object() && (p.rule_arg.empty() || c.meta["user_id"].as_string() == p.rule_arg)) take = true;
        if (take && (settings_.is_ignored(it.title_id) || settings_.is_ignored(it.key))) take = false;  // ignoré : jamais sauvegardé par une règle
        if (take && std::find(keys.begin(), keys.end(), it.key) == keys.end()) keys.push_back(it.key);
    }
    return keys;
}

Json Engine::plans_json() {
    plans_->refresh_next(now_unix(), utc_offset());
    Json a = Json::array();
    for (const auto& p : plans_->list()) {
        Json j = p.to_json();
        j.set("item_count", static_cast<std::uint64_t>(plan_items(p).size()));
        a.push(j);
    }
    return a;
}

std::string Engine::save_plan(const Json& body, std::string& error) {
    plans::Plan p = plans::Plan::from_json(body);
    if (p.name.empty()) { error = "donnez un nom au plan"; return ""; }
    if (p.items.empty() && p.rule.empty()) { error = "choisissez quoi sauvegarder"; return ""; }
    if (p.timing == plans::Timing::weekly && !p.weekdays) { error = "choisissez au moins un jour"; return ""; }
    p.next_run = plans::next_run(p, now_unix(), utc_offset());
    const std::string id = plans_->put(p);
    ++revision_;
    return id;
}

bool Engine::delete_plan(const std::string& id) {
    if (!plans_->remove(id)) return false;
    ++revision_;
    return true;
}

int Engine::run_plan(const std::string& id, std::string& error) {
    plans::Plan p;
    if (!plans_->get(id, p)) { error = "plan inconnu"; return 0; }
    std::vector<Part> parts;
    for (const auto& s : p.parts) { Part x; if (parse_part(s, x)) parts.push_back(x); }
    int queued = 0;
    std::string last_err;
    for (const auto& key : plan_items(p)) {
        std::string task, err;
        const std::string account = p.rule == "ps5_saves" ? p.rule_arg : "";
        if (backup(key, parts, p.place, task, err, account, p.keep)) ++queued;
        else last_err = err;
    }
    const std::string result = std::to_string(queued) + " sauvegarde(s) lancée(s)" + (last_err.empty() ? "" : " · " + last_err);
    plans_->mark_run(id, now_unix(), result, utc_offset());
    if (!queued && !last_err.empty()) error = last_err;
    // La rétention s'applique à la fin de chaque sauvegarde (voir on_finished).
    ++revision_;
    return queued;
}

void Engine::apply_retention(const std::string& folder, const std::string& item_key, int keep) {
    if (keep <= 0) return;
    const Json idx = backup::read_index(folder);
    struct V { std::string bundle; std::int64_t t; bool protect; };
    std::vector<V> versions;
    for (const auto& b : idx["backups"].items())
        if (b["key"].as_string() == item_key && b["complete"].as_bool())
            versions.push_back({b["bundle"].as_string(), b["created_unix"].as_int(), fs::exists(fs::host(fs::join(fs::join(folder, b["bundle"].as_string()), "protected")))});
    std::sort(versions.begin(), versions.end(), [](const V& a, const V& b) { return a.t > b.t; });
    int kept = 0;
    bool removed = false;
    for (const auto& v : versions) {
        if (v.protect || kept < keep) { if (!v.protect) ++kept; continue; }
        log("retention remove item=%s bundle=%s", item_key.c_str(), v.bundle.c_str());
        fs::remove_tree(fs::host(fs::join(folder, v.bundle)));
        removed = true;
    }
    if (removed) backup::rebuild_index(folder);
}

// ---- Gestion des backups : résumé par jeu (copies partout, contenus, proposition de nettoyage) ----------------
// Proposition : garder une copie par contenu différent (la plus récente) et la plus
// récente de chaque disque ; le reste des copies au contenu identique est proposé à la corbeille. Jamais
// proposées : les copies protégées (inconnues ici, refusées au moment de supprimer), incomplètes, d'un autre
// compte PSN, ou pas encore comparées (on ne devine pas).
Json Engine::backups_summary(const std::string& account, bool with_peers) {
    const Json ov = saves_overview();
    auto norm = [](std::string id) {  // « 0x14B0846B… » / « 14b0846b… » → « 14b0846b… »
        if (id.rfind("0x", 0) == 0 || id.rfind("0X", 0) == 0) id = id.substr(2);
        for (auto& c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto nz = id.find_first_not_of('0');
        return nz == std::string::npos ? std::string() : id.substr(nz);
    };
    const std::vector<std::string> cfg_accounts = settings().my_accounts;
    const std::map<std::string, std::string> cfg_names = settings().account_names;  // copie : les réglages lus ne vivent pas au-delà de l'expression
    std::map<std::string, std::string> acc_ids;
    std::set<std::string> local_keys;  // comptes présents sur CETTE console (détectés, jamais écrits en dur)
    for (const auto& a : ov["accounts"].items()) {
        acc_ids[a["key"].as_string()] = norm(a["account_id"].as_string());
        if (a["on_console"].as_bool()) local_keys.insert(a["key"].as_string());
    }
    Json games = Json::array();
    std::int64_t total_copies = 0, total_remove = 0, total_freed = 0, to_compare = 0;
    for (const auto& e : ov["entries"].items()) {
        // Filtre par compte : on cache seulement les copies d'un AUTRE utilisateur de cette console (il a sa propre vue).
        // Une copie venue d'ailleurs (autre console, numéro d'utilisateur inconnu ici) reste toujours visible : même
        // personne PSN → elle est à moi ; sinon elle est montrée comme « autre compte ». Rien ne disparaît jamais.
        bool foreign = false;
        if (!account.empty() && e["account"].as_string() != account) {
            const auto ra = acc_ids.find(account);
            const auto ea = acc_ids.find(e["account"].as_string());
            const bool same_person = ra != acc_ids.end() && ea != acc_ids.end() && !ra->second.empty() && ra->second == ea->second;
            if (!same_person) {
                if (local_keys.count(e["account"].as_string())) continue;
                foreign = true;
            }
        }
        if (e["backups"].size() == 0) continue;
        const std::string uid = e["account"].as_string().substr(e["account"].as_string().find(':') + 1);
        const Json ins = saves::cached_inspections(dir_, e["title_id"].as_string(), uid);
        std::map<std::string, const Json*> by_src;
        const Json* console_v = nullptr;
        for (const auto& v : ins["versions"].items()) {
            if (v["source"]["kind"].as_string() == "console") { if (!v["stale"].as_bool()) console_v = &v; }
            else by_src[v["source"]["place"].as_string() + "|" + v["source"]["bundle"].as_string()] = &v;
        }
        // « À moi » = le compte demandé (celui de la console), sinon celui de l'entrée.
        const std::string mine = !account.empty() && acc_ids.count(account) ? acc_ids[account] : acc_ids[e["account"].as_string()];
        std::map<std::string, char> letters;
        auto letter = [&](const std::string& fp) -> std::string {
            if (fp.empty()) return "";
            auto it = letters.find(fp);
            if (it == letters.end()) it = letters.emplace(fp, static_cast<char>('A' + letters.size() % 26)).first;
            return std::string(1, it->second);
        };
        if (console_v) letter((*console_v)["fingerprint"].as_string());  // la console a toujours la lettre A
        Json copies = Json::array();
        struct C { std::size_t i; std::string place, fp; std::int64_t at, bytes; bool usable; };
        std::vector<C> cs;
        bool all_compared = true, any_outdated = false;
        for (const auto& b : e["backups"].items()) {
            Json c = b;
            const auto it = by_src.find(b["place"].as_string() + "|" + b["bundle"].as_string());
            const Json* v = it == by_src.end() ? nullptr : it->second;
            std::string fp, owner;
            if (v) {
                fp = (*v)["fingerprint"].as_string();
                for (const auto& im : (*v)["images"].items()) if (owner.empty()) owner = norm(im["account_id"].as_string());
                c.set("group", letter(fp));
                c.set("progress_text", (*v)["progress_text"]);
                c.set("play_time_seconds", (*v)["play_time_seconds"]);
                c.set("percent", (*v)["percent"]);
                c.set("level", (*v)["level"]);
                c.set("content_bytes", (*v)["bytes"]);
                c.set("game_saved_at", (*v)["newest_inner_mtime"]);
                if ((*v)["settings_only"].as_bool()) c.set("settings_only", true);
                for (const auto& im : (*v)["images"].items()) if (im["catalog"]["source"].as_string() == "generic") c.set("progress_auto", true);
            } else if (b["progress"].is_object()) {  // pas encore comparée : avancée notée au moment de la copie
                c.set("progress_text", b["progress"]["text"]);
                c.set("play_time_seconds", b["progress"]["play_time_seconds"]);
                c.set("percent", b["progress"]["percent"]);
                c.set("level", b["progress"]["level"]);
            }
            // Protégée ? (témoin « protected » ; vérifié sur les disques de la console, pas sur le NAS : trop lent)
            bool prot = false;
            if (b["place"].as_string().rfind("remote:", 0) != 0) {
                Place place;
                std::string err, folder;
                if (resolve_place(b["place"].as_string(), place, err) && places::backup_folder(place, settings().custom_folder, folder))
                    prot = fs::exists(fs::host(fs::join(fs::join(folder, b["bundle"].as_string()), "protected")));
            }
            if (prot) c.set("protected", true);
            bool other = !owner.empty() && !mine.empty() && owner != mine;
            if (other) for (const auto& alias : cfg_accounts) if (norm(alias) == owner) other = false;  // ancien identifiant à moi
            if (foreign) other = true;  // copie d'un autre compte : montrée, jamais proposée au nettoyage
            if (other) c.set("other_account", owner);
            // À qui est cette copie : l'identifiant inscrit dans la sauvegarde, son nom s'il est connu (registre), et s'il
            // faut la re-signer pour la reprendre sur le compte de la console (ancien identifiant, autre console…).
            if (!owner.empty()) {
                c.set("owner", owner);
                std::string up = owner;
                for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                while (up.size() < 16) up = "0" + up;
                const auto given = cfg_names.find(up);
                const std::string nm = given != cfg_names.end() ? given->second : identities_ ? identities_->name(owner) : std::string();
                if (!nm.empty()) c.set("owner_name", nm);
                if (!mine.empty() && owner != mine) c.set("needs_resign", true);
            }
            const bool complete = b["complete"].as_bool();
            if (complete && !other && fp.empty()) all_compared = false;
            if (complete && !other && v && saves::reader_outdated(*v)) any_outdated = true;  // avancée à relire (les doublons restent sûrs)
            cs.push_back({copies.size(), b["place"].as_string(), fp, b["created_unix"].as_int(), v ? (*v)["bytes"].as_int() : 0, complete && !other && !fp.empty() && !prot});
            copies.push(c);
        }
        // Proposition : la plus récente de chaque contenu et de chaque disque est gardée.
        std::set<std::size_t> keep;
        std::map<std::string, std::size_t> newest_fp, newest_place;
        for (const auto& c : cs) {
            if (!c.usable) continue;
            auto upd = [&](std::map<std::string, std::size_t>& m, const std::string& k) { auto it = m.find(k); if (it == m.end() || cs[it->second].at < c.at) m[k] = c.i; };
            upd(newest_fp, c.fp);
            upd(newest_place, c.place);
        }
        for (const auto& [k, i] : newest_fp) keep.insert(i);
        for (const auto& [k, i] : newest_place) keep.insert(i);
        Json remove = Json::array(), kept = Json::array();
        std::int64_t freed = 0;
        for (const auto& c : cs) {
            if (!c.usable) continue;
            if (keep.count(c.i)) kept.push(static_cast<std::int64_t>(c.i));
            else if (all_compared) { remove.push(static_cast<std::int64_t>(c.i)); freed += c.bytes; }
        }
        Json g = Json::object();
        for (const char* k : {"account", "title_id", "title", "item", "status", "play_time_tracked"}) g.set(k, e[k]);
        Json con = e["console"];
        if (con.is_object() && console_v) con.set("group", letter((*console_v)["fingerprint"].as_string()));
        g.set("console", con);
        if (foreign) g.set("other_account", true);
        g.set("copies", copies);
        g.set("contents", static_cast<std::int64_t>(letters.size()));
        g.set("compared", all_compared && !any_outdated);
        if (any_outdated) g.set("reread", true);
        g.set("keep", kept);
        g.set("remove", remove);
        g.set("freed_bytes", freed);
        g.set("most_advanced", ins["most_advanced"]);
        games.push(g);
        total_copies += static_cast<std::int64_t>(copies.size());
        total_remove += static_cast<std::int64_t>(remove.size());
        total_freed += freed;
        if (!all_compared || any_outdated) ++to_compare;
    }
    // Backups complets (jeu, appli, homebrew, ROM — pas seulement la partie) de chaque disque et NAS : listés eux aussi,
    // pour pouvoir les supprimer (corbeille) même sans comparaison. Regroupés par élément.
    {
        std::vector<std::pair<std::string, std::string>> plist;  // id, nom affiché
        { MuGuard l(mu_, __FILE__, __LINE__); for (const auto& pl : places_) plist.push_back({pl.id, pl.label}); }
        for (const auto& r : remotes_->list()) if (r.type != "switch" && r.for_backups() && remotes_->same_as(r.id, "backups").empty()) plist.push_back({r.id, r.tagged()});  // un même NAS compté une fois
        std::map<std::string, Json> full;  // clé de l'élément → entrée
        std::map<std::string, std::string> labels;
        for (const auto& [pid, plabel] : plist) {
            // NAS : liste gardée en cache (relue au plus toutes les 2 min), pour que le résumé reste rapide.
            const Json idx = pid.rfind("remote:", 0) == 0 ? remote_index(pid) : backups_json(pid);
            for (const auto& b : idx["backups"].items()) {
                bool only_save = b["parts"].size() > 0;
                for (const auto& part : b["parts"].items()) if (part.as_string() != "save") only_save = false;
                if (only_save || b["legacy"].as_bool()) continue;  // les parties sont déjà dans la liste ci-dessus
                const std::string key = !b["key"].as_string().empty() ? b["key"].as_string() : b["title"].as_string();
                Json& g = full[key];
                if (!g.is_object()) {
                    g = Json::object();
                    g.set("account", "");
                    g.set("title_id", b["title_id"]);
                    g.set("title", b["title"]);
                    g.set("item", key);
                    g.set("kind", b["kind"]);
                    g.set("full", true);
                    g.set("status", "full");
                    g.set("console", Json());
                    g.set("copies", Json::array());
                    g.set("contents", static_cast<std::int64_t>(0));
                    g.set("compared", false);
                    g.set("keep", Json::array());
                    g.set("remove", Json::array());
                    g.set("freed_bytes", static_cast<std::int64_t>(0));
                }
                Json c = Json::object();
                c.set("place", pid);
                c.set("place_label", plabel);
                c.set("bundle", b["bundle"]);
                c.set("created", b["created"]);
                c.set("created_unix", b["created_unix"]);
                c.set("complete", b["complete"]);
                c.set("bytes", b["bytes"]);
                c.set("parts", b["parts"]);
                Json cs = g["copies"];
                cs.push(c);
                g.set("copies", cs);
            }
        }
        for (auto& [k, g] : full) {
            if (settings().is_ignored(g["title_id"].as_string()) || settings().is_ignored(k)) continue;
            total_copies += static_cast<std::int64_t>(g["copies"].size());
            games.push(g);
        }
    }
    // Copies rangées sur les PS5 jumelées (leurs disques, leur NAS) : listées ici aussi, « peu importe où elles sont
    // rangées ». Lues en arrière-plan (refresh_peer_backups) ; marquées « on_peer » : jamais nettoyées d'ici.
    if (with_peers) {
        Json pb;
        { MuGuard l(mu_, __FILE__, __LINE__); pb = peer_backups_; }
        std::map<std::string, std::string> aid_local;  // « ps5|<id PSN> » → compte de cette console
        for (const auto& la : accounts::list()) {
            const std::string aid = norm(la.account_id);
            if (!aid.empty()) for (const char* pf : {"ps5", "ps4"}) aid_local.emplace(std::string(pf) + "|" + aid, std::string(pf) + ":" + la.user_id);
        }
        std::set<std::string> my_nas;
        for (const auto& r : remotes_->list()) { my_nas.insert(r.name); my_nas.insert(r.tagged()); }  // elle les nomme « NAS (FTP) »
        std::vector<Json> gv(games.items().begin(), games.items().end());
        for (const auto& [inst, x] : pb.members()) {
            const std::string pname = x["name"].as_string();
            std::map<std::string, std::string> peer_aid;  // utilisateur de l'autre PS5 → id PSN
            for (const auto& a : x["accounts"].items()) peer_aid[a["user_id"].as_string()] = norm(a["account_id"].as_string());
            for (const auto& pg : x["summary"]["games"].items()) {
                const std::string acc = pg["account"].as_string();
                std::string mapped = acc;
                bool foreign = false;
                if (!acc.empty() && acc.size() > 4) {
                    const auto pa = peer_aid.find(acc.substr(4));
                    const auto m = aid_local.find(acc.substr(0, 3) + "|" + (pa == peer_aid.end() ? std::string() : pa->second));
                    if (m != aid_local.end()) mapped = m->second; else foreign = true;
                    if (!account.empty() && mapped != account && local_keys.count(mapped)) continue;  // un autre utilisateur d'ici
                }
                if (settings().is_ignored(pg["title_id"].as_string()) || settings().is_ignored(pg["item"].as_string())) continue;
                Json ncs = Json::array();
                for (const auto& c : pg["copies"].items()) {
                    const std::string pl = c["place"].as_string();
                    if (pl.rfind("peer:", 0) == 0) continue;                                            // vu par elle chez une autre
                    if (pl.rfind("remote:", 0) == 0 && my_nas.count(c["place_label"].as_string())) continue;  // même NAS, déjà listé ici
                    Json nc = c;
                    nc.set("place", "peer:" + inst + ":" + pl);
                    nc.set("place_label", pname + " · " + c["place_label"].as_string());
                    nc.set("peer", inst);
                    nc.set("peer_name", pname);
                    nc.set("on_peer", true);
                    if (foreign && !nc["other_account"].is_string()) nc.set("other_account", nc["owner"].as_string());
                    ncs.push(nc);
                }
                if (ncs.size() == 0) continue;
                total_copies += static_cast<std::int64_t>(ncs.size());
                const bool full = pg["full"].as_bool();
                Json* into = nullptr;
                for (auto& g : gv)
                    if (g["full"].as_bool() == full && (full ? g["item"].as_string() == pg["item"].as_string()
                                                            : g["title_id"].as_string() == pg["title_id"].as_string() && g["account"].as_string() == mapped)) { into = &g; break; }
                if (into) {
                    Json cs = (*into)["copies"];
                    for (const auto& c : ncs.items()) cs.push(c);
                    into->set("copies", cs);
                } else {
                    Json g = pg;
                    g.set("account", mapped);
                    g.set("copies", ncs);
                    g.set("keep", Json::array());
                    g.set("remove", Json::array());
                    g.set("freed_bytes", static_cast<std::int64_t>(0));
                    g.set("console", Json());
                    g.set("on_peer_only", true);
                    if (foreign) g.set("other_account", true);
                    gv.push_back(g);
                }
            }
        }
        games = Json::array();
        for (auto& g : gv) games.push(g);
    }
    Json out = Json::object();
    out.set("games", games);
    out.set("copies", total_copies);
    out.set("to_remove", total_remove);
    out.set("freed_bytes", total_freed);
    out.set("to_compare", to_compare);
    return out;
}

// Ignorer un jeu : il n'est plus listé ni sauvegardé par une règle ; « trash » met toutes ses copies (tous comptes,
// tous disques et NAS) à la corbeille, d'où elles peuvent être remises pendant 30 jours. on=false le réintègre.
// Supprimer la sauvegarde d'un jeu sur la console (comme Réglages › Données sauvegardées), avec deux garde-fous :
// une copie à jour (ou plus récente) doit exister dans Backups — on peut toujours la restaurer —, et une copie de
// sécurité brute (images + lignes de la base) est faite juste avant. Jamais pendant que le jeu tourne.
Json Engine::delete_console_save(const Json& body, std::string& error) {
    const std::string tid = body["title_id"].as_string(), uid = body["user_id"].as_string();
    const Json ov = saves_overview(true);
    const Json* entry = nullptr;
    for (const auto& e : ov["entries"].items())
        if (e["title_id"].as_string() == tid && (e["account"].as_string() == "ps5:" + uid || e["account"].as_string() == "ps4:" + uid) && e["console"].is_object()) { entry = &e; break; }
    if (!entry) { error = "aucune sauvegarde de ce jeu sur ce compte"; return Json(); }
    const std::string st = (*entry)["status"].as_string();
    // Sans copie à jour : refusé, sauf « force » (l'utilisateur a confirmé plusieurs fois, ex. jeu incompatible
    // dont il ne veut plus rien). La copie de sécurité brute est faite dans tous les cas.
    if (st != "up_to_date" && st != "backup_newer" && !body["force"].as_bool()) {
        error = st == "console_newer" ? "la sauvegarde de la console est plus récente que vos copies : sauvegardez-la d'abord"
                                       : "aucune copie dans Backups : sauvegardez-la d'abord";
        return Json();
    }
    {   // copies de sécurité des suppressions : gardées 30 jours, comme la corbeille (nom : <date ISO>-<jeu>-<compte>)
        const std::string safety = fs::host(fs::join(dir_, "delete-safety"));
        const std::string limit = iso_time(now_unix() - 30 * 86400).substr(0, 10);
        std::vector<fs::Entry> es;
        if (fs::list(safety, es))
            for (const auto& e : es)
                if (e.dir && e.name.size() > 10 && e.name.substr(0, 10) < limit) { log("delete-safety purge %s", e.name.c_str()); fs::remove_tree(fs::join(safety, e.name)); }
    }
    const auto r = saves::delete_console_save(dir_, uid, tid);
    if (!r.ok) { error = r.error; return Json(); }
    request_scan();
    Json out = r.report;
    out.set("ok", true);
    return out;
}

Json Engine::ignore_title(const Json& body, std::string& error) {
    // Un jeu par son numéro (title_id), ou n'importe quel élément par sa clé (« key » : appli, homebrew, ROM).
    const std::string tid = body["title_id"].as_string().empty() ? body["key"].as_string() : body["title_id"].as_string();
    if (tid.empty() || tid.size() > 512 || (body["key"].as_string().empty() && (tid.size() > 16 || tid.find_first_of("/.") != std::string::npos))) { error = "élément inconnu"; return Json(); }
    const bool on = body["on"].as_bool(true);
    Settings s = settings();
    s.ignored.erase(std::remove(s.ignored.begin(), s.ignored.end(), tid), s.ignored.end());
    if (on) s.ignored.push_back(tid);
    set_settings(s);
    Json out = Json::object();
    out.set("ignored", on);
    std::int64_t trashed = 0, refused = 0;
    if (on && body["trash"].as_bool()) {
        std::set<std::string> done;  // « place|bundle » déjà traités (une sauvegarde peut apparaître deux fois)
        auto trash = [&](const std::string& place, const std::string& bundle) {
            if (!done.insert(place + "|" + bundle).second) return;
            Json req = Json::object();
            req.set("place", place);
            req.set("bundle", bundle);
            req.set("confirm", true);
            std::string err;
            version_action("delete", req, err);
            if (err.empty()) ++trashed; else { ++refused; log("ignore %s: %s %s", tid.c_str(), bundle.c_str(), err.c_str()); }
        };
        // Sauvegardes de parties (tous comptes) …
        const Json ov = saves_overview(true);
        for (const auto& e : ov["entries"].items())
            if (e["title_id"].as_string() == tid || e["item"].as_string() == tid)
                for (const auto& b : e["backups"].items()) trash(b["place"].as_string(), b["bundle"].as_string());
        // … et backups complets (jeu, appli, homebrew, ROM) de chaque disque et NAS.
        std::vector<std::string> place_ids;
        { MuGuard l(mu_, __FILE__, __LINE__); for (const auto& pl : places_) if (pl.type == Place::Type::usb) place_ids.push_back(pl.id); }
        for (const auto& r : remotes_->list()) if (r.type != "switch" && r.for_backups() && remotes_->same_as(r.id, "backups").empty()) place_ids.push_back(r.id);  // NAS / FTP de backups (un même NAS une fois)
        for (const auto& pid : place_ids) {
            const Json idx = backups_json(pid);
            for (const auto& b : idx["backups"].items())
                if ((!b["title_id"].as_string().empty() && b["title_id"].as_string() == tid) || b["key"].as_string() == tid) trash(pid, b["bundle"].as_string());
        }
    }
    out.set("trashed", trashed);
    out.set("refused", refused);
    log("ignore title=%s on=%d trashed=%lld refused=%lld", tid.c_str(), on ? 1 : 0, static_cast<long long>(trashed), static_cast<long long>(refused));
    return out;
}

// ---- Joueurs : un joueur = un ou plusieurs identifiants de compte (formatage, autre console) --------------------
// Le joueur de la console = ses comptes + les identifiants déclarés « à moi » (my_accounts). Les autres identifiants
// vus dans les sauvegardes lues sont listés avec ce qu'on sait d'eux et une suggestion quand des indices montrent que
// c'est la même personne (même contenu de partie sous les deux identifiants = sauvegarde restaurée).
static std::string up_id(std::string id) {
    if (id.rfind("0x", 0) == 0 || id.rfind("0X", 0) == 0) id = id.substr(2);
    for (auto& c : id) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    while (id.size() < 16 && !id.empty()) id = "0" + id;
    return id;
}

Json Engine::players() {
    const Settings cfg = settings();
    const Json stats = saves::account_stats(dir_);
    std::map<std::string, std::string> titles_name;
    { MuGuard l(mu_, __FILE__, __LINE__); for (const auto& it : items_) if (!it.title_id.empty()) titles_name.emplace(it.title_id, it.title); }
    auto describe = [&](const std::string& id, const std::string& name, bool current) {
        Json a = Json::object();
        const Json& st = stats[id];
        a.set("account_id", id);
        const auto n = cfg.account_names.find(id);
        const std::string known = identities_->name(id);  // nom noté par l'app (console, backups, registres des disques)
        a.set("name", n != cfg.account_names.end() ? n->second : !name.empty() ? name : known);
        a.set("known_name", known);
        a.set("current", current);
        a.set("images", st["images"].as_int());
        a.set("first", st["first"].as_int());
        a.set("last", st["last"].as_int());
        a.set("games", static_cast<std::int64_t>(st["titles"].size()));
        return a;
    };
    std::set<std::string> mine;
    Json players = Json::array();
    for (const auto& acc : accounts::list()) {
        const std::string main = up_id(acc.account_id);
        if (main.empty()) continue;
        Json p = Json::object();
        p.set("name", acc.name.empty() ? acc.user_id : acc.name);
        p.set("user_id", acc.user_id);
        p.set("console", true);
        Json accs = Json::array();
        accs.push(describe(main, acc.name, true));
        mine.insert(main);
        for (const auto& alias : cfg.my_accounts) {
            const std::string id = up_id(alias);
            if (mine.insert(id).second) accs.push(describe(id, "", false));
        }
        p.set("accounts", accs);
        players.push(p);
    }
    // Autres identifiants : ce qu'on en sait, et les jeux en commun avec le joueur de la console.
    Json others = Json::array();
    for (const auto& [id, st] : stats.members()) {
        if (mine.count(id)) continue;
        Json o = describe(id, "", false);
        std::int64_t shared = 0, identical = 0;
        Json same = Json::array();
        for (const auto& [tid, fps] : st["titles"].members()) {
            bool common = false, equal = false;
            for (const auto& m : mine) {
                const Json& theirs = stats[m]["titles"][tid];
                if (!theirs.is_array()) continue;
                common = true;
                for (const auto& f : fps.items()) for (const auto& g : theirs.items()) if (f.as_string() == g.as_string()) equal = true;
            }
            if (common) ++shared;
            if (equal) { ++identical; if (same.size() < 8) { const auto tn = titles_name.find(tid); same.push(tn != titles_name.end() ? tn->second : tid); } }
        }
        o.set("shared_games", shared);
        o.set("identical_games", identical);
        o.set("identical_titles", same);
        // Suggestion : au moins un jeu au contenu identique sous les deux identifiants (restauration d'une sauvegarde).
        if (identical > 0 && players.size() > 0) {
            o.set("suggest_player", players[0]["name"]);
            o.set("reason", "identical_content");
        }
        others.push(o);
    }
    Json out = Json::object();
    out.set("players", players);
    out.set("others", others);
    return out;
}

// link {account_id, on} : déclarer (ou retirer) un identifiant comme étant le joueur de la console ;
// name {account_id, name} : donner un nom lisible à un identifiant.
Json Engine::player_action(const std::string& action, const Json& body, std::string& error) {
    const std::string id = up_id(body["account_id"].as_string());
    if (id.size() != 16 || id.find_first_not_of("0123456789ABCDEF") != std::string::npos) { error = "identifiant invalide"; return Json(); }
    Settings s = settings();
    if (action == "link") {
        auto same = [&](const std::string& x) { return up_id(x) == id; };
        s.my_accounts.erase(std::remove_if(s.my_accounts.begin(), s.my_accounts.end(), same), s.my_accounts.end());
        if (body["on"].as_bool(true)) s.my_accounts.push_back(id);
    } else if (action == "name") {
        const std::string n = body["name"].as_string();
        if (n.empty()) s.account_names.erase(id);
        else if (n.size() <= 64) s.account_names[id] = n;
        else { error = "nom trop long"; return Json(); }
    } else { error = "action inconnue"; return Json(); }
    set_settings(s);
    return players();
}

bool Engine::send_save(const std::string& instance, const std::string& place_id, const std::string& bundle_name, const std::string& title_id,
                       const std::string& target_user, std::string& send_id, std::string& error) {
    if (!link_) { error = "liaison indisponible"; return false; }
    if (bundle_name.empty() || bundle_name.find("..") != std::string::npos || title_id.empty()) { error = "sauvegarde invalide"; return false; }
    // Où sont les images : un disque de la console, ou le NAS.
    std::string remote_id, bundle_path;
    if (place_id.rfind("remote:", 0) == 0) {
        store::Remote rm;
        if (!remotes_->find(place_id, rm)) { error = "appareil inconnu"; return false; }
        std::string root = rm.ftp.root.empty() ? "/" : rm.ftp.root;
        if (root.size() > 1 && root.back() == '/') root.pop_back();
        remote_id = place_id;
        bundle_path = bundle_name.find('/') == std::string::npos ? fs::join(fs::join(root, "PBC Backup"), bundle_name) : fs::join(root, bundle_name);
    } else {
        Place place;
        if (!resolve_place(place_id, place, error)) return false;
        std::string folder;
        if (!places::backup_folder(place, settings().custom_folder, folder)) { error = "dossier introuvable"; return false; }
        bundle_path = fs::join(folder, bundle_name);
    }
    send_id = random_id(6);
    {
        Json s = Json::object();
        std::string game = title_id, item_key;
        {
            MuGuard l(mu_, __FILE__, __LINE__);
            for (const auto& it : items_) if (it.title_id == title_id) { game = it.title; item_key = it.key; break; }
        }
        std::string peer = instance;
        const Json peers = link_->peers();  // copie gardée en vie pendant la boucle
        for (const auto& p : peers.items()) if (p["instance"].as_string() == instance && !p["name"].as_string().empty()) peer = p["name"].as_string();
        s.set("game", game); s.set("item", item_key); s.set("peer", peer);
        s.set("bytes_done", static_cast<std::int64_t>(0)); s.set("bytes_total", static_cast<std::int64_t>(0));
        s.set("instance", instance); s.set("title_id", title_id); s.set("state", "running"); s.set("step", "preparing");
        s.set("files_done", static_cast<std::int64_t>(0)); s.set("files_total", static_cast<std::int64_t>(0)); s.set("started", now_unix());
        std::lock_guard<std::mutex> l(sends_mu_);
        sends_.set(send_id, s);
    }
    auto set = [this, send_id](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sends_mu_); Json s = sends_[send_id]; s.set(k, v); sends_.set(send_id, s); };
    std::thread([this, instance, remote_id, bundle_path, title_id, target_user, send_id, set] {
        const std::string staging = fs::join(fs::join(dir_, "link-send"), send_id);
        fs::remove_tree(fs::host(staging));
        fs::mkdirs(fs::host(staging));
        auto fail = [&](const std::string& e) { set("state", "failed"); set("error", e); fs::remove_tree(fs::host(staging)); log("link send %s failed: %s", send_id.c_str(), e.c_str()); };
        std::string err;
        std::unique_ptr<store::Store> st;
        if (!remote_id.empty()) st = remotes_->open(remote_id, err);
        else st = store::local();
        if (!st) { fail("appareil injoignable" + (err.empty() ? std::string() : " : " + err)); return; }
        std::atomic<bool> stop{false};
        const auto files = saves::fetch_images(*st, bundle_path, staging, title_id, "", [](const backup::Progress&) {}, stop, err);
        if (files.empty()) { fail(err.empty() ? std::string("aucune image dans cette copie") : err); return; }
        set("files_total", static_cast<std::int64_t>(files.size()));
        std::int64_t total = 0, sent = 0;
        for (const auto& path : files) { struct ::stat sb{}; if (::stat(fs::host(path).c_str(), &sb) == 0) total += sb.st_size; }
        set("bytes_total", total);
        set("step", "sending");
        int done = 0;
        for (const auto& path : files) {
            const std::string name(fs::basename(path));
            if (!link_->upload(instance, send_id, name, fs::host(path), err)) { fail(name + " : " + err); return; }
            struct ::stat sb{};
            if (::stat(fs::host(path).c_str(), &sb) == 0) sent += sb.st_size;
            set("files_done", static_cast<std::int64_t>(++done));
            set("bytes_done", sent);
        }
        set("step", "restoring");
        Json b = Json::object(), out;
        b.set("transfer", send_id); b.set("title_id", title_id); b.set("user_id", target_user);
        if (!link_->call(instance, "POST", "/api/v2/link/inbox/restore", b, out, err)) { fail(err); return; }
        set("remote_task", out["task"]);
        set("state", "done");
        fs::remove_tree(fs::host(staging));
        log("link send %s done files=%d", send_id.c_str(), done);
    }).detach();
    return true;
}

Json Engine::link_sends() const { std::lock_guard<std::mutex> l(sends_mu_); return sends_; }

bool Engine::peer_fs_path(const std::string& place_id, const std::string& rel, std::string& path, std::string& error) {
    if (rel.size() > 1024 || rel.find('\0') != std::string::npos || (!rel.empty() && rel[0] == '/')) { error = "chemin invalide"; return false; }
    for (std::size_t s = 0; s <= rel.size();) {  // aucun segment « .. » ni « . »
        const auto e = rel.find('/', s);
        const std::string seg = rel.substr(s, e == std::string::npos ? std::string::npos : e - s);
        if (seg == ".." || seg == ".") { error = "chemin invalide"; return false; }
        if (e == std::string::npos) break;
        s = e + 1;
    }
    Place place;
    // « files:<disque> » : « Fichiers » depuis l'autre PS5 — tout le disque, sauf le dossier d'état du moteur.
    if (place_id.rfind("files:", 0) == 0) {
        if (!resolve_place(place_id.substr(6), place, error)) return false;
        path = rel.empty() ? place.mount : fs::join(place.mount, rel);
        if (forbidden_local(path)) { error = "dossier réservé au moteur"; return false; }
        return true;
    }
    if (!resolve_place(place_id, place, error)) return false;
    std::string folder;
    if (!places::backup_folder(place, settings().custom_folder, folder)) { error = "dossier PBC Backup impossible à créer"; return false; }
    path = rel.empty() ? folder : fs::join(folder, rel);
    return true;
}

bool Engine::peer_fs_op(const Json& b, Json& out, std::string& error) {
    std::string path;
    if (!peer_fs_path(b["place"].as_string(), b["rel"].as_string(), path, error)) return false;
    const std::string op = b["op"].as_string();
    out = Json::object();
    if (op == "mkdirs") { if (!fs::mkdirs(fs::host(path))) { error = "dossier impossible"; return false; } return true; }
    if (op == "commit") {  // <rel>.part → <rel>
        if (std::rename(fs::host(path + ".part").c_str(), fs::host(path).c_str()) != 0) { error = "validation impossible"; return false; }
        return true;
    }
    if (op == "abort") { fs::remove_tree(fs::host(path + ".part")); return true; }
    if (op == "stat") {
        fs::Entry e;
        out.set("exists", fs::stat(fs::host(path), e));
        out.set("name", e.name); out.set("dir", e.dir); out.set("size", static_cast<std::int64_t>(e.size)); out.set("mtime", e.mtime);
        return true;
    }
    if (op == "list") {
        std::vector<fs::Entry> es;
        if (!fs::list(fs::host(path), es)) { error = "dossier illisible"; return false; }
        Json a = Json::array();
        for (const auto& e : es) { if (forbidden_local(fs::join(path, e.name))) continue; Json x = Json::object(); x.set("name", e.name); x.set("dir", e.dir); x.set("size", static_cast<std::int64_t>(e.size)); x.set("mtime", e.mtime); a.push(x); }
        out.set("entries", a);
        return true;
    }
    if (op == "space") {
        fs::Space sp;
        if (!fs::space(fs::host(path), sp)) { error = "place inconnue"; return false; }
        out.set("free", static_cast<std::int64_t>(sp.free)); out.set("total", static_cast<std::int64_t>(sp.total));
        return true;
    }
    if (op == "remove") { if (!fs::remove_tree(fs::host(path))) { error = "suppression impossible"; return false; } return true; }
    if (op == "rename") {
        std::string to;
        if (!peer_fs_path(b["place"].as_string(), b["to"].as_string(), to, error)) return false;
        if (std::rename(fs::host(path).c_str(), fs::host(to).c_str()) != 0) { error = "renommage impossible"; return false; }
        return true;
    }
    error = "opération inconnue";
    return false;
}

// Format d'installation d'une image. En automatique : selon les montages OBSERVÉS sur cette console
// (journal ShadowMountPlus) — si ce format a échoué ici sans jamais réussir, image non compressée ;
// sinon le jeu reste tel quel. Aucune règle par firmware : un seul constat ne fait pas une règle.
static std::string install_format(const std::string& asked, const std::string& image) {
    if (asked == "ffpfsc" || asked == "exfat" || asked == "keep") return asked;
    const auto dot = image.rfind('.');
    const std::string ext = dot == std::string::npos ? std::string() : image.substr(dot + 1);
    const Json seen = images::observed_mounts();
    if (ext == "ffpfsc" && seen[ext]["failed"].as_int() > 0 && seen[ext]["ok"].as_int() == 0) return "exfat";
    return "keep";
}

bool Engine::install_backup(const std::string& place_id, const std::string& bundle_name, const std::string& target, const std::string& format, const std::string& user_id, Json& tasks, std::string& error, bool drop_copy, bool replace) {
    if (bundle_name.empty() || bundle_name.find("..") != std::string::npos || bundle_name.find('/') != std::string::npos) { error = "copie invalide"; return false; }
    Place place;
    if (!resolve_place(place_id, place, error)) return false;
    std::string folder;
    if (!places::backup_folder(place, settings().custom_folder, folder)) { error = "dossier introuvable"; return false; }
    const std::string bundle = fs::join(folder, bundle_name);
    std::string text;
    Json m;
    if (!fs::read(fs::host(fs::join(bundle, "manifest.json")), text) || !Json::parse(text, m)) { error = "copie illisible (manifeste)"; return false; }
    if (!fs::exists(fs::host(fs::join(bundle, "complete"))) && !fs::exists(fs::host(fs::join(bundle, "_pbc/complete")))) { error = "copie incomplète"; return false; }
    tasks = Json::array();
    std::string tid = m["item"]["title_id"].as_string();
    bool app = false;
    for (const auto& c : m["components"].items()) {
        if (c["part"].as_string() != "app") continue;
        const std::string image = c["meta"]["image"].as_string();
        if (image.empty()) {
            // Jeu en dossier (data/app/<ID>/) : tel quel → dossier recopié dans le dossier de jeux (le plus compatible,
            // ShadowMountPlus lit les dossiers) ; .exfat → image créée depuis le dossier ; .ffpfsc → dossier compressé.
            std::string dir;
            {
                std::vector<fs::Entry> es;
                fs::list(fs::host(fs::join(bundle, "data/app")), es);
                for (const auto& e : es) if (e.dir && e.name != "image" && e.name != "registration") { dir = fs::join(fs::join(bundle, "data/app"), e.name); break; }
            }
            if (dir.empty() || !fs::exists(fs::host(fs::join(dir, "sce_sys/param.json")))) { error = "dossier du jeu absent de la copie"; return false; }
            const std::string id = tid.empty() ? std::string(fs::basename(dir)) : tid;
            const std::string fmt = format == "exfat" || format == "ffpfsc" ? format : "keep";
            const std::string to = target.empty() ? std::string("internal") : target;
            Place tp;
            std::string e2;
            if (!resolve_place(to, tp, e2)) { error = e2; return false; }
            const std::string root = images::games_root(tp.mount);
            const std::string dst = fs::join(root, fmt == "exfat" ? id + ".exfat" : fmt == "ffpfsc" ? id + ".ffpfsc" : id);
            // Droits d'origine notés dans le manifeste (chemin relatif au dossier du jeu) : un transport les perd.
            std::map<std::string, unsigned> modes;
            {
                const std::string pre = "app/" + std::string(fs::basename(dir)) + "/";
                for (const auto& f : m["files"].items())
                    if (f.has("mode") && f["path"].as_string().rfind(pre, 0) == 0) modes[f["path"].as_string().substr(pre.size())] = static_cast<unsigned>(f["mode"].as_int());
            }
            auto fix_perms = [&](const std::string& where) {
                const perms::Report pr = perms::apply_app(fs::host(where), modes);
                log("install_backup: droits %s exécutables=%d corrigés=%d %s", where.c_str(), pr.exec_files, pr.fixed, pr.ok() ? "ok" : pr.summary().c_str());
                if (!pr.ok()) error = "copie vérifiée, mais droits d'exécution impossibles à appliquer : " + pr.summary() + " — l'appli ne pourrait pas démarrer";
                return pr.ok();
            };
            std::string previous;
            if (replace && fmt == "keep" && fs::exists(fs::host(dst))) {
                // Mise à jour d'une appli (synchro des applis) : l'ancienne version est mise de côté, à côté ; elle part
                // une fois la nouvelle installée et vérifiée, ou reprend sa place si quelque chose échoue.
                previous = fs::join(std::string(fs::dirname(dst)), ".pbc-previous-" + id);
                fs::remove_tree(fs::host(previous));
                if (::rename(fs::host(dst).c_str(), fs::host(previous).c_str()) != 0) { error = "ancienne version impossible à mettre de côté"; return false; }
                log("install_backup: ancienne version de %s mise de côté (%s)", id.c_str(), previous.c_str());
            }
            if (fs::exists(fs::host(dst))) {  // déjà là (installation reprise) : droits relus, puis l'inscription
                if (fmt == "keep" && !fix_perms(dst)) return false;
                if (fmt == "keep") { if (!images::register_folder(id, dst, error)) return false; }
                else if (!images::register_image(id, dst, error)) return false;
                app = true;
                continue;
            }
            if (drop_copy && fmt == "keep" && ::rename(fs::host(dir).c_str(), fs::host(dst).c_str()) == 0) {  // même disque : renommage
                log("install_backup: dossier déplacé (renommage) %s -> %s", dir.c_str(), dst.c_str());
                if (!fix_perms(dst)) {  // le renommage garde les droits du transport (souvent 0666)
                    if (!previous.empty()) { ::rename(fs::host(dst).c_str(), fs::host(dir).c_str()); ::rename(fs::host(previous).c_str(), fs::host(dst).c_str()); }
                    return false;
                }
                if (!previous.empty()) fs::remove_tree(fs::host(previous));  // nouvelle version en place : l'ancienne part
                if (!images::register_folder(id, dst, error)) return false;
                app = true;
                continue;
            }
            Json b = Json::object();
            b.set("source", dir);
            b.set("mount", true);
            b.set("target", to);
            if (fmt == "exfat") b.set("type", "make_image");
            else if (fmt == "ffpfsc") b.set("type", "compress");
            else { b.set("type", "transfer"); b.set("to", to); b.set("mode", "copy"); b.set("verify", true); b.set("output", dst); if (!previous.empty()) b.set("previous", previous); }
            std::string tidk;
            if (!image_task(b, tidk, error)) return false;
            tasks.push(tidk);
            app = true;
            continue;
        }
        const std::string src = fs::join(fs::join(bundle, "data/app/image"), std::string(fs::basename(image)));
        if (!fs::exists(fs::host(src))) { error = "image du jeu absente de la copie"; return false; }
        const std::string fmt = install_format(format, image);
        const bool src_ffpfsc = std::string(fs::basename(image)).find(".ffpfsc") != std::string::npos;
        // Nom de l'image installée selon le format choisi.
        std::string out_name(fs::basename(image));
        if (fmt == "exfat" && src_ffpfsc) out_name = (tid.empty() ? out_name.substr(0, out_name.size() - 7) : tid) + ".exfat";
        if (fmt == "ffpfsc" && !src_ffpfsc) { const auto d = out_name.rfind('.'); out_name = (d == std::string::npos ? out_name : out_name.substr(0, d)) + ".ffpfsc"; }
        // Même image déjà sur le disque visé (installation reprise) : pas de nouvelle copie, juste l'inscription.
        {
            Place tp;
            std::string e2;
            if (resolve_place(target.empty() ? std::string("internal") : target, tp, e2)) {
                const std::string dst = fs::join(images::games_root(tp.mount), out_name);
                struct ::stat a {}, b2 {};
                if (::stat(fs::host(dst).c_str(), &a) == 0 && ::stat(fs::host(src).c_str(), &b2) == 0 && (a.st_size == b2.st_size || out_name != std::string(fs::basename(image)))) {
                    if (!images::register_image(tid, dst, error)) return false;
                    app = true;
                    continue;
                }
            }
        }
        const std::string to = target.empty() ? std::string("internal") : target;
        // Tel quel, copie de transit sur le même disque : un simple renommage (instantané, rien n'est recopié) — le jeu
        // a déjà été vérifié à son arrivée. Autre disque (ou copie à garder) : copie vérifiée comme avant.
        if (drop_copy && !(fmt == "exfat" && src_ffpfsc) && !(fmt == "ffpfsc" && !src_ffpfsc)) {
            Place tp;
            std::string e3;
            if (resolve_place(to, tp, e3)) {
                const std::string root = images::games_root(tp.mount), dst = fs::join(root, out_name);
                fs::mkdirs(fs::host(root));
                if (::rename(fs::host(src).c_str(), fs::host(dst).c_str()) == 0) {
                    ::rename((fs::host(src) + ".vhash").c_str(), (fs::host(dst) + ".vhash").c_str());
                    log("install_backup: image déplacée (renommage) %s -> %s", src.c_str(), dst.c_str());
                    if (!images::register_image(tid, dst, error)) return false;
                    app = true;
                    continue;
                }
            }
        }
        Json b = Json::object();
        b.set("source", src);
        b.set("mount", true);  // ShadowMountPlus monte l'image et inscrit le jeu
        if (fmt == "exfat" && src_ffpfsc) {         // décompression vers une image non compressée
            b.set("type", "uncompress");
            b.set("to", "exfat");
            b.set("target", to);
        } else if (fmt == "ffpfsc" && !src_ffpfsc) {  // compression vers .ffpfsc
            b.set("type", "compress");
            b.set("target", to);
        } else {                                     // tel quel : copie vérifiée
            b.set("type", "transfer");
            b.set("to", to);
            b.set("mode", "copy");
            b.set("verify", true);
        }
        std::string id;
        if (!image_task(b, id, error)) return false;
        tasks.push(id);
        app = true;
    }
    // Backport du jeu : remis à sa place d'origine (backports/<ID>) s'il n'y en a pas déjà un — jamais écrasé.
    for (const auto& c : m["components"].items()) {
        if (c["part"].as_string() != "backport") continue;
        const std::string to = c["restore_to"].as_string();
        std::string from;
        {
            std::vector<fs::Entry> es;
            fs::list(fs::host(fs::join(bundle, "data/backport")), es);
            for (const auto& e : es) if (e.dir) { from = fs::join(fs::join(bundle, "data/backport"), e.name); break; }
        }
        if (to.empty() || from.empty() || to.find("/backports/") == std::string::npos) continue;
        if (fs::exists(fs::host(to))) { log("install_backup: backport déjà présent, gardé tel quel : %s", to.c_str()); continue; }
        fs::mkdirs(fs::host(std::string(fs::dirname(to))));
        if (::rename(fs::host(from).c_str(), fs::host(to).c_str()) != 0) {  // autre disque : copie
            std::function<bool(const std::string&, const std::string&)> cp = [&](const std::string& a, const std::string& b) -> bool {
                fs::mkdirs(fs::host(b));
                std::vector<fs::Entry> es;
                if (!fs::list(fs::host(a), es)) return false;
                for (const auto& e : es) {
                    const std::string sa = fs::join(a, e.name), sb = fs::join(b, e.name);
                    if (e.dir) { if (!cp(sa, sb)) return false; continue; }
                    // Par morceaux (avant : le fichier entier en mémoire, jusqu'à 1 Go) ; nom temporaire puis renommage.
                    const std::string tmp = fs::host(sb) + ".pbc-tmp";
                    FILE* in = std::fopen(fs::host(sa).c_str(), "rb");
                    FILE* out = in ? std::fopen(tmp.c_str(), "wb") : nullptr;
                    bool ok = in && out;
                    char buf[65536];
                    for (std::size_t n; ok && (n = std::fread(buf, 1, sizeof buf, in)) > 0;) ok = std::fwrite(buf, 1, n, out) == n;
                    if (in) { ok = ok && !std::ferror(in); std::fclose(in); }
                    if (out) { ok = std::fflush(out) == 0 && ok; std::fclose(out); }
                    if (!ok || ::rename(tmp.c_str(), fs::host(sb).c_str()) != 0) { ::unlink(tmp.c_str()); return false; }
                }
                return true;
            };
            if (!cp(from, to)) { log("install_backup: backport non copié : %s", to.c_str()); continue; }
        }
        log("install_backup: backport installé : %s", to.c_str());
        app = true;
    }
    // Sauvegarde : restaurée re-signée pour le compte de cette console (premier compte si non précisé).
    bool has_save = false;
    for (const auto& c : m["components"].items()) if (c["part"].as_string() == "save") has_save = true;
    if (has_save && !tid.empty()) {
        std::string uid = user_id;
        if (uid.empty()) for (const auto& a : accounts::list()) { uid = a.user_id; break; }
        std::string id, e2;
        if (uid.size() == 8 && restore_save(place_id, bundle_name, uid, tid, true, id, e2, false)) tasks.push(id);
        else log("install_backup: save not restored: %s", e2.c_str());
    }
    if (!app && !has_save) { error = "rien à installer dans cette copie"; return false; }
    // Copie de transit (jeu reçu d'une autre PS5) : une fois le jeu installé et sa sauvegarde restaurée, elle est retirée
    // de « PBC Backup » — le jeu n'existe qu'une fois, dans le dossier de jeux. Gardée si une étape échoue.
    if (drop_copy) {
        std::vector<std::string> ids;
        for (const auto& t : tasks.items()) ids.push_back(t.as_string());
        std::thread([this, ids, bundle] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                bool pending = false, failed = false;
                for (const auto& id : ids) {
                    bool found = false;
                    if (tasks::Task x; queue_->get(id, x)) {
                        found = true;
                        if (x.state == tasks::State::failed || x.state == tasks::State::cancelled) failed = true;
                        else if (x.state != tasks::State::done) pending = true;
                    }
                    if (!found) failed = true;
                }
                if (failed) { log("install_backup: étape en échec, copie de transit gardée : %s", bundle.c_str()); return; }
                if (pending) continue;
                log("install_backup: installé, copie de transit retirée : %s", bundle.c_str());
                fs::remove_tree(fs::host(bundle));
                return;
            }
        }).detach();
    }
    return true;
}

bool Engine::send_game(const std::string& peer_place, const std::string& item_key, const std::string& format, const std::string& remote_user, std::string& send_id, std::string& error, bool keep_copy, bool replace) {
    return send_item(peer_place, item_key, false, format, "", remote_user, send_id, error, keep_copy, replace);
}

// Envoyer à une PS5 jumelée : le jeu (copie puis installation là-bas) ou seulement sa sauvegarde actuelle
// (copie de la sauvegarde puis restauration là-bas sur le compte choisi, re-signée si l'identifiant diffère).
bool Engine::send_item(const std::string& peer_place, const std::string& item_key, bool save_only, const std::string& format,
                       const std::string& account, const std::string& remote_user, std::string& send_id, std::string& error, bool keep_copy, bool replace) {
    std::string inst, remote_place;
    if (!store::parse_peer_id(peer_place, inst, remote_place)) { error = "destination invalide"; return false; }
    if (!link_) { error = "liaison indisponible"; return false; }
    std::string backup_id, title_id;
    { MuGuard l(mu_, __FILE__, __LINE__); for (const auto& it : items_) if (it.key == item_key) { title_id = it.title_id; break; } }
    if (save_only && title_id.empty()) { error = "jeu inconnu"; return false; }
    if (!backup(item_key, save_only ? std::vector<Part>{Part::save} : std::vector<Part>{}, peer_place, backup_id, error, account)) return false;  // copie vérifiée vers le disque de l'autre
    send_id = random_id(6);
    {
        Json s = Json::object();
        std::string game = item_key;
        { MuGuard l(mu_, __FILE__, __LINE__); for (const auto& it : items_) if (it.key == item_key) { game = it.title; break; } }
        std::string peer = inst;
        const Json peers = link_->peers();
        for (const auto& p : peers.items()) if (p["instance"].as_string() == inst && !p["name"].as_string().empty()) peer = p["name"].as_string();
        s.set("game", game); s.set("item", item_key); s.set("peer", peer); s.set("instance", inst);
        s.set("state", "running"); s.set("step", "sending"); s.set("started", now_unix()); s.set("backup_task", backup_id);
        s.set("bytes_done", static_cast<std::int64_t>(0)); s.set("bytes_total", static_cast<std::int64_t>(0));
        s.set("files_done", static_cast<std::int64_t>(0)); s.set("files_total", static_cast<std::int64_t>(0));
        std::lock_guard<std::mutex> l(sends_mu_);
        sends_.set(send_id, s);
    }
    auto set = [this, send_id](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sends_mu_); Json s = sends_[send_id]; s.set(k, v); sends_.set(send_id, s); };
    if (save_only) set("kind", "save");
    std::thread([this, inst, remote_place, backup_id, format, save_only, title_id, remote_user, keep_copy, replace, set] {
        // 1. La copie : on suit la tâche de backup (son avancement devient celui de l'envoi).
        std::string bundle;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            tasks::Task t;
            bool found = false;
            found = queue_->get(backup_id, t);
            if (!found) { set("state", "failed"); set("error", "copie introuvable"); return; }
            const Json tj = t.to_json();
            set("bytes_done", tj["progress"]["bytes_done"]); set("bytes_total", tj["progress"]["bytes_total"]);
            set("files_done", tj["progress"]["files_done"]); set("files_total", tj["progress"]["files_total"]);
            if (t.state == tasks::State::done) { bundle = t.bundle; break; }
            if (t.state == tasks::State::failed || t.state == tasks::State::cancelled) { set("state", "failed"); set("error", t.error.empty() ? std::string("copie interrompue") : t.error); return; }
        }
        // 2a. Sauvegarde seule : restauration demandée à l'autre console, sur le compte choisi (re-signée si besoin).
        if (save_only) {
            set("step", "restoring");
            Json b = Json::object(), out;
            b.set("type", "restore_save"); b.set("place", remote_place); b.set("bundle", std::string(fs::basename(bundle)));
            b.set("title_id", title_id); b.set("user_id", remote_user); b.set("allow_resign", true);
            std::string err;
            if (!link_->call(inst, "POST", "/api/v2/tasks", b, out, err)) { set("state", "failed"); set("error", "restauration : " + err); return; }
            set("remote_task", out["id"]);
            { Json fg; std::string e5; if (link_->call(inst, "GET", "/api/v2/app/foreground", Json::object(), fg, e5) && fg["open"].as_bool()) set("foreground", fg); }
            set("state", "done");
            return;
        }
        // 2. L'installation, demandée à l'autre console (même disque que la copie).
        set("step", "installing");
        Json b = Json::object(), out;
        b.set("place", remote_place); b.set("bundle", std::string(fs::basename(bundle))); b.set("target", remote_place);
        b.set("format", format.empty() ? std::string("auto") : format);  // l'autre PS5 convertit selon son système
        if (!remote_user.empty()) b.set("user_id", remote_user);           // compte choisi pour la sauvegarde du jeu
        b.set("drop_copy", !keep_copy);  // pas de doublon : la copie de transit part une fois le jeu installé
        if (replace) b.set("replace", true);  // mise à jour : remplace la version installée là-bas (jamais en double)
        std::string err;
        if (!link_->call(inst, "POST", "/api/v2/backups/install", b, out, err)) { set("state", "failed"); set("error", "installation : " + err); return; }
        set("remote_tasks", out["tasks"]);
        // Application ouverte là-bas (souvent Backup Center) : l'interface proposera de la fermer pour que le jeu apparaisse.
        { Json fg; std::string e5; if (link_->call(inst, "GET", "/api/v2/app/foreground", Json::object(), fg, e5) && fg["open"].as_bool()) set("foreground", fg); }
        set("state", "done");
    }).detach();
    return true;
}

bool Engine::restore_inbox(const std::string& transfer, const std::string& title_id, const std::string& user_id, std::string& task_id, std::string& error) {
    if (transfer.empty() || transfer.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos || title_id.empty()) { error = "envoi inconnu"; return false; }
    const std::string dir = fs::join(fs::join(dir_, "inbox"), transfer);
    if (!fs::exists(fs::host(dir))) { error = "envoi inconnu"; return false; }
    // Compte qui reçoit : celui demandé, sinon le premier compte PS5 de cette console.
    std::string uid = user_id;
    if (uid.empty()) for (const auto& a : accounts::list()) { uid = a.user_id; break; }
    if (uid.size() != 8) { error = "aucun compte sur cette console"; return false; }
    Json r = Json::object();
    r.set("images_dir", dir);
    r.set("user_id", uid);
    r.set("title_id", title_id);
    r.set("allow_resign", true);  // la copie vient d'une autre console : re-signée pour ce compte (copie de sécurité avant)
    r.set("state_dir", dir_);
    r.set("check_only", false);
    std::string title = title_id, item_key = "title:" + title_id;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) if (it.title_id == title_id) { title = it.title; item_key = it.key; break; }
    }
    r.set("item_key", item_key);
    task_id = queue_->add_restore_save(r, "Reçu d'une autre PS5 · " + title);
    return true;
}

Json Engine::takeover(const std::string& from, const std::string& user_id, const Json& titles, std::string& error) {
    std::string id = from;
    if (id.rfind("0x", 0) == 0 || id.rfind("0X", 0) == 0) id = id.substr(2);
    for (auto& c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (const auto nz = id.find_first_not_of('0'); nz != std::string::npos) id = id.substr(nz);
    if (id.empty() || id.size() > 16 || id.find_first_not_of("0123456789abcdef") != std::string::npos) { error = "identifiant invalide"; return Json(); }
    if (user_id.size() != 8 || user_id.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) { error = "compte invalide"; return Json(); }
    const Json sum = backups_summary("ps5:" + user_id, false);
    const std::string running = images::running_title();
    std::set<std::string> wanted;
    for (const auto& t : titles.items()) wanted.insert(t.as_string());
    Json games = Json::array(), tasks = Json::array();
    for (const auto& g : sum["games"].items()) {
        if (g["full"].as_bool()) continue;
        const Json* best = nullptr;
        for (const auto& c : g["copies"].items()) {
            if (c["owner"].as_string() != id || !c["complete"].as_bool(true) || c["settings_only"].as_bool()) continue;
            const auto play = [](const Json& x) { return x["play_time_seconds"].as_int(-1); };
            if (!best || play(c) > play(*best) || (play(c) == play(*best) && c["created_unix"].as_int() > (*best)["created_unix"].as_int())) best = &c;
        }
        if (!best) continue;
        const Json& con = g["console"];
        const std::int64_t cp = con["progress"]["play_time_seconds"].as_int(-1), bp = (*best)["play_time_seconds"].as_int(-1);
        // Proposée : pas de partie sur la console, ou la copie est plus avancée (temps de jeu), ou plus récente à défaut.
        std::string reason;
        if (!con.is_object()) reason = "no_console_save";
        else if (cp >= 0 && bp > cp) reason = "more_play_time";
        else if (cp < 0 && bp < 0 && (*best)["game_saved_at"].as_int() > con["mtime"].as_int()) reason = "newer";
        Json o = Json::object();
        for (const char* k : {"title_id", "title", "item"}) o.set(k, g[k]);
        Json c2 = Json::object();
        for (const char* k : {"place", "place_label", "bundle", "progress_text", "play_time_seconds", "created_unix", "game_saved_at"}) c2.set(k, (*best)[k]);
        o.set("copy", c2);
        Json cs = Json::object();
        if (con.is_object()) { cs.set("progress_text", con["progress"]["text"]); cs.set("play_time_seconds", con["progress"]["play_time_seconds"]); cs.set("mtime", con["mtime"]); }
        o.set("console", con.is_object() ? cs : Json());
        o.set("suggest", !reason.empty());
        if (!reason.empty()) o.set("reason", reason);
        if (running == g["title_id"].as_string()) o.set("running", true);
        if (wanted.count(g["title_id"].as_string())) {
            if (running == g["title_id"].as_string()) o.set("skipped", "game_running");  // jamais pendant que le jeu tourne
            else {
                std::string tid, err;
                if (restore_save((*best)["place"].as_string(), (*best)["bundle"].as_string(), user_id, g["title_id"].as_string(), true, tid, err, false)) { o.set("task", tid); tasks.push(tid); }
                else o.set("error", err);
            }
        }
        games.push(o);
    }
    Json out = Json::object();
    out.set("from", id);
    out.set("user_id", user_id);
    out.set("games", games);
    out.set("tasks", tasks);
    return out;
}

// « Tout comparer » : une tâche de comparaison par jeu dont des copies n'ont pas encore été lues (le cache évite de
// relire les autres). La sauvegarde de la console du jeu en cours n'est jamais ouverte.
Json Engine::compare_all(const std::string& account, std::string& error) {
    const Json sum = backups_summary(account, false);
    const std::string running = images::running_title();
    int queued = 0, skipped_running = 0;
    for (const auto& g : sum["games"].items()) {
        if (g["full"].as_bool()) continue;  // backup complet (jeu, appli, ROM) : rien à comparer ici
        if (g["compared"].as_bool() && (!g["console"].is_object() || g["console"]["group"].is_string())) continue;  // déjà tout lu
        const std::string acc = g["account"].as_string();
        Json body = Json::object();
        body.set("title_id", g["title_id"]);
        body.set("user_id", acc.substr(acc.find(':') + 1));
        Json sources = Json::array();
        if (g["console"].is_object()) {
            if (running == g["title_id"].as_string()) ++skipped_running;
            else { Json c = Json::object(); c.set("kind", "console"); sources.push(c); }
        }
        for (const auto& c : g["copies"].items()) {
            if (!c["complete"].as_bool()) continue;
            Json s = Json::object();
            s.set("place", c["place"]);
            s.set("bundle", c["bundle"]);
            sources.push(s);
        }
        if (sources.size() == 0) continue;
        body.set("sources", sources);
        std::string id, err;
        if (inspect_save(body, id, err)) ++queued;
        else log("compare-all %s: %s", g["title_id"].as_string().c_str(), err.c_str());
    }
    Json out = Json::object();
    out.set("queued", queued);
    out.set("skipped_running", skipped_running);
    if (!queued && !skipped_running) error.clear();
    return out;
}

// ---- Gestion des backups : protéger, mesurer, supprimer une version (disque de la console ou NAS) ----
// Une version est désignée par (emplacement, nom relatif). Protégée = fichier témoin « protected » dans son
// dossier (respecté par la rotation et par la suppression). Supprimer exige une confirmation explicite.
std::unique_ptr<store::Store> Engine::version_store(const std::string& place_id, const std::string& bundle, std::string& path, std::string& error) {
    if (bundle.empty() || bundle.find("..") != std::string::npos || bundle[0] == '/') { error = "sauvegarde invalide"; return nullptr; }
    if (place_id.rfind("remote:", 0) == 0) {
        store::Remote r;
        if (!remotes_->find(place_id, r)) { error = "appareil inconnu"; return nullptr; }
        std::string root = r.ftp.root.empty() ? "/" : r.ftp.root;
        if (root.size() > 1 && root.back() == '/') root.pop_back();
        path = bundle.find('/') == std::string::npos ? fs::join(r.folder(), bundle) : fs::join(root, bundle);
        return remotes_->open(place_id, error);
    }
    Place place;
    if (!resolve_place(place_id, place, error)) return nullptr;
    std::string folder;
    if (!places::backup_folder(place, settings().custom_folder, folder)) { error = "dossier introuvable"; return nullptr; }
    path = fs::join(folder, bundle);
    return store::local();
}

// Dossier « PBC Backup » d'un appareil (racine de la corbeille) et chemin d'une version relatif à lui.
void Engine::pbc_root(const std::string& place_id, const std::string& path, std::string& root, std::string& rel) {
    if (place_id.rfind("remote:", 0) == 0) {
        store::Remote r;
        if (remotes_->find(place_id, r)) root = r.folder();  // « <dossier>/PBC Backup », ou le dossier lui-même s'il s'appelle ainsi
        else root = "/PBC Backup";
    } else {
        Place place;
        std::string err;
        if (!resolve_place(place_id, place, err) || !places::backup_folder(place, settings().custom_folder, root)) root = path.substr(0, path.rfind('/'));
    }
    rel = path.rfind(root + "/", 0) == 0 ? path.substr(root.size() + 1) : path.substr(path.rfind('/') + 1);
}

// Corbeille : « <date>/… » plus vieux que 30 jours → supprimés pour de bon.
void Engine::purge_trash(store::Store& st, const std::string& trash, std::int64_t now) {
    std::vector<fs::Entry> days;
    if (!st.list(trash, days)) return;
    const std::string limit = iso_time(now - 30 * 86400).substr(0, 10);
    for (const auto& d : days)
        if (d.dir && d.name.size() == 10 && d.name < limit) { log("trash purge %s/%s", trash.c_str(), d.name.c_str()); st.remove(fs::join(trash, d.name)); }
}

// Contenu de la corbeille d'un appareil : chaque version mise de côté (jour, chemin, nom d'origine).
Json Engine::trash_list(const std::string& place_id, std::string& error) {
    std::string path;
    auto st = version_store(place_id, "x", path, error);
    if (!st) return Json();
    std::string root, rel;
    pbc_root(place_id, path, root, rel);
    const std::string trash = fs::join(root, trash_dir);
    purge_trash(*st, trash, now_unix());
    Json out = Json::array();
    std::vector<fs::Entry> days;
    if (!st->list(trash, days)) return out;
    for (const auto& d : days) {
        if (!d.dir) continue;
        // Une version = un .pbck, ou un dossier daté « …/saves/<date> » (3 niveaux au plus sous le jour).
        std::function<void(const std::string&, const std::string&, int)> walk = [&](const std::string& dir, const std::string& relp, int depth) {
            std::vector<fs::Entry> es;
            if (depth > 5 || !st->list(dir, es)) return;
            for (const auto& x : es) {
                const std::string r = relp.empty() ? x.name : relp + "/" + x.name;
                const bool version = (x.name.size() > 5 && x.name.compare(x.name.size() - 5, 5, ".pbck") == 0) || (relp.size() >= 6 && relp.compare(relp.size() - 6, 6, "/saves") == 0);
                if (version) {
                    Json j = Json::object();
                    j.set("day", d.name);
                    j.set("path", fs::join(fs::join(trash, d.name), r));
                    j.set("bundle", r);
                    out.push(j);
                } else if (x.dir) walk(fs::join(dir, x.name), r, depth + 1);
            }
        };
        walk(fs::join(trash, d.name), "", 0);
    }
    return out;
}

// Remettre une version de la corbeille à sa place d'origine.
Json Engine::trash_restore(const Json& body, std::string& error) {
    const std::string place_id = body["place"].as_string(), from = body["path"].as_string();
    std::string path;
    auto st = version_store(place_id, "x", path, error);
    if (!st) return Json();
    std::string root, rel;
    pbc_root(place_id, path, root, rel);
    const std::string trash = fs::join(root, trash_dir) + "/";
    if (from.rfind(trash, 0) != 0 || from.find("..") != std::string::npos || from.size() < trash.size() + 12) { error = "élément de corbeille invalide"; return Json(); }
    const std::string back = fs::join(root, from.substr(trash.size() + 11));  // après « <date>/ »
    fs::Entry x;
    if (st->stat(back, x)) { error = "une version porte déjà ce nom à cet endroit"; return Json(); }
    st->mkdirs(back.substr(0, back.rfind('/')));
    if (!st->rename(from, back)) { error = "remise en place impossible"; return Json(); }
    log("trash restore place=%s to=%s", place_id.c_str(), back.c_str());
    if (place_id.rfind("remote:", 0) == 0) { backup::rebuild_index(*st, root); std::lock_guard<std::mutex> l(remote_index_mu_); remote_index_.erase(place_id); }
    else backup::rebuild_index(root);
    ++revision_;
    Json out = Json::object();
    out.set("restored", back);
    return out;
}

Json Engine::task_backup(const std::string& task_id, std::string& error) {
    tasks::Task t;
    if (!queue_->get(task_id, t) || t.type != tasks::Type::backup) { error = "tâche inconnue"; return Json(); }
    if (t.state != tasks::State::done || t.bundle.empty()) { error = "cette tâche n'a pas fini sa sauvegarde"; return Json(); }
    const std::string remote = t.request["remote"].as_string(), folder = t.request["folder"].as_string();
    std::string rel;
    if (!remote.empty()) {  // relatif à la racine de l'appareil (version_store)
        store::Remote r;
        if (!remotes_->find(remote, r)) { error = "appareil inconnu (oublié ?)"; return Json(); }
        std::string root = r.ftp.root.empty() ? "/" : r.ftp.root;
        if (root.size() > 1 && root.back() == '/') root.pop_back();
        const std::string pre = root == "/" ? "/" : root + "/";
        if (t.bundle.rfind(pre, 0) != 0) { error = "sauvegarde hors du dossier de l'appareil"; return Json(); }
        rel = t.bundle.substr(pre.size());
    } else {  // disque de la console : relatif à son dossier de sauvegardes
        if (folder.empty() || t.bundle.rfind(folder + "/", 0) != 0) { error = "emplacement de la sauvegarde inconnu"; return Json(); }
        rel = t.bundle.substr(folder.size() + 1);
    }
    std::string path;
    auto st = version_store(remote.empty() ? t.request["disk_id"].as_string() : remote, rel, path, error);
    if (!st) return Json();
    fs::Entry e;
    if (!st->stat(path, e) || !e.dir) { error = "cette sauvegarde n'est plus là (supprimée ou déplacée)"; return Json(); }
    Json out = Json::object();
    out.set("place", remote.empty() ? t.request["disk_id"].as_string() : remote);
    out.set("bundle", rel);
    out.set("title_id", t.request["item"]["title_id"]);
    out.set("item", t.request["item"]["key"]);
    out.set("title", t.title);
    return out;
}

Json Engine::version_action(const std::string& action, const Json& body, std::string& error) {
    const std::string place_id = body["place"].as_string(), bundle = body["bundle"].as_string();
    std::string path;
    auto st = version_store(place_id, bundle, path, error);
    if (!st) return Json();
    fs::Entry e;
    if (!st->stat(path, e) || !e.dir) { error = "version introuvable"; return Json(); }
    const std::string marker = fs::join(path, "protected");
    fs::Entry m;
    const bool is_protected = st->stat(marker, m);
    Json out = Json::object();
    if (action == "protect") {
        const bool on = body["on"].as_bool(true);
        if (on && !is_protected && !st->write_small(marker, "protégée par l'utilisateur\n")) { error = "écriture impossible"; return Json(); }
        if (!on && is_protected && !st->remove(marker)) { error = "suppression du témoin impossible"; return Json(); }
        out.set("protected", on);
    } else if (action == "measure") {
        std::uint64_t bytes = 0, files = 0;
        std::function<void(const std::string&, int)> walk = [&](const std::string& d, int depth) {
            std::vector<fs::Entry> es;
            if (depth > 8 || !st->list(d, es)) return;
            for (const auto& x : es) { if (x.dir) walk(fs::join(d, x.name), depth + 1); else { bytes += x.size; ++files; } }
        };
        walk(path, 0);
        out.set("bytes", bytes);
        out.set("files", files);
        out.set("protected", is_protected);
    } else if (action == "delete") {
        if (!body["confirm"].as_bool()) { error = "confirmation requise"; return Json(); }
        if (is_protected) { error = "version protégée : retirez d'abord la protection"; return Json(); }
        // Par défaut, la version part dans la corbeille du même disque (« PBC Backup/Corbeille/<date>/… »,
        // simple renommage) ; elle peut y être remise pendant 30 jours. « permanent » supprime vraiment.
        if (!body["permanent"].as_bool()) {
            std::string root, rel;
            pbc_root(place_id, path, root, rel);
            const std::string day = iso_time(now_unix()).substr(0, 10);
            const std::string dest = fs::join(fs::join(fs::join(root, trash_dir), day), rel);
            st->mkdirs(dest.substr(0, dest.rfind('/')));
            fs::Entry x;
            if (st->stat(dest, x)) { error = "déjà dans la corbeille du jour"; return Json(); }
            log("backup trash place=%s bundle=%s to=%s", place_id.c_str(), bundle.c_str(), dest.c_str());
            if (!st->rename(path, dest)) { error = "mise à la corbeille impossible (" + path + ")"; return Json(); }
            purge_trash(*st, fs::join(root, trash_dir), now_unix());
            out.set("trashed", dest);
        } else {
            log("backup delete place=%s bundle=%s", place_id.c_str(), bundle.c_str());
            if (!st->remove(path)) { error = "suppression impossible (" + path + ")"; return Json(); }
            out.set("deleted", true);
        }
    } else { error = "action inconnue"; return Json(); }
    // Les listes changent : index local refait, cache du NAS oublié.
    if (action != "measure") {
        if (place_id.rfind("remote:", 0) == 0) {
            // Le NAS garde aussi son index.json : refait tout de suite, sinon la version mise à la corbeille réapparaît.
            std::string root, rel;
            pbc_root(place_id, path, root, rel);
            backup::rebuild_index(*st, root);
            std::lock_guard<std::mutex> l(remote_index_mu_);
            remote_index_.erase(place_id);
        }
        else {
            Place place;
            std::string err, folder;
            if (resolve_place(place_id, place, err) && places::backup_folder(place, settings().custom_folder, folder)) backup::rebuild_index(folder);
        }
        ++revision_;
    }
    return out;
}

bool Engine::forget_remote(const std::string& id) {
    if (!remotes_->remove(id)) return false;
    ++revision_;
    return true;
}

// Sauvegardes d'un dossier « PBC Backup » de la console (disque USB, interne) : nos .pbck (index) et les
// dossiers par plateforme (« PS5 backup/[PS5] Nom (ID)/saves/<date> », y compris importés d'un ancien script).
static Json local_index(const std::string& folder) {
    Json idx = backup::read_index(folder);
    auto st = store::local();
    Json all = idx["backups"].is_array() ? idx["backups"] : Json::array();
    std::set<std::string> seen;
    for (const auto& b : all.items()) seen.insert(b["bundle"].as_string());
    const Json plat = backup::scan_platform(*st, folder);  // gardé en vie pendant la boucle
    for (const auto& b : plat["backups"].items())
        if (seen.insert(b["bundle"].as_string()).second) all.push(b);
    if (!idx.is_object()) idx = Json::object();
    idx.set("backups", all);
    return idx;
}

Json Engine::backups_json(const std::string& place_id) {
    if (place_id.rfind("remote:", 0) == 0) return remote_index(place_id, true);
    Place place;
    std::string error, folder;
    if (!resolve_place(place_id, place, error)) { Json j = Json::object(); j.set("error", error); return j; }
    folder = settings().custom_folder.empty() ? fs::join(place.mount, places::default_folder) : settings().custom_folder;
    if (!fs::is_dir(fs::host(folder))) { Json j = Json::object(); j.set("backups", Json::array()); return j; }
    return local_index(folder);
}

Json Engine::playtime() { std::lock_guard<std::mutex> l(playtime_mu_); return playtime_->all(); }

Json Engine::saves_overview(bool include_ignored) {
    struct Acc { std::string key, name, user_id, account_id, platform; bool on_console = false; unsigned titles = 0; };
    std::map<std::string, Acc> accs;          // clé : plateforme:utilisateur
    struct Entry { std::string account, title_id, title, item_key; Json console; Json backups = Json::array(); std::int64_t newest = 0, newest_save = 0; };
    std::map<std::string, Entry> entries;     // clé : compte|jeu
    auto acc_key = [](const std::string& format, const std::string& uid) { return std::string(format == "ps4-savedata" ? "ps4:" : "ps5:") + uid; };
    std::vector<Place> places;
    std::map<std::string, std::string> tid_by_name;  // nom en minuscules → numéro (anciennes sauvegardes sans numéro)
    auto low = [](std::string v) { for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return v; };
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        places = places_;
        for (const auto& it : items_) if (!it.title_id.empty() && !it.title.empty()) tid_by_name.emplace(low(it.title), it.title_id);
        for (const auto& it : items_)
            for (const auto& c : it.components) {
                if (c.part != Part::save || !c.meta.is_object() || c.meta["user_id"].as_string().empty()) continue;
                const std::string k = acc_key(c.meta["format"].as_string(), c.meta["user_id"].as_string());
                Acc& a = accs[k];
                a.key = k; a.name = c.meta["account_name"].as_string(); a.user_id = c.meta["user_id"].as_string();
                a.account_id = c.meta["account_id"].as_string(); a.platform = k.substr(0, 3) == "ps4" ? "PS4" : "PS5"; a.on_console = true;
                Entry& e = entries[k + "|" + it.title_id];
                e.account = k; e.title_id = it.title_id; e.title = it.title; e.item_key = it.key;
                Json con = Json::object();
                con.set("bytes", c.bytes);
                con.set("mtime", c.meta["mtime"].as_int());
                e.console = con;
            }
    }
    const std::string custom = settings().custom_folder;
    const auto local_accounts = accounts::list();
    // Même personne PSN, autre numéro d'utilisateur (copie faite sur une autre console) : rattachée au compte de CETTE
    // console qui a le même identifiant PSN — ses copies sont les siennes (Backups, état des parties, restauration).
    auto norm_aid = [](std::string id) {
        if (id.rfind("0x", 0) == 0 || id.rfind("0X", 0) == 0) id = id.substr(2);
        for (auto& c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto nz = id.find_first_not_of('0');
        return nz == std::string::npos ? std::string() : id.substr(nz);
    };
    std::map<std::string, std::string> same_person;  // « ps5|<id PSN> » → clé locale
    for (const auto& la : local_accounts) {
        const std::string aid = norm_aid(la.account_id);
        if (aid.empty()) continue;
        for (const char* pf : {"ps5", "ps4"}) same_person.emplace(std::string(pf) + "|" + aid, std::string(pf) + ":" + la.user_id);
    }
    for (const auto& r : remotes_->list()) {  // appareils réseau : index gardé en cache
        if (!r.for_backups() || !remotes_->same_as(r.id, "backups").empty()) continue;  // synchro seulement, ou le même NAS par un autre protocole : comptés une fois
        Place p;
        p.id = r.id;
        p.label = r.tagged();  // « NAS (FTP) » : qui est quoi, partout où la copie apparaît
        p.mount = "remote";
        places.push_back(p);
    }
    for (const auto& p : places) {
        Json idx;
        if (p.mount == "remote") idx = remote_index(p.id);
        else {
            const std::string folder = custom.empty() ? fs::join(p.mount, places::default_folder) : custom;
            if (!fs::is_dir(fs::host(folder))) continue;
            idx = local_index(folder);
        }
        for (const auto& b0 : idx["backups"].items()) {
            // Dossier sans numéro (« Split Fiction » au lieu de « [PS5] Split Fiction (PPSA08560) ») : rattaché au
            // jeu du même nom, pour ne pas faire une deuxième entrée sans icône.
            Json b = b0;
            if (b["title_id"].as_string().empty() && !b["title"].as_string().empty()) {
                const auto f = tid_by_name.find(low(b["title"].as_string()));
                if (f != tid_by_name.end()) b.set("title_id", f->second);
                else {  // jeu pas installé ici : la liste des jeux livrée avec l'app (sinon pas d'icône)
                    const std::string t = titles::ps_by_name(b["title"].as_string(), b["kind"].as_string() == "game_ps4" ? "CUSA" : "PPSA");
                    if (!t.empty()) { b.set("title_id", t); tid_by_name.emplace(low(b["title"].as_string()), t); }
                }
            }
            // Backup fait hors de l'app (dossier daté du NAS, sans manifeste) : pas de compte noté. On le
            // rattache au compte de cette console qui a la sauvegarde de ce jeu, sinon au premier compte PS5.
            Json legacy_accounts = Json::array();
            const Json* list = &b["accounts"];
            if (b["accounts"].size() == 0 && b["legacy"].as_bool()) {
                const bool ps4 = b["kind"].as_string() == "game_ps4";
                const std::string fmt = ps4 ? "ps4-savedata" : "ps5-savedata";
                for (const auto& la : local_accounts)
                    if (entries.count(acc_key(fmt, la.user_id) + "|" + b["title_id"].as_string())) {
                        Json x = Json::object(); x.set("format", fmt); x.set("user_id", la.user_id); x.set("account_id", la.account_id); x.set("account_name", la.name);
                        legacy_accounts.push(x);
                    }
                if (legacy_accounts.size() == 0 && !local_accounts.empty()) {
                    const auto& la = local_accounts.front();
                    Json x = Json::object(); x.set("format", fmt); x.set("user_id", la.user_id); x.set("account_id", la.account_id); x.set("account_name", la.name);
                    legacy_accounts.push(x);
                }
                list = &legacy_accounts;
            }
            for (const auto& a : list->items()) {
                identities_->note(a["account_id"].as_string(), a["account_name"].as_string(), a["user_id"].as_string(), b["created_unix"].as_int());
                std::string k = acc_key(a["format"].as_string(), a["user_id"].as_string());
                bool aliased = false;
                if (const auto sp = same_person.find(k.substr(0, 3) + "|" + norm_aid(a["account_id"].as_string())); sp != same_person.end() && sp->second != k) { k = sp->second; aliased = true; }
                Acc& acc = accs[k];
                if (acc.key.empty()) {
                    acc.key = k; acc.platform = k.substr(0, 3) == "ps4" ? "PS4" : "PS5";
                    if (aliased) {  // le compte de la console : son nom et son numéro, pas ceux de l'autre console
                        for (const auto& la : local_accounts) if (k.substr(4) == la.user_id) { acc.name = la.name; acc.user_id = la.user_id; acc.account_id = la.account_id; }
                    } else { acc.name = a["account_name"].as_string(); acc.user_id = a["user_id"].as_string(); acc.account_id = a["account_id"].as_string(); }
                }
                Entry& e = entries[k + "|" + b["title_id"].as_string()];
                if (e.account.empty()) { e.account = k; e.title_id = b["title_id"].as_string(); e.title = b["title"].as_string(); e.item_key = b["key"].as_string(); }
                Json v = Json::object();
                v.set("place", p.id);
                v.set("place_label", p.label);
                v.set("bundle", b["bundle"]);
                v.set("created", b["created"]);
                v.set("created_unix", b["created_unix"]);
                v.set("complete", b["complete"]);
                v.set("bytes", b["bytes"]);  // taille de la copie : une grosse copie illisible n'est pas une copie vide
                if (b["legacy"].as_bool()) v.set("legacy", true);
                if (a["save_mtime"].as_int()) v.set("save_mtime", a["save_mtime"]);
                if (a["progress"].is_object()) v.set("progress", a["progress"]);  // avancée notée à la copie
                e.backups.push(v);
                if (b["complete"].as_bool()) {
                    e.newest = std::max<std::int64_t>(e.newest, b["created_unix"].as_int());
                    // Heure de la sauvegarde du jeu contenue (connue pour nos backups ; pas pour les anciens faits par script).
                    e.newest_save = std::max<std::int64_t>(e.newest_save, a["save_mtime"].as_int());
                }
            }
        }
    }
    // Copies rangées sur les PS5 jumelées (cache de fond) : une partie jouée et copiée là-bas, plus récente que celle d'ici,
    // fait passer le jeu en « backup plus récent » (à restaurer). Elles ne s'ajoutent pas à la liste des versions d'ici.
    {
        Json pb;
        { MuGuard l(mu_, __FILE__, __LINE__); pb = peer_backups_; }
        for (const auto& [inst, x] : pb.members()) {
            std::map<std::string, std::string> peer_aid;
            for (const auto& pa : x["accounts"].items()) peer_aid[pa["user_id"].as_string()] = norm_aid(pa["account_id"].as_string());
            for (const auto& g : x["summary"]["games"].items()) {
                const std::string acc = g["account"].as_string();
                if (acc.size() < 5 || g["full"].as_bool()) continue;
                const auto pa = peer_aid.find(acc.substr(4));
                const auto sp = same_person.find(acc.substr(0, 3) + "|" + (pa == peer_aid.end() ? std::string() : pa->second));
                if (sp == same_person.end()) continue;
                const auto en = entries.find(sp->second + "|" + g["title_id"].as_string());
                if (en == entries.end()) continue;
                for (const auto& c : g["copies"].items()) {
                    if (!c["complete"].as_bool(true) || c["place"].as_string().rfind("peer:", 0) == 0) continue;
                    const std::int64_t saved = std::max(c["save_mtime"].as_int(), c["game_saved_at"].as_int());
                    if (saved > en->second.newest_save) { en->second.newest_save = saved; en->second.newest = std::max(en->second.newest, c["created_unix"].as_int()); }
                }
            }
        }
    }
    // Avancée des sauvegardes de la console (base de la console, sans monter les images), par compte.
    std::map<std::string, std::map<std::string, std::vector<progress::SaveText>>> prog;
    for (const auto& [k, a] : accs) if (a.on_console) prog[k] = progress::console(a.user_id, a.platform == "PS4");
    std::map<std::string, std::int64_t> played;
    {
        Json all;
        { std::lock_guard<std::mutex> l(playtime_mu_); all = playtime_->all(); }  // copie gardée : pas de boucle sur un temporaire
        for (const auto& [k, v] : all.members()) played[k] = v.as_int();
    }
    const Settings cfg = settings();
    Json out = Json::object(), al = Json::array(), el = Json::array();
    for (auto& [key, e] : entries) {
        if (!include_ignored && (cfg.is_ignored(e.title_id) || cfg.is_ignored(e.item_key))) continue;  // ignoré : ni listé ni compté
        if (e.console.is_object()) {
            const auto pa = prog.find(e.account);
            if (pa != prog.end()) {
                const auto pt = pa->second.find(e.title_id);
                if (pt != pa->second.end()) {
                    Json pg = progress::summary(pt->second);
                    // Le nom du jeu devant le texte n'apprend rien : « Grand Theft Auto V — Daddy's… » → « Daddy's… ».
                    std::string txt = pg["text"].as_string();
                    const std::string name = !e.title.empty() ? e.title : titles::name(e.title_id);
                    if (!name.empty() && txt.rfind(name, 0) == 0) { txt = txt.substr(name.size()); while (!txt.empty() && (txt[0] == ' ' || txt.rfind("—", 0) == 0)) txt = txt[0] == ' ' ? txt.substr(1) : txt.substr(std::string("—").size()); }
                    pg.set("text", txt);
                    e.console.set("progress", pg);
                }
            }
        }
        const bool console = e.console.is_object();
        std::string status;
        // Sauvegarde d'une appli du système (LAPY…, NPXS… : mémoire de sauvegarde d'un écran de la console), pas un jeu :
        // listée, jamais comptée « à sauvegarder ».
        const bool system_title = e.title_id.rfind("LAPY", 0) == 0 || e.title_id.rfind("NPXS", 0) == 0;
        if (console && system_title) status = "system";
        else if (!console) status = "backup_only";
        else if (!e.newest) status = "not_backed_up";
        // Plus d'une minute d'écart : l'un des deux côtés est plus récent (sinon : à jour).
        // « Plus récent dans un backup » : seulement si la sauvegarde de jeu qu'il contient est plus récente que
        // celle de la console (heure notée dans nos backups). L'heure d'une copie ne prouve rien sur le contenu.
        else status = e.console["mtime"].as_int() > e.newest + 60 ? "console_newer" : e.newest_save > e.console["mtime"].as_int() + 60 ? "backup_newer" : "up_to_date";
        ++accs[e.account].titles;
        Json j = Json::object();
        j.set("account", e.account);
        j.set("title_id", e.title_id);
        // Jeu absent de la console (seulement dans un backup) : vrai nom depuis la table des titres, et une
        // jaquette officielle par son numéro (clé « tid:ps5:PPSA… », comme pour la Switch).
        std::string title = e.title;
        if ((title.empty() || title == e.title_id) && !e.title_id.empty()) { const std::string n = titles::name(e.title_id); if (!n.empty()) title = n; }
        j.set("title", title);
        const bool ps4 = e.account.rfind("ps4:", 0) == 0;
        j.set("item", !e.item_key.empty() && e.item_key.rfind("save:", 0) != 0 ? e.item_key : !e.title_id.empty() ? std::string(ps4 ? "tid:ps4:" : "tid:ps5:") + e.title_id : e.item_key);
        j.set("console", console ? e.console : Json());
        j.set("backups", e.backups);
        j.set("status", status);
        if (const auto pl = played.find(e.title_id); pl != played.end()) j.set("play_time_tracked", pl->second);
        el.push(j);
    }
    for (const auto& [key, a] : accs) {
        Json j = Json::object();
        j.set("key", a.key);
        j.set("name", a.name.empty() ? a.user_id : a.name);
        j.set("user_id", a.user_id);
        j.set("account_id", a.account_id);
        j.set("platform", a.platform);
        j.set("on_console", a.on_console);
        j.set("titles", static_cast<std::uint64_t>(a.titles));
        al.push(j);
    }
    out.set("accounts", al);
    out.set("entries", el);
    return out;
}

bool Engine::authorized(const std::string& t) const {
    if (t.empty()) return false;
    if (t == token_) return true;
    MuGuard l(mu_, __FILE__, __LINE__);
    for (const auto& d : devices_.items()) if (d["token"].as_string() == t) return true;
    return false;
}

bool Engine::pair(const std::string& code, const std::string& name, std::string& device_token, std::string& error) {
    MuGuard l(mu_, __FILE__, __LINE__);
    const std::int64_t now = now_unix();
    if (now - pair_window_start_ > 60) { pair_window_start_ = now; pair_attempts_ = 0; }
    if (++pair_attempts_ > 5) { error = "trop d'essais, réessayez dans une minute"; return false; }
    if (code != pair_code_) { error = "code incorrect"; return false; }
    Json d = Json::object();
    d.set("id", random_id(4));
    d.set("name", name.empty() ? std::string("Navigateur") : name.substr(0, 48));
    d.set("token", random_id(16));
    d.set("created", iso_time(now));
    device_token = d["token"].as_string();
    devices_.push(d);
    fs::write_atomic(fs::host(fs::join(dir_, "devices.json")), devices_.dump(true) + "\n");
    log("device paired name=%s", d["name"].as_string().c_str());
    return true;
}

std::string Engine::issue_device_token(const std::string& name) {
    MuGuard l(mu_, __FILE__, __LINE__);
    Json d = Json::object();
    d.set("id", random_id(4));
    d.set("name", name.substr(0, 48));
    d.set("token", random_id(16));
    d.set("created", iso_time(now_unix()));
    d.set("console", true);  // une autre PS5 (liaison), pas un navigateur
    const std::string t = d["token"].as_string();
    devices_.push(d);
    fs::write_atomic(fs::host(fs::join(dir_, "devices.json")), devices_.dump(true) + "\n");
    log("device issued for console name=%s", name.c_str());
    return t;
}

Json Engine::devices_json() const {
    MuGuard l(mu_, __FILE__, __LINE__);
    Json a = Json::array();
    for (const auto& d : devices_.items()) {
        Json x = Json::object();
        x.set("id", d["id"]);
        x.set("name", d["name"]);
        x.set("created", d["created"]);
        a.push(x);
    }
    return a;
}

bool Engine::forget_device(const std::string& id) {
    MuGuard l(mu_, __FILE__, __LINE__);
    Json kept = Json::array();
    bool found = false;
    for (const auto& d : devices_.items()) { if (d["id"].as_string() == id) found = true; else kept.push(d); }
    if (!found) return false;
    devices_ = kept;
    fs::write_atomic(fs::host(fs::join(dir_, "devices.json")), devices_.dump(true) + "\n");
    return true;
}

Settings Engine::settings() const { MuGuard l(mu_, __FILE__, __LINE__); return settings_; }

bool Engine::set_settings(const Settings& s) {
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        settings_ = s;
    }
    autostart::write_launchers_file(dir_, s.launchers);  // lu par l'appli télé, même moteur arrêté
    updater_->set_channel(s.update_channel == "beta");
    updater_->set_source(s.update_source);
    fetcher_->set_enabled(s.covers_online);
    if (link_) link_->wan_configure(s.wan_enabled, static_cast<std::uint16_t>(s.wan_port), s.wan_public_host);
    ++revision_;
    return fs::write_atomic(fs::host(fs::join(dir_, "settings.json")), s.to_json().dump(true) + "\n");
}

}  // namespace pbc
