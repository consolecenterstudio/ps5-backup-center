// Onglet Transferts : d'un appareil à l'autre (PS5, disques, Switch, NAS).
// Écran principal : DEPUIS / VERS / QUE FAIRE ? ; puis choix des éléments et options, suivi des
// transferts, et sauvegarde complète de la console (cases à cocher par catégorie).
// Règle : un jeu en cours d'utilisation n'est jamais touché — le moteur refuse et la tâche attend.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "ui.hpp"
#include "ui_util.hpp"

namespace app {

using pbc::Json;
using namespace ui_util;

std::string Task::phase_text() const {
    const double f = phase_fraction();
    if (f < 0) return "";
    const int pct = static_cast<int>(f * 100);
    std::string t = phase == "verify" ? trf("Vérification %d %%", pct) : trf("Comparaison avec la copie déjà là %d %%", pct);
    if (phase_eta) t += " · " + std::string(tr("reste")) + " " + duration_text(static_cast<double>(phase_eta));
    return t;
}

namespace {

struct Action { const char* name; const char* text; };
const Action actions[] = {
    {N_("Copier"), N_("L'original reste. Vérifié fichier par fichier (empreinte).")},
    {N_("Déplacer"), N_("Copie vérifiée, puis l'original est supprimé. Libère de la place.")},
    {N_("Compresser"), N_("Jeu PS5 en image .ffpfsc : souvent 30 à 40 % de place en moins.")},
    {N_("Synchroniser"), N_("Sauvegardes : garder la plus récente des deux côtés.")},
};
constexpr int action_count = 4;

// Catégories du choix des éléments (clé du moteur, libellé).
struct Cat { const char* kind; const char* name; };
const Cat cats[] = {{"game_ps5", N_("Jeux PS5")}, {"game_ps4", N_("Jeux PS4")}, {"app", N_("Applications")}, {"rom", "ROM"}};
constexpr int cat_count = 4;

// Sauvegarde complète : catégories dans l'ordre de restauration.
struct FullCat { const char* id; const char* name; const char* text; bool expert; bool on; };
const FullCat full_cats[] = {
    {"accounts", N_("Comptes"), N_("utilisateurs, identifiant de compte, activation hors ligne"), false, true},
    {"saves", N_("Sauvegardes de jeux"), N_("images chiffrées + versions lisibles"), false, true},
    {"games", N_("Jeux"), N_("leurs inscriptions (les images restent sur vos disques)"), false, true},
    {"images", N_("Images des jeux"), N_(".ffpfsc · seulement celles qui ne sont pas déjà sur un disque"), false, false},
    {"homebrew", N_("Applications homebrew et leurs données"), N_("émulateurs, profils, thèmes, BIOS (/data)"), false, true},
    {"settings", N_("Démarrage et réglages"), N_("payloads au démarrage, shadowmount, kstuff"), false, true},
    {"captures", N_("Captures et vidéos"), N_("photos, clips"), false, true},
    {"system", N_("Système (expert)"), N_("registre, bases des applis · restauration guidée seulement"), true, false},
};
constexpr int full_count = 8;

std::uint64_t item_bytes(const Item& it, bool saves) {
    std::uint64_t b = 0;
    for (const auto& c : it.components) if (saves || c.part != "save") b += c.bytes;
    return b;
}

}  // namespace

const char* task_kind(const std::string& t) {
    if (t == "transfer") return tr("Transfert");
    if (t == "link_send") return tr("Envoi vers une autre PS5");
    if (t == "compress") return tr("Compression");
    if (t == "make_pkg") return tr("Création d'un paquet .pkg");
    if (t == "uncompress") return tr("Décompression");
    if (t == "full_backup") return tr("Sauvegarde complète");
    if (t == "make_image") return tr("Création d'image");
    if (t == "extract_image") return tr("Extraction d'image");
    if (t == "validate_image") return tr("Vérification d'image");
    if (t == "read_speed_test") return tr("Test de vitesse");
    if (t == "build_ampr_index") return tr("Index AMPR");
    if (t == "set_read_only") return tr("Lecture seule");
    if (t == "backup") return tr("Sauvegarde");
    if (t == "restore" || t == "restore_save") return tr("Restauration");
    if (t == "import_switch" || t == "export_switch") return tr("Synchro Switch");
    if (t == "files_copy") return tr("Fichiers · copie");
    if (t == "files_move") return tr("Fichiers · déplacement");
    if (t == "files_delete") return tr("Fichiers · suppression");
    if (t == "game_uninstall") return tr("Désinstallation");
    if (t == "install_job") return tr("Installation");
    return tr("Tâche");
}

namespace {

bool active(const std::string& st) { return st == "queued" || st == "running" || st == "paused"; }

}  // namespace

// ---- Données --------------------------------------------------------------------------------

std::vector<Ui::TrDevice> Ui::tr_devices() const {
    std::vector<TrDevice> v;
    std::string disks;
    std::uint64_t free = 0;
    for (const auto& p : snap_->places) {
        if (p.type != "usb" && p.type != "internal") continue;
        disks += (disks.empty() ? "" : " · ") + (p.type == "internal" ? std::string(tr("Interne")) : p.label);
        free += p.free;
    }
    v.push_back({"ps5", tr("Cette PS5"), disks.empty() ? std::string(tr("tous ses disques")) : disks, "ps5", dev_internal, free, false});
    for (const auto& p : snap_->places) {
        if (p.type != "usb" && p.type != "internal") continue;
        v.push_back({p.id, p.type == "internal" ? std::string(tr("Stockage interne")) : p.label,
                     p.type == "internal" ? std::string(tr("PS5 · interne")) : trf("Disque USB · %s", p.mount.c_str()), p.type,
                     p.type == "usb" ? dev_usb : dev_internal, p.free, true});
    }
    // Rangés par famille : NAS, puis nuage (avec le logo de son service), puis Switch.
    auto cloudy = [](const std::string& t) { return cloud_proto(t); };
    for (int fam = 0; fam < 3; ++fam)
        for (const auto& r : remotes_all_.items()) {
            const std::string t = r["type"].as_string();
            if ((fam == 0 && t != "switch" && !cloudy(t)) || (fam == 1 && cloudy(t)) || (fam == 2 && t == "switch")) {
                const std::string host = r["config"]["host"].as_string(), service = t == "share" ? host : r["config"]["service"].as_string();
                std::string sub;
                if (t == "share") sub = std::string(tr("Partage · lecture seule"));  // lien reçu : une source, jamais une destination
                else if (t == "switch") sub = "Switch · " + host;
                else if (cloudy(t)) {
                    const char* svc = t == "gdrive" ? "Google Drive" : t == "dropbox" ? "Dropbox" : t == "onedrive" ? "OneDrive" : t == "mega" ? "MEGA" : service == "b2" ? "Backblaze B2" : service == "aws" ? "Amazon S3" : "S3";
                    sub = trf("Nuage · %s", svc);
                } else sub = trf("NAS · %s", host.c_str());
                v.push_back({r["id"].as_string(), r["name"].as_string(), sub, t,
                             t == "switch" ? dev_switch : cloudy(t) ? cloud_dev(t, service) : dev_nas, 0, false});
            }
        }
    // Autres PS5 jumelées : chacun de leurs disques est une destination (sauvegarde vérifiée, comme vers le NAS).
    for (const auto& p : snap_->places) {
        if (p.type != "peer") continue;
        v.push_back({p.id, p.label, trf("Autre PS5 · %s libres", size_text(p.free).c_str()), "peer", dev_internal, p.free, true});
    }
    // Dernière carte : « + Ajouter un service » (NAS, nuage, Switch, PS5) sans quitter Transferts ; il apparaît ensuite ici.
    v.push_back({"add", tr("+ Ajouter un service"), tr("NAS, nuage, Switch, PS5…"), "add", dev_cloud, 0, false});
    return v;
}

std::vector<Ui::TrGroup> Ui::tr_groups(const std::vector<TrDevice>& devs) const {
    std::vector<TrGroup> g;
    auto add = [&](const std::string& key, const std::string& name, int image, int i) {
        for (auto& x : g) if (x.key == key) { x.members.push_back(i); return; }
        g.push_back({key, name, image, {i}});
    };
    for (int i = 0; i < static_cast<int>(devs.size()); ++i) {
        const TrDevice& d = devs[static_cast<std::size_t>(i)];
        const std::string& k = d.kind;
        if (k == "ps5" || k == "internal" || k == "usb") add("ps5", tr("Cette PS5"), dev_internal, i);
        else if (k == "peer") {  // « example (12.40) · USB 0 » → bulle « example (12.40) »
            const auto dot = d.name.find(" · ");
            const std::string inst = d.id.substr(0, d.id.find(':', 5));
            add(inst, dot == std::string::npos ? d.name : d.name.substr(0, dot), dev_internal, i);
        } else if (k == "share") add("share", tr("Partages reçus"), dev_cloud, i);
        else if (cloud_proto(k)) add("cloud", tr("Nuage"), dev_cloud, i);
        else if (k == "switch") add("switch", "Switch", dev_switch, i);
        else if (k == "add") add("add", d.name, dev_nas, i);
        else add("nas", "NAS", dev_nas, i);
    }
    for (auto& x : g) if (x.members.size() == 1) x.image = devs[static_cast<std::size_t>(x.members[0])].image;  // seule : son image (logo du service)
    // Ordre : Cette PS5, « + Ajouter un service », les autres PS5, les consoles (Switch), les NAS, le nuage.
    auto rank = [](const std::string& k) { return k == "ps5" ? 0 : k == "add" ? 1 : k == "switch" ? 3 : k == "nas" ? 4 : k == "cloud" ? 5 : k == "share" ? 6 : 2; };
    std::stable_sort(g.begin(), g.end(), [&](const TrGroup& a, const TrGroup& b) { return rank(a.key) < rank(b.key); });
    return g;
}

std::vector<const Item*> Ui::tr_items() const {
    const auto devs = tr_devices();
    const TrDevice* from = tr_from_ < static_cast<int>(devs.size()) ? &devs[static_cast<std::size_t>(tr_from_)] : nullptr;
    std::vector<const Item*> v;
    if (!from || (from->kind != "ps5" && from->kind != "usb" && from->kind != "internal")) return v;
    const char* kind = tr_action_ == 2 ? "game_ps5" : cats[tr_cat_].kind;  // Compresser : jeux PS5 seulement
    for (const auto& it : snap_->items) {
        if (it.kind != kind) continue;
        if (from->kind != "ps5" && it.place != from->id) continue;
        v.push_back(&it);
    }
    std::sort(v.begin(), v.end(), [](const Item* a, const Item* b) { return a->title < b->title; });
    return v;
}

void Ui::load_tr_backups(const std::string& place) {
    if (tr_backups_loading_ || (place == tr_backups_place_ && tr_backups_loaded_ && now_ - tr_backups_loaded_ < 60000)) return;
    tr_backups_loading_ = true;
    tr_backups_loaded_ = now_;
    if (place != tr_backups_place_) tr_backups_ = Json::object();
    tr_backups_place_ = place;
    model_.command("GET", "/api/v2/backups?place=" + url_encode(place), "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (!Json::parse(body, j)) j = Json::object();
        if (!e.empty()) j.set("error", e);
        std::lock_guard<std::mutex> l(async_mu_);
        pending_tr_backups_ = std::move(j);
        has_pending_tr_backups_ = true;
    });
}

// Sauvegardes complètes de l'appareil, les plus récentes d'abord.
std::vector<std::size_t> Ui::tr_backup_rows() const {
    std::vector<std::size_t> rows;
    const Json& list = tr_backups_["backups"];
    for (std::size_t i = 0; i < list.size(); ++i) if (list[i]["complete"].as_bool(true)) rows.push_back(i);
    std::sort(rows.begin(), rows.end(), [&](std::size_t a, std::size_t b) {
        const std::string ta = list[a]["title"].as_string(), tb = list[b]["title"].as_string();
        if (ta != tb) return ta < tb;
        return list[a]["created_unix"].as_int() > list[b]["created_unix"].as_int();
    });
    return rows;
}

// ---- Dessin ---------------------------------------------------------------------------------

void Ui::tr_device_card(Surface& s, const TrDevice& d, int x, int y, int w, int h, bool focus, bool chosen, const std::string& tag) {
    // La carte commune (device_card) : même dessin que l'onglet Appareils ; « + Ajouter un service » en carte légère.
    DevCard c;
    c.dev = d.image; c.name = d.name; c.tag = tag; c.light = d.kind == "add";
    c.line = h < 100 && (d.has_free || d.free) ? trf("%s libres · %s", size_text(d.free).c_str(), d.sub.c_str()) : d.sub;
    if (chosen) c.line_col = color::accent;
    const int tx = device_card(s, c, x, y, w, h, focus, chosen);
    if (h >= 100 && (d.has_free || d.free)) s.text(font_, tx, y + 92, trf("%s libres", size_text(d.free).c_str()), small, chosen ? color::accent : color::muted, x + w - 18 - tx);
}

void Ui::load_images() {
    if (!tr_images_loading_ && (!tr_images_loaded_ || now_ - tr_images_loaded_ > 20000)) {
        tr_images_loading_ = true;
        tr_images_loaded_ = now_;
        model_.command("GET", "/api/v2/images?measure=0", "", [this](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j)) j = Json::object();
            std::lock_guard<std::mutex> l(async_mu_);
            pending_images_ = std::move(j);
            has_pending_images_ = true;
        });
    }
}

// ---- Outils de l'image (fiche du jeu, Triangle) ------------------------------------------------

void Ui::open_image_tools(const Item& it) {
    if (it.kind != "game_ps5" && it.kind != "game_ps4") { toast(tr("Les outils d'image concernent les jeux PS5 et PS4.")); return; }
    load_images();
    const auto f = tr_images_.find(it.key);
    if (f == tr_images_.end()) { toast(tr_images_loading_ ? tr("Lecture des images en cours…") : tr("Aucune image ni dossier de jeu trouvé pour ce jeu.")); return; }
    const Json& g = f->second;
    const std::string st = g["storage"].as_string(), ct = g["container"].as_string();
    std::vector<std::pair<std::string, std::string>> a;  // (action, libellé)
    if (st == "native") {
        a = {{"speed", tr("Test de vitesse de lecture")}, {"native", tr("Copier / déplacer : par le menu Stockage de la PS5")}};
    } else if (st == "image" && ct == "ffpfsc") {
        a = {{"validate", tr("Vérifier l'image")}, {"repair", tr("Vérifier et réparer")}, {"uncompress", tr("Décompresser en dossier")},
             {"uncompress_exfat", tr("Décompresser en image .exfat")}, {"make_pkg", tr("Créer un paquet .pkg (Kraken)")}, {"read_only", tr("Lecture seule (ShadowMount)")},
             {"speed", tr("Test de vitesse de lecture")}, {"transfer", tr("Copier ou déplacer vers un autre disque…")}};
    } else if (st == "image") {
        a = {{"validate", tr("Vérifier l'image")}, {"extract", tr("Extraire en dossier")}};
        if (ct != "ffpkg") a.push_back({"compress", tr("Compresser en .ffpfsc")});
        a.push_back({"make_pkg", tr("Créer un paquet .pkg (Kraken)")});
        a.push_back({"speed", tr("Test de vitesse de lecture")});
        a.push_back({"transfer", tr("Copier ou déplacer vers un autre disque…")});
    } else {
        a = {{"compress", tr("Compresser en .ffpfsc")}, {"make_pkg", tr("Créer un paquet .pkg (Kraken)")}, {"make_image", tr("Créer une image .exfat")}, {"ampr", tr("Construire l'index AMPR")},
             {"speed", tr("Test de vitesse de lecture")}, {"transfer", tr("Copier ou déplacer vers un autre disque…")}};
    }
    if (st != "native") a.insert(a.begin(), {"install", tr("Installer…")});  // le monter là où il est, ou ailleurs
    img_item_ = it.key;
    img_actions_.clear();
    std::vector<std::string> labels;
    for (auto& [id, label] : a) { img_actions_.push_back(id); labels.push_back(label); }
    std::string where = g["disk"]["label"].as_string();
    open_popup(5, it.title + " · " + (st == "native" ? std::string(tr("installé")) : st == "image" ? trf("image .%s", ct.c_str()) : std::string(tr("dossier"))) + (where.empty() ? "" : " · " + where), labels, 0);
    // Une ligne pour dire à quoi sert chaque outil.
    for (const auto& id : img_actions_) {
        if (false) {}
        else if (id == "validate") popup_subs_.push_back(tr("relit tout et contrôle chaque bloc, sans rien modifier"));
        else if (id == "repair") popup_subs_.push_back(tr("contrôle, puis corrige les blocs abîmés"));
        else if (id == "uncompress") popup_subs_.push_back(tr("recrée le dossier du jeu à côté"));
        else if (id == "uncompress_exfat") popup_subs_.push_back(tr("image non compressée, pour les consoles qui ne lisent pas le .ffpfsc"));
        else if (id == "read_only") popup_subs_.push_back(tr("l'image est montée sans écriture (conseillé pour APR-EMU)"));
        else if (id == "speed") popup_subs_.push_back(tr("mesure la vitesse de lecture du disque du jeu"));
        else if (id == "transfer") popup_subs_.push_back(tr("vers un autre disque, copie vérifiée"));
        else if (id == "install") popup_subs_.push_back(tr("le monter là où il est, ou sur un autre disque ou une autre PS5"));
        else if (id == "extract") popup_subs_.push_back(tr("recrée le dossier du jeu à côté"));
        else if (id == "compress") popup_subs_.push_back(tr("prend moins de place, se lance tel quel"));
        else if (id == "make_image") popup_subs_.push_back(tr("regroupe le dossier en une seule image"));
        else if (id == "make_pkg") popup_subs_.push_back(tr("un seul fichier compressé et vérifié, à côté ; l'original reste"));
        else if (id == "ampr") popup_subs_.push_back(tr("pour les jeux APR-EMU : prépare leur index"));
        else popup_subs_.push_back("");
    }
}

void Ui::open_tr_format() {
    open_actions(tr("Dans quel format ?"), {
        {tr("Image ShadowMount (.ffpfsc)"), [this] { tr_format_ = 0; }},
        {trf("Paquet Kraken (.pkg) · %s", tr("Rapide (conseillé)")), [this] { tr_format_ = 1; tr_pkg_level_ = 1; }},
        {trf("Paquet Kraken (.pkg) · %s", tr("Sans compression (le plus sûr)")), [this] { tr_format_ = 1; tr_pkg_level_ = 0; }},
        {trf("Paquet Kraken (.pkg) · %s", tr("Plus petit (plus lent)")), [this] { tr_format_ = 1; tr_pkg_level_ = 2; }},
        {tr("Annuler"), [] {}}});
    popup_note_ = tr("Image .ffpfsc : montée telle quelle par ShadowMount. Paquet .pkg : un seul fichier, lancé par Kraken Mount ; en .pkg on copie, l'original reste.");
}

void Ui::load_packages() {
    pk_at_ = now_;
    model_.command("GET", "/api/v2/images?measure=0", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) return;
        Json list = Json::array();
        for (const auto& x : j["images"].items()) if (x["container"].as_string() == "pkg") list.push(x);
        post_ui([this, list] { pk_list_ = list; });
    });
}

std::string Ui::package_line(const Json& x) const {
    std::string t = x["title"].as_string().empty() ? x["title_id"].as_string() : x["title"].as_string();
    t += " · " + std::string(tr(x["disk"]["label"].as_string().c_str())) + " · " + size_text(static_cast<std::uint64_t>(x["size"].as_int()));
    const std::string v = x["validation"].as_string();
    if (x["running"].as_bool()) t += std::string(" · ") + tr("en cours de jeu");
    else if (v == "ok") {
        const std::time_t at = static_cast<std::time_t>(x["validated_at"].as_int());
        char d[16];
        std::strftime(d, sizeof d, "%d/%m/%Y", std::localtime(&at));
        t += " · " + trf("✓ vérifié le %s", d);
    }
    else if (v == "bad") t += std::string(" · ") + tr("paquet abîmé");
    else t += std::string(" · ") + tr("jamais vérifié");
    return t;
}

