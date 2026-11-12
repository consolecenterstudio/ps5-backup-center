// Onglet Transferts : d'un appareil à l'autre (PS5, disques, Switch, NAS).
// Écran principal : DEPUIS / VERS / QUE FAIRE ? ; puis choix des éléments et options, suivi des
// transferts, et sauvegarde complète de la console (cases à cocher par catégorie).
// Règle : un jeu en cours d'utilisation n'est jamais touché — le moteur refuse et la tâche attend.
#include <algorithm>
#include <cstdio>

#include "ui.hpp"
#include "ui_util.hpp"

namespace app {

using pbc::Json;
using namespace ui_util;

std::string Task::phase_text() const {
    const double f = phase_fraction();
    if (f < 0) return "";
    const int pct = static_cast<int>(f * 100);
    return phase == "verify" ? trf("Vérification %d %%", pct) : trf("Comparaison avec la copie déjà là %d %%", pct);
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
    for (const auto& r : remotes_all_.items()) {
        const std::string t = r["type"].as_string();
        const std::string host = r["config"]["host"].as_string();
        v.push_back({r["id"].as_string(), r["name"].as_string(), (t == "switch" ? "Switch · " + host : trf("Réseau · %s", host.c_str())), t,
                     t == "switch" ? dev_switch : dev_nas, 0, false});
    }
    // Autres PS5 jumelées : chacun de leurs disques est une destination (sauvegarde vérifiée, comme vers le NAS).
    for (const auto& p : snap_->places) {
        if (p.type != "peer") continue;
        v.push_back({p.id, p.label, trf("Autre PS5 · %s libres", size_text(p.free).c_str()), "peer", dev_internal, p.free, true});
    }
    // Dernière carte : ajouter un NAS (ou un serveur FTP) sans quitter Transferts. Il apparaît ensuite ici.
    v.push_back({"add", tr("Ajouter un NAS"), tr("Synology, QNAP, FTP…"), "add", dev_nas, 0, false});
    return v;
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

void Ui::tr_device_card(Surface& s, const TrDevice& d, int x, int y, int w, int h, bool focus, bool chosen) {
    if (focus) for (int g = 3; g >= 1; --g) s.rect(x - g * 4, y - g * 4, w + g * 8, h + g * 8, rgb(80, 150, 255, 20), 20 + g * 4);
    s.rect(x, y, w, h, chosen ? color::panel2 : color::panel, 20);
    s.outline(x, y, w, h, focus ? color::accent : chosen ? color::accent2 : color::line, 20, focus ? 4 : chosen ? 3 : 1);
    const int img = h - 24;
    if (devices_[d.image].valid()) s.image(devices_[d.image], x + 12, y + 12, img, img);
    const int tx = x + img + 24, tw = w - img - 36;
    s.text(font_, tx, y + 12, d.name, body, color::text, tw);
    if (h < 100) {  // carte compacte : une seule ligne sous le nom (place libre, sinon description)
        s.text(font_, tx, y + 46, d.has_free || d.free ? trf("%s libres · %s", size_text(d.free).c_str(), d.sub.c_str()) : d.sub, small, chosen ? color::accent : color::muted, tw);
        return;
    }
    s.text(font_, tx, y + 50, d.sub, small, color::muted, tw);
    if (d.has_free || d.free) s.text(font_, tx, y + 76, trf("%s libres", size_text(d.free).c_str()), small, chosen ? color::accent : color::muted, tw);
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
             {"uncompress_exfat", tr("Décompresser en image .exfat")}, {"read_only", tr("Lecture seule (ShadowMount)")}, {"speed", tr("Test de vitesse de lecture")},
             {"transfer", tr("Copier ou déplacer vers un autre disque…")}};
    } else if (st == "image") {
        a = {{"validate", tr("Vérifier l'image")}, {"extract", tr("Extraire en dossier")}};
        if (ct != "ffpkg") a.push_back({"compress", tr("Compresser en .ffpfsc")});
        a.push_back({"speed", tr("Test de vitesse de lecture")});
        a.push_back({"transfer", tr("Copier ou déplacer vers un autre disque…")});
    } else {
        a = {{"compress", tr("Compresser en .ffpfsc")}, {"make_image", tr("Créer une image .exfat")}, {"ampr", tr("Construire l'index AMPR")},
             {"speed", tr("Test de vitesse de lecture")}, {"transfer", tr("Copier ou déplacer vers un autre disque…")}};
    }
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
        else if (id == "extract") popup_subs_.push_back(tr("recrée le dossier du jeu à côté"));
        else if (id == "compress") popup_subs_.push_back(tr("prend moins de place, se lance tel quel"));
        else if (id == "make_image") popup_subs_.push_back(tr("regroupe le dossier en une seule image"));
        else if (id == "ampr") popup_subs_.push_back(tr("pour les jeux APR-EMU : prépare leur index"));
        else popup_subs_.push_back("");
    }
}

