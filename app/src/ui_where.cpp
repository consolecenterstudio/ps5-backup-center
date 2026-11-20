// « Où ranger ? » : une seule fenêtre pour choisir une destination, à chaque action et dans Paramètres › Où ranger.
#include <algorithm>
#include <cmath>

#include "ui.hpp"
#include "ui_util.hpp"

namespace app {

using pbc::Json;
using namespace ui_util;

std::string Ui::where_label(const std::string& id) const {
    if (id.empty() || id == "auto") return tr("Automatique (premier disque USB, sinon interne)");
    if (id == "internal") return tr("Stockage interne");
    for (const auto& d : tr_devices()) if (d.id == id) return d.name;
    for (const auto& p : snap_->places) if (p.id == id) return p.type == "internal" ? std::string(tr("Stockage interne")) : p.label;
    return id;
}

// filter : 0 sauvegardes (tout sauf la Switch, les liens reçus et les NAS « synchro seulement »), 1 téléchargements
// (les disques de cette console), 2 « Envoyer vers… » (tous les appareils).
void Ui::open_where(const std::string& title, const std::string& note, const std::string& def, int filter, std::function<void(const std::string&)> pick) {
    wh_open_ = true;
    wh_title_ = title;
    wh_note_ = note;
    wh_filter_ = filter;
    wh_auto_ = filter == 0;
    wh_default_ = def.empty() ? (wh_auto_ ? "auto" : "internal") : def;
    if (!wh_auto_ && wh_default_ == "auto") wh_default_ = "internal";
    wh_pick_ = std::move(pick);
    wh_sel_ = 0;  // le choix par défaut, en premier
    wh_cards_.clear();
}

void Ui::ask_where_then(const std::string& title, const std::string& note, const std::string& def, int filter, std::function<void(const std::string&)> pick) {
    if (!ask_where()) {  // « Demander où ranger » : Non — toujours le défaut
        std::string id = def.empty() ? (filter == 0 ? "auto" : "internal") : def;
        if (filter != 0 && id == "auto") id = "internal";
        pick(id);
        return;
    }
    open_where(title, note, def, filter, std::move(pick));
}

void Ui::save_where_setting(const std::string& key, const Json& value) {
    if (key == "ask_where") wh_ask_set_ = value.as_bool(true) ? 1 : 0;
    if (key == "send_place") { send_place_set_ = value.as_string(); send_place_pending_ = true; }
    Json b = Json::object();
    b.set(key, value);
    model_.command("POST", "/api/v2/settings", b.dump(), [this](const std::string& e, const std::string&) {
        post_toast(e.empty() ? tr("Destination par défaut enregistrée") : e);
    });
}

// Bibliothèque › ≡ › « Envoyer vers… » : vers un disque de cette console, une copie vérifiée ; vers le NAS, le nuage ou
// une autre PS5, une sauvegarde vérifiée rangée comme les autres. Rien n'est retiré de la console.
void Ui::send_marked_to(const std::vector<std::string>& keys, const std::string& place) {
    if (keys.empty()) return;
    std::string kind;
    for (const auto& d : tr_devices()) if (d.id == place) kind = d.kind;
    if (kind == "switch") { toast(tr("Vers une Switch : passez par Transferts › Synchroniser (jeux et parties des émulateurs).")); return; }
    if (kind.empty() || kind == "share" || kind == "ps5" || kind == "add") { toast(tr("Destination introuvable (disque débranché ?)")); return; }
    for (const auto& key : keys) {
        std::string body;
        if (kind == "internal" || kind == "usb")
            body = "{\"type\":\"transfer\",\"mode\":\"copy\",\"item\":" + json_str(key) + ",\"to\":" + json_str(place) + ",\"saves\":false,\"verify\":true,\"verify_mode\":\"quick\",\"compress\":false}";
        else
            body = "{\"type\":\"backup\",\"item\":" + json_str(key) + ",\"parts\":[],\"place\":" + json_str(place) + "}";
        model_.command("POST", "/api/v2/tasks", body, [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
    }
    lib_marks_.clear();
    toast(trf("%d élément(s) envoyé(s) vers %s : suivez-les dans Transferts › Transferts en cours", static_cast<int>(keys.size()), where_label(place).c_str()));
}

void Ui::draw_where(Surface& s) {
    if (!switches_loaded_ || now_ - switches_loaded_ > 10000) load_remotes();  // NAS, nuage, Switch : la liste à jour
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 170));
    const int x = 100, y = 50, w = 1720, h = 970;
    window_frame(s, x, y, w, h, 112);
    if (!popup_) help_in(x, y, w, h);
    s.text(font_, x + 40, y + 26, wh_title_, large, color::text, w - 80);
    s.text(font_, x + 40, y + 76, wh_note_, small, color::muted, w - 80);
    // Les cartes : d'abord le choix par défaut, puis chaque famille (cette PS5, autres PS5, Switch, NAS, nuage).
    const auto devs = tr_devices();
    struct Entry { std::string id, name, sub; int image; bool light; };
    struct Section { std::string title; std::vector<Entry> items; };
    std::vector<Section> secs;
    // Ce que l'action accepte : les sauvegardes ne vont ni sur une Switch ni sur un NAS « synchro seulement » ;
    // les téléchargements restent sur un disque de cette console.
    auto allowed = [&](const TrDevice& d) {
        const std::string& k = d.kind;
        if (k == "ps5" || k == "share" || k == "add") return false;
        if (wh_filter_ == 1) return k == "internal" || k == "usb";
        if (wh_filter_ == 0) {
            if (k == "switch") return false;
            for (const auto& p : snap_->places) if (p.id == d.id && (p.use == "sync" || p.readonly)) return false;
        }
        return true;
    };
    auto find = [&](const std::string& id) -> const TrDevice* { for (const auto& d : devs) if (d.id == id && allowed(d)) return &d; return nullptr; };
    {
        Section def{tr("PAR DÉFAUT"), {}};
        if (wh_default_ == "auto") def.items.push_back({"auto", tr("Automatique"), tr("premier disque USB, sinon interne"), dev_usb, false});
        else if (wh_default_ == "internal") { const TrDevice* d = nullptr; for (const auto& x2 : devs) if (x2.kind == "internal") d = &x2; if (d) def.items.push_back({d->id, d->name, d->sub, d->image, false}); }
        else if (const TrDevice* d = find(wh_default_)) def.items.push_back({d->id, d->name, d->sub, d->image, false});
        if (!def.items.empty()) secs.push_back(def);
    }
    const std::string def_id = secs.empty() ? std::string() : secs[0].items[0].id;
    auto add_to = [&](const std::string& title, const Entry& e) {
        if (e.id == def_id) return;
        for (auto& sec : secs) if (sec.title == title) { sec.items.push_back(e); return; }
        secs.push_back({title, {e}});
    };
    if (wh_auto_ && def_id != "auto") add_to(tr("CETTE PS5"), {"auto", tr("Automatique"), tr("premier disque USB, sinon interne"), dev_usb, false});
    for (const auto& d : devs) {
        if (!allowed(d)) continue;
        const std::string& k = d.kind;
        const std::string sub = d.has_free ? d.sub + " · " + trf("%s libres", size_text(d.free).c_str()) : d.sub;
        if (k == "internal" || k == "usb") add_to(tr("CETTE PS5"), {d.id, d.name, sub, d.image, false});
        else if (k == "peer") {  // « example (12.40) · USB 0 » : une rangée par console, une carte par disque
            const auto dot = d.name.find(" · ");
            const std::string console = dot == std::string::npos ? d.name : d.name.substr(0, dot), disk = dot == std::string::npos ? d.name : d.name.substr(dot + 3);
            // La photo de la PS5, comme pour cette PS5 : seule pour l'interne, avec son disque pour un USB.
            add_to(trf("AUTRE PS5 · %s", console.c_str()), {d.id, disk, trf("%s libres", size_text(d.free).c_str()), disk.find("USB") != std::string::npos ? dev_usb : dev_internal, false});
        }
        else if (k == "switch") add_to("SWITCH", {d.id, d.name, sub, d.image, false});
        else if (cloud_proto(k)) add_to(tr("NUAGE"), {d.id, d.name, sub, d.image, false});
        else add_to("NAS", {d.id, d.name, sub, d.image, false});
    }
    // Ordre des familles : par défaut, cette PS5, autres PS5, Switch, NAS, nuage.
    auto rank = [&](const std::string& t) {
        if (t == tr("PAR DÉFAUT")) return 0;
        if (t == tr("CETTE PS5")) return 1;
        if (t == "SWITCH") return 3;
        if (t == "NAS") return 4;
        if (t == tr("NUAGE")) return 5;
        return 2;  // AUTRE PS5 · …
    };
    std::stable_sort(secs.begin(), secs.end(), [&](const Section& a2, const Section& b2) { return rank(a2.title) < rank(b2.title); });
    if (wh_filter_ != 1) secs.push_back({tr("AUTRE CHOSE"), {{"add", tr("+ Ajouter un service"), tr("NAS, nuage, Switch, PS5…"), dev_cloud, true}}});
    // Mise en page (3 cartes par rangée) puis défilement pour garder la carte choisie visible.
    const int cols = 3, gap = 18, cw = (w - 80 - (cols - 1) * gap) / cols, ch = 96, top = y + 130, bottom = y + h - 100;
    struct Pos { std::string id; int x, y; const Entry* e; bool head; std::string title; };
    std::vector<Pos> lay;
    int yy = 0;
    for (const auto& sec : secs) {
        lay.push_back({"", 0, yy, nullptr, true, sec.title});
        yy += 36;
        for (std::size_t i = 0; i < sec.items.size(); ++i) {
            if (i && i % cols == 0) yy += ch + gap;
            lay.push_back({sec.items[i].id, x + 40 + static_cast<int>(i % cols) * (cw + gap), yy, &sec.items[i], false, ""});
        }
        yy += ch + 26;
    }
    wh_cards_.clear();
    for (const auto& p : lay) if (!p.head) wh_cards_.push_back({p.id, p.x, p.y});
    wh_sel_ = std::clamp(wh_sel_, 0, std::max(0, static_cast<int>(wh_cards_.size()) - 1));
    int scroll = 0;
    if (!wh_cards_.empty()) {
        const int sy = wh_cards_[static_cast<std::size_t>(wh_sel_)].y;
        if (sy + ch > bottom - top) scroll = sy + ch - (bottom - top);
    }
    int ci = 0;
    bool more = false;  // des cartes plus bas, hors de la fenêtre
    for (const auto& p : lay) if (!p.head && top + p.y - scroll + ch > bottom + 10) more = true;
    for (const auto& p : lay) {
        const int py = top + p.y - scroll;
        if (p.head) { if (py >= top - 4 && py + 36 + ch <= bottom + 10) s.text(font_, x + 40, py, p.title, small, p.title == tr("PAR DÉFAUT") ? color::accent : color::muted); continue; }
        const bool on = ci == wh_sel_;
        ++ci;
        if (py < top - 10 || py + ch > bottom + 10) continue;
        DevCard c;
        c.dev = p.e->image;
        c.name = p.e->name;
        c.line = p.e->sub;
        c.light = p.e->light;
        device_card(s, c, p.x, py, cw, ch, on, p.id == def_id);
    }
    if (scroll > 0 || more) s.text(font_, x + w - 360, top - 34, tr("Haut / bas pour voir la suite"), small, color::muted);
    if (!popup_) draw_footer(s, {{0, tr("Ranger ici")}, {-1, tr("Haut / bas")}, {-1, tr("Gauche / droite")}, {1, tr("Fermer")}});
}

void Ui::input_where(std::uint32_t k) {
    if (k & key::circle) { wh_open_ = false; return; }
    const int n = static_cast<int>(wh_cards_.size());
    if (!n) return;
    const auto& cur = wh_cards_[static_cast<std::size_t>(std::clamp(wh_sel_, 0, n - 1))];
    if (k & key::left) { wh_sel_ = std::max(0, wh_sel_ - 1); return; }
    if (k & key::right) { wh_sel_ = std::min(n - 1, wh_sel_ + 1); return; }
    if (k & (key::up | key::down)) {  // la carte la plus proche dans la rangée du dessus / du dessous
        const bool down = k & key::down;
        int best = -1, by = 0, bdx = 0;
        for (int i = 0; i < n; ++i) {
            const auto& c = wh_cards_[static_cast<std::size_t>(i)];
            if (down ? c.y <= cur.y : c.y >= cur.y) continue;
            const int dy = std::abs(c.y - cur.y), dx = std::abs(c.x - cur.x);
            if (best < 0 || dy < by || (dy == by && dx < bdx)) { best = i; by = dy; bdx = dx; }
        }
        if (best >= 0) wh_sel_ = best;
        return;
    }
    if (k & key::cross) {
        const std::string id = cur.id;
        if (id == "add") { wh_open_ = false; open_add_service(); return; }
        wh_open_ = false;
        if (wh_pick_) wh_pick_(id);
    }
}

// Paramètres › Où ranger : à gauche les choix par défaut (0 à 2), à droite « demander » (3) et les appareils (4).
void Ui::input_storage(std::uint32_t k) {
    const bool right_col = set_row_ >= 3;
    if (k & key::circle) { set_zone_ = 0; return; }
    if (k & key::up) { set_row_ = right_col ? std::max(3, set_row_ - 1) : std::max(0, set_row_ - 1); return; }
    if (k & key::down) { set_row_ = right_col ? std::min(4, set_row_ + 1) : std::min(2, set_row_ + 1); return; }
    if (k & key::right) { if (!right_col) set_row_ = std::min(4, 3 + set_row_); return; }
    if (k & key::left) { if (right_col) set_row_ = std::min(2, set_row_ - 3); else set_zone_ = 0; return; }
    if (!(k & key::cross)) return;
    switch (set_row_) {
        case 0:
            open_where(tr("Sauvegardes : où ranger par défaut ?"), tr("Proposé en premier à chaque sauvegarde ; les sauvegardes de nuit y vont directement."), snap_->default_place, 0,
                       [this](const std::string& id) {
                           model_.command("POST", "/api/v2/settings", "{\"default_place\":" + json_str(id) + "}", [this](const std::string& e, const std::string&) {
                               post_toast(e.empty() ? tr("Destination par défaut enregistrée") : e);
                           });
                       });
            return;
        case 1:
            open_where(tr("Téléchargements : où ranger par défaut ?"), tr("Proposé en premier à chaque lien de partage ou torrent ajouté."), snap_->net["dl_place"].as_string(), 1,
                       [this](const std::string& id) { net_set("dl_place", Json(id == "internal" ? std::string() : id)); toast(tr("Destination par défaut enregistrée")); });
            return;
        case 2:
            open_actions(tr("Envoyer vers… : où ranger par défaut ?"), {
                {std::string(send_place().empty() ? "● " : "○ ") + tr("Demander à chaque fois"), [this] { save_where_setting("send_place", Json(std::string())); }},
                {std::string(!send_place().empty() ? "● " : "○ ") + tr("Choisir un endroit…"), [this] {
                    open_where(tr("Envoyer vers… : où ranger par défaut ?"), tr("Proposé en premier quand vous envoyez des jeux cochés ou copiez entre appareils."), send_place(), 2,
                               [this](const std::string& id) { save_where_setting("send_place", Json(id)); });
                }},
            });
            return;
        case 3:
            open_actions(tr("Demander où ranger"), {
                {std::string(ask_where() ? "● " : "○ ") + tr("Oui, le choix par défaut en premier"), [this] { save_where_setting("ask_where", Json(true)); }},
                {std::string(!ask_where() ? "● " : "○ ") + tr("Non, toujours le choix par défaut"), [this] { save_where_setting("ask_where", Json(false)); }},
            });
            return;
        default:
            tab_ = destinations;
            set_zone_ = 0;
            return;
    }
}

}  // namespace app