void Ui::open_packages() {
    model_.command("GET", "/api/v2/images?measure=0", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Liste impossible")) : e); return; }
        Json list = Json::array();
        for (const auto& x : j["images"].items()) if (x["container"].as_string() == "pkg") list.push(x);
        post_ui([this, list] {
            pk_list_ = list;
            pk_at_ = now_;
            std::vector<std::pair<std::string, std::function<void()>>> acts;
            for (const auto& i : list.items()) { const Json x = i; acts.push_back({package_line(x), [this, x] { open_package(x); }}); }
            if (acts.empty()) { toast(tr("Aucun paquet .pkg sur les disques : Outils d'un jeu › Créer un paquet .pkg")); return; }
            acts.push_back({tr("Fermer"), [] {}});
            open_actions(tr("Paquets .pkg"), std::move(acts));
            popup_note_ = tr("Les paquets .pkg des disques de cette console (créés ici, Orbit…) : choisissez-en un pour le vérifier, le copier, le déplacer ou le supprimer.");
        });
    });
}

void Ui::open_install(const std::string& path, const std::string& name, std::uint64_t bytes, const std::string& disk_id_in, bool pkg) {
    std::string disk_id = disk_id_in;
    if (disk_id.empty()) {  // le disque du jeu : le point de montage le plus long qui contient le chemin
        std::size_t best = 0;
        for (const auto& p : snap_->places)
            if ((p.type == "internal" || p.type == "usb") && !p.mount.empty() && path.rfind(p.mount + "/", 0) == 0 && p.mount.size() > best) { best = p.mount.size(); disk_id = p.id; }
        if (disk_id.empty()) for (const auto& p : snap_->places) if (p.type == "internal") disk_id = p.id;
    }
    std::string here_label = tr("ce disque");
    for (const auto& p : snap_->places) if (p.id == disk_id) here_label = p.label;
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    acts.push_back({trf("Le monter là où il est (%s)", here_label.c_str()), [this, path, pkg] {
        Json b = Json::object();
        b.set("path", path);
        model_.command("POST", "/api/v2/install/here", b.dump(), [this, pkg](const std::string& e, const std::string& body) {
            Json j;
            Json::parse(body, j);
            post_toast(!e.empty() ? e : j["by"].as_string() == "krakenmount" ? std::string(tr("Rangé dans le dossier de jeux : Kraken Mount le monte et l'inscrit"))
                       : j["by"].as_string() == "smp_ours" ? std::string(tr("Rangé dans le dossier de jeux : notre ShadowMount intégré le monte et l'inscrit"))
                       : j["by"].as_string() == "smp_theirs" ? std::string(tr("Rangé dans le dossier de jeux : ShadowMountPlus le monte et l'inscrit dès que vous fermez l'application"))
                       : pkg ? std::string(tr("Préparation lancée : le jeu apparaîtra sur l'écran d'accueil et se lancera depuis son .pkg"))
                       : j["already"].as_bool() ? std::string(tr("Déjà installé : dans le dossier de jeux, monté par ShadowMount"))
                                                : std::string(tr("Rangé dans le dossier de jeux de ce disque : ShadowMount le monte dans quelques secondes")));
            std::lock_guard<std::mutex> l(async_mu_);
            pk_at_ = 0;
            apps_dl_at_ = 0;
        });
    }});
    if (pkg) acts.push_back({tr("Installer par la console (comme un jeu du Store)"), [this, path, name] {
        open_actions(trf("Installer « %s » par la console ?", name.c_str()), {
            {tr("Oui, installer"), [this, path] {
                Json b = Json::object();
                b.set("path", path);
                model_.command("POST", "/api/v2/packages/install", b.dump(), [this](const std::string& e, const std::string&) {
                    post_toast(e.empty() ? tr("Installation demandée : la console l'installe en arrière-plan (sa notification montre l'avancement)") : e);
                });
            }},
            {tr("Annuler"), [] {}}});
        popup_note_ = tr("La console recopie le jeu dans son stockage, à l'endroit choisi dans ses réglages (Stockage) : il faut la place du jeu entier. Le paquet reste où il est ; on pourra le libérer après. Jamais pendant une partie.");
    }});
    const std::uint64_t need = bytes + (512ull << 20);
    for (const auto& p : snap_->places) {
        if (p.readonly || p.id == disk_id) continue;
        const bool local = p.type == "internal" || p.type == "usb", peer = p.type == "peer";
        if (!local && !peer) continue;
        const std::string pid = p.id, label = p.label, dir = (p.mount == "/user" || p.mount.empty() ? std::string("/data") : p.mount) + "/homebrew";
        const bool small = bytes && p.free && p.free < need;
        const std::string line = trf("L'installer sur %s", label.c_str()) + " · " + (small ? trf("%s libres · trop petit", size_text(p.free).c_str()) : p.free ? trf("%s libres", size_text(p.free).c_str()) : std::string());
        acts.push_back({line, [this, path, name, pid, label, dir, local, small] {
            if (small) { toast(trf("Pas assez de place sur %s", label.c_str())); return; }
            if (!local) {  // une autre PS5 : copie vérifiée dans son dossier de jeux, montée là-bas
                Json b = Json::object();
                b.set("path", path); b.set("place", pid); b.set("game", true);
                model_.command("POST", "/api/v2/packages/send", b.dump(), [this](const std::string& e, const std::string&) {
                    post_toast(e.empty() ? tr("Ajouté aux tâches : suivez-le dans Transferts › Transferts en cours") : e);
                });
                return;
            }
            auto go = [this, path, dir](const char* mode) {
                Json b = Json::object();
                b.set("type", "transfer"); b.set("source", path); b.set("target_dir", dir); b.set("mode", mode);
                model_.command("POST", "/api/v2/images/plan", b.dump(), [this, b](const std::string& e, const std::string& resp) {
                    Json pl;
                    if (!e.empty()) { post_toast(e); return; }
                    if (Json::parse(resp, pl) && !pl["ok"].as_bool(true)) { post_toast(pl["error"].as_string()); return; }
                    model_.command("POST", "/api/v2/tasks", b.dump(), [this](const std::string& e2, const std::string&) {
                        post_toast(e2.empty() ? tr("Ajouté aux tâches : suivez-le dans Transferts › Transferts en cours") : e2);
                        std::lock_guard<std::mutex> l(async_mu_);
                        pk_at_ = 0;
                    });
                });
            };
            open_actions(tr("Après la copie, que fait-on de l'original ?"), {
                {tr("Le garder (deux exemplaires)"), [go] { go("copy"); }},
                {tr("Le retirer une fois la copie vérifiée"), [go] { go("move"); }},
                {tr("Annuler"), [] {}}});
        }});
    }
    if (pkg)  // un paquet : l'envoyer sur une autre PS5 et l'y faire installer par sa console, en une fois
        for (const auto& p : snap_->places) {
            if (p.readonly || p.type != "peer" || (bytes && p.free && p.free < need)) continue;
            const std::string pid = p.id;
            acts.push_back({trf("L'envoyer sur %s et l'y installer", p.label.c_str()), [this, path, pid] {
                Json b = Json::object();
                b.set("path", path); b.set("place", pid); b.set("install", true);
                model_.command("POST", "/api/v2/packages/send", b.dump(), [this](const std::string& e, const std::string&) {
                    post_toast(e.empty() ? tr("Envoi lancé : l'autre PS5 l'installera une fois la copie vérifiée") : e);
                });
            }});
        }
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(trf("Installer « %s »", name.c_str()), std::move(acts));
    popup_note_ = pkg ? tr("Paquet .pkg Kraken : « Le monter là où il est » le lance depuis le paquet, sans rien décompresser — par Kraken Mount s'il est installé, sinon par notre version intégrée (Paramètres › Montage des jeux). Ou : l'installer par la console, ou ailleurs.")
                      : tr("Monté par ShadowMount : rien n'est décompressé, le jeu se lance depuis l'image ou le dossier. Sur un autre disque ou une autre PS5 : copie vérifiée, puis montée.");
}

void Ui::open_package(const Json& x) {
    const std::string path = x["path"].as_string(), name = x["title"].as_string().empty() ? x["title_id"].as_string() : x["title"].as_string();
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    acts.push_back({tr("Vérifier le paquet"), [this, path] {
        Json b = Json::object();
        b.set("type", "validate_image"); b.set("source", path);
        model_.command("POST", "/api/v2/tasks", b.dump(), [this](const std::string& e, const std::string&) {
            post_toast(e.empty() ? tr("Ajouté aux tâches : suivez-le dans Transferts › Transferts en cours") : e);
        });
    }});
    acts.push_back({tr("Installer…"), [this, x, path, name] {
        open_install(path, name, static_cast<std::uint64_t>(x["size"].as_int()), "", true);
    }});
    acts.push_back({tr("Copier ou déplacer vers un autre disque…"), [this, x, path, name] {
        std::vector<std::pair<std::string, std::function<void()>>> dests;
        for (const auto& p : snap_->places) {
            if ((p.type != "internal" && p.type != "usb") || p.id == x["disk"]["id"].as_string() || p.mount == x["disk"]["mount"].as_string()) continue;
            const std::string dir = (p.mount == "/user" ? std::string("/data") : p.mount) + "/homebrew", label = p.label;
            dests.push_back({label + " · " + trf("%s libres", size_text(p.free).c_str()), [this, path, name, dir, label] {
                auto go = [this, path, dir](const char* mode) {
                    Json b = Json::object();
                    b.set("type", "transfer"); b.set("source", path); b.set("target_dir", dir); b.set("mode", mode);
                    model_.command("POST", "/api/v2/images/plan", b.dump(), [this, b](const std::string& e, const std::string& resp) {
                        Json pl;
                        if (!e.empty()) { post_toast(e); return; }
                        if (Json::parse(resp, pl) && !pl["ok"].as_bool(true)) { post_toast(pl["error"].as_string()); return; }
                        model_.command("POST", "/api/v2/tasks", b.dump(), [this](const std::string& e2, const std::string&) {
                            post_toast(e2.empty() ? tr("Ajouté aux tâches : suivez-le dans Transferts › Transferts en cours") : e2);
                            std::lock_guard<std::mutex> l(async_mu_);
                            pk_at_ = 0;
                        });
                    });
                };
                open_actions(trf("« %s » vers %s", name.c_str(), label.c_str()), {
                    {tr("Copier (l'original reste)"), [go] { go("copy"); }},
                    {tr("Déplacer (copie vérifiée, puis l'original retiré)"), [go] { go("move"); }},
                    {tr("Annuler"), [] {}}});
            }});
        }
        if (dests.empty()) { toast(tr("Aucun autre disque sur cette console")); return; }
        dests.push_back({tr("Annuler"), [] {}});
        open_actions(tr("Copier ou déplacer vers un autre disque…"), std::move(dests));
        popup_note_ = tr("Copie vérifiée ; un paquet .pkg n'est jamais inscrit dans ShadowMount.");
    }});
    acts.push_back({tr("Envoyer vers un NAS, le nuage ou une autre PS5…"), [this, path, name] {
        std::vector<std::pair<std::string, std::function<void()>>> dests;
        for (const auto& p : snap_->places) {
            if (p.readonly || (p.type != "ftp" && p.type != "peer")) continue;  // NAS, nuage, autre PS5 (pas les partages reçus)
            const std::string pid = p.id, label = p.label;
            dests.push_back({label, [this, path, pid] {
                Json b = Json::object();
                b.set("path", path); b.set("place", pid);
                model_.command("POST", "/api/v2/packages/send", b.dump(), [this](const std::string& e, const std::string&) {
                    post_toast(e.empty() ? tr("Ajouté aux tâches : suivez-le dans Transferts › Transferts en cours") : e);
                });
            }});
        }
        if (dests.empty()) { toast(tr("Aucun NAS, nuage ni autre PS5 : Appareils › + Ajouter un service")); return; }
        dests.push_back({tr("Annuler"), [] {}});
        open_actions(trf("Envoyer « %s »", name.c_str()), std::move(dests));
        popup_note_ = tr("Copie vérifiée ; l'original reste. Autre PS5 : dans son dossier de jeux ; NAS ou nuage : dans « Paquets .pkg ».");
    }});
    acts.push_back({tr("Supprimer ce paquet…"), [this, path, name] {
        open_actions(trf("Supprimer « %s » ?", name.c_str()), {
            {tr("Oui, supprimer ce paquet pour de bon"), [this, path] {
                Json b = Json::object();
                b.set("path", path); b.set("confirm", "SUPPRIMER");
                model_.command("POST", "/api/v2/packages/delete", b.dump(), [this](const std::string& e, const std::string&) {
                    post_toast(e.empty() ? tr("Paquet supprimé") : e);
                    std::lock_guard<std::mutex> l(async_mu_);
                    pk_at_ = 0;
                });
            }},
            {tr("Annuler"), [] {}}});
        popup_note_ = tr("Le jeu, son image ou son dossier ne sont pas touchés : seulement ce fichier .pkg.");
    }});
    acts.push_back({tr("Fermer"), [] {}});
    open_actions(name + " · " + tr("paquet .pkg"), std::move(acts));
    popup_note_ = x["path"].as_string();
}

void Ui::run_image_tool(const std::string& a) {
    if (a.empty()) return;
    if (a == "install") {  // Installer… : le chemin et la taille de l'image ou du dossier
        const auto f = tr_images_.find(img_item_);
        if (f == tr_images_.end()) return;
        const Json& g = f->second;
        std::string title = g["title"].as_string();
        for (const auto& i : snap_->items) if (i.key == img_item_) title = i.title;
        open_install(g["path"].as_string(), title, static_cast<std::uint64_t>(g["size"].as_int()), "", g["container"].as_string() == "pkg");
        return;
    }
    // Paquet .pkg : d'abord la compression (Rapide conseillé ; Sans compression si une console refuse le paquet compressé).
    if (a == "make_pkg") {
        open_actions(tr("Créer un paquet .pkg (Kraken)"), {
            {tr("Rapide (conseillé)"), [this] { run_image_tool("make_pkg:1"); }},
            {tr("Sans compression (le plus sûr)"), [this] { run_image_tool("make_pkg:0"); }},
            {tr("Plus petit (plus lent)"), [this] { run_image_tool("make_pkg:2"); }},
            {tr("Annuler"), [] {}}});
        popup_note_ = tr("Le paquet est relu et vérifié avant d'être gardé ; l'original reste. Sans compression : plus gros, mais lu par toutes les consoles.");
        return;
    }
    if (a == "native") { toast(tr("Jeu installé par la PS5 : Réglages › Stockage pour le déplacer, ou extrayez-le d'abord en dossier.")); return; }
    if (a == "transfer") {  // Transferts, source = disque du jeu, jeu déjà coché
        const auto devs = tr_devices();
        const auto f = tr_images_.find(img_item_);
        const std::string disk = f != tr_images_.end() ? f->second["disk"]["id"].as_string() : "";
        for (int i = 0; i < static_cast<int>(devs.size()); ++i) if (devs[static_cast<std::size_t>(i)].id == disk) tr_from_ = i;
        if (tr_to_ == tr_from_) tr_to_ = tr_from_ == 0 ? 1 : 0;
        sheet_ = false;
        tab_ = transfers;
        tr_screen_ = 0;
        tr_zone_ = 1;
        tr_action_ = 0;
        tr_sel_ = {img_item_};
        toast(tr("Choisissez la destination, puis Copier ou Déplacer."));
        return;
    }
    const std::string item = ",\"item\":" + json_str(img_item_);
    std::string body;
    if (a == "validate") body = "{\"type\":\"validate_image\"" + item + "}";
    else if (a == "repair") body = "{\"type\":\"validate_image\",\"repair\":true" + item + "}";
    else if (a == "uncompress") body = "{\"type\":\"uncompress\",\"to\":\"folder\"" + item + "}";
    else if (a == "uncompress_exfat") body = "{\"type\":\"uncompress\",\"to\":\"exfat\"" + item + "}";
    else if (a == "read_only") body = "{\"type\":\"set_read_only\"" + item + "}";
    else if (a == "speed") body = "{\"type\":\"read_speed_test\",\"seconds\":20" + item + "}";
    else if (a == "extract") body = "{\"type\":\"extract_image\"" + item + "}";
    else if (a == "compress") body = "{\"type\":\"compress\"" + item + "}";
    else if (a == "make_image") body = "{\"type\":\"make_image\"" + item + "}";
    else if (a.rfind("make_pkg:", 0) == 0) body = "{\"type\":\"make_pkg\",\"pkg_level\":" + a.substr(9, 1) + item + "}";
    else if (a == "ampr") body = "{\"type\":\"build_ampr_index\"" + item + "}";
    else return;
    // D'abord le plan (place nécessaire, jeu en cours, image montée…), puis la tâche.
    model_.command("POST", "/api/v2/images/plan", body, [this, body](const std::string& e, const std::string& resp) {
        Json p;
        if (!e.empty()) { post_toast(e); return; }
        if (Json::parse(resp, p) && !p["ok"].as_bool(true)) { post_toast(p["error"].as_string().empty() ? tr("Opération impossible") : p["error"].as_string()); return; }
        model_.command("POST", "/api/v2/tasks", body, [this](const std::string& e2, const std::string&) {
            post_toast(e2.empty() ? tr("Ajouté aux tâches : suivez-le dans Transferts › Transferts en cours") : e2);
        });
    });
    sheet_ = false;
}

void Ui::draw_transfers(Surface& s) {
    if (!switches_loaded_ || now_ - switches_loaded_ > 15000) load_remotes();
    load_images();
    switch (tr_screen_) {
        case 1: draw_tr_choice(s); break;
        case 2: draw_tr_progress(s); break;
        case 3: draw_tr_full(s); break;
        case 4: draw_tr_sync(s); break;
        case 5: draw_tr_files(s); break;
        default: draw_tr_main(s); break;
    }
}