void Ui::run_image_tool(const std::string& a) {
    if (a.empty()) return;
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
    s.text(font_, margin, 112, tr("D'un appareil à l'autre"), title, color::text);
    s.text(font_, margin, 178, tr("Jeux, applications, ROM : copier, déplacer ou compresser, entre la PS5, ses disques, les Switch et le NAS."), body, color::muted);
    const auto devs = tr_devices();
    const int n = static_cast<int>(devs.size());
    tr_from_ = std::min(tr_from_, std::max(0, n - 1));
    tr_to_ = std::min(tr_to_, std::max(0, n - 1));
    // Rangées DEPUIS / VERS : tous les appareils visibles (à la ligne, 5 par ligne), comme sur le web.
    const int gap = 20, per = 5, cw = (1920 - 2 * margin - (per - 1) * gap) / per, ch = 84, lgap = 10;
    const int lines = std::max(1, (n + per - 1) / per), rows_h = lines * ch + (lines - 1) * lgap;
    auto row = [&](const char* label, int y, int zone, int chosen) {
        s.text(font_, margin, y, label, small, color::accent);
        for (int i = 0; i < n; ++i) {
            const int x = margin + (i % per) * (cw + gap), cy = y + 30 + (i / per) * (ch + lgap);
            const bool other = zone == 1 && i == tr_from_;  // la source ne peut pas être la destination
            tr_device_card(s, devs[static_cast<std::size_t>(i)], x, cy, cw, ch, tr_zone_ == zone && i == chosen, i == chosen);
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
        if (focus) for (int g = 3; g >= 1; --g) s.rect(x - g * 4, y - g * 4, aw + g * 8, ah + g * 8, rgb(80, 150, 255, 20), 20 + g * 4);
        s.rect(x, y, aw, ah, chosen ? color::panel2 : color::panel, 20);
        s.outline(x, y, aw, ah, focus ? color::accent : chosen ? color::accent2 : color::line, 20, focus ? 4 : chosen ? 3 : 1);
        s.text(font_, x + 22, y + 18, tr(actions[i].name), large, color::text);
        s.text(font_, x + 22, y + 62, tr(actions[i].text), small, color::muted, aw - 44);
    }
    // En bas : sauvegarde complète et transferts en cours.
    int running = 0, waiting = 0;
    for (const auto& t : snap_->tasks) { if (t.state == "running") ++running; else if (active(t.state)) ++waiting; }
    const int by = y_act + 30 + ah + 22, bw = (1920 - 2 * margin - 2 * gap) / 3, bh = lines > 1 ? 92 : 112;
    for (int i = 0; i < 3; ++i) {
        const int x = margin + i * (bw + gap);
        const bool focus = tr_zone_ == 3 && tr_opt_ == i;
        if (focus) for (int g = 3; g >= 1; --g) s.rect(x - g * 4, by - g * 4, bw + g * 8, bh + g * 8, rgb(80, 150, 255, 20), 20 + g * 4);
        // Même carte que les actions au-dessus (coins arrondis partout, pas de dégradé coupé net).
        s.rect(x, by, bw, bh, color::panel, 20);
        s.outline(x, by, bw, bh, focus ? color::accent : color::line, 20, focus ? 4 : 1);
        if (i == 0) {
            s.text(font_, x + 24, by + (bh - 74) / 2, tr("Sauvegarde complète de la PS5"), large, color::text, bw - 48);
            s.text(font_, x + 24, by + (bh - 74) / 2 + 44, tr("Comptes, sauvegardes, jeux, homebrews, réglages : tout cocher, tout retrouver après une remise à zéro."), small, color::muted, bw - 48);
        } else if (i == 1) {
            s.text(font_, x + 24, by + (bh - 74) / 2, tr("Fichiers"), large, color::text, bw - 48);
            s.text(font_, x + 24, by + (bh - 74) / 2 + 44, tr("Copier ou déplacer n'importe quels fichiers ou dossiers, d'un endroit à l'autre."), small, color::muted, bw - 48);
        } else {
            const std::string b = trf("%d en cours · %d en attente", running, waiting);
            s.text(font_, x + 24, by + (bh - 74) / 2, tr("Transferts en cours"), large, color::text, bw - 120);
            s.text(font_, x + 24, by + (bh - 74) / 2 + 44, b, small, running ? color::accent : color::muted, bw - 120);
            if (running) spinner(s, x + bw - 60, by + bh / 2, 20);
        }
    }
    if (tr_zone_ == 0) draw_footer(s, {{0, tr("Choisir la source")}, {3, tr("Inverser")}, {-1, tr("Bas : destination")}, {-1, tr("L1 / R1  onglets")}});
    else if (tr_zone_ == 1) draw_footer(s, {{0, tr("Choisir la destination")}, {3, tr("Inverser")}, {1, tr("Retour")}});
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
    struct Opt { const char* name; const char* text; bool on; };
    const Opt opts[] = {{tr("Compresser avant l'envoi"), tr("dossiers PS5 → image .ffpfsc"), tr_compress_ || tr_action_ == 2},
                        {tr("Avec les sauvegardes"), tr("les sauvegardes du jeu suivent"), tr_saves_},
                        {tr("Vérifier à l'arrivée"), tr("empreinte SHA-256 de chaque fichier"), tr_verify_}};
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
    if (tr_zone_ == 1) draw_footer(s, {{0, tr("Cocher")}, {-1, tr("Droite : options")}, {-1, tr("Bas : lancer")}, {1, tr("Retour")}});
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
            s.text(font_, px, y + 54, p, small, color::muted, 540);
        }
        if (t.state == "running") spinner(s, margin + w - 60, y + h / 2, 18);
    }
    draw_footer(s, {{2, tr("Pause / reprendre")}, {3, tr("Annuler")}, {1, tr("Retour")}});
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
    if (tr_zone_ == 1) draw_footer(s, {{0, tr("Cocher / décocher")}, {-1, tr("Bas : lancer")}, {1, tr("Retour")}});
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
    s.text(font_, ox + 22, ly + 170, tr("Les sauvegardes de jeu (saves) se remettent sur un compte depuis l'onglet Sauvegardes."), small, color::warn, ow - 44);
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
    draw_footer(s, {{0, tr_zone_ == 3 ? tr("Restaurer") : tr("Cocher")}, {-1, tr("Bas : lancer")}, {1, tr("Retour")}});
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
    if (saves_only) toast(queued ? tr("Restaurations lancées ; les saves seules se remettent depuis l'onglet Sauvegardes.") : tr("Ce sont des saves : remettez-les sur un compte depuis l'onglet Sauvegardes."));
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
            body = "{\"type\":\"compress\",\"item\":" + json_str(key) + ",\"target\":" + json_str(to.id) + "}";
        else
            body = "{\"type\":\"transfer\",\"mode\":\"" + std::string(modes[tr_action_ == 1]) + "\",\"item\":" + json_str(key) + ",\"to\":" + json_str(to.id) +
                   ",\"saves\":" + (tr_saves_ ? "true" : "false") + ",\"verify\":" + (tr_verify_ ? "true" : "false") + ",\"compress\":" + (tr_compress_ ? "true" : "false") + "}";
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
        if (tr_zone_ <= 1) {
            int& v = tr_zone_ == 0 ? tr_from_ : tr_to_;
            if (k & key::left) v = std::max(0, v - 1);
            if (k & key::right) v = std::min(std::max(0, n - 1), v + 1);
            if (tr_zone_ == 1 && tr_to_ == tr_from_ && (k & (key::left | key::right)) && n > 1) {  // on saute la source
                if (k & key::right && tr_to_ + 1 < n) ++tr_to_; else if (tr_to_ > 0) --tr_to_; else ++tr_to_;
            }
            // Plusieurs lignes d'appareils : Haut / Bas passent d'abord d'une ligne à l'autre.
            if ((k & key::down) && v + 5 < n) { v += 5; if (tr_zone_ == 1 && tr_to_ == tr_from_) tr_to_ = std::min(n - 1, tr_to_ + 1); return; }
            if ((k & key::up) && v - 5 >= 0) { v -= 5; return; }
            if ((k & key::cross) && devs[static_cast<std::size_t>(tr_zone_ == 0 ? tr_from_ : tr_to_)].kind == "add") { open_editor("", "ftp"); return; }
            if ((k & key::down) && devs[static_cast<std::size_t>(tr_zone_ == 0 ? tr_from_ : tr_to_)].kind == "add") { toast(tr("Croix pour ajouter un NAS, ou choisissez un autre appareil.")); return; }
            if (k & (key::down | key::cross)) { tr_zone_ += 1; if (tr_zone_ == 1 && tr_to_ == tr_from_) tr_to_ = tr_from_ == 0 ? 1 : 0; }
            if ((k & (key::up | key::circle)) && tr_zone_ == 1) tr_zone_ = 0;
            return;
        }
        if (tr_zone_ == 2) {
            if (k & key::left) tr_action_ = std::max(0, tr_action_ - 1);
            if (k & key::right) tr_action_ = std::min(action_count - 1, tr_action_ + 1);
            if (k & (key::up | key::circle)) tr_zone_ = 1;
            if (k & key::down) { tr_zone_ = 3; tr_opt_ = 0; }
            if (k & key::cross) {
                if (tr_from_ == tr_to_) { toast(tr("Choisissez une destination différente de la source.")); return; }
                const TrDevice& f = devs[static_cast<std::size_t>(tr_from_)];
                const TrDevice& t = devs[static_cast<std::size_t>(tr_to_)];
                if (f.kind == "add" || t.kind == "add") { open_editor("", "ftp"); return; }
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
        if (k & key::right) tr_opt_ = std::min(2, tr_opt_ + 1);
        if (k & (key::up | key::circle)) tr_zone_ = 2;
        if ((k & key::cross) && tr_opt_ == 1) {  // Fichiers
            tr_screen_ = 5; fl_step_ = 0; fl_zone_ = 0; fl_row_ = fl_top_ = 0; fl_sel_.clear();
            if (fl_place_[0] >= 0) fl_pcur_ = fl_place_[0];
            return;
        }
        if (k & key::cross) {
            tr_screen_ = tr_opt_ == 0 ? 3 : 2; tr_zone_ = 1; tr_full_ = 0; tr_task_ = 0;
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
            if (k & key::cross) {
                if (tr_opt_ == 0) { if (tr_action_ == 2) toast(tr("Compresser : l'image est toujours créée.")); else tr_compress_ = !tr_compress_; }
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
        if (tr_task_ < static_cast<int>(list.size())) {
            const Task& t = *list[static_cast<std::size_t>(tr_task_)];
            const char* a = nullptr;
            if (k & key::square) a = t.state == "paused" ? "resume" : t.state == "running" || t.state == "queued" ? "pause" : nullptr;
            if ((k & key::triangle) && (t.type == "files_copy" || t.type == "files_move") && (t.state == "cancelled" || t.state == "failed")) { files_task_menu(t); return; }
            if (k & key::triangle) a = active(t.state) ? "cancel" : "remove";
            if (a) model_.command("POST", "/api/v2/tasks/" + t.id + "/" + a, "{}");
        }
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
        if (focus) for (int g = 3; g >= 1; --g) s.rect(x - g * 5, cy - g * 5, cw + g * 10, ch + g * 10, rgb(80, 150, 255, 20), 22 + g * 5);
        s.rect(x, cy, cw, ch, color::panel, 22);
        s.outline(x, cy, cw, ch, focus ? color::accent : color::line, 22, focus ? 4 : 2);
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
        s.text(font_, x + 16, cy + 128, tr("Croix : comparer et synchroniser"), small, focus ? color::accent : color::muted, cw - 32);
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
    return k == "here" ? N_("Cette PS5") : k == "peer" ? N_("Autre PS5") : k == "nas" ? "NAS" : k == "switch" ? "Switch" : "";
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
    else if (side == 0) draw_footer(s, {{0, tr("Cocher")}, {3, tr("Ouvrir le dossier")}, {2, tr("Remonter")}, {-1, fl_sel_.empty() ? "" : tr("R2 : suivant (VERS)")},
                                        {-1, fl_sel_.empty() ? "" : tr("Options : supprimer")}});
    else draw_footer(s, {{0, tr("Ouvrir")}, {3, tr("Choisir ce dossier")}, {2, tr("Remonter")}, {1, tr("Retour : DEPUIS")}});
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
            else { tr_screen_ = 0; tr_zone_ = 3; tr_opt_ = 1; }
        }
        return;
    }
    const auto& ents = fl_entries_[side].items();
    const int extra = side == 1 || !fl_sel_.empty() ? 1 : 0, count = 1 + static_cast<int>(ents.size()) + extra;
    if (k & key::up) fl_row_ = std::max(0, fl_row_ - 1);
    if (k & key::down) fl_row_ = std::min(count - 1, fl_row_ + 1);
    if (k & key::left) { fl_zone_ = 0; fl_pcur_ = std::max(0, fl_place_[side]); return; }
    if (k & key::circle) { fl_zone_ = 0; fl_pcur_ = std::max(0, fl_place_[side]); return; }
    if (k & key::square) { fl_open(side, fl_place_[side], fl_parent(fl_path_[side])); return; }
    const bool parent_row = fl_row_ == 0, mk_row = extra && fl_row_ == count - 1;
    const Json* e = !parent_row && !mk_row && fl_row_ - 1 < static_cast<int>(ents.size()) ? &ents[static_cast<std::size_t>(fl_row_ - 1)] : nullptr;
    if (side == 0) {
        if ((k & key::cross) && mk_row) {  // « Suivant : choisir la destination »
            fl_step_ = 1; fl_zone_ = 1;
            if (fl_place_[1] < 0) fl_place_[1] = fl_place_[0];
            fl_open(1, fl_place_[1], fl_path_[1]);
            return;
        }
        if (k & key::cross) {
            if (parent_row) fl_open(0, fl_place_[0], fl_parent(fl_path_[0]));
            else if (e) { const std::string n = (*e)["name"].as_string(); if (fl_sel_.count(n)) fl_sel_.erase(n); else fl_sel_.insert(n); }
        }
        if ((k & key::triangle) && e && (*e)["dir"].as_bool()) fl_open(0, fl_place_[0], fl_child(fl_path_[0], (*e)["name"].as_string()));
        if ((k & key::options) && !fl_sel_.empty()) { files_delete_flow(); return; }
        if ((k & key::r2) && !fl_sel_.empty()) {
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
            open_hold_confirm(trf("Supprimer définitivement %d élément(s)", n),
                              tr("Maintenez Carré 5 secondes pour supprimer. Rond pour annuler."),
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
    auto go = [this, inst, console_name, title_id, title](bool delete_source) {
        open_hold_confirm(delete_source ? trf("Désinstaller %s et supprimer son image", title.c_str()) : trf("Désinstaller %s", title.c_str()),
                          tr("Maintenez Carré 5 secondes pour confirmer. Rond pour annuler."),
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
        });
    };
    std::string note = std::string(tr("Vos parties sont sauvegardées d'abord (copie restaurable) et restent sur la console.")) + "\n" +
                       tr("Le jeu est retiré de ShadowMountPlus puis désinstallé.");
    if (!detail.empty()) note += "\n" + detail;
    open_actions(console_name.empty() ? trf("Désinstaller %s de cette PS5 ?", title.c_str()) : trf("Désinstaller %s de %s ?", title.c_str(), console_name.c_str()), {
        {tr("Désinstaller et garder l'image"), [go] { go(false); }},
        {tr("Désinstaller et supprimer l'image"), [go] { go(true); }},
        {tr("Annuler"), [] {}},
    });
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

}  // namespace app