void Ui::draw_tr_main(Surface& s) {
    draw_page_head(s, tr("Transferts"), tr("Copier, déplacer ou compresser jeux, applis et ROM entre vos appareils."));
    const auto devs = tr_devices();
    const int n = static_cast<int>(devs.size());
    tr_from_ = std::min(tr_from_, std::max(0, n - 1));
    tr_to_ = std::min(tr_to_, std::max(0, n - 1));
    // Rangées DEPUIS / VERS : une seule ligne chacune, qui défile à gauche / droite (comme les rangées de l'Accueil) :
    // « Que faire ? » et les boutons du bas restent toujours à l'écran, quel que soit le nombre d'appareils.
    const int gap = 20, per = 5, cw = (1920 - 2 * margin - (per - 1) * gap) / per, ch = 84;
    const int lines = 1, rows_h = ch;
    const auto groups = tr_groups(devs);
    const int gn = static_cast<int>(groups.size());
    // Une bulle par appareil ou famille ; ouverte (Croix), la rangée montre ses éléments à la place, Rond la referme.
    auto row = [&](const char* label, int y, int zone, int chosen) {
        s.text(font_, margin, y, label, small, color::accent);
        const bool open = tr_open_[zone] >= 0 && tr_open_[zone] < gn;
        std::vector<TrDevice> cards;
        std::vector<int> idx;  // index de l'appareil (ouvert) ou du groupe (fermé)
        int focus_i = 0;
        if (open) {
            const TrGroup& g = groups[static_cast<std::size_t>(tr_open_[zone])];
            s.text(font_, margin + Surface::measure(label, small) + 20, y, "› " + g.name, small, color::text);
            for (int m : g.members) {
                TrDevice d = devs[static_cast<std::size_t>(m)];
                const auto dot = d.name.find(" · ");
                if (g.key.rfind("peer:", 0) == 0 && dot != std::string::npos) d.name = d.name.substr(dot + 3);
                if (d.kind == "ps5") d.name = tr("Tous ses disques");
                cards.push_back(d); idx.push_back(m);
            }
            tr_msel_[zone] = std::min(tr_msel_[zone], std::max(0, static_cast<int>(cards.size()) - 1));
            focus_i = tr_msel_[zone];
        } else {
            for (int gi = 0; gi < gn; ++gi) {
                const TrGroup& g = groups[static_cast<std::size_t>(gi)];
                if (g.members.size() == 1) { cards.push_back(devs[static_cast<std::size_t>(g.members[0])]); idx.push_back(gi); continue; }
                TrDevice d;
                d.name = g.name; d.kind = "group"; d.image = g.image;
                std::string chosen_name;
                std::uint64_t free = 0;
                for (int m : g.members) { if (m == chosen) chosen_name = devs[static_cast<std::size_t>(m)].name; free += devs[static_cast<std::size_t>(m)].has_free && devs[static_cast<std::size_t>(m)].kind != "ps5" ? devs[static_cast<std::size_t>(m)].free : 0; }
                std::string members;
                for (int m : g.members) {
                    std::string nm = devs[static_cast<std::size_t>(m)].name;
                    const auto dot = nm.find(" · ");
                    if (g.key.rfind("peer:", 0) == 0 && dot != std::string::npos) nm = nm.substr(dot + 3);
                    if (members.find(nm) == std::string::npos) members += (members.empty() ? "" : " · ") + nm;
                }
                d.sub = !chosen_name.empty() ? chosen_name : members;  // comme Appareils : qui est dedans ; le nombre est en étiquette
                (void)free;
                cards.push_back(d); idx.push_back(gi);
            }
            if (tr_zone_ != zone)  // rangée sans le curseur : il revient sur la bulle de l'appareil choisi
                for (int gi = 0; gi < gn; ++gi)
                    for (int m : groups[static_cast<std::size_t>(gi)].members) if (m == chosen) tr_gsel_[zone] = gi;
            tr_gsel_[zone] = std::min(tr_gsel_[zone], std::max(0, gn - 1));
            focus_i = tr_gsel_[zone];
        }
        const int cn = static_cast<int>(cards.size());
        // Trop de cartes : la dernière place montre ce qui est caché (« + 2 de plus › NAS · Nuage »), toujours à la même taille.
        const int vis = cn > per ? per - 1 : per;
        int& first = tr_first_[zone];
        if (focus_i < first) first = focus_i;
        if (focus_i >= first + vis) first = focus_i - vis + 1;
        first = std::max(0, std::min(first, std::max(0, cn - vis)));
        if (first > 0) s.text(font_, margin - 34, y + 30 + ch / 2 - 22, "‹", large, color::accent);
        if (cn > per) {
            // Ce qui reste à droite d'abord ; arrivé au bout, ce qui est passé à gauche (« ‹ 2 avant »).
            std::string names;
            int right = 0, left = 0;
            for (int i = first + vis; i < cn; ++i) ++right;
            for (int i = 0; i < first; ++i) ++left;
            const bool ahead = right > 0;
            for (int i = 0; i < cn; ++i) {
                if (ahead ? i < first + vis : i >= first) continue;
                const std::string nm = cards[static_cast<std::size_t>(i)].name;
                if (names.find(nm) == std::string::npos) names += (names.empty() ? "" : " · ") + nm;
            }
            const int px = margin + vis * (cw + gap), py = y + 30;
            DevCard more;
            more.light = true; more.name = ahead ? trf("+ %d de plus ›", right) : trf("‹ %d avant", left); more.line = names;
            device_card(s, more, px, py, cw, ch, false);
        }
        for (int i = first; i < cn && i < first + vis; ++i) {
            const int x = margin + (i - first) * (cw + gap), cy = y + 30;
            bool is_chosen, other;
            if (open) { is_chosen = idx[static_cast<std::size_t>(i)] == chosen; other = zone == 1 && idx[static_cast<std::size_t>(i)] == tr_from_; }
            else {
                const TrGroup& g = groups[static_cast<std::size_t>(idx[static_cast<std::size_t>(i)])];
                is_chosen = std::find(g.members.begin(), g.members.end(), chosen) != g.members.end();
                other = zone == 1 && g.members.size() == 1 && g.members[0] == tr_from_;
            }
            const bool grp = !open && cards[static_cast<std::size_t>(i)].kind == "group";  // famille : « 3 › » en étiquette, comme Appareils
            tr_device_card(s, cards[static_cast<std::size_t>(i)], x, cy, cw, ch, tr_zone_ == zone && i == focus_i, is_chosen,
                           grp ? std::to_string(groups[static_cast<std::size_t>(idx[static_cast<std::size_t>(i)])].members.size()) + " ›" : std::string());
            if (other) s.rect(x, cy, cw, ch, rgb(6, 16, 29, 150), 20);
        }
    };
    // Aéré : titres de section un peu plus loin des cartes, plus d'espace entre DEPUIS, VERS et QUE FAIRE.
    const int y_from = 262, y_to = y_from + 30 + rows_h + 24, y_act = y_to + 30 + rows_h + 24;
    row(tr("DEPUIS"), y_from, 0, tr_from_);
    row(tr("VERS"), y_to, 1, tr_to_);
    s.text(font_, margin, y_act, tr("QUE FAIRE ?"), small, color::accent);
    const int aw = (1920 - 2 * margin - 3 * gap) / 4, ah = lines > 1 ? 96 : 128;
    for (int i = 0; i < action_count; ++i) {
        const int x = margin + i * (aw + gap), y = y_act + 30;
        const bool focus = tr_zone_ == 2 && tr_action_ == i, chosen = tr_action_ == i;
        card_frame(s, x, y, aw, ah, focus, chosen);
        s.text(font_, x + 22, y + 18, tr(actions[i].name), large, color::text);
        s.text(font_, x + 22, y + 62, tr(actions[i].text), small, color::muted, aw - 44);
    }
    // En bas : sauvegarde complète et transferts en cours.
    int running = 0, waiting = 0;
    for (const auto& t : snap_->tasks) { if (t.state == "running") ++running; else if (active(t.state)) ++waiting; }
    const int by = y_act + 30 + ah + 22, bw = (1920 - 2 * margin - 3 * gap) / 4, bh = lines > 1 ? 92 : 112;
    if (now_ - pk_at_ > 10000) load_packages();
    for (int i = 0; i < 4; ++i) {
        const int x = margin + i * (bw + gap);
        const bool focus = tr_zone_ == 3 && tr_opt_ == i;
        card_frame(s, x, by, bw, bh, focus);  // même bulle que les actions au-dessus
        if (i == 0) {
            s.text(font_, x + 24, by + (bh - 74) / 2, tr("Sauvegarde complète de la PS5"), large, color::text, bw - 48);
            s.text(font_, x + 24, by + (bh - 74) / 2 + 44, tr("Comptes, sauvegardes, jeux, homebrews, réglages : tout cocher, tout retrouver après une remise à zéro."), small, color::muted, bw - 48);
        } else if (i == 1) {
            s.text(font_, x + 24, by + (bh - 74) / 2, tr("Fichiers"), large, color::text, bw - 48);
            s.text(font_, x + 24, by + (bh - 74) / 2 + 44, tr("Copier ou déplacer n'importe quels fichiers ou dossiers, d'un endroit à l'autre."), small, color::muted, bw - 48);
        } else if (i == 2) {  // Paquets .pkg (docs/PLAN-paquets-fpkg.md, lot 2)
            std::uint64_t total = 0;
            int n = 0;
            for (const auto& x : pk_list_.items()) { ++n; total += static_cast<std::uint64_t>(x["size"].as_int()); }
            s.text(font_, x + 24, by + (bh - 74) / 2, tr("Paquets .pkg"), large, color::text, bw - 48);
            s.text(font_, x + 24, by + (bh - 74) / 2 + 44, n ? trf("%d paquet(s) · %s", n, size_text(total).c_str()) : std::string(tr("aucun paquet sur les disques")), small, color::muted, bw - 48);
        } else {
            const std::string b = trf("%d en cours · %d en attente", running, waiting);
            s.text(font_, x + 24, by + (bh - 74) / 2, tr("Transferts en cours"), large, color::text, bw - 120);
            s.text(font_, x + 24, by + (bh - 74) / 2 + 44, b, small, running ? color::accent : color::muted, bw - 120);
            if (running) spinner(s, x + bw - 60, by + bh / 2, 20);
        }
    }
    if (tr_zone_ == 0 && tr_open_[0] >= 0) draw_footer(s, {{0, tr("Choisir la source")}, {2, tr("Ajouter un service")}, {3, tr("Inverser")}, {1, tr("Refermer")}});
    else if (tr_zone_ == 0) draw_footer(s, {{0, tr("Ouvrir / choisir")}, {2, tr("Ajouter un service")}, {3, tr("Inverser")}, {-1, tr("Bas : destination")}});
    else if (tr_zone_ == 1) draw_footer(s, {{0, tr_open_[1] >= 0 ? tr("Choisir la destination") : tr("Ouvrir / choisir")}, {2, tr("Ajouter un service")}, {3, tr("Inverser")}, {1, tr_open_[1] >= 0 ? tr("Refermer") : tr("Retour")}});
    else if (tr_zone_ == 2) draw_footer(s, {{0, tr_action_ == 3 ? tr("Ouvrir la synchronisation") : tr("Choisir les éléments")}, {3, tr("Inverser")}, {1, tr("Retour")}});
    else draw_footer(s, {{0, tr("Ouvrir")}, {1, tr("Retour")}});
}

void Ui::draw_tr_choice(Surface& s) {
    const auto devs = tr_devices();
    if (devs.empty()) return;
    const TrDevice& from = devs[static_cast<std::size_t>(std::min<int>(tr_from_, static_cast<int>(devs.size()) - 1))];
    const TrDevice& to = devs[static_cast<std::size_t>(std::min<int>(tr_to_, static_cast<int>(devs.size()) - 1))];
    if (from.kind == "ftp") { draw_tr_restore(s, from, to); return; }
    s.text(font_, margin, 112, trf("TRANSFERTS · %s → %s · %s", from.name.c_str(), to.name.c_str(), tr(actions[tr_action_].name)), small, color::accent);
    s.text(font_, margin, 140, tr_action_ == 2 ? tr("Quels jeux compresser") : tr("Quoi transférer"), title, color::text);
    // Catégories
    int x = margin;
    const int py = 222;
    for (int i = 0; i < cat_count; ++i) {
        if (tr_action_ == 2 && i) break;
        int count = 0;
        for (const auto& it : snap_->items) if (it.kind == cats[i].kind) ++count;
        char b[64];
        std::snprintf(b, sizeof b, "%s · %d", tr(cats[i].name), count);
        const int w = Surface::measure(b, body) + 40;
        const bool on = tr_cat_ == i, focus = tr_zone_ == 0 && tr_pill_ == i;
        s.rect(x, py, w, 48, on ? color::text : color::panel2, 24);
        if (focus) s.outline(x - 4, py - 4, w + 8, 56, color::accent, 28, 3);
        s.text(font_, x + 20, py + 11, b, body, on ? color::bg : color::text);
        x += w + 14;
    }
    {
        const char* all = tr("Tout cocher");
        const int w = Surface::measure(all, body) + 40;
        const bool focus = tr_zone_ == 0 && tr_pill_ == (tr_action_ == 2 ? 1 : cat_count);
        s.rect(x, py, w, 48, color::panel, 24);
        s.outline(x, py, w, 48, focus ? color::accent : color::line, 24, focus ? 3 : 1);
        s.text(font_, x + 20, py + 11, all, body, color::text);
    }
    // Liste
    const auto items = tr_items();
    const int lx = margin, lw = 1130, ly = 300, rh = 92, vis = 6;
    tr_row_ = std::min(tr_row_, std::max(0, static_cast<int>(items.size()) - 1));
    if (tr_row_ < tr_top_) tr_top_ = tr_row_;
    if (tr_row_ >= tr_top_ + vis) tr_top_ = tr_row_ - vis + 1;
    if (items.empty()) {
        s.rect(lx, ly, lw, 180, color::panel, 20);
        s.text(font_, lx + 32, ly + 40, from.kind == "switch" || from.kind == "ftp" ? tr("Le contenu de cet appareil se choisit dans sa synchronisation (Transferts › Synchroniser).") : tr("Rien de ce type sur cet appareil."), body, color::muted, lw - 64);
    }
    for (int i = tr_top_; i < static_cast<int>(items.size()) && i < tr_top_ + vis; ++i) {
        const Item& it = *items[static_cast<std::size_t>(i)];
        const int y = ly + (i - tr_top_) * (rh + 10);
        const bool focus = tr_zone_ == 1 && tr_row_ == i, on = tr_sel_.count(it.key) > 0;
        s.rect(lx, y, lw, rh, focus ? color::panel2 : color::panel, 18);
        s.outline(lx, y, lw, rh, focus ? color::accent : on ? color::accent2 : color::line, 18, focus ? 3 : 1);
        s.rect(lx + 22, y + (rh - 32) / 2, 32, 32, on ? color::accent2 : rgb(34, 52, 77), 8);
        if (on) for (int d = 0; d < 9; ++d) { s.rect(lx + 28 + d, y + rh / 2 + d / 2 - 2, 4, 4, color::text, 2); }
        if (on) for (int d = 0; d < 13; ++d) { s.rect(lx + 34 + d, y + rh / 2 + 2 - d, 4, 4, color::text, 2); }
        cover(s, it, lx + 74, y + 10, rh - 20, false);
        const int tx = lx + 74 + rh, tw = lw - (tx - lx) - 170;
        s.text(font_, tx, y + 14, it.title, body, color::text, tw);
        std::string place = "?";
        for (const auto& p : snap_->places) if (p.id == it.place) place = p.type == "internal" ? std::string(tr("Interne")) : p.label;
        std::string sub;
        const auto img = tr_images_.find(it.key);
        if (img != tr_images_.end()) {  // le moteur sait comment et où vit le jeu
            const Json& g = img->second;
            const std::string st = g["storage"].as_string(), ct = g["container"].as_string();
            std::string disk = g["disk"]["label"].as_string();
            if (disk == "internal" || g["disk"]["id"].as_string() == "internal") disk = tr("Interne");
            sub = (disk.empty() ? place : disk) + " · " +
                  (st == "native" ? std::string(tr("installé (natif)")) : st == "image" ? trf("image .%s", ct.c_str()) : std::string(tr("dossier")));
            if (ct == "ffpfsc") sub += std::string(" · ") + tr("déjà compressée");
            else if (st == "folder" && it.kind == "game_ps5") sub += std::string(" · ") + tr("compressible");
            if (g["running"].as_bool()) sub += std::string(" · ") + tr("en cours de jeu");
            else if (g["mounted"].as_bool()) sub += std::string(" · ") + tr("monté");
        } else {
            sub = place;
            if (!it.image_type.empty()) sub += " · " + it.image_type;
        }
        s.text(font_, tx, y + 50, sub, small, color::muted, tw);
        const std::string sz = size_text(item_bytes(it, tr_saves_));
        s.text(font_, lx + lw - 24 - Surface::measure(sz, body), y + 30, sz, body, color::text);
    }
    if (static_cast<int>(items.size()) > vis) {  // barre de défilement
        const int track = vis * (rh + 10) - 10, bar = std::max(40, track * vis / static_cast<int>(items.size()));
        const int by = ly + (track - bar) * tr_top_ / std::max(1, static_cast<int>(items.size()) - vis);
        s.rect(lx + lw + 10, ly, 6, track, rgb(255, 255, 255, 20), 3);
        s.rect(lx + lw + 10, by, 6, bar, color::accent, 3);
    }
    // Options
    const int ox = lx + lw + 40, ow = 1920 - margin - ox;
    s.text(font_, ox, ly - 34, tr("OPTIONS"), small, color::accent);
    struct Opt { std::string name, text; bool on; };
    const char* lvl = tr_pkg_level_ == 0 ? N_("Sans compression (le plus sûr)") : tr_pkg_level_ == 2 ? N_("Plus petit (plus lent)") : N_("Rapide (conseillé)");
    const std::string fmt = tr_format_ ? trf("Format : paquet .pkg (%s) — Triangle pour changer", tr(lvl)) : std::string(tr("Format : image .ffpfsc — Triangle pour changer"));
    const Opt opts[] = {{tr_action_ == 2 ? tr("Compresser") : tr("Compresser avant l'envoi"), (tr_compress_ || tr_action_ == 2) ? fmt : std::string(tr("dossiers PS5 → image .ffpfsc")), tr_compress_ || tr_action_ == 2},
                        {tr("Avec les sauvegardes"), tr("les sauvegardes du jeu suivent"), tr_saves_},
                        {tr_verify_ ? tr("Vérification complète") : tr("Vérification rapide"), tr_verify_ ? tr("SHA-256 de chaque fichier copié") : tr("présence et taille ; original gardé si déplacement"), tr_verify_}};
    for (int i = 0; i < 3; ++i) {
        const int y = ly + i * 112;
        const bool focus = tr_zone_ == 2 && tr_opt_ == i;
        s.rect(ox, y, ow, 100, focus ? color::panel2 : color::panel, 18);
        s.outline(ox, y, ow, 100, focus ? color::accent : color::line, 18, focus ? 3 : 1);
        s.text(font_, ox + 22, y + 18, opts[i].name, body, color::text, ow - 120);
        s.text(font_, ox + 22, y + 56, opts[i].text, small, color::muted, ow - 120);
        const int sx = ox + ow - 84, sy = y + 34;  // interrupteur
        s.rect(sx, sy, 60, 32, opts[i].on ? color::accent2 : rgb(34, 52, 77), 16);
        s.rect(opts[i].on ? sx + 32 : sx + 4, sy + 4, 24, 24, color::text, 12);
    }
    // Résumé et lancement
    std::uint64_t total = 0;
    int count = 0;
    for (const auto& it : snap_->items) if (tr_sel_.count(it.key)) { total += item_bytes(it, tr_saves_); ++count; }
    const int sy = 922, sh = 76;
    s.rect(margin, sy - 12, 1920 - 2 * margin, sh + 4, rgb(11, 26, 46), 18);
    const std::string b = count > 1 ? trf("%d éléments · %s → %s", count, size_text(total).c_str(), to.name.c_str())
                                    : trf("%d élément · %s → %s", count, size_text(total).c_str(), to.name.c_str());
    s.text(font_, margin + 24, sy, b, body, color::text, 1100);
    std::string room = to.has_free ? trf("Place libre : %s", size_text(to.free).c_str()) : std::string(tr("Place libre vérifiée par le moteur avant de commencer"));
    const bool tight = to.has_free && total > to.free;
    if (tight) room = trf("Pas assez de place : %s libres", size_text(to.free).c_str());
    s.text(font_, margin + 24, sy + 34, room, small, tight ? color::bad : color::muted, 1100);
    const char* go = tr_action_ == 2 ? tr("Compresser") : tr("Lancer le transfert");
    const int gw = Surface::measure(go, body) + 80, gx = 1920 - margin - 24 - gw;
    const bool focus = tr_zone_ == 3;
    s.rect(gx, sy + 2, gw, 52, count && !tight ? color::accent2 : rgb(34, 52, 77), 26);
    if (focus) s.outline(gx - 4, sy - 2, gw + 8, 60, color::accent, 30, 3);
    s.text(font_, gx + 40, sy + 13, go, body, color::text);
    if (tr_zone_ == 1) draw_footer(s, {{0, tr("Cocher")}, {-1, tr("Droite : options")}, {-1, tr("Bas : bouton Lancer")}, {1, tr("Retour")}});
    else if (tr_zone_ == 2 && tr_opt_ == 0 && (tr_compress_ || tr_action_ == 2)) draw_footer(s, {{0, tr_action_ == 2 ? tr("Format") : tr("Choisir")}, {3, tr("Format")}, {1, tr("Retour")}});
    else draw_footer(s, {{0, tr_zone_ == 3 ? tr("Lancer") : tr("Choisir")}, {1, tr("Retour")}});
}

void Ui::draw_tr_progress(Surface& s) {
    std::vector<const Task*> list;
    int running = 0, waiting = 0;
    for (const auto& t : snap_->tasks) if (active(t.state)) { list.push_back(&t); if (t.state == "running") ++running; else ++waiting; }
    for (const auto& t : snap_->tasks) if (!active(t.state) && list.size() < 7) list.push_back(&t);
    s.text(font_, margin, 112, tr("Transferts en cours"), title, color::text);
    const std::string b = trf("%d en cours · %d en attente · ils continuent même si l'app est fermée", running, waiting);
    s.text(font_, margin, 178, b, body, color::muted);
    tr_task_ = std::min(tr_task_, std::max(0, static_cast<int>(list.size()) - 1));
    const int w = 1920 - 2 * margin, h = 106;
    if (list.empty()) {
        s.rect(margin, 250, w, 140, color::panel, 20);
        s.text(font_, margin + 32, 300, tr("Aucun transfert pour l'instant."), body, color::muted);
    }
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const Task& t = *list[static_cast<std::size_t>(i)];
        const int y = 240 + i * (h + 12);
        if (y + h > 990) break;
        const bool focus = tr_task_ == i;
        s.rect(margin, y, w, h, focus ? color::panel2 : color::panel, 18);
        s.outline(margin, y, w, h, focus ? color::accent : color::line, 18, focus ? 3 : 1);
        const Item* it = nullptr;
        for (const auto& x : snap_->items) if (x.key == t.item) it = &x;
        int tx = margin + 24;
        if (it) { cover(s, *it, tx, y + 13, h - 26, false); tx += h; }
        s.text(font_, tx, y + 14, t.title.empty() ? std::string(task_kind(t.type)) : t.title, body, color::text, 900);
        const bool ph = t.state == "running" && t.phase_fraction() >= 0;  // vérification : sa propre barre
        std::string sub = std::string(task_kind(t.type)) + " · " + (ph ? t.phase_text() : t.state == "running" && !t.step.empty() ? t.step : std::string(state_name(t.state)));
        if (!t.error.empty()) sub += " · " + t.error;
        s.text(font_, tx, y + 48, sub, small, t.state == "failed" ? color::bad : color::muted, 900);
        const int px = margin + w - 560, pw = 400;
        if (ph) {
            const double f = t.phase_fraction();
            s.rect(px, y + 30, pw, 10, rgb(255, 255, 255, 26), 5);
            s.rect(px, y + 30, std::max(10, static_cast<int>(pw * f)), 10, color::ok, 5);
            std::string line = t.phase_text() + " · " + size_text(static_cast<std::uint64_t>(f * static_cast<double>(t.phase_total))) + " / " + size_text(t.phase_total);
            if (t.files_total > 1) line += " · " + trf("%d/%d fichiers", static_cast<int>(t.files_done), static_cast<int>(t.files_total));
            s.text(font_, px, y + 54, line, small, color::muted, 540);
        } else if (t.total) {
            const double f = std::min(1.0, static_cast<double>(t.done) / static_cast<double>(t.total));
            s.rect(px, y + 30, pw, 10, rgb(255, 255, 255, 26), 5);
            s.rect(px, y + 30, std::max(10, static_cast<int>(pw * f)), 10, t.state == "done" ? color::ok : color::accent2, 5);
            char p[160];
            std::string tail;
            if (t.state == "running" && t.speed > 0) tail = trf(" · %s/s · reste %s", size_text(static_cast<std::uint64_t>(t.speed)).c_str(), duration_text(static_cast<double>(t.total - std::min(t.done, t.total)) / t.speed).c_str());
            std::snprintf(p, sizeof p, "%d %% · %s / %s%s", static_cast<int>(f * 100), size_text(t.done).c_str(), size_text(t.total).c_str(), tail.c_str());
            s.text(font_, px, y + 54, t.type == "install_job" ? install_job_line(t) : std::string(p), small, color::muted, 540);
        }
        if (t.state == "running") spinner(s, margin + w - 60, y + h / 2, 18);
    }
    {   // Triangle : « Annuler » sur un transfert en cours, « Retirer de la liste » sur un terminé ; Options : vider les terminés.
        const bool fin = tr_task_ < static_cast<int>(list.size()) && !active(list[static_cast<std::size_t>(tr_task_)]->state);
        int finished = 0;
        for (const auto& t : snap_->tasks) if (!active(t.state)) ++finished;
        const std::string clear = trf("Options : vider les terminés (%d)", finished);
        (void)clear;
        if (fin) draw_footer(s, {{-1, tr("Options : retirer, vider")}, {1, tr("Retour")}});
        else draw_footer(s, {{3, tr("Pause / reprendre")}, {-1, tr("Options : arrêter la tâche…")}, {1, tr("Retour")}});
    }
}

void Ui::draw_tr_full(Surface& s) {
    if (!tr_full_loading_ && (!tr_full_loaded_ || now_ - tr_full_loaded_ > (tr_full_info_["computing"].as_bool() || !tr_full_info_.is_object() ? 3000u : 60000u))) {
        tr_full_loading_ = true;
        tr_full_loaded_ = now_;
        model_.command("GET", "/api/v2/fullbackup", "", [this](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j)) j = Json::object();
            std::lock_guard<std::mutex> l(async_mu_);
            pending_full_info_ = std::move(j);
            has_pending_full_info_ = true;
        });
    }
    s.text(font_, margin, 112, tr("TRANSFERTS · SAUVEGARDE COMPLÈTE"), small, color::accent);
    s.text(font_, margin, 140, tr("Tout ce qui est à vous sur cette PS5"), title, color::text);
    s.text(font_, margin, 206, tr("Cochez ce que vous voulez garder. Après une remise à zéro, rouvrez l'app : tout revient dans le bon ordre."), body, color::muted);
    static const char* modes[] = {N_("Sauvegarder"), N_("Restaurer"), N_("Vers une autre PS5")};
    int mx = margin + 3 * 12;
    for (int i = 0; i < 3; ++i) mx += Surface::measure(tr(modes[i]), body) + 40;
    for (int i = 2; i >= 0; --i) {
        const int w = Surface::measure(tr(modes[i]), body) + 40;
        mx -= w;
        const bool on = tr_full_mode_ == i, focus = tr_zone_ == 0 && on;
        s.rect(mx, 268, w, 48, on ? color::text : color::panel2, 24);
        if (focus) s.outline(mx - 4, 264, w + 8, 56, color::accent, 28, 3);
        s.text(font_, mx + 20, 279, tr(modes[i]), body, on ? color::bg : color::text);
        mx -= 12;
    }
    const int gap = 18, cw = (1920 - 2 * margin - gap) / 2, ch = 96, y0 = 340;
    std::uint64_t total = 0;
    int checked = 0;
    for (int i = 0; i < full_count; ++i) {
        const FullCat& c = full_cats[i];
        const int x = margin + (i % 2) * (cw + gap), y = y0 + (i / 2) * (ch + 14);
        const bool on = !tr_full_off_.count(i), focus = tr_zone_ == 1 && tr_full_ == i;
        const Json& info = tr_full_info_["categories"][c.id];
        const bool known = info.is_object();
        if (on && known) { total += static_cast<std::uint64_t>(info["bytes"].as_int()); }
        if (on) ++checked;
        s.rect(x, y, cw, ch, focus ? color::panel2 : color::panel, 18);
        s.outline(x, y, cw, ch, focus ? color::accent : color::line, 18, focus ? 3 : 1);
        s.rect(x + 24, y + (ch - 34) / 2, 34, 34, on ? color::accent2 : rgb(34, 52, 77), 9);
        if (on) {
            for (int d = 0; d < 9; ++d) s.rect(x + 30 + d, y + ch / 2 + d / 2 - 2, 4, 4, color::text, 2);
            for (int d = 0; d < 13; ++d) s.rect(x + 36 + d, y + ch / 2 + 2 - d, 4, 4, color::text, 2);
        }
        s.text(font_, x + 84, y + 14, tr(c.name), large, color::text, cw - 220);
        const std::string detail = known && !info["detail"].as_string().empty() ? info["detail"].as_string() : std::string(tr(c.text));
        s.text(font_, x + 84, y + 56, detail, small, c.expert ? color::warn : color::muted, cw - 260);
        std::string sz = known ? size_text(static_cast<std::uint64_t>(info["bytes"].as_int())) : std::string("…");
        s.text(font_, x + cw - 28 - Surface::measure(sz, body), y + 32, sz, body, color::text);
        if (!known || tr_full_info_["computing"].as_bool()) spinner(s, x + cw - 120, y + ch / 2, 14);
    }
    // Barre du bas : résumé, destination, lancement.
    const auto devs = tr_devices();
    const std::string dest = tr_to_ < static_cast<int>(devs.size()) ? devs[static_cast<std::size_t>(tr_to_)].name : std::string("?");
    const int by = 788, bw = 1920 - 2 * margin, bh = 110;
    s.rect(margin, by, bw, bh, rgb(11, 26, 46), 20);
    const std::string b = checked > 1 ? trf("%d catégories · %s → %s / PBC Backup / Cette PS5", checked, total ? size_text(total).c_str() : tr("taille en calcul"), dest.c_str())
                                      : trf("%d catégorie · %s → %s / PBC Backup / Cette PS5", checked, total ? size_text(total).c_str() : tr("taille en calcul"), dest.c_str());
    s.text(font_, margin + 28, by + 22, b, body, color::text, bw - 700);
    s.text(font_, margin + 28, by + 62, tr("Une seule sauvegarde datée, vérifiée fichier par fichier."), small, color::muted, bw - 700);
    const char* go = tr_full_mode_ == 0 ? tr("Tout sauvegarder") : tr_full_mode_ == 1 ? tr("Restaurer") : tr("Transférer");
    const int gw = Surface::measure(go, body) + 80, gx = margin + bw - 28 - gw;
    const char* pick = tr("Destination…");
    const int dw = Surface::measure(pick, body) + 60, dx = gx - 20 - dw;
    s.rect(dx, by + 29, dw, 52, color::panel2, 26);
    if (tr_zone_ == 2 && tr_opt_ == 0) s.outline(dx - 4, by + 25, dw + 8, 60, color::accent, 30, 3);
    s.text(font_, dx + 30, by + 41, pick, body, color::text);
    s.rect(gx, by + 29, gw, 52, color::accent2, 26);
    if (tr_zone_ == 2 && tr_opt_ == 1) s.outline(gx - 4, by + 25, gw + 8, 60, color::accent, 30, 3);
    s.text(font_, gx + 40, by + 41, go, body, color::text);
    if (tr_zone_ == 1) draw_footer(s, {{0, tr("Cocher / décocher")}, {-1, tr("Bas : bouton Lancer")}, {1, tr("Retour")}});
    else draw_footer(s, {{0, tr("Choisir")}, {-1, tr("Gauche / droite")}, {1, tr("Retour")}});
}

// ---- Depuis un NAS : choisir des sauvegardes et les remettre sur la PS5 --------------------------

void Ui::draw_tr_restore(Surface& s, const TrDevice& from, const TrDevice& to) {
    load_tr_backups(from.id);
    const bool here = to.kind == "ps5";  // « Cette PS5 » : chaque fichier retourne à sa place d'origine
    s.text(font_, margin, 112, trf("TRANSFERTS · %s → %s · Restaurer", from.name.c_str(), to.name.c_str()), small, color::accent);
    s.text(font_, margin, 140, tr("Quoi remettre sur la PS5"), title, color::text);
    const auto rows = tr_backup_rows();
    const Json& list = tr_backups_["backups"];
    const int lx = margin, lw = 1130, ly = 230, rh = 92, vis = 7;
    tr_row_ = std::min(tr_row_, std::max(0, static_cast<int>(rows.size()) - 1));
    if (tr_row_ < tr_top_) tr_top_ = tr_row_;
    if (tr_row_ >= tr_top_ + vis) tr_top_ = tr_row_ - vis + 1;
    if (rows.empty()) {
        s.rect(lx, ly, lw, 180, color::panel, 20);
        if (tr_backups_loading_) { spinner(s, lx + 60, ly + 90, 22); s.text(font_, lx + 110, ly + 76, tr("Lecture des sauvegardes du NAS…"), body, color::muted); }
        else {
            const std::string err = tr_backups_["error"].as_string();
            s.text(font_, lx + 32, ly + 50, err.empty() ? tr("Aucune sauvegarde sur cet appareil pour l'instant.") : trf("NAS injoignable : %s", err.c_str()), body, err.empty() ? color::muted : color::bad, lw - 64);
            if (!err.empty()) s.text(font_, lx + 32, ly + 96, tr("Vérifiez qu'il est allumé et que la PS5 peut le joindre (réseau, pare-feu)."), small, color::muted, lw - 64);
        }
    }
    for (int r = tr_top_; r < static_cast<int>(rows.size()) && r < tr_top_ + vis; ++r) {
        const Json& b = list[rows[static_cast<std::size_t>(r)]];
        const int y = ly + (r - tr_top_) * (rh + 10);
        const bool focus = tr_zone_ == 1 && tr_row_ == r, on = tr_sel_.count("bk:" + b["bundle"].as_string()) > 0;
        s.rect(lx, y, lw, rh, focus ? color::panel2 : color::panel, 18);
        s.outline(lx, y, lw, rh, focus ? color::accent : on ? color::accent2 : color::line, 18, focus ? 3 : 1);
        s.rect(lx + 22, y + (rh - 32) / 2, 32, 32, on ? color::accent2 : rgb(34, 52, 77), 8);
        if (on) {
            for (int d = 0; d < 9; ++d) s.rect(lx + 28 + d, y + rh / 2 + d / 2 - 2, 4, 4, color::text, 2);
            for (int d = 0; d < 13; ++d) s.rect(lx + 34 + d, y + rh / 2 + 2 - d, 4, 4, color::text, 2);
        }
        const Item* it = nullptr;
        for (const auto& i : snap_->items) if (i.key == b["key"].as_string()) it = &i;
        int tx = lx + 74;
        if (it) { cover(s, *it, tx, y + 10, rh - 20, false); tx += rh; }
        s.text(font_, tx, y + 14, b["title"].as_string(), body, color::text, lw - (tx - lx) - 170);
        std::string parts;
        for (const auto& p : b["parts"].items()) { if (!parts.empty()) parts += ", "; parts += part_name(p.as_string()); }
        s.text(font_, tx, y + 50, b["created"].as_string().substr(0, 16) + " · " + parts, small, color::muted, lw - (tx - lx) - 170);
        const std::string sz = size_text(static_cast<std::uint64_t>(b["bytes"].as_int()));
        s.text(font_, lx + lw - 24 - Surface::measure(sz, body), y + 30, sz, body, color::text);
    }
    // À droite : où ça va, et ce qui se passe.
    const int ox = lx + lw + 40, ow = 1920 - margin - ox;
    s.text(font_, ox, ly - 34, tr("OÙ ÇA VA"), small, color::accent);
    s.rect(ox, ly, ow, 300, color::panel, 18);
    s.text(font_, ox + 22, ly + 20, here ? tr("À sa place d'origine") : tr("Dans un dossier du disque"), body, color::text, ow - 44);
    s.text(font_, ox + 22, ly + 60, here ? tr("Jeu, mises à jour, applis : chaque fichier revient où il était, inscription comprise. Rien n'est écrasé.")
                                         : trf("Copié dans « PBC Restaurés » sur %s : les fichiers, sans inscription du jeu.", to.name.c_str()),
           small, color::muted, ow - 44);
    s.text(font_, ox + 22, ly + 170, tr("Les sauvegardes de jeu (saves) se remettent sur un compte depuis l'onglet Parties."), small, color::warn, ow - 44);
    // Résumé et lancement
    std::uint64_t total = 0;
    int count = 0;
    for (const auto i : rows) if (tr_sel_.count("bk:" + list[i]["bundle"].as_string())) { total += static_cast<std::uint64_t>(list[i]["bytes"].as_int()); ++count; }
    const int sy = 922;
    s.rect(margin, sy - 12, 1920 - 2 * margin, 80, rgb(11, 26, 46), 18);
    const std::string b = count > 1 ? trf("%d sauvegardes · %s → %s", count, size_text(total).c_str(), to.name.c_str())
                                    : trf("%d sauvegarde · %s → %s", count, size_text(total).c_str(), to.name.c_str());
    s.text(font_, margin + 24, sy, b, body, color::text, 1100);
    s.text(font_, margin + 24, sy + 34, tr("Chaque fichier est vérifié à l'arrivée (empreinte)."), small, color::muted, 1100);
    const char* go = tr("Restaurer");
    const int gw = Surface::measure(go, body) + 80, gx = 1920 - margin - 24 - gw;
    s.rect(gx, sy + 2, gw, 52, count ? color::accent2 : rgb(34, 52, 77), 26);
    if (tr_zone_ == 3) s.outline(gx - 4, sy - 2, gw + 8, 60, color::accent, 30, 3);
    s.text(font_, gx + 40, sy + 13, go, body, color::text);
    draw_footer(s, {{0, tr_zone_ == 3 ? tr("Restaurer") : tr("Cocher")}, {-1, tr("Bas : bouton Lancer")}, {1, tr("Retour")}});
}

void Ui::input_tr_restore(std::uint32_t k) {
    const auto rows = tr_backup_rows();
    const int count = static_cast<int>(rows.size());
    if (k & key::circle) { if (tr_zone_ == 3) tr_zone_ = 1; else { tr_screen_ = 0; tr_zone_ = 2; } return; }
    if (tr_zone_ == 3) {
        if (k & key::up) tr_zone_ = 1;
        if (k & key::cross) tr_restore_launch();
        return;
    }
    tr_zone_ = 1;
    if (k & key::up) tr_row_ = std::max(0, tr_row_ - 1);
    if (k & key::down) { if (tr_row_ + 1 < count) ++tr_row_; else tr_zone_ = 3; }
    if ((k & (key::cross | key::square)) && tr_row_ < count) {
        const std::string key = "bk:" + tr_backups_["backups"][rows[static_cast<std::size_t>(tr_row_)]]["bundle"].as_string();
        if (!tr_sel_.erase(key)) tr_sel_.insert(key);
    }
}

void Ui::tr_restore_launch() {
    const auto devs = tr_devices();
    if (tr_from_ >= static_cast<int>(devs.size()) || tr_to_ >= static_cast<int>(devs.size())) return;
    const TrDevice& from = devs[static_cast<std::size_t>(tr_from_)];
    const TrDevice& to = devs[static_cast<std::size_t>(tr_to_)];
    const Json& list = tr_backups_["backups"];
    int queued = 0, saves_only = 0;
    for (const auto i : tr_backup_rows()) {
        const Json& b = list[i];
        if (!tr_sel_.count("bk:" + b["bundle"].as_string())) continue;
        std::string parts = "[";
        for (const auto& p : b["parts"].items()) {
            if (p.as_string() == "save") continue;  // les saves passent par un compte (onglet Sauvegardes)
            if (parts.size() > 1) parts += ",";
            parts += json_str(p.as_string());
        }
        parts += "]";
        if (parts == "[]") { ++saves_only; continue; }
        std::string target;
        if (to.kind != "ps5") {
            std::string mount = "/data";
            for (const auto& p : snap_->places) if (p.id == to.id) mount = p.type == "internal" ? std::string("/data") : p.mount;
            target = mount + "/PBC Restaurés/" + b["title"].as_string();
        }
        model_.command("POST", "/api/v2/tasks", "{\"type\":\"restore\",\"place\":" + json_str(from.id) + ",\"bundle\":" + json_str(b["bundle"].as_string()) +
                       ",\"parts\":" + parts + ",\"target\":" + json_str(target) + ",\"overwrite\":false}",
                       [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
        ++queued;
    }
    if (!queued && !saves_only) { toast(tr("Cochez au moins une sauvegarde.")); return; }
    tr_sel_.clear();
    tr_screen_ = 2;
    tr_task_ = 0;
    if (saves_only) toast(queued ? tr("Restaurations lancées ; les saves seules se remettent depuis l'onglet Parties.") : tr("Ce sont des saves : remettez-les sur un compte depuis l'onglet Parties."));
    else toast(queued > 1 ? tr("Restaurations ajoutées à la file") : tr("Restauration ajoutée à la file"));
}

// ---- Entrées --------------------------------------------------------------------------------

void Ui::tr_launch() {
    const auto devs = tr_devices();
    if (tr_to_ >= static_cast<int>(devs.size())) return;
    const TrDevice& to = devs[static_cast<std::size_t>(tr_to_)];
    std::vector<std::string> keys(tr_sel_.begin(), tr_sel_.end());
    if (keys.empty()) { toast(tr("Cochez au moins un élément.")); return; }
    static const char* modes[] = {"copy", "move"};
    for (const auto& key : keys) {
        std::string body;
        if ((to.kind == "ftp" || to.kind == "peer") && tr_action_ != 2)  // vers le NAS ou une autre PS5 : une sauvegarde vérifiée, rangée comme les autres
            body = "{\"type\":\"backup\",\"item\":" + json_str(key) + ",\"parts\":" + (tr_saves_ ? std::string("[]") : std::string("[\"game\",\"update\",\"dlc\",\"app\",\"state\",\"config\"]")) + ",\"place\":" + json_str(to.id) + "}";
        else if (tr_action_ == 2)
            body = "{\"type\":\"compress\",\"item\":" + json_str(key) + ",\"target\":" + json_str(to.id) + ",\"format\":\"" + (tr_format_ ? "pkg" : "ffpfsc") +
                   "\",\"pkg_level\":" + std::to_string(tr_pkg_level_) + "}";
        else
            body = "{\"type\":\"transfer\",\"mode\":\"" + std::string(modes[tr_action_ == 1 && !(tr_compress_ && tr_format_)]) + "\",\"item\":" + json_str(key) + ",\"to\":" + json_str(to.id) +
                   ",\"saves\":" + (tr_saves_ ? "true" : "false") + ",\"verify\":true,\"verify_mode\":" + std::string(tr_verify_ ? "\"full\"" : "\"quick\"") + ",\"compress\":" + (tr_compress_ ? "true" : "false") +
                   ",\"format\":\"" + (tr_format_ ? "pkg" : "ffpfsc") + "\",\"pkg_level\":" + std::to_string(tr_pkg_level_) + "}";
        model_.command("POST", "/api/v2/tasks", body, [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
    }
    tr_sel_.clear();
    tr_screen_ = 2;
    tr_task_ = 0;
    if (to.kind == "ftp" && tr_action_ == 1) { toast(tr("Vers le NAS : copie vérifiée, l'original reste sur la console.")); return; }
    if (to.kind == "peer") { toast(tr("Vers l'autre PS5 : copie vérifiée dans son « PBC Backup », l'original reste ici.")); return; }
    toast(keys.size() > 1 ? tr("Transferts ajoutés à la file") : tr("Transfert ajouté à la file"));
}

void Ui::input_transfers(std::uint32_t k) {
    const auto devs = tr_devices();
    const int n = static_cast<int>(devs.size());
    if (tr_screen_ == 4) { input_tr_sync(k); return; }
    if (tr_screen_ == 5) { input_tr_files(k); return; }
    if (tr_screen_ == 0) {
        if (k & key::triangle) { std::swap(tr_from_, tr_to_); return; }
        if ((k & key::square) && tr_zone_ <= 1) { open_add_service(); return; }  // Carré : ajouter un service
        if (tr_zone_ <= 1) {
            const int z = tr_zone_;
            int& v = z == 0 ? tr_from_ : tr_to_;
            const auto groups = tr_groups(devs);
            const int gn = static_cast<int>(groups.size());
            auto pick = [&](int dev) {  // un appareil choisi : on passe à la rangée suivante
                if (devs[static_cast<std::size_t>(dev)].kind == "add") { open_add_service(); return; }
                if (z == 1 && devs[static_cast<std::size_t>(dev)].kind == "share") { toast(tr("Partage en lecture seule : choisissez une autre destination.")); return; }
                // Même disque que la source : permis (Compresser écrit l'image à côté du jeu) ; refusé plus loin pour Copier / Déplacer.
                v = dev;
                tr_open_[z] = -1;
                tr_zone_ += 1;
                if (tr_zone_ == 1 && tr_to_ == tr_from_) tr_to_ = tr_from_ == 0 ? 1 : 0;
            };
            if (tr_open_[z] >= 0 && tr_open_[z] < gn) {  // bulle ouverte : ses éléments
                const auto& m = groups[static_cast<std::size_t>(tr_open_[z])].members;
                const int mn = static_cast<int>(m.size());
                if ((k & key::left) && tr_msel_[z] == 0) { tr_open_[z] = -1; tr_first_[z] = 0; return; }  // Gauche au début : on ressort de la bulle
                if (k & key::left) tr_msel_[z] = std::max(0, tr_msel_[z] - 1);
                if (k & key::right) tr_msel_[z] = std::min(mn - 1, tr_msel_[z] + 1);
                if (k & key::circle) { tr_open_[z] = -1; tr_first_[z] = 0; return; }
                if (k & key::cross) pick(m[static_cast<std::size_t>(std::min(tr_msel_[z], mn - 1))]);
                return;
            }
            if (k & key::left) tr_gsel_[z] = std::max(0, tr_gsel_[z] - 1);
            if (k & key::right) tr_gsel_[z] = std::min(std::max(0, gn - 1), tr_gsel_[z] + 1);
            if (k & key::cross && tr_gsel_[z] < gn) {
                const TrGroup& g = groups[static_cast<std::size_t>(tr_gsel_[z])];
                if (g.members.size() == 1) { pick(g.members[0]); return; }
                tr_open_[z] = tr_gsel_[z];
                tr_msel_[z] = 0;
                for (int i = 0; i < static_cast<int>(g.members.size()); ++i) if (g.members[static_cast<std::size_t>(i)] == v) tr_msel_[z] = i;
                tr_first_[z] = 0;
                return;
            }
            if (k & key::down) { tr_zone_ += 1; if (tr_zone_ == 1 && tr_to_ == tr_from_) tr_to_ = tr_from_ == 0 ? 1 : 0; }
            if ((k & (key::up | key::circle)) && z == 1) tr_zone_ = 0;
            return;
        }
        if (tr_zone_ == 2) {
            if (k & key::left) tr_action_ = std::max(0, tr_action_ - 1);
            if (k & key::right) tr_action_ = std::min(action_count - 1, tr_action_ + 1);
            if (k & (key::up | key::circle)) tr_zone_ = 1;
            if (k & key::down) { tr_zone_ = 3; tr_opt_ = std::clamp(tr_action_, 0, 3); }  // même colonne (bug vu le 19/11 : le curseur sautait à gauche)
            if (k & key::cross) {
                if (tr_from_ == tr_to_ && tr_action_ != 2) { toast(tr("Même disque : seulement pour Compresser (l'image est écrite à côté du jeu).")); return; }
                if (tr_from_ == tr_to_) toast(tr("Compresser sur le même disque : l'image est écrite à côté du jeu, vérifiée, et le jeu d'origine reste en place."));
                const TrDevice& f = devs[static_cast<std::size_t>(tr_from_)];
                const TrDevice& t = devs[static_cast<std::size_t>(tr_to_)];
                if (f.kind == "add" || t.kind == "add") { open_add_service(); return; }
                if (t.kind == "share") { toast(tr("Partage en lecture seule : choisissez une autre destination.")); tr_zone_ = 1; return; }
                if (tr_action_ == 3 || f.kind == "switch" || t.kind == "switch") {  // les Switch passent par l'écran Synchroniser
                    tr_screen_ = 4; sy_zone_ = 0; sy_card_ = 0; sy_a_ = 0; sy_b_ = static_cast<int>(switches_.size());  // B : Eden
                    return;
                }
                if (f.kind == "peer") { toast(tr("Depuis l'autre PS5 : ouvrez Backup Center sur elle et envoyez vers celle-ci.")); return; }
                if (t.kind == "peer" && tr_action_ == 2) { toast(tr("Compresser vers l'autre PS5 : bientôt. Copiez, puis compressez là-bas.")); return; }
                if (f.kind == "ftp") {  // depuis le NAS : ses sauvegardes, remises sur la PS5
                    if (t.kind == "ftp") { toast(tr("D'un NAS à l'autre : pas encore. Passez par un disque USB.")); return; }
                    if (tr_action_ != 0) { toast(tr("Depuis le NAS, on copie (restaure) : l'original reste sur le NAS.")); tr_action_ = 0; }
                    tr_screen_ = 1; tr_zone_ = 1; tr_row_ = tr_top_ = 0; tr_sel_.clear();
                    load_tr_backups(f.id);
                    return;
                }
                tr_screen_ = 1; tr_zone_ = 1; tr_row_ = tr_top_ = 0; tr_cat_ = tr_pill_ = 0; tr_sel_.clear();
            }
            return;
        }
        if (k & key::left) tr_opt_ = std::max(0, tr_opt_ - 1);
        if (k & key::right) tr_opt_ = std::min(3, tr_opt_ + 1);
        if (k & key::up) { tr_zone_ = 2; tr_action_ = std::clamp(tr_opt_, 0, action_count - 1); }  // remonte sur l'action du dessus
        if (k & key::circle) tr_zone_ = 2;
        if ((k & key::cross) && tr_opt_ == 2) { open_packages(); return; }  // Paquets .pkg
        if ((k & key::cross) && tr_opt_ == 1) {  // Fichiers
            tr_screen_ = 5; fl_step_ = 0; fl_zone_ = 0; fl_row_ = fl_top_ = 0; fl_sel_.clear();
            if (fl_place_[0] >= 0) fl_pcur_ = fl_place_[0];
            return;
        }
        if (k & key::cross) {
            tr_screen_ = tr_opt_ == 0 ? 3 : 2; tr_zone_ = 1; tr_full_ = 0; tr_task_ = 0;  // 0 sauvegarde complète, 3 en cours
            if (tr_screen_ == 3 && tr_to_ < n && (devs[static_cast<std::size_t>(tr_to_)].kind == "ps5" || devs[static_cast<std::size_t>(tr_to_)].kind == "internal" || devs[static_cast<std::size_t>(tr_to_)].kind == "switch"))
                for (int i = 0; i < n; ++i) if (devs[static_cast<std::size_t>(i)].kind == "usb" || devs[static_cast<std::size_t>(i)].kind == "ftp") { tr_to_ = i; break; }
        }
        return;
    }
    if (tr_screen_ == 1 && tr_from_ < n && devs[static_cast<std::size_t>(tr_from_)].kind == "ftp") { input_tr_restore(k); return; }
    if (tr_screen_ == 1) {
        const auto items = tr_items();
        const int count = static_cast<int>(items.size());
        if (tr_zone_ == 0) {
            const int last = tr_action_ == 2 ? 1 : cat_count;  // dernière pastille : « Tout cocher »
            if (k & key::left) tr_pill_ = std::max(0, tr_pill_ - 1);
            if (k & key::right) tr_pill_ = std::min(last, tr_pill_ + 1);
            if ((k & (key::left | key::right)) && tr_pill_ < last) { tr_cat_ = tr_pill_; tr_row_ = tr_top_ = 0; }
            if (k & key::cross) {
                if (tr_pill_ == last) for (const Item* it : tr_items()) tr_sel_.insert(it->key);
                else tr_zone_ = 1;
            }
            if (k & key::down) tr_zone_ = 1;
            if (k & key::circle) { tr_screen_ = 0; tr_zone_ = 2; }
            return;
        }
        if (tr_zone_ == 1) {
            if (k & key::up) { if (tr_row_ == 0) tr_zone_ = 0; else --tr_row_; }
            if (k & key::down) { if (tr_row_ + 1 < count) ++tr_row_; else tr_zone_ = 3; }
            if (k & key::right) { tr_zone_ = 2; tr_opt_ = 0; }
            if ((k & (key::cross | key::square)) && tr_row_ < count) {
                const std::string& key = items[static_cast<std::size_t>(tr_row_)]->key;
                if (!tr_sel_.erase(key)) tr_sel_.insert(key);
            }
            if (k & key::circle) { tr_screen_ = 0; tr_zone_ = 2; }
            return;
        }
        if (tr_zone_ == 2) {
            if (k & key::up) { if (tr_opt_ == 0) tr_zone_ = 0; else --tr_opt_; }
            if (k & key::down) { if (tr_opt_ < 2) ++tr_opt_; else tr_zone_ = 3; }
            if (k & (key::left | key::circle)) tr_zone_ = 1;
            if ((k & key::triangle) && tr_opt_ == 0 && (tr_compress_ || tr_action_ == 2)) { open_tr_format(); return; }  // le format
            if (k & key::cross) {
                if (tr_opt_ == 0) { if (tr_action_ == 2) open_tr_format(); else tr_compress_ = !tr_compress_; }
                else if (tr_opt_ == 1) tr_saves_ = !tr_saves_;
                else tr_verify_ = !tr_verify_;
            }
            return;
        }
        if (k & key::up) tr_zone_ = 1;
        if (k & key::circle) tr_zone_ = 1;
        if (k & key::cross) tr_launch();
        return;
    }
    if (tr_screen_ == 2) {
        std::vector<const Task*> list;
        for (const auto& t : snap_->tasks) if (active(t.state)) list.push_back(&t);
        for (const auto& t : snap_->tasks) if (!active(t.state) && list.size() < 7) list.push_back(&t);
        if (k & key::up) tr_task_ = std::max(0, tr_task_ - 1);
        if (k & key::down) tr_task_ = std::min(std::max(0, static_cast<int>(list.size()) - 1), tr_task_ + 1);
        if (k & key::circle) { tr_screen_ = 0; tr_zone_ = 3; return; }
        // Règle des touches : △ = pause / reprendre ; Options = menu (Arrêter la tâche… avec confirmation, Retirer, Vider).
        const Task* cur = tr_task_ < static_cast<int>(list.size()) ? list[static_cast<std::size_t>(tr_task_)] : nullptr;
        if (k & key::options) {
            int finished = 0;
            for (const auto& t : snap_->tasks) if (!active(t.state)) ++finished;
            std::vector<std::pair<std::string, std::function<void()>>> acts;
            if (cur && (cur->type == "files_copy" || cur->type == "files_move") && (cur->state == "cancelled" || cur->state == "failed")) {
                const Task t = *cur;
                acts.push_back({tr("Que faire de cette copie ?…"), [this, t] { files_task_menu(t); }});
            }
            if (cur && cur->state == "running" && cur->phase == "verify") { const Task t = *cur; acts.push_back({tr("Vérification…"), [this, t] { verify_menu(t); }}); }
            if (cur && active(cur->state)) {
                const std::string id = cur->id, name = cur->title;
                acts.push_back({tr("Arrêter la tâche…"), [this, id, name] {
                    open_actions(trf("Arrêter « %s » ?", name.c_str()), {
                        {tr("Oui, arrêter la tâche"), [this, id] { model_.command("POST", "/api/v2/tasks/" + id + "/cancel", "{}"); post_toast(tr("Tâche arrêtée")); }},
                        {tr("Non, continuer"), [] {}},
                    });
                    popup_note_ = tr("Ce qui est déjà copié reste vérifié ; la tâche pourra être relancée.");
                }});
            }
            if (cur && !active(cur->state)) { const std::string id = cur->id; acts.push_back({tr("Retirer de la liste"), [this, id] { model_.command("POST", "/api/v2/tasks/" + id + "/remove", "{}"); }}); }
            if (finished) acts.push_back({trf("Vider les terminés (%d)", finished), [this, finished] {
                open_actions(tr("Vider les transferts terminés ?"), {
                    {trf("Oui, retirer les %d terminés de la liste", finished), [this] {
                        for (const char* st : {"done", "failed", "cancelled"})
                            model_.command("POST", "/api/v2/tasks/clear", std::string("{\"state\":\"") + st + "\"}", [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
                        post_toast(tr("Transferts terminés retirés de la liste"));
                        tr_task_ = 0;
                    }},
                    {tr("Annuler"), [] {}},
                });
                popup_note_ = tr("Seulement la liste : rien n'est retiré des disques. Les transferts en cours ne sont pas touchés.");
            }});
            if (!acts.empty()) open_actions(cur ? cur->title : std::string(tr("Transferts")), std::move(acts));
            return;
        }
        if (cur && (k & key::triangle)) {
            const char* a = cur->state == "paused" ? "resume" : cur->state == "running" || cur->state == "queued" ? "pause" : nullptr;
            if (a) model_.command("POST", "/api/v2/tasks/" + cur->id + "/" + a, "{}");
        }
        if (cur && (k & key::square)) toast(tr("Pause : △ ; arrêter ou retirer : Options"));
        return;
    }
    // Sauvegarde complète
    if (tr_zone_ == 0) {
        if (k & key::left) tr_full_mode_ = std::max(0, tr_full_mode_ - 1);
        if (k & key::right) tr_full_mode_ = std::min(2, tr_full_mode_ + 1);
        if (k & (key::down | key::cross)) tr_zone_ = 1;
        if (k & key::circle) { tr_screen_ = 0; tr_zone_ = 3; }
        return;
    }
    if (tr_zone_ == 1) {
        if (k & key::left && tr_full_ % 2) --tr_full_;
        if (k & key::right && !(tr_full_ % 2)) ++tr_full_;
        if (k & key::up) { if (tr_full_ < 2) tr_zone_ = 0; else tr_full_ -= 2; }
        if (k & key::down) { if (tr_full_ + 2 < full_count) tr_full_ += 2; else { tr_zone_ = 2; tr_opt_ = 1; } }
        if (k & (key::cross | key::square)) { if (!tr_full_off_.erase(tr_full_)) tr_full_off_.insert(tr_full_); }
        if (k & key::circle) { tr_screen_ = 0; tr_zone_ = 3; }
        return;
    }
    if (k & key::left) tr_opt_ = 0;
    if (k & key::right) tr_opt_ = 1;
    if (k & key::up) tr_zone_ = 1;
    if (k & key::circle) { tr_zone_ = 1; return; }
    if (!(k & key::cross)) return;
    if (tr_opt_ == 0) {  // destination : la suivante (disques, NAS) ; la source « Cette PS5 » est exclue
        for (int step = 1; step <= n; ++step) {
            const int c = (tr_to_ + step) % std::max(1, n);
            const std::string& kd = devs[static_cast<std::size_t>(c)].kind;
            if (kd == "usb" || kd == "ftp" || kd == "peer") { tr_to_ = c; break; }  // disques, NAS, autre PS5
        }
        return;
    }
    if (tr_full_mode_ != 0) { toast(tr_full_mode_ == 1 ? tr("Restaurer : choisissez la sauvegarde complète dans l'onglet Sauvegardes (bientôt).") : tr("Vers une autre PS5 : jumelez-la (Appareils › PlayStation 5), puis choisissez un de ses disques comme destination.")); return; }
    if (tr_to_ == 0 || tr_to_ >= n) { toast(tr("Choisissez une destination (disque USB, NAS ou autre PS5).")); return; }
    std::string list = "[";
    for (int i = 0; i < full_count; ++i)
        if (!tr_full_off_.count(i)) list += (list.size() > 1 ? "," : "") + json_str(full_cats[i].id);
    list += "]";
    model_.command("POST", "/api/v2/tasks", "{\"type\":\"full_backup\",\"categories\":" + list + ",\"place\":" + json_str(devs[static_cast<std::size_t>(tr_to_)].id) + "}",
                   [this](const std::string& e, const std::string&) { post_toast(e.empty() ? tr("Sauvegarde complète lancée") : e); });
    tr_screen_ = 2;
    tr_task_ = 0;
}

// ---- Transferts › Synchroniser ---------------------------------------------------------------------------
// En haut, les synchros habituelles : chaque Switch avec Eden / ProsperoEden (cartes aux deux images et l'état).
// En dessous, « Nouvelle synchro » : côté A et côté B parmi les Switch, Eden, les NAS, les comptes PS5 et les
// disques USB ; seules les paires que le moteur sait comparer (Switch ⇄ Eden) sont actives, les autres « bientôt ».
std::vector<Ui::SySide> Ui::sy_sides() const {
    std::vector<SySide> out;
    for (const auto& sw : switches_) out.push_back({sw.second, "switch", sw.first, dev_switch});
    out.push_back({"Eden / ProsperoEden", "eden", "", dev_internal});
    for (const auto& a : overview_["accounts"].items()) if (a["on_console"].as_bool()) out.push_back({a["name"].as_string() + " (PS5)", "account", a["key"].as_string(), dev_internal});
    for (const auto& p : snap_->places) {
        if (p.type == "ftp") out.push_back({p.label, "nas", p.id, dev_nas});
        else if (p.type == "usb") out.push_back({p.label, "usb", p.id, dev_usb});
    }
    return out;
}

void Ui::open_switch_sync(const std::string& remote_id) {
    const int accounts = static_cast<int>(overview_["accounts"].size());
    for (std::size_t i = 0; i < switches_.size(); ++i)
        if (switches_[i].first == remote_id) { tab_ = parties; pt_player_ = accounts + static_cast<int>(i); pt_zone_ = 1; saves_row_ = 0; return; }
}

void Ui::draw_tr_sync(Surface& s) {
    s.text(font_, margin, 112, tr("Synchroniser"), title, color::text);
    s.text(font_, margin, 196, tr("Synchros habituelles"), large, color::text);
    const int cw = 560, ch = 170, gap = 24, cy = 256;
    const int n = static_cast<int>(switches_.size());
    sy_card_ = std::clamp(sy_card_, 0, std::max(0, n - 1));
    if (!n) s.text(font_, margin, cy + 40, tr("Aucune Switch ajoutée : ajoutez-la dans l'onglet Appareils."), body, color::muted);
    const int first = std::clamp(sy_card_ - 2, 0, std::max(0, n - 3));
    for (int i = first; i < n && i < first + 3; ++i) {
        const int x = margin + (i - first) * (cw + gap);
        const bool focus = sy_zone_ == 0 && i == sy_card_;
        card_frame(s, x, cy, cw, ch, focus);
        if (devices_[dev_switch].valid()) s.image(devices_[dev_switch], x + 16, cy + 16, 90, 90);
        s.text(font_, x + 118, cy + 40, "⇄", large, color::accent);
        if (devices_[dev_internal].valid()) s.image(devices_[dev_internal], x + 166, cy + 16, 90, 90);
        s.text(font_, x + 272, cy + 22, switches_[static_cast<std::size_t>(i)].second, body, color::text, cw - 290);
        s.text(font_, x + 272, cy + 58, "Eden / ProsperoEden", small, color::muted, cw - 290);
        // État : la dernière comparaison gardée, sinon « pas encore comparée ».
        const auto c = sync_cache_.find("switch:" + switches_[static_cast<std::size_t>(i)].first + "|eden:/data/prosperoeden");
        std::string state = tr("Pas encore comparée");
        std::uint32_t tone = color::muted;
        if (c != sync_cache_.end()) {
            int diff = 0;
            for (const auto& e : c->second["entries"].items()) { const std::string st = e["status"].as_string(); diff += st == "a_newer" || st == "b_newer" || st == "same_day"; }
            state = diff ? trf("%d sauvegarde(s) différente(s)", diff) : std::string(tr("À jour"));
            tone = diff ? color::warn : color::ok;
        }
        s.text(font_, x + 272, cy + 96, state, small, tone, cw - 290);
        s.text(font_, x + 16, cy + 128, tr("Comparer et synchroniser ›"), small, focus ? color::accent : color::muted, cw - 32);
    }
    // Nouvelle synchro : côté A, côté B.
    const auto sides = sy_sides();
    const int ns = static_cast<int>(sides.size());
    if (ns) { sy_a_ = std::clamp(sy_a_, 0, ns - 1); sy_b_ = std::clamp(sy_b_, 0, ns - 1); }
    const int ny = 480;
    s.text(font_, margin, ny, tr("Nouvelle synchro"), large, color::text);
    auto side_card = [&](int zone, int idx, int x, const char* label) {
        const bool focus = sy_zone_ == zone;
        s.rect(x, ny + 60, 700, 150, color::panel, 22);
        s.outline(x, ny + 60, 700, 150, focus ? color::accent : color::line, 22, focus ? 4 : 2);
        s.text(font_, x + 24, ny + 76, label, small, color::muted);
        if (idx < ns) {
            const SySide& sd = sides[static_cast<std::size_t>(idx)];
            if (devices_[sd.image].valid()) s.image(devices_[sd.image], x + 24, ny + 106, 90, 90);
            s.text(font_, x + 132, ny + 124, sd.name, large, color::text, 500);
        }
        if (focus) s.text(font_, x + 700 - 80, ny + 124, "‹  ›", large, color::accent);
    };
    side_card(1, sy_a_, margin, tr("Côté A"));
    s.text(font_, margin + 724, ny + 110, "⇄", title, color::accent);
    side_card(2, sy_b_, margin + 820, tr("Côté B"));
    const bool supported = sy_a_ < ns && sy_b_ < ns &&
        ((sides[static_cast<std::size_t>(sy_a_)].kind == "switch" && sides[static_cast<std::size_t>(sy_b_)].kind == "eden") ||
         (sides[static_cast<std::size_t>(sy_a_)].kind == "eden" && sides[static_cast<std::size_t>(sy_b_)].kind == "switch"));
    const std::string go = supported ? std::string(tr("Comparer")) : std::string(tr("Bientôt"));
    const int gw = Surface::measure(go, body) + 64, gy = ny + 236;
    s.rect(margin, gy, gw, 60, sy_zone_ == 3 ? color::text : supported ? rgb(61, 139, 255, 140) : rgb(255, 255, 255, 22), 30);
    s.text(font_, margin + 32, gy + 14, go, body, sy_zone_ == 3 ? color::bg : supported ? color::text : color::muted);
    if (!supported) s.text(font_, margin + gw + 24, gy + 18, tr("Pour l'instant : une Switch avec Eden / ProsperoEden. Les autres paires arrivent."), small, color::muted, 1920 - margin - (margin + gw + 24));
    if (sy_zone_ == 0) draw_footer(s, {{0, tr("Ouvrir")}, {-1, tr("Bas : nouvelle synchro")}, {1, tr("Retour")}});
    else draw_footer(s, {{0, tr("Valider")}, {-1, tr("Gauche / droite : changer")}, {1, tr("Retour")}});
}

void Ui::input_tr_sync(std::uint32_t k) {
    const auto sides = sy_sides();
    const int ns = static_cast<int>(sides.size()), n = static_cast<int>(switches_.size());
    if (k & key::circle) { tr_screen_ = 0; tr_zone_ = 2; return; }
    if (k & key::up) sy_zone_ = std::max(0, sy_zone_ - 1);
    if (k & key::down) sy_zone_ = std::min(3, sy_zone_ + 1);
    if (sy_zone_ == 0) {
        if (k & key::left) sy_card_ = std::max(0, sy_card_ - 1);
        if (k & key::right) sy_card_ = std::min(std::max(0, n - 1), sy_card_ + 1);
        if ((k & key::cross) && sy_card_ < n) open_switch_sync(switches_[static_cast<std::size_t>(sy_card_)].first);
        return;
    }
    if (sy_zone_ == 1 || sy_zone_ == 2) {
        int& v = sy_zone_ == 1 ? sy_a_ : sy_b_;
        if ((k & key::left) && ns) v = (v + ns - 1) % ns;
        if ((k & key::right) && ns) v = (v + 1) % ns;
        if (k & key::cross) sy_zone_ += 1;
        return;
    }
    if ((k & key::cross) && sy_a_ < ns && sy_b_ < ns) {
        const SySide &a = sides[static_cast<std::size_t>(sy_a_)], &b = sides[static_cast<std::size_t>(sy_b_)];
        if (a.kind == "switch" && b.kind == "eden") { open_switch_sync(a.id); return; }
        if (a.kind == "eden" && b.kind == "switch") { open_switch_sync(b.id); return; }
        toast(tr("Bientôt : cette paire n'est pas encore prise en charge."));
    }
}

// ---- Transferts › Fichiers : n'importe quels fichiers ou dossiers, d'un endroit à l'autre -------------------------------
// Étape 0 DEPUIS (cocher), étape 1 VERS (choisir le dossier), puis Copier ici / Déplacer ici. Le moteur fait le travail
// (tâche « files ») : jamais d'écrasement, chaque fichier vérifié, originaux retirés seulement après (Déplacer).
namespace {
const char* fl_kind(const std::string& k) {
    return k == "here" ? N_("Cette PS5") : k == "peer" ? N_("Autre PS5") : k == "nas" ? "NAS" : k == "switch" ? "Switch" : k == "share" ? N_("Partage") : "";
}
std::string fl_child(const std::string& p, const std::string& name) { return (p == "/" ? std::string() : p) + "/" + name; }
std::string fl_parent(const std::string& p) { const auto i = p.rfind('/'); return i == 0 || i == std::string::npos ? std::string("/") : p.substr(0, i); }
std::string fl_q(const std::string& v) {  // encodage d'adresse (paramètres)
    static const char* h = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : v) { if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/' || c == ':') o += static_cast<char>(c); else { o += '%'; o += h[c >> 4]; o += h[c & 15]; } }
    return o;
}
}  // namespace

void Ui::fl_open(int side, int place, const std::string& path) {
    if (place < 0 || place >= static_cast<int>(fl_places_.items().size())) return;
    const Json& p = fl_places_.items()[static_cast<std::size_t>(place)];
    fl_place_[side] = place;
    fl_path_[side] = path.empty() ? (p["root"].as_string().empty() ? std::string("/") : p["root"].as_string()) : path;
    fl_loading_[side] = true;
    fl_error_[side].clear();
    if (side == 0) fl_sel_.clear();
    fl_row_ = fl_top_ = 0;
    const std::string id = p["id"].as_string(), at = fl_path_[side];
    model_.command("GET", "/api/v2/files/list?place=" + fl_q(id) + "&path=" + fl_q(at), "", [this, side, at](const std::string& e, const std::string& body) {
        Json j;
        const bool ok = e.empty() && Json::parse(body, j);
        post_ui([this, side, at, ok, j, e] {
            if (fl_path_[side] != at) return;  // un autre dossier a été ouvert entre-temps
            fl_loading_[side] = false;
            if (!ok) { fl_entries_[side] = Json::array(); fl_error_[side] = e.empty() ? tr("Réponse illisible") : e; return; }
            fl_entries_[side] = j["entries"];
            fl_free_[side] = static_cast<std::uint64_t>(j["free"].as_int());
        });
    });
}

namespace {
// Un jeu qui s'installe (« Installer… » depuis Fichiers) : une image, un .pkg, ou un dossier.
bool installable_entry(const pbc::Json& e) {
    if (e["dir"].as_bool()) return true;
    std::string n = e["name"].as_string();
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* x : {".pkg", ".exfat", ".ffpkg", ".ffpfsc", ".ffpfs"}) {
        const std::size_t l = std::strlen(x);
        if (n.size() > l && n.compare(n.size() - l, l, x) == 0) return true;
    }
    return false;
}
}  // namespace

void Ui::draw_tr_files(Surface& s) {
    if (!fl_places_.is_array() && !fl_places_loading_) {
        fl_places_loading_ = true;
        model_.command("GET", "/api/v2/files/places", "", [this](const std::string& e, const std::string& body) {
            Json j;
            const bool ok = e.empty() && Json::parse(body, j) && j.is_array();
            post_ui([this, ok, j, e] {
                fl_places_loading_ = false;
                if (!ok) { post_toast(e.empty() ? std::string(tr("Réponse illisible")) : e); return; }
                fl_places_ = j;
                if (!fl_pending_place_.empty()) {  // lien reçu ouvert depuis Téléchargements
                    int k = 0;
                    for (const auto& p : j.items()) { if (p["id"].as_string() == fl_pending_place_) { fl_pending_place_.clear(); fl_open(0, k, ""); break; } ++k; }
                    fl_pending_place_.clear();
                }
                int here2 = -1, here1 = -1, i = 0;
                for (const auto& p : j.items()) { if (p["kind"].as_string() == "here") { if (here1 < 0) here1 = i; else if (here2 < 0) here2 = i; } ++i; }
                if (fl_place_[0] < 0 && here1 >= 0) fl_open(0, here1, "");
                if (fl_place_[1] < 0) { fl_place_[1] = here2 >= 0 ? here2 : here1; fl_path_[1].clear(); }
                fl_pcur_ = std::max(0, fl_place_[0]);
            });
        });
    }
    const int side = fl_step_;
    s.text(font_, margin, 112, tr("Fichiers"), title, color::text);
    s.text(font_, margin, 178, side == 0 ? tr("1 · DEPUIS  →  2 · VERS  →  Copier ou déplacer") : tr("1 · DEPUIS ✓  →  2 · VERS  →  Copier ou déplacer"), body, color::muted);
    const auto& places = fl_places_.items();
    // Colonne des endroits.
    const int lx = margin, lw = 470, top = 240;
    int y = top;
    for (int i = 0; i < static_cast<int>(places.size()) && y < 900; ++i) {
        const Json& p = places[static_cast<std::size_t>(i)];
        const bool focus = fl_zone_ == 0 && fl_pcur_ == i, chosen = fl_place_[side] == i;
        s.rect(lx, y, lw, 84, chosen ? color::panel2 : color::panel, 18);
        s.outline(lx, y, lw, 84, focus ? color::accent : chosen ? color::accent2 : color::line, 18, focus ? 4 : chosen ? 3 : 1);
        s.text(font_, lx + 20, y + 12, tr(fl_kind(p["kind"].as_string())), small, color::accent, lw - 40);
        s.text(font_, lx + 20, y + 40, p["label"].as_string(), body, color::text, lw - 40);
        y += 96;
    }
    if (places.empty()) s.text(font_, lx, top, tr("Chargement…"), body, color::muted);
    // Dossier ouvert.
    const int rx = lx + lw + 40, rw = 1920 - margin - rx;
    s.text(font_, rx, top, fl_path_[side], body, color::muted, rw);
    const auto& ents = fl_entries_[side].items();
    const int extra = side == 1 || !fl_sel_.empty() ? 1 : 0;  // VERS : « Nouveau dossier » ; DEPUIS : « Suivant »
    const int count = 1 + static_cast<int>(ents.size()) + extra, rh = 58, vis = 11;
    if (fl_row_ < fl_top_) fl_top_ = fl_row_;
    if (fl_row_ >= fl_top_ + vis) fl_top_ = fl_row_ - vis + 1;
    if (fl_loading_[side]) s.text(font_, rx, top + 50, tr("Chargement…"), body, color::muted);
    else if (!fl_error_[side].empty()) s.text(font_, rx, top + 50, fl_error_[side], body, color::warn, rw);
    else for (int r = fl_top_; r < std::min(count, fl_top_ + vis); ++r) {
        const int ry = top + 44 + (r - fl_top_) * (rh + 6);
        const bool focus = fl_zone_ == 1 && fl_row_ == r;
        s.rect(rx, ry, rw, rh, focus ? color::panel2 : color::panel, 14);
        if (focus) s.outline(rx - 3, ry - 3, rw + 6, rh + 6, color::accent, 16, 3);
        std::string name, size_s;
        bool dir = true, checked = false, special = false;
        if (r == 0) { name = std::string("‹  ") + tr("Dossier parent"); special = true; }
        else if (r == count - 1 && extra) { name = side == 1 ? std::string("+  ") + tr("Nouveau dossier…") : std::string(tr("Suivant : choisir la destination")) + "  →"; special = true; }
        else {
            const Json& e = ents[static_cast<std::size_t>(r - 1)];
            name = e["name"].as_string(); dir = e["dir"].as_bool();
            if (!dir) size_s = size_text(static_cast<std::uint64_t>(e["size"].as_int()));
            checked = side == 0 && fl_sel_.count(name);
        }
        int tx = rx + 20;
        if (side == 0 && !special) {
            s.rect(tx, ry + 17, 24, 24, checked ? color::accent2 : color::panel2, 6);
            s.outline(tx, ry + 17, 24, 24, checked ? color::accent2 : color::muted, 6, 2);
            if (checked) s.text(font_, tx + 4, ry + 14, "✓", body, color::text);
            tx += 40;
        }
        if (!special && dir) { s.rect(tx, ry + 18, 28, 22, color::warn, 4); tx += 42; }
        else if (!special) { s.rect(tx + 4, ry + 15, 20, 28, rgb(43, 74, 117), 3); tx += 42; }
        s.text(font_, tx, ry + 13, name, body, special ? color::accent : color::text, rx + rw - tx - 180);
        if (!size_s.empty()) s.text(font_, rx + rw - 20 - Surface::measure(size_s, small), ry + 18, size_s, small, color::muted);
    }
    // Résumé sous la liste.
    std::string info;
    if (side == 0) {
        std::uint64_t bytes = 0;
        for (const auto& e : ents) if (fl_sel_.count(e["name"].as_string()) && !e["dir"].as_bool()) bytes += static_cast<std::uint64_t>(e["size"].as_int());
        info = fl_sel_.empty() ? std::string(tr("Cochez des fichiers ou des dossiers")) : trf("%d élément(s)", static_cast<int>(fl_sel_.size())) + (bytes ? " · " + size_text(bytes) : std::string());
    } else {
        info = trf("Destination : %s", fl_path_[1].c_str());
        if (fl_free_[1]) info += " · " + trf("%s libres", size_text(fl_free_[1]).c_str());
    }
    s.text(font_, rx, 952, info, body, color::accent, rw);
    if (fl_zone_ == 0) draw_footer(s, {{0, tr("Ouvrir cet endroit")}, {1, side == 0 ? tr("Retour") : tr("Retour : DEPUIS")}});
    else if (side == 0) {
        const auto& fe = fl_entries_[0].items();  // la ligne sous le curseur : un jeu qui s'installe → Carré : Installer…
        const bool inst = fl_row_ >= 1 && fl_row_ - 1 < static_cast<int>(fe.size()) && installable_entry(fe[static_cast<std::size_t>(fl_row_ - 1)]);
        draw_footer(s, {{0, tr("Ouvrir le dossier")}, {-1, tr("L2 : cocher")}, {2, inst ? tr("Installer…") : ""}, {3, fl_sel_.empty() ? "" : tr("Choisir la destination")},
                        {-1, fl_sel_.empty() ? "" : tr("Options : supprimer…")}, {1, tr("Dossier parent")}});
    }
    else draw_footer(s, {{0, tr("Ouvrir le dossier")}, {3, tr("Choisir ce dossier")}, {1, tr("Dossier parent")}});
}


// « Installer… » d'un jeu vu dans Fichiers, où qu'il soit (la source : NAS, nuage, autre PS5, disque d'ici) : la cible est
// une PS5 — celle-ci ou une autre jumelée — et un de ses disques ; jamais un NAS (docs/maquettes/installer-depuis-partout.html).
// « Installer… » (maquettes installer-depuis-partout.html et installer-etapes.html) : la PS5 et le disque cibles, puis
// le format quand il y a le choix (l'image, le .pkg rangé à côté — monté ou par la console —, ou l'image convertie en
// .pkg), chacun avec ses étapes et son temps estimé ; ce qui n'est pas possible est grisé avec la raison.
void Ui::open_install_from(const Json& src, const std::string& path, const std::string& name, std::uint64_t bytes, bool dir, const Json& alt, const std::string& title) {
    std::string low = name;
    for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const bool pkg = !dir && low.size() > 4 && low.compare(low.size() - 4, 4, ".pkg") == 0;
    const std::string sid = src["id"].as_string(), src_label = src["label"].as_string(), platform = src["platform"].as_string();
    // L'image et le .pkg de ce jeu (le second rangé à côté, s'il y en a un).
    std::string img_path, pkg_path;
    std::uint64_t img_size = 0, pkg_size = 0;
    if (pkg) { pkg_path = path; pkg_size = bytes; } else { img_path = path; img_size = bytes; }
    if (alt.is_object() && !alt["path"].as_string().empty()) {
        if (pkg && !alt["dir"].as_bool() && alt["container"].as_string() != "pkg") { img_path = alt["path"].as_string(); img_size = static_cast<std::uint64_t>(alt["size"].as_int()); }
        if (!pkg && alt["container"].as_string() == "pkg") { pkg_path = alt["path"].as_string(); pkg_size = static_cast<std::uint64_t>(alt["size"].as_int()); }
    }
    const std::string shown = title.empty() ? name : title;
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    for (const auto& p : fl_places_.items()) {
        const std::string kind = p["kind"].as_string(), pid = p["id"].as_string();
        if ((kind != "here" && kind != "peer") || p["readonly"].as_bool() || pid == sid) continue;
        const std::string label = kind == "peer" ? p["label"].as_string() : trf("cette PS5 · %s", p["label"].as_string().c_str());
        const std::uint64_t free = static_cast<std::uint64_t>(p["free"].as_int());
        const std::string room = free ? trf("%s libres", size_text(free).c_str()) : std::string();
        acts.push_back({trf("L'installer sur %s", label.c_str()) + (room.empty() ? "" : " · " + room), [this, sid, pid, label, free, platform, img_path, img_size, pkg_path, pkg_size, dir, shown] {
            Json q = Json::object();
            q.set("src_place", sid); q.set("dst_place", pid); q.set("free", static_cast<std::int64_t>(free)); q.set("platform", platform);
            if (!img_path.empty()) { q.set("image_path", img_path); q.set("image_size", static_cast<std::int64_t>(img_size)); q.set("image_dir", dir); }
            if (!pkg_path.empty()) { q.set("pkg_path", pkg_path); q.set("pkg_size", static_cast<std::int64_t>(pkg_size)); }
            model_.command("POST", "/api/v2/install/options", q.dump(), [this, sid, pid, label, img_path, img_size, pkg_path, pkg_size, dir, shown](const std::string& e, const std::string& body) {
                Json r;
                if (!e.empty() || !Json::parse(body, r)) { post_toast(e.empty() ? std::string(tr("Réponse illisible")) : e); return; }
                post_ui([this, r, sid, pid, label, img_path, img_size, pkg_path, pkg_size, dir, shown] { open_install_formats(r, sid, pid, label, img_path, img_size, pkg_path, pkg_size, dir, shown); });
            });
        }});
    }
    if (acts.empty()) { toast(tr("Aucune PS5 ni disque où l'installer")); return; }
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(trf("Installer « %s » · depuis %s", shown.c_str(), src_label.c_str()), std::move(acts));
    popup_note_ = tr("Copie vérifiée vers le disque choisi, puis montée ou installée. Jamais pendant une partie ; la source reste là où elle est.");
}

// « fake PKG PS5 », « PKG officiel PS5 », « PKG PS4 » : ce que dit l'en-tête d'un .pkg (vide s'il n'a pas été lu).
static std::string pkg_desc(const Json& p) {
    if (!p.is_object() || p["platform"].as_string().empty()) return "";
    std::string plat = p["platform"].as_string();
    for (auto& c : plat) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const std::int64_t f = p.has("fake") ? p["fake"].as_int() : -1;
    return (f == 1 ? std::string("fake PKG") : f == 0 ? std::string(tr("PKG officiel")) : std::string("PKG")) + " " + plat;
}

// Le format, avec ses étapes et son temps estimé (« Installer l'image · 3 étapes · ~22 min »).
void Ui::open_install_formats(const Json& r, const std::string& sid, const std::string& pid, const std::string& label, const std::string& img_path, std::uint64_t img_size,
                              const std::string& pkg_path, std::uint64_t pkg_size, bool dir, const std::string& shown) {
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    // Les .pkg nommés par ce que dit leur en-tête (fake PKG PS5, PKG PS4…) et par leur fichier : on sait lequel on prend.
    const std::string pd = pkg_desc(r["pkg"]);
    const std::string pkg_file = pkg_path.substr(pkg_path.find_last_of('/') + 1);
    for (const auto& o : r["options"].items()) {
        const std::string as = o["as"].as_string();
        std::string name;
        if (as == "image") name = dir ? std::string(tr("Installer le dossier")) : trf("Installer l'image (.%s)", img_path.substr(img_path.find_last_of('.') + 1).c_str());
        else if (as == "pkg") name = pd.empty() ? std::string(tr("Lancer depuis le .pkg avec Kraken")) : trf("Lancer depuis le .pkg (%s) avec Kraken", pd.c_str());
        else if (as == "console") name = pd.empty() ? std::string(tr("Installer le .pkg par la console")) : trf("Installer le .pkg (%s) par la console", pd.c_str());
        else if (as == "convert") name = tr("Convertir en .pkg puis l'installer");
        else continue;
        const int n = static_cast<int>(o["steps"].size());
        std::string sub = o["ok"].as_bool() ? trf("%d étapes · ~%s", n, duration_text(static_cast<double>(o["eta"].as_int())).c_str())
                                           : std::string(tr("impossible")) + " : " + tr(o["why"].as_string().c_str());
        if (o["ok"].as_bool() && as == "convert") sub += " · " + trf("%s libres nécessaires pendant la conversion", size_text(static_cast<std::uint64_t>(o["need"].as_int())).c_str());
        if ((as == "pkg" || as == "console") && !pkg_file.empty()) sub += " · " + pkg_file;
        if (!o["note"].as_string().empty()) sub += " · " + std::string(tr(o["note"].as_string().c_str()));
        const bool ok = o["ok"].as_bool();
        const std::string why = o["why"].as_string();
        const std::string src_path = as == "image" || as == "convert" ? img_path : pkg_path;
        const std::uint64_t size = as == "image" || as == "convert" ? img_size : pkg_size;
        acts.push_back({name + " · " + sub, [this, ok, why, as, sid, pid, src_path, size, dir, shown] {
            if (!ok) { toast(tr(why.c_str())); return; }
            Json b = Json::object();
            b.set("src_place", sid); b.set("path", src_path); b.set("dir", dir && as == "image"); b.set("dst_place", pid);
            b.set("how", as == "console" ? "console" : "mount"); b.set("as", as); b.set("size", static_cast<std::int64_t>(size)); b.set("title", shown);
            model_.command("POST", "/api/v2/install/from", b.dump(), [this](const std::string& e, const std::string&) {
                post_toast(e.empty() ? tr("Installation lancée : suivez ses étapes dans Transferts en cours") : e);
            });
        }});
    }
    if (acts.empty()) { toast(tr("Ce format ne s'installe pas")); return; }
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(trf("%s → %s", shown.c_str(), label.c_str()), std::move(acts));
    popup_note_ = tr("Le temps est estimé d'après les vitesses mesurées les fois d'avant ; il se précise pendant. On peut tout arrêter à n'importe quelle étape : ce qui a été copié est retiré, la source n'est jamais touchée.");
}

// Textes venus du moteur (raisons, notes d'une installation), traduits à l'affichage.
[[maybe_unused]] const char* install_engine_texts[] = {N_("kstuff absent"), N_("pas assez de place"), N_("conversion : seulement sur cette PS5 pour l'instant"), N_("l'autre PS5 ne répond pas"), N_("l'autre PS5 a une version trop ancienne pour les .pkg"), N_("ShadowMountPlus l'inscrit dès que l'application est fermée"),
    N_("mise à jour : elle s'installe par la console, une fois le jeu installé"), N_("contenu additionnel : il s'installe par la console, une fois le jeu installé"),
    N_("jeu PS4 : Kraken ne lance que des .pkg PS5"), N_("paquet officiel de Sony : Kraken ne lance que des fake PKG"), N_("jeu PS4 : la conversion fabrique un .pkg PS5"),
    N_("paquet officiel : il lui faut sa licence pour se lancer")};

// Une installation par étapes, en une ligne : « Étape 2 sur 4 · Conversion en .pkg · 43 % · reste ~31 min ».
std::string install_job_line(const Task& t) {
    const Json& rep = t.report;
    const Json& steps = rep["steps"];
    const int n = static_cast<int>(steps.size()), cur = static_cast<int>(rep["cur"].as_int());
    if (!n) return "";
    const std::string key = steps[static_cast<std::size_t>(std::clamp(cur, 0, n - 1))]["key"].as_string(), as = rep["as"].as_string();
    std::string label = key == "copy" ? trf("Copie depuis %s", rep["source"].as_string().c_str()) : key == "verify" ? std::string(tr("Vérification"))
                      : key == "convert" ? std::string(tr("Conversion en .pkg")) : key == "cleanup" ? std::string(tr("Rangement"))
                      : as == "console" ? std::string(tr("Installation par la console")) : as == "image" ? std::string(tr("Montage (ShadowMount)")) : std::string(tr("Installation (Kraken)"));
    if (t.state == "done") return rep["note"].as_string().empty() ? std::string(tr("Installé")) + " · " + rep["target"].as_string() : std::string(tr(rep["note"].as_string().c_str()));
    std::string line = trf("Étape %d sur %d", cur + 1, n) + " · " + label;
    if (t.total) line += " · " + trf("%d %%", static_cast<int>(std::min<std::uint64_t>(100, t.done * 100 / t.total)));
    if (t.speed > 0) line += " · " + size_text(static_cast<std::uint64_t>(t.speed)) + "/s";
    if (rep["eta"].as_int() > 0 && t.state == "running") line += " · " + std::string(tr("reste")) + " ~" + duration_text(static_cast<double>(rep["eta"].as_int()));
    return line;
}

void Ui::input_tr_files(std::uint32_t k) {
    const int side = fl_step_;
    const int np = static_cast<int>(fl_places_.items().size());
    if (fl_zone_ == 0) {
        if (k & key::up) fl_pcur_ = std::max(0, fl_pcur_ - 1);
        if (k & key::down) fl_pcur_ = std::min(std::max(0, np - 1), fl_pcur_ + 1);
        if (k & (key::cross | key::right)) {
            if (fl_pcur_ != fl_place_[side] || fl_entries_[side].items().empty() || fl_path_[side].empty()) fl_open(side, fl_pcur_, "");
            fl_zone_ = 1;
        }
        if (k & key::circle) {
            if (side == 1) { fl_step_ = 0; fl_pcur_ = std::max(0, fl_place_[0]); }
            else if (fl_from_downloads_) { fl_from_downloads_ = false; tr_screen_ = 0; tab_ = downloads; }  // ouvert depuis Téléchargements : on y revient
            else { tr_screen_ = 0; tr_zone_ = 3; tr_opt_ = 1; }
        }
        return;
    }
    const auto& ents = fl_entries_[side].items();
    const int extra = side == 1 || !fl_sel_.empty() ? 1 : 0, count = 1 + static_cast<int>(ents.size()) + extra;
    if (k & key::up) fl_row_ = std::max(0, fl_row_ - 1);
    if (k & key::down) fl_row_ = std::min(count - 1, fl_row_ + 1);
    // Règle des touches : ○ (ou ←) = dossier parent ; à la racine de l'endroit, retour à la liste des endroits.
    if (k & (key::circle | key::left)) {
        const auto& pl = fl_places_.items();
        std::string root = fl_place_[side] >= 0 && fl_place_[side] < static_cast<int>(pl.size()) ? pl[static_cast<std::size_t>(fl_place_[side])]["root"].as_string() : std::string();
        if (root.empty()) root = "/";
        if (fl_path_[side] == root || fl_path_[side] == "/" || fl_path_[side].empty()) {
            // Ouvert depuis Téléchargements (un lien reçu) : à la racine du partage, ○ ramène à Téléchargements, au même endroit.
            if (fl_from_downloads_ && side == 0 && (k & key::circle)) { fl_from_downloads_ = false; tr_screen_ = 0; tab_ = downloads; return; }
            fl_zone_ = 0; fl_pcur_ = std::max(0, fl_place_[side]);
        }
        else fl_open(side, fl_place_[side], fl_parent(fl_path_[side]));
        return;
    }
    const bool parent_row = fl_row_ == 0, mk_row = extra && fl_row_ == count - 1;
    const Json* e = !parent_row && !mk_row && fl_row_ - 1 < static_cast<int>(ents.size()) ? &ents[static_cast<std::size_t>(fl_row_ - 1)] : nullptr;
    if (side == 0) {
        if ((k & key::cross) && mk_row) {  // « Suivant : choisir la destination »
            fl_step_ = 1; fl_zone_ = 1;
            if (fl_place_[1] < 0) fl_place_[1] = fl_place_[0];
            fl_open(1, fl_place_[1], fl_path_[1]);
            return;
        }
        // ✕ ouvre un dossier (un fichier : le coche) ; L2 coche ; △ passe à la destination.
        if (k & key::cross) {
            if (parent_row) fl_open(0, fl_place_[0], fl_parent(fl_path_[0]));
            else if (e && (*e)["dir"].as_bool()) fl_open(0, fl_place_[0], fl_child(fl_path_[0], (*e)["name"].as_string()));
            else if (e) { const std::string n = (*e)["name"].as_string(); if (fl_sel_.count(n)) fl_sel_.erase(n); else fl_sel_.insert(n); }
        }
        if ((k & key::l2) && e) { const std::string n = (*e)["name"].as_string(); if (fl_sel_.count(n)) fl_sel_.erase(n); else fl_sel_.insert(n); }
        if ((k & key::square) && e && installable_entry(*e) && fl_place_[0] >= 0) {  // Carré : Installer… (le jeu sous le curseur)
            const Json& sp = fl_places_.items()[static_cast<std::size_t>(fl_place_[0])];
            open_install_from(sp, fl_child(fl_path_[0], (*e)["name"].as_string()), (*e)["name"].as_string(), static_cast<std::uint64_t>((*e)["size"].as_int()), (*e)["dir"].as_bool());
            return;
        }
        if ((k & key::options) && !fl_sel_.empty()) { files_delete_flow(); return; }
        if ((k & key::options) && fl_place_[0] >= 0) {  // rien de coché : ce dossier (ou celui sous le curseur) devient une bibliothèque de jeux
            const Json& sp = fl_places_.items()[static_cast<std::size_t>(fl_place_[0])];
            const std::string dir = e && (*e)["dir"].as_bool() ? fl_child(fl_path_[0], (*e)["name"].as_string()) : fl_path_[0];
            if (sp["kind"].as_string() == "switch" || dir.empty()) return;
            const std::string pid = sp["id"].as_string(), label = sp["label"].as_string();
            open_actions(label + " · " + dir, {
                {tr("Utiliser comme bibliothèque de jeux"), [this, pid, dir] {
                    Json b = Json::object(); b.set("place", pid); b.set("path", dir);
                    model_.command("POST", "/api/v2/gamelibs/add", b.dump(), [this](const std::string& e2, const std::string&) {
                        post_toast(e2.empty() ? tr("Bibliothèque de jeux ajoutée : ses jeux apparaissent bientôt dans « À installer »") : e2);
                    });
                    gamelib_at_ = 0;
                }},
                {tr("Annuler"), [] {}}});
            popup_note_ = tr("Ses jeux (images, .pkg, dossiers de jeu) sont lus en fond et montrés dans l'Accueil et la Bibliothèque, prêts à installer. Rien n'y est modifié.");
            return;
        }
        if ((k & (key::r2 | key::triangle)) && !fl_sel_.empty()) {
            fl_step_ = 1; fl_zone_ = 1;
            if (fl_place_[1] < 0) fl_place_[1] = fl_place_[0];
            fl_open(1, fl_place_[1], fl_path_[1]);
        }
        return;
    }
    if (k & key::cross) {
        if (parent_row) fl_open(1, fl_place_[1], fl_parent(fl_path_[1]));
        else if (mk_row) {
            open_keyboard(tr("Nom du nouveau dossier"), "", [this](const std::string& name) {
                if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) { toast(tr("Nom de dossier invalide")); return; }
                const std::string path = fl_child(fl_path_[1], name), id = fl_places_.items()[static_cast<std::size_t>(fl_place_[1])]["id"].as_string();
                Json b = Json::object();
                b.set("place", id);
                b.set("path", path);
                model_.command("POST", "/api/v2/files/mkdir", b.dump(), [this, path](const std::string& err, const std::string&) {
                    post_ui([this, path, err] { if (!err.empty()) toast(err); else fl_open(1, fl_place_[1], path); });
                });
            });
        } else if (e && (*e)["dir"].as_bool()) fl_open(1, fl_place_[1], fl_child(fl_path_[1], (*e)["name"].as_string()));
    }
    if (k & key::triangle) {  // choisir ce dossier : Copier ici / Déplacer ici
        const auto& pl = fl_places_.items();
        if (fl_place_[0] < 0 || fl_place_[1] < 0 || fl_sel_.empty()) return;
        const Json& sp = pl[static_cast<std::size_t>(fl_place_[0])];
        const Json& dp = pl[static_cast<std::size_t>(fl_place_[1])];
        Json body = Json::object();
        body.set("src_place", sp["id"]); body.set("src_dir", fl_path_[0]);
        body.set("dst_place", dp["id"]); body.set("dst_dir", fl_path_[1]);
        Json names = Json::array();
        for (const auto& n : fl_sel_) names.push(n);
        body.set("names", names);
        // Depuis un lien reçu (lecture seule) : un téléchargement, suivi dans Téléchargements ; jamais « Déplacer ».
        if (sp["readonly"].as_bool()) {
            open_actions(trf("%d élément(s) → %s", static_cast<int>(fl_sel_.size()), fl_path_[1].c_str()), {
                {tr("Télécharger ici"), [this, body] {
                    Json b = body;
                    b.set("move", false);
                    model_.command("POST", "/api/v2/files/copy", b.dump(), [this](const std::string& err, const std::string&) {
                        post_toast(err.empty() ? std::string(tr("Téléchargement lancé : suivez-le dans Téléchargements")) : err);
                    });
                    fl_sel_.clear(); fl_step_ = 0; tr_screen_ = 0; tab_ = downloads; dl_zone_ = 1; dl_row_ = 0;
                }},
                {tr("Annuler"), [] {}},
            });
            popup_note_ = tr("Pause et reprise sans repartir de zéro. Chaque fichier est vérifié ; rien n'est jamais écrasé.");
            return;
        }
        auto launch = [this, body](bool move) {
            Json b = body;
            b.set("move", move);
            model_.command("POST", "/api/v2/files/copy", b.dump(), [this, move](const std::string& err, const std::string&) {
                post_toast(err.empty() ? std::string(move ? tr("Déplacement lancé : suivez-le dans Transferts en cours") : tr("Copie lancée : suivez-la dans Transferts en cours")) : err);
            });
            fl_sel_.clear(); fl_step_ = 0; tr_screen_ = 2; tr_zone_ = 1; tr_task_ = 0;
        };
        open_actions(trf("%d élément(s) → %s", static_cast<int>(fl_sel_.size()), fl_path_[1].c_str()), {
            {tr("Copier ici"), [launch] { launch(false); }},
            {tr("Déplacer ici"), [launch] { launch(true); }},
            {tr("Annuler"), [] {}},
        });
        popup_note_ = std::string(tr("Rien n'est jamais écrasé : un fichier identique est sauté, un fichier différent est gardé à côté « (2) ». Chaque fichier est vérifié.")) + " " +
                      tr("Déplacer : les originaux sont retirés seulement après une copie vérifiée de tout.");
    }
}

// Copie « Fichiers » annulée ou en échec : retirer ce qu'elle a créé (deux confirmations), ou la retirer de la liste.
// Transfert en vérification : arrêter seulement la vérification (copie gardée) ou annuler tout le transfert.
void Ui::verify_menu(const Task& t) {
    const std::string id = t.id;
    open_actions(tr("Vérification en cours"), {
        {tr("Arrêter la vérification (garder la copie)"), [this, id] {
            model_.command("POST", "/api/v2/tasks/" + id + "/skip-verify", "{}", [this](const std::string& e, const std::string&) {
                post_toast(e.empty() ? std::string(tr("Vérification arrêtée : la copie est gardée, marquée non vérifiée")) : e);
            });
        }},
        {tr("Annuler le transfert (la copie est retirée)"), [this, id] { model_.command("POST", "/api/v2/tasks/" + id + "/cancel", "{}"); }},
        {tr("Fermer"), [] {}},
    });
    popup_note_ = tr("Pause et redémarrage ne perdent rien : le transfert reprend là où il s'est arrêté. Un déplacement non vérifié garde l'original.");
}

void Ui::files_task_menu(const Task& t) {
    const std::string id = t.id;
    const int copied = static_cast<int>(t.report["copied"].as_int());
    open_actions(tr("Copie interrompue"), {
        {tr("Retirer les fichiers copiés…"), [this, id, copied] {
            open_actions(tr("Retirer les fichiers que cette copie a créés ?"), {
                {tr("Oui, retirer ces fichiers"), [this, id] {
                    model_.command("POST", "/api/v2/tasks/" + id + "/cleanup", "{}", [this](const std::string& e, const std::string&) {
                        post_toast(e.empty() ? std::string(tr("Retrait en cours…")) : e);
                    });
                }},
                {tr("Annuler"), [] {}},
            });
            popup_note_ = (copied > 0 ? trf("%d fichier(s) créé(s) par cette copie.", copied) + " " : std::string()) +
                          tr("Seulement ce que cette copie a créé : un fichier qui était déjà là n'est jamais touché, ni rien d'autre.");
        }},
        {tr("Retirer de la liste"), [this, id] { model_.command("POST", "/api/v2/tasks/" + id + "/remove", "{}"); }},
        {tr("Fermer"), [] {}},
    });
    popup_note_ = tr("Les fichiers déjà copiés sont restés à destination.");
}

// « Supprimer » (Fichiers, côté DEPUIS) : plusieurs validations. 1) la liste de ce qui part, « Continuer » ;
// 2) maintenir Carré 5 s. Le moteur exige aussi sa propre confirmation et refuse les endroits protégés.
void Ui::files_delete_flow() {
    const auto& pl = fl_places_.items();
    if (fl_place_[0] < 0 || static_cast<std::size_t>(fl_place_[0]) >= pl.size() || fl_sel_.empty()) return;
    const Json& place = pl[static_cast<std::size_t>(fl_place_[0])];
    if (place["kind"].as_string() == "peer") { toast(tr("Suppression sur l'autre PS5 : pas encore possible")); return; }
    if (place["readonly"].as_bool()) { toast(tr("Partage en lecture seule")); return; }
    const std::string place_id = place["id"].as_string(), dir = fl_path_[0];
    std::vector<std::string> names(fl_sel_.begin(), fl_sel_.end());
    std::uint64_t bytes = 0;
    int folders = 0;
    for (const auto& e : fl_entries_[0].items())
        if (fl_sel_.count(e["name"].as_string())) { if (e["dir"].as_bool()) ++folders; else bytes += static_cast<std::uint64_t>(e["size"].as_int()); }
    std::string list;
    for (std::size_t i = 0; i < names.size() && i < 6; ++i) list += (list.empty() ? "" : "\n") + std::string("• ") + names[i];
    if (names.size() > 6) list += "\n" + trf("… et %d autre(s)", static_cast<int>(names.size() - 6));
    std::string note = place["label"].as_string() + " · " + dir + "\n" + list + "\n";
    if (bytes) note += trf("Fichiers : %s", size_text(bytes).c_str()) + (folders ? " · " : "");
    if (folders) note += trf("%d dossier(s), avec tout leur contenu", folders);
    note += "\n" + std::string(tr("Définitif : pas de corbeille."));
    const int n = static_cast<int>(names.size());
    open_actions(trf("Supprimer %d élément(s) ?", n), {
        {tr("Continuer"), [this, place_id, dir, names, n] {
            open_hold_confirm(trf("Supprimer définitivement %d élément(s) ?", n),
                              tr("Ils ne pourront plus être remis."),
                              [this, place_id, dir, names] {
                Json b = Json::object();
                b.set("place", place_id);
                b.set("dir", dir);
                Json a = Json::array();
                for (const auto& x : names) a.push(x);
                b.set("names", a);
                b.set("confirm", "SUPPRIMER");
                model_.command("POST", "/api/v2/files/delete", b.dump(), [this](const std::string& err, const std::string&) {
                    post_ui([this, err] {
                        if (!err.empty()) { toast(err); return; }
                        fl_sel_.clear();
                        toast(tr("Suppression lancée : suivez-la dans Transferts en cours"));
                    });
                });
            });
        }},
        {tr("Annuler"), [] {}},
    });
    popup_note_ = note;
}

// « Désinstaller » : 1) ce qui va se passer, garder (par défaut) ou supprimer l'image ;
// 2) Carré maintenu 5 s. Le travail et les vérifications (jeu fermé, parties sauvegardées d'abord) se font dans le moteur
// de la console qui a le jeu — ici ou sur une PS5 jumelée.
void Ui::uninstall_flow(const std::string& inst, const std::string& console_name, const std::string& title_id, const std::string& title, const std::string& detail) {
    // D'abord, demander au moteur de la console qui a le jeu comment il se désinstalle (ShadowMountPlus ou la console)
    // et ce qui peut partir avec (son image, son dossier, son .pkg — ou rien pour un jeu installé par la console).
    const std::string url = inst.empty() ? "/api/v2/games/uninstall-info?title_id=" + url_encode(title_id)
                                         : "/api/v2/link/peer-uninstall-info?instance=" + url_encode(inst) + "&title_id=" + url_encode(title_id);
    model_.command("GET", url, "", [this, inst, console_name, title_id, title, detail](const std::string& e, const std::string& body) {
        Json info;
        if (!e.empty() || !Json::parse(body, info)) info = Json::object();  // inconnu : comme avant (une image)
        post_ui([this, inst, console_name, title_id, title, detail, info] { uninstall_menu(inst, console_name, title_id, title, detail, info); });
    });
}

void Ui::uninstall_menu(const std::string& inst, const std::string& console_name, const std::string& title_id, const std::string& title, const std::string& detail, const Json& info) {
    const std::string kind = info.has("kind") ? info["kind"].as_string() : std::string("image");
    const bool by_console = info["by"].as_string() == "console";
    auto go = [this, inst, console_name, title_id, title, kind](bool delete_source) {
        const std::string q = !delete_source ? trf("Désinstaller %s ?", title.c_str()) : kind == "pkg" ? trf("Désinstaller %s et supprimer son .pkg ?", title.c_str())
                            : kind == "folder" ? trf("Désinstaller %s et supprimer son dossier ?", title.c_str()) : trf("Désinstaller %s et supprimer son image ?", title.c_str());
        open_hold_confirm(q, tr("Le jeu est retiré de la console ; vos parties restent."),
                          [this, inst, console_name, title_id, title, delete_source] {
            Json b = Json::object();
            if (!inst.empty()) b.set("instance", inst);
            b.set("title_id", title_id);
            b.set("delete_source", delete_source);
            b.set("confirm", "DESINSTALLER");
            model_.command("POST", inst.empty() ? "/api/v2/games/uninstall" : "/api/v2/link/peer-uninstall", b.dump(),
                           [this, console_name, title](const std::string& err, const std::string&) {
                post_toast(!err.empty() ? err : console_name.empty() ? trf("Désinstallation de %s lancée : suivez-la dans Tâches", title.c_str())
                                                                     : trf("%s : désinstallation de %s lancée", console_name.c_str(), title.c_str()));
            });
        }, tr("Maintenez Carré 5 secondes pour confirmer"));
    };
    std::string note = std::string(tr("Vos parties sont sauvegardées d'abord (copie restaurable) et restent sur la console.")) + "\n" +
                       (kind == "pkg" ? tr("Lancé depuis son .pkg (Kraken) : la console le désinstalle elle-même.")
                        : by_console ? tr("Installé par la console : elle le désinstalle elle-même ; rien d'autre à supprimer.")
                                     : tr("Le jeu est retiré de ShadowMountPlus puis désinstallé."));
    if (!detail.empty()) note += "\n" + detail;
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    if (kind == "pkg") {
        acts.push_back({tr("Désinstaller et garder le .pkg"), [go] { go(false); }});
        acts.push_back({tr("Désinstaller et supprimer le .pkg"), [go] { go(true); }});
    } else if (kind == "folder") {
        acts.push_back({tr("Désinstaller et garder le dossier"), [go] { go(false); }});
        acts.push_back({tr("Désinstaller et supprimer le dossier"), [go] { go(true); }});
    } else if (kind == "image") {
        acts.push_back({tr("Désinstaller et garder l'image"), [go] { go(false); }});
        acts.push_back({tr("Désinstaller et supprimer l'image"), [go] { go(true); }});
    } else {
        acts.push_back({tr("Désinstaller"), [go] { go(false); }});
    }
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(console_name.empty() ? trf("Désinstaller %s de cette PS5 ?", title.c_str()) : trf("Désinstaller %s de %s ?", title.c_str(), console_name.c_str()), std::move(acts));
    popup_note_ = note;
}

// Autre PS5 : la liste de ses jeux et applis installés, puis le même parcours (le travail se fait là-bas).
void Ui::peer_uninstall_list(const std::string& inst, const std::string& name) {
    model_.command("GET", "/api/v2/link/peer?instance=" + inst + "&what=items", "", [this, inst, name](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Lecture impossible")) : e); return; }
        post_ui([this, inst, name, j] {
            std::vector<std::pair<std::string, std::function<void()>>> rows;
            const Json& list = j.is_array() ? j : j["items"];
            for (const auto& it : list.items()) {
                const std::string kind = it["kind"].as_string(), key = it["key"].as_string(), tid = it["title_id"].as_string(), title = it["title"].as_string();
                if ((kind != "game_ps5" && kind != "game_ps4" && kind != "app") || key.rfind("save:", 0) == 0 || tid.size() != 9 || tid == "PPSA99950") continue;
                if (tid.rfind("PPSA", 0) != 0 && tid.rfind("CUSA", 0) != 0) continue;
                const std::string detail = it["image_type"].as_string();
                rows.push_back({title + (detail.empty() ? std::string() : "  ·  " + detail), [this, inst, name, tid, title, detail] { uninstall_flow(inst, name, tid, title, detail); }});
            }
            if (rows.empty()) { toast(tr("Aucun jeu à désinstaller là-bas")); return; }
            rows.push_back({tr("Fermer"), [] {}});
            open_actions(trf("Désinstaller un jeu sur %s", name.c_str()), std::move(rows));
        });
    });
}

// ---- Bibliothèques de jeux non installés (maquette docs/maquettes/bibliotheque-nas.html) -------------------------
// L'index du moteur (dossiers déclarés : NAS, nuages, USB, l'autre PS5), relu toutes les 60 s seulement sur l'Accueil
// et la Bibliothèque : une liste déjà prête, rien n'est lu sur le réseau depuis l'écran.
void Ui::load_gamelib() {
    if (gamelib_at_ && now_ - gamelib_at_ < 60000) return;
    gamelib_at_ = now_ ? now_ : 1;
    model_.command("GET", "/api/v2/gamelibs/games", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j) || !j.is_object()) return;
        post_ui([this, j] { gamelib_ = j; });
    });
}

// Un par jeu (le premier trouvé), absents de cette PS5 (sauvegarde seule comprise), dans l'ordre des titres.
std::vector<std::size_t> Ui::gamelib_missing() const {
    std::vector<std::size_t> out;
    if (!gamelib_.is_object()) return out;
    std::set<std::string> here, seen;
    for (const auto& it : snap_->items) if (!it.title_id.empty() && it.key.rfind("save:", 0) != 0) here.insert(it.title_id);
    const Json& games = gamelib_["games"];
    for (std::size_t i = 0; i < games.size(); ++i) {
        const std::string tid = games[i]["title_id"].as_string();
        const std::string k = tid.empty() ? games[i]["path"].as_string() : tid;
        if ((!tid.empty() && here.count(tid)) || !seen.insert(k).second) continue;
        out.push_back(i);
    }
    auto low = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    std::sort(out.begin(), out.end(), [&games, &low](std::size_t a, std::size_t b) { return low(games[a]["title"].as_string()) < low(games[b]["title"].as_string()); });
    return out;
}

// Un jeu d'une bibliothèque : d'où il vient, son format, et « Installer… » (la PS5 et le disque cibles).
void Ui::open_gamelib_game(const Json& g) {
    if (!fl_places_.is_array()) {  // les cibles possibles : celles de Fichiers
        model_.command("GET", "/api/v2/files/places", "", [this, g](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j) || !j.is_array()) { post_toast(e.empty() ? std::string(tr("Réponse illisible")) : e); return; }
            post_ui([this, j, g] { if (!fl_places_.is_array()) fl_places_ = j; open_gamelib_game(g); });
        });
        return;
    }
    const std::string path = g["path"].as_string();
    const auto slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    Json src = Json::object();
    src.set("id", g["place"]); src.set("label", g["place_label"]); src.set("platform", g["platform"]);
    const std::uint64_t bytes = static_cast<std::uint64_t>(g["size"].as_int());
    const bool dir = g["dir"].as_bool();
    Json alt;  // le .pkg (ou l'image) rangé à côté : l'autre format proposé
    for (const auto& v : g["variants"].items())
        if ((v["container"].as_string() == "pkg") != (g["container"].as_string() == "pkg") && !v["dir"].as_bool()) { alt = v; break; }
    std::string on_peer;
    for (const auto& p : peer_games_.items())
        for (const auto& pg : p["games"].items()) if (pg["title_id"].as_string() == g["title_id"].as_string()) on_peer = p["name"].as_string();
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    acts.push_back({tr("Installer…"), [this, src, path, name, bytes, dir, alt, title = g["title"].as_string()] { open_install_from(src, path, name, bytes, dir, alt, title); }});
    // Ses mises à jour et ses DLC en .pkg : installés par la console, une fois le jeu installé (même parcours : la PS5, puis le format).
    auto extra = [&](const Json& x, const std::string& label, const std::string& shown) {
        const std::string xp = x["path"].as_string(), xn = xp.substr(xp.find_last_of('/') + 1);
        const std::uint64_t xs = static_cast<std::uint64_t>(x["size"].as_int());
        acts.push_back({label, [this, src, xp, xn, xs, shown] { open_install_from(src, xp, xn, xs, false, Json(), shown); }});
    };
    for (const auto& u : g["updates"].items()) {
        const std::string v = u["version"].as_string();
        extra(u, v.empty() ? std::string(tr("Installer la mise à jour…")) : trf("Installer la mise à jour %s…", v.c_str()),
              g["title"].as_string() + " · " + (v.empty() ? std::string(tr("mise à jour")) : trf("mise à jour %s", v.c_str())));
    }
    for (const auto& d : g["dlcs"].items()) {
        std::string dn = d["path"].as_string();
        dn = dn.substr(dn.find_last_of('/') + 1);
        if (dn.size() > 4) dn.resize(dn.size() - 4);
        extra(d, trf("Installer le DLC « %s »…", dn.c_str()), g["title"].as_string() + " · DLC " + dn);
    }
    acts.push_back({tr("Fermer"), [] {}});
    open_actions(g["title"].as_string(), std::move(acts));
    const std::string main_pd = g["container"].as_string() == "pkg" ? pkg_desc(g["pkg"]) : "";
    std::string note = trf("Sur %s", g["place_label"].as_string().c_str()) + " · " + (dir ? std::string(tr("dossier")) : !main_pd.empty() ? main_pd : "." + g["container"].as_string());
    if (bytes) note += " · " + size_text(bytes);
    if (!g["platform"].as_string().empty()) { std::string pl = g["platform"].as_string(); for (auto& c : pl) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); note += " · " + pl; }
    if (!g["version"].as_string().empty()) note += " · " + trf("Version %s", g["version"].as_string().c_str());
    if (!g["title_id"].as_string().empty()) note += " · " + g["title_id"].as_string();
    // L'autre format, les mises à jour et les DLC en .pkg (jamais pris pour le jeu), un backport.
    if (alt.is_object()) { const std::string d = alt["container"].as_string() == "pkg" ? pkg_desc(alt["pkg"]) : ""; note += " · " + trf("aussi en %s", (d.empty() ? "." + alt["container"].as_string() : d).c_str()); }
    const std::size_t nu = g["updates"].size(), nd = g["dlcs"].size();
    if (nu == 1) note += " · " + (g["updates"][static_cast<std::size_t>(0)]["version"].as_string().empty() ? std::string(tr("une mise à jour en .pkg")) : trf("mise à jour %s en .pkg", g["updates"][static_cast<std::size_t>(0)]["version"].as_string().c_str()));
    else if (nu > 1) note += " · " + trf("%d mises à jour en .pkg", static_cast<int>(nu));
    if (nd) note += " · " + trf("%d DLC en .pkg", static_cast<int>(nd));
    if (g["backport"].as_bool()) note += " · " + (g["backport_fw"].as_string().empty() ? std::string(tr("backport disponible")) : trf("backport disponible (%s)", g["backport_fw"].as_string().c_str()));
    if (!on_peer.empty()) note += " · " + trf("déjà sur %s", on_peer.c_str());
    popup_note_ = note + "\n" + path;
}

// Paramètres › Bibliothèques de jeux : les dossiers déclarés ; Relire, Retirer. On en ajoute depuis Fichiers.
void Ui::open_gamelibs() {
    model_.command("GET", "/api/v2/gamelibs", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Réponse illisible")) : e); return; }
        post_ui([this, j] {
            std::vector<std::pair<std::string, std::function<void()>>> acts;
            for (const auto& l : j["libs"].items()) {
                const std::string place = l["place"].as_string(), path = l["path"].as_string();
                const std::string label = l["label"].as_string().empty() ? std::string(tr("appareil absent")) : l["label"].as_string();
                const std::string state = l["scanning"].as_bool() ? std::string(tr("lecture…")) : l["error"].as_string() == "lecture incomplète" ? std::string(tr("lecture incomplète")) : !l["error"].as_string().empty() ? std::string(tr("injoignable"))
                                        : l["scanned_at"].as_int() ? trf("%d jeu(x)", static_cast<int>(l["games"].as_int())) : std::string(tr("pas encore lu"));
                acts.push_back({label + " · " + path + " · " + state, [this, place, path, label] {
                    open_actions(label + " · " + path, {
                        {tr("Relire maintenant"), [this, place, path] {
                            Json b = Json::object(); b.set("place", place); b.set("path", path);
                            model_.command("POST", "/api/v2/gamelibs/rescan", b.dump(), [this](const std::string& e2, const std::string&) { post_toast(e2.empty() ? tr("Relecture demandée") : e2); });
                            gamelib_at_ = 0;
                        }},
                        {tr("Retirer cette bibliothèque"), [this, place, path] {
                            Json b = Json::object(); b.set("place", place); b.set("path", path);
                            model_.command("POST", "/api/v2/gamelibs/remove", b.dump(), [this](const std::string& e2, const std::string&) { post_toast(e2.empty() ? tr("Bibliothèque retirée (rien n'est effacé)") : e2); });
                            gamelib_at_ = 0;
                        }},
                        {tr("Annuler"), [] {}}});
                }});
            }
            acts.push_back({tr("Ajouter un dossier…"), [this] { tab_ = transfers; tr_screen_ = 5; fl_step_ = 0; fl_zone_ = 0; fl_row_ = fl_top_ = 0; fl_sel_.clear(); toast(tr("Fichiers : ouvrez le dossier des jeux, puis Options › « Utiliser comme bibliothèque de jeux »")); }});
            acts.push_back({tr("Fermer"), [] {}});
            open_actions(tr("Bibliothèques de jeux"), std::move(acts));
            popup_note_ = tr("Des dossiers où sont rangés des jeux pas forcément installés (NAS, nuage, disque USB, l'autre PS5) : lus en fond, montrés dans l'Accueil et la Bibliothèque (« À installer »). Rien n'y est jamais modifié.");
        });
    });
}

}  // namespace app
