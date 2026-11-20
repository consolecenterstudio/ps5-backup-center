// Téléchargements : ce qui arrive d'Internet (liens reçus MEGA, Google Drive, Dropbox, OneDrive), à part de Transferts
// (qui copie entre ses propres appareils). Liens reçus en haut ; ouvrir un lien mène à ses fichiers (écran Fichiers,
// source = le lien) ; « En cours » avec le débit et le temps restant, pause / reprise sans repartir de zéro ;
// « Terminés » avec la vitesse moyenne. Notifications à lui (téléchargement fini, lien qui expire bientôt).
#include <algorithm>
#include <ctime>
#include <set>

#include "ui.hpp"
#include "ui_util.hpp"

namespace app {

using pbc::Json;
using namespace ui_util;

namespace {
bool dl_active(const std::string& st) { return st == "running" || st == "queued" || st == "paused"; }
bool dl_over(const std::string& st) { return st == "done" || st == "failed" || st == "cancelled"; }
const char* svc_name(const std::string& s) {
    return s == "mega" ? "MEGA" : s == "gdrive" ? "Google Drive" : s == "dropbox" ? "Dropbox" : s == "onedrive" ? "OneDrive" : "";
}
}  // namespace

std::vector<const Json*> Ui::dl_shares() const {
    std::vector<const Json*> v;
    if (remotes_all_.is_array())
        for (const auto& r : remotes_all_.items()) if (r["type"].as_string() == "share") v.push_back(&r);
    return v;
}

std::vector<const Task*> Ui::dl_tasks(bool active) const {
    std::vector<const Task*> v;
    for (const auto& t : snap_->tasks)
        if (t.origin == "download" && (active ? dl_active(t.state) : dl_over(t.state))) v.push_back(&t);
    if (!active) std::sort(v.begin(), v.end(), [](const Task* a, const Task* b) { return a->finished > b->finished; });
    return v;
}

std::string Ui::dl_place_name(const std::string& id) const {
    for (const auto& p : snap_->places) if (p.id == id) return p.type == "internal" ? std::string(tr("Interne")) : p.label;
    return id;
}

// Ouvrir les fichiers d'un lien reçu : l'écran Fichiers de Transferts, avec le lien comme source.
void Ui::open_share_files(const std::string& id) {
    fl_from_downloads_ = true;  // ○ à la racine ramène à Téléchargements (charte : on revient d'où l'on vient)
    tab_ = transfers;
    tr_screen_ = 5;
    fl_step_ = 0;
    fl_zone_ = 1;
    fl_sel_.clear();
    int idx = -1, i = 0;
    if (fl_places_.is_array()) for (const auto& p : fl_places_.items()) { if (p["id"].as_string() == id) idx = i; ++i; }
    if (idx >= 0) { fl_open(0, idx, ""); return; }
    fl_pending_place_ = id;  // liste des endroits relue (le lien vient peut-être d'être ajouté), puis ouverte
    fl_places_ = Json();
    fl_place_[0] = -1;
}

// Notifications à lui, sur n'importe quel écran : téléchargement fini, lien qui expire dans moins d'un jour.
void Ui::dl_notify() {
    for (const auto& t : snap_->tasks) {
        if (t.origin != "download" || !dl_over(t.state) || dl_seen_done_.count(t.id)) continue;
        dl_seen_done_.insert(t.id);
        if (!dl_seen_init_) continue;  // au démarrage : ce qui était déjà fini ne se notifie pas
        if (t.state == "done") {
            const std::int64_t secs = std::max<std::int64_t>(1, t.finished - t.started);
            toast(trf("Téléchargé : %s (%s, %s/s)", t.title.c_str(), size_text(t.total).c_str(), size_text(t.total / static_cast<std::uint64_t>(secs)).c_str()));
        } else if (t.state == "failed") toast(trf("Téléchargement en échec : %s", t.title.c_str()));
    }
    dl_seen_init_ = true;
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    for (const Json* r : dl_shares()) {
        const std::int64_t exp = (*r)["config"]["expires"].as_int();
        const std::string id = (*r)["id"].as_string();
        if (!exp || exp - now > 86400 || dl_warned_.count(id)) continue;
        dl_warned_.insert(id);
        toast(trf("Le lien « %s » expire bientôt : Téléchargements › Actions › Garder plus longtemps", (*r)["name"].as_string().c_str()));
    }
}

void Ui::load_torrents() {
    if (tor_loading_ || (tor_loaded_ && now_ - tor_loaded_ < 2000)) return;
    tor_loading_ = true;
    tor_loaded_ = now_;
    model_.command("GET", "/api/v2/torrents", "", [this](const std::string& e, const std::string& body) {
        Json j;
        const bool ok = e.empty() && Json::parse(body, j);
        post_ui([this, ok, j] {
            tor_loading_ = false;
            if (!ok) return;
            // Un lien en préparation qui a disparu de la liste (sans avoir été annulé) est prêt : il est devenu un torrent partagé.
            std::set<std::string> now_jobs;
            for (const auto& c : j["creating"].items()) if (c["state"].as_string() == "hashing") now_jobs.insert(c["job"].as_string());
            for (const auto& c : tor_["creating"].items()) {
                const std::string id = c["job"].as_string();
                if (c["state"].as_string() == "hashing" && !now_jobs.count(id) && !tor_jobs_cancelled_.count(id)) {
                    bool failed = false;
                    for (const auto& c2 : j["creating"].items()) if (c2["job"].as_string() == id) failed = true;
                    if (!failed) toast(trf("Lien prêt pour « %s » : Croix dessus pour son code QR", c["name"].as_string().c_str()));
                }
            }
            tor_ = j;
        });
    });
}

std::vector<const Json*> Ui::dl_tors() const {
    std::vector<const Json*> v;
    for (const auto& c : tor_["creating"].items()) v.push_back(&c);
    for (const auto& t : tor_["torrents"].items()) v.push_back(&t);
    return v;
}

void Ui::create_link(const std::string& path) {
    const auto sl = path.rfind('/');
    const std::string name = sl == std::string::npos ? path : path.substr(sl + 1);
    open_actions(trf("Créer un lien pour « %s » ?", name.c_str()), {
        {tr("Oui, créer le lien"), [this, path] {
            Json b = Json::object();
            b.set("path", path);
            model_.command("POST", "/api/v2/torrents/create", b.dump(), [this](const std::string& e, const std::string&) {
                post_toast(e.empty() ? tr("Préparation du lien : suivez-la dans « En cours »") : e);
                std::lock_guard<std::mutex> l(async_mu_);
                tor_loaded_ = 0;
            });
        }},
        {tr("Annuler"), [] {}},
    });
    popup_note_ = tr("La console calcule les empreintes (quelques minutes pour un gros jeu), puis le partage depuis là où il est : rien n'est copié. Le code QR du lien magnet est dans sa fiche ; le partage dure tant qu'il reste dans la liste.");
}

// Ajouter un lien magnet ou un .torrent : d'abord où le ranger (un disque de cette console ; le réglage de Paramètres ›
// Réseau est proposé en premier), puis le moteur s'en charge.
void Ui::add_torrent(const std::string& source) {
    ask_where_then(tr("Où ranger ce téléchargement ?"), tr("Sur un disque de cette console. Pour le NAS ou le nuage : le copier ensuite (Transferts)."),
                   snap_->net["dl_place"].as_string(), 1, [this, source](const std::string& id) {
        std::string save = "/data";
        for (const auto& p : snap_->places) if (p.id == id && p.type == "usb") save = p.mount;
        Json b = Json::object();
        b.set("source", source);
        b.set("save", save + "/Téléchargements");
        model_.command("POST", "/api/v2/torrents", b.dump(), [this](const std::string& e, const std::string&) {
            post_toast(e.empty() ? tr("Ajouté : il apparaît dans « En cours »") : e);
            std::lock_guard<std::mutex> l(async_mu_);
            tor_loaded_ = 0;
        });
    });
}

void Ui::torrent_post(const std::string& id, const std::string& what, const std::string& body, const std::string& done) {
    model_.command("POST", "/api/v2/torrents/" + id + "/" + what, body.empty() ? "{}" : body, [this, done](const std::string& e, const std::string&) {
        if (!e.empty() || !done.empty()) post_toast(e.empty() ? done : e);
        std::lock_guard<std::mutex> l(async_mu_);
        tor_loaded_ = 0;
        td_loaded_ = 0;
    });
}

// Priorité d'un fichier (ou de tous, index -1) : 7 haute, 4 normale, 1 basse, 0 ne pas télécharger.
void Ui::torrent_file_priority(const std::string& id, int index, int current, const std::string& name) {
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    static const int prios[4] = {7, 4, 1, 0};
    static const char* const labels[4] = {N_("Haute"), N_("Normale"), N_("Basse"), N_("Ne pas télécharger")};
    for (int i = 0; i < 4; ++i) {
        const int p = prios[i];
        acts.push_back({std::string(current == p ? "● " : "○ ") + tr(labels[i]), [this, id, index, p] {
            Json b = Json::object();
            b.set("priority", static_cast<std::int64_t>(p));
            if (index >= 0) b.set("index", static_cast<std::int64_t>(index));
            torrent_post(id, index >= 0 ? "file" : "files", b.dump(), p == 0 ? tr("Ne sera pas téléchargé") : tr("Priorité changée"));
        }});
    }
    open_actions(index >= 0 ? trf("Priorité de « %s »", name.c_str()) : std::string(tr("Priorité de tous les fichiers")), std::move(acts));
    popup_note_ = tr("Haute : ses morceaux passent en premier. « Ne pas télécharger » : il est sauté (ce qui est déjà là reste).");
}

// Toutes les actions d'un torrent (≡) : depuis la liste ou depuis sa fiche.
void Ui::open_torrent_menu(const Json& t, bool from_detail) {
    const std::string id = t["id"].as_string(), name = t["name"].as_string(), st = t["state"].as_string();
    const bool paused = t["paused"].as_bool(), forced = t["forced"].as_bool(), seq = t["sequential"].as_bool();
    const std::int64_t queue = t["queue"].as_int(-1);
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    if (!from_detail) acts.push_back({tr("Voir la fiche (fichiers, pairs, lien magnet)"), [this, id] { open_torrent_detail(id); }});
    acts.push_back({st == "complete" ? tr("Partager à nouveau (sans arrêt automatique)") : paused ? tr("Reprendre") : tr("Mettre en pause"), [this, id, paused] {
        torrent_post(id, paused ? "resume" : "pause", "", paused ? tr("Reprise là où il s'était arrêté") : tr("En pause : il reprendra là où il s'est arrêté"));
    }});
    if (st != "seeding" && st != "complete" && !forced)
        acts.push_back({tr("Forcer le démarrage (sans attendre son tour)"), [this, id] { torrent_post(id, "force", "", tr("Démarré sans attendre son tour")); }});
    if (forced)
        acts.push_back({tr("Remettre dans la file (attendre son tour)"), [this, id] { torrent_post(id, "resume", "", tr("Remis dans la file")); }});
    acts.push_back({tr("Chercher des pairs maintenant"), [this, id] { torrent_post(id, "reannounce", "", tr("Recherche de pairs lancée (trackers et DHT)")); }});
    acts.push_back({tr("Ajouter un pair à la main (ip:port)…"), [this, id] {
        open_keyboard(tr("Pair : ip:port (par exemple 192.168.1.20:6891)"), "", [](const std::string&) {});
        kb_done_ = [this, id] {
            const std::string t = kb_text_;
            if (t.empty()) return;
            Json b = Json::object();
            b.set("addr", t);
            torrent_post(id, "add_peer", b.dump(), tr("Connexion à ce pair demandée : il apparaît dans Pairs s'il répond"));
        };
    }});
    if (queue >= 0) {
        acts.push_back({tr("Passer en premier dans la file"), [this, id] { torrent_post(id, "queue_top", "", tr("En tête de la file")); }});
        acts.push_back({tr("Monter dans la file"), [this, id] { torrent_post(id, "queue_up", "", tr("Monté d'une place")); }});
        acts.push_back({tr("Descendre dans la file"), [this, id] { torrent_post(id, "queue_down", "", tr("Descendu d'une place")); }});
    }
    if (st != "seeding" && st != "complete")
        acts.push_back({seq ? tr("Télécharger dans l'ordre : oui (changer)") : tr("Télécharger dans l'ordre : non (changer)"), [this, id, seq] {
            Json b = Json::object();
            b.set("on", !seq);
            torrent_post(id, "sequential", b.dump(), !seq ? tr("Dans l'ordre : le début des fichiers arrive en premier") : tr("Dans le désordre : le plus rapide d'abord"));
        }});
    if (st != "seeding" && st != "complete") {
        const bool fl = t["first_last"].as_bool();
        acts.push_back({fl ? tr("Premiers et derniers morceaux d'abord : oui (changer)") : tr("Premiers et derniers morceaux d'abord : non (changer)"), [this, id, fl] {
            Json b = Json::object();
            b.set("on", !fl);
            torrent_post(id, "first_last", b.dump(), !fl ? tr("Le début et la fin de chaque fichier arrivent en premier (aperçu d'une vidéo, archive lisible plus tôt)") : tr("Morceaux dans l'ordre habituel"));
        }});
    }
    if (st == "seeding" || st == "complete") {
        const bool ss = t["superseed"].as_bool();
        acts.push_back({ss ? tr("Super-seed : oui (changer)") : tr("Super-seed : non (changer)"), [this, id, ss] {
            Json b = Json::object();
            b.set("on", !ss);
            torrent_post(id, "superseed", b.dump(), !ss ? tr("Super-seed : chaque pair reçoit des morceaux différents (premier partage, envoi économe)") : tr("Partage normal"));
        }});
    }
    acts.push_back({tr("Renommer…"), [this, id, name] {
        open_keyboard(tr("Nouveau nom du torrent (son dossier ou son fichier)"), name, [](const std::string&) {});
        kb_done_ = [this, id] {
            const std::string t = kb_text_;
            if (t.empty()) return;
            Json b = Json::object();
            b.set("name", t);
            torrent_post(id, "rename", b.dump(), tr("Renommé"));
        };
    }});
    const std::int64_t ld = t["limit_down"].as_int(0), lu = t["limit_up"].as_int(0);
    acts.push_back({tr("Limiter ce torrent…"), [this, id, ld, lu, name] {
        static const int rates[] = {0, 256, 512, 1024, 2048, 5120, 10240};
        auto label = [](int ko) { return ko <= 0 ? std::string(tr("Sans limite")) : ko >= 1024 ? trf("%lld Mo/s", static_cast<long long>(ko / 1024)) : trf("%lld Ko/s", static_cast<long long>(ko)); };
        std::vector<std::pair<std::string, std::function<void()>>> a2;
        for (int r : rates)
            a2.push_back({std::string(ld == r || (r == 0 && ld <= 0) ? "● " : "○ ") + label(r), [this, id, r, lu] {
                Json b = Json::object();
                b.set("down", static_cast<std::int64_t>(r));
                b.set("up", lu > 0 ? lu : static_cast<std::int64_t>(0));
                torrent_post(id, "limits", b.dump(), tr("Limite de ce torrent changée"));
            }});
        open_actions(trf("Réception de « %s »", name.c_str()), std::move(a2));
        popup_note_ = tr("En plus du débit de Paramètres › Réseau, qui vaut pour tous les torrents.");
    }});
    acts.push_back({tr("Revérifier les fichiers sur le disque"), [this, id] { torrent_post(id, "recheck", "", tr("Vérification lancée")); }});
    acts.push_back({tr("Télécharger le .torrent sur le téléphone (code QR)"), [this, id, name] {
        mag_torrent_ = name;
        mag_open_ = true;
        mag_qr_ = Json();
        model_.command("GET", "/api/v2/web-link?place=" + url_encode("torrent=" + id), "", [this](const std::string& e, const std::string& body) {
            Json j;
            const bool ok = e.empty() && Json::parse(body, j);
            post_ui([this, ok, j] { if (ok) mag_qr_ = j; });
        });
    }});
    acts.push_back({tr("Enregistrer le fichier .torrent (à côté des fichiers)"), [this, id] {
        model_.command("POST", "/api/v2/torrents/" + id + "/export", "{}", [this](const std::string& e, const std::string& body) {
            Json j;
            post_toast(!e.empty() ? e : Json::parse(body, j) ? trf("Enregistré : %s", j["path"].as_string().c_str()) : std::string(tr("Enregistré")));
        });
    }});
    if (t["created"].as_bool()) {  // lien créé ici : il partage les fichiers d'origine, jamais effacés d'ici
        acts.push_back({tr("Arrêter de partager (rien n'est effacé)"), [this, id] { td_open_ = false; torrent_post(id, "remove", "", tr("Partage arrêté : vos fichiers restent")); }});
        open_actions(name, std::move(acts));
        return;
    }
    acts.push_back({tr("Retirer de la liste (garder les fichiers)"), [this, id] { td_open_ = false; torrent_post(id, "remove", "", tr("Retiré : les fichiers restent")); }});
    acts.push_back({tr("Supprimer aussi les fichiers…"), [this, id, name] {
        open_hold_confirm(trf("Supprimer « %s » et ses fichiers ?", name.c_str()), tr("Ce qui a été téléchargé est effacé du disque."), [this, id] {
            Json b = Json::object();
            b.set("files", true);
            td_open_ = false;
            torrent_post(id, "remove", b.dump(), tr("Supprimé"));
        }, tr("Maintenez Carré 5 secondes pour supprimer"));
    }});
    open_actions(name, std::move(acts));
}

std::string Ui::bt_options_summary(const Json& n) {
    const std::int64_t ratio = n["bt_seed_ratio"].as_int(0), hours = n["bt_seed_hours"].as_int(0);
    std::vector<std::string> parts;
    if (ratio > 0 || hours > 0) {
        std::string stop;
        if (ratio > 0) stop = trf("ratio %s", decimal_text(static_cast<double>(ratio) / 100.0).c_str());
        if (hours > 0) stop += (stop.empty() ? std::string() : std::string(" ") + tr("ou") + " ") + (hours % 24 == 0 && hours >= 48 ? trf("%d j", static_cast<int>(hours / 24)) : trf("%d h", static_cast<int>(hours)));
        parts.push_back(trf("arrêt : %s", stop.c_str()));
    }
    if (n["bt_night"].as_bool(false)) parts.push_back(trf("nuit : %d h – %d h", static_cast<int>(n["bt_night_from"].as_int(1)), static_cast<int>(n["bt_night_to"].as_int(7))));
    if (n["bt_prealloc"].as_bool(false)) parts.push_back(tr("espace réservé"));
    if (n["bt_encrypt"].as_string() == "force") parts.push_back(tr("chiffré"));
    if (!n["bt_blocklist"].items().empty()) parts.push_back(trf("%d bloquée(s)", static_cast<int>(n["bt_blocklist"].items().size())));
    if (!n["bt_done_move"].as_string().empty() || !n["bt_done_copy"].as_string().empty()) parts.push_back(tr("à la fin : copie ou déplacement"));
    if (!n["bt_watch"].as_string().empty()) parts.push_back(tr("dossier surveillé"));
    if (parts.empty()) return tr("partage sans fin");
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : " · ") + p;
    return out;
}

void Ui::open_bt_options() {
    const Json& n = snap_->net;
    using Acts = std::vector<std::pair<std::string, std::function<void()>>>;
    auto mark = [](bool on, const std::string& t) { return (on ? std::string("● ") : std::string("○ ")) + t; };
    auto set = [this](const char* key, Json v) {
        net_set(key, std::move(v));
        toast(tr("Réglage enregistré"));
    };
    const std::int64_t ratio = n["bt_seed_ratio"].as_int(0), hours = n["bt_seed_hours"].as_int(0);
    const bool pre = n["bt_prealloc"].as_bool(false), space = n["bt_space_check"].as_bool(true), night = n["bt_night"].as_bool(false);
    const int nf = static_cast<int>(n["bt_night_from"].as_int(1)), nt = static_cast<int>(n["bt_night_to"].as_int(7));
    auto ratio_text = [](std::int64_t r) { return r <= 0 ? std::string(tr("Jamais")) : trf("Ratio %s", decimal_text(static_cast<double>(r) / 100.0).c_str()); };
    auto hours_text = [](std::int64_t h) { return h <= 0 ? std::string(tr("Jamais")) : h % 24 == 0 && h >= 48 ? trf("%d jours", static_cast<int>(h / 24)) : trf("%d h", static_cast<int>(h)); };
    Acts acts;
    acts.push_back({trf("Arrêter de partager après un ratio : %s", ratio_text(ratio).c_str()), [this, ratio, mark, set, ratio_text] {
        Acts a;
        for (int r : {0, 100, 150, 200, 300, 500}) a.push_back({mark(ratio == r, ratio_text(r)), [set, r] { set("bt_seed_ratio", Json(static_cast<std::int64_t>(r))); }});
        open_actions(tr("Arrêter de partager après un ratio"), std::move(a));
        popup_note_ = tr("Ratio : ce qui a été envoyé divisé par la taille. 2,0 : chaque octet reçu a été renvoyé deux fois. Les liens créés ici ne s'arrêtent jamais seuls.");
    }});
    acts.push_back({trf("Arrêter de partager après une durée : %s", hours_text(hours).c_str()), [this, hours, mark, set, hours_text] {
        Acts a;
        for (int h : {0, 1, 6, 24, 72, 168}) a.push_back({mark(hours == h, hours_text(h)), [set, h] { set("bt_seed_hours", Json(static_cast<std::int64_t>(h))); }});
        open_actions(tr("Arrêter de partager après une durée"), std::move(a));
        popup_note_ = tr("Temps de partage une fois le téléchargement terminé. Les liens créés ici ne s'arrêtent jamais seuls.");
    }});
    acts.push_back({trf("Réserver tout l'espace au départ : %s", pre ? tr("Oui") : tr("Non")), [this, pre, mark, set] {
        open_actions(tr("Réserver tout l'espace au départ"), {{mark(pre, tr("Oui")), [set] { set("bt_prealloc", Json(true)); }}, {mark(!pre, tr("Non")), [set] { set("bt_prealloc", Json(false)); }}});
        popup_note_ = tr("Oui : la place est prise sur le disque dès l'ajout ; le disque ne se remplit pas en cours de route. Pour les torrents ajoutés ensuite.");
    }});
    acts.push_back({trf("Refuser si le disque est trop plein : %s", space ? tr("Oui") : tr("Non")), [this, space, mark, set] {
        open_actions(tr("Refuser si le disque est trop plein"), {{mark(space, tr("Oui")), [set] { set("bt_space_check", Json(true)); }}, {mark(!space, tr("Non")), [set] { set("bt_space_check", Json(false)); }}});
        popup_note_ = tr("Oui : un torrent plus gros que la place libre (moins 512 Mo) est mis en pause avec un message, au lieu de remplir le disque.");
    }});
    const std::string dcopy = n["bt_done_copy"].as_string(), dmove = n["bt_done_move"].as_string();
    acts.push_back({trf("À la fin, copier vers : %s", dcopy.empty() ? tr("Non") : where_label(dcopy).c_str()), [this, dcopy, mark] {
        open_actions(tr("À la fin d'un téléchargement, le copier vers…"), {
            {mark(dcopy.empty(), tr("Non")), [this] { net_set("bt_done_copy", Json(std::string())); toast(tr("Réglage enregistré")); }},
            {mark(!dcopy.empty(), dcopy.empty() ? std::string(tr("Choisir un endroit…")) : where_label(dcopy) + "  ·  " + tr("changer")), [this, dcopy] {
                open_where(tr("À la fin, copier vers…"), tr("Une copie vérifiée dans son dossier « Téléchargements » ; le torrent reste partagé d'ici."),
                           dcopy.empty() ? std::string("internal") : dcopy, 2, [this](const std::string& id) {
                    net_set("bt_done_copy", Json(id)); toast(tr("Réglage enregistré"));
                });
            }},
        });
        popup_note_ = tr("Une tâche de copie (Transferts en cours) se lance seule à la fin de chaque téléchargement. Jamais pour un lien créé ici.");
    }});
    acts.push_back({trf("À la fin, déplacer vers : %s", dmove.empty() ? tr("Non") : where_label(dmove).c_str()), [this, dmove, mark] {
        open_actions(tr("À la fin d'un téléchargement, déplacer ses fichiers vers…"), {
            {mark(dmove.empty(), tr("Non")), [this] { net_set("bt_done_move", Json(std::string())); toast(tr("Réglage enregistré")); }},
            {mark(!dmove.empty(), dmove.empty() ? std::string(tr("Choisir un disque…")) : where_label(dmove) + "  ·  " + tr("changer")), [this, dmove] {
                open_where(tr("À la fin, déplacer vers…"), tr("Un disque de cette console : ses fichiers vont dans son dossier « Téléchargements » et restent partagés."),
                           dmove.empty() ? std::string("internal") : dmove, 1, [this](const std::string& id) { net_set("bt_done_move", Json(id)); toast(tr("Réglage enregistré")); });
            }},
        });
        popup_note_ = tr("Par exemple : télécharger sur l'interne (rapide), puis ranger sur le disque USB. Le partage continue depuis le nouvel endroit.");
    }});
    const std::string watch = n["bt_watch"].as_string();
    acts.push_back({trf("Dossier surveillé : %s", watch.empty() ? tr("Non") : where_label(watch).c_str()), [this, watch, mark] {
        open_actions(tr("Dossier surveillé"), {
            {mark(watch.empty(), tr("Non")), [this] { net_set("bt_watch", Json(std::string())); toast(tr("Réglage enregistré")); }},
            {mark(!watch.empty(), watch.empty() ? std::string(tr("Choisir un disque…")) : where_label(watch) + "  ·  " + tr("changer")), [this, watch] {
                open_where(tr("Dossier surveillé sur…"), tr("Un disque de cette console : son dossier « Torrents à ajouter » est créé et surveillé."),
                           watch.empty() ? std::string("internal") : watch, 1, [this](const std::string& id) { net_set("bt_watch", Json(id)); toast(tr("Réglage enregistré")); });
            }},
        });
        popup_note_ = tr("Un fichier .torrent déposé dans « Torrents à ajouter » (par FTP, depuis un PC, une clé USB) est ajouté seul en moins d'une minute, puis renommé « .ajouté ».");
    }});
    const std::string enc = n["bt_encrypt"].as_string().empty() ? std::string("prefer") : n["bt_encrypt"].as_string();
    auto enc_text = [](const std::string& e) { return std::string(e == "force" ? tr("Obligatoire") : e == "off" ? tr("Désactivé") : tr("Préféré")); };
    acts.push_back({trf("Chiffrement du protocole : %s", enc_text(enc).c_str()), [this, enc, mark, set, enc_text] {
        open_actions(tr("Chiffrement du protocole"), {
            {mark(enc == "prefer", enc_text("prefer") + "  ·  " + tr("chiffré quand l'autre sait le faire")), [set] { set("bt_encrypt", Json(std::string("prefer"))); }},
            {mark(enc == "force", enc_text("force") + "  ·  " + tr("seulement avec ceux qui chiffrent")), [set] { set("bt_encrypt", Json(std::string("force"))); }},
            {mark(enc == "off", enc_text("off") + "  ·  " + tr("jamais chiffré")), [set] { set("bt_encrypt", Json(std::string("off"))); }},
        });
        popup_note_ = tr("Le chiffrement cache aux fournisseurs d'accès qu'il s'agit de BitTorrent (certains le ralentissent). Obligatoire : moins de sources.");
    }});
    std::vector<std::string> blocked;
    for (const auto& e : n["bt_blocklist"].items()) blocked.push_back(e.as_string());
    acts.push_back({trf("Adresses bloquées : %d", static_cast<int>(blocked.size())), [this, blocked] {
        auto save = [this](std::vector<std::string> v) {
            Json a = Json::array();
            for (const auto& x : v) a.push(x);
            net_set("bt_blocklist", a);
            toast(tr("Réglage enregistré"));
        };
        std::vector<std::pair<std::string, std::function<void()>>> a;
        a.push_back({tr("+ Ajouter une adresse ou une plage…"), [this, blocked, save] {
            open_keyboard(tr("Adresse à bloquer (1.2.3.4, 1.2.3.0/24 ou 1.2.3.4-1.2.3.9)"), "", [](const std::string&) {});
            kb_done_ = [this, blocked, save] {
                const std::string t = kb_text_;
                if (t.empty()) return;
                if (blocked.size() >= 200) { toast(tr("200 adresses au plus")); return; }
                std::vector<std::string> v = blocked;
                v.push_back(t);
                save(v);
            };
        }});
        for (std::size_t i = 0; i < blocked.size(); ++i)
            a.push_back({trf("Retirer %s", blocked[i].c_str()), [blocked, i, save] { std::vector<std::string> v = blocked; v.erase(v.begin() + static_cast<long>(i)); save(v); }});
        open_actions(tr("Adresses bloquées"), std::move(a));
        popup_note_ = tr("Aucun échange avec ces adresses, pour tous les torrents. Une adresse invalide est ignorée.");
    }});
    acts.push_back({night ? trf("Plein débit la nuit : de %d h à %d h", nf, nt) : trf("Plein débit la nuit : %s", tr("Non")), [this, night, nf, nt, mark] {
        Acts a;
        a.push_back({mark(!night, tr("Non")), [this] { net_set("bt_night", Json(false)); toast(tr("Réglage enregistré")); }});
        for (auto [f, t] : {std::pair<int, int>{0, 6}, {1, 7}, {2, 8}, {23, 7}, {22, 6}})
            a.push_back({mark(night && nf == f && nt == t, trf("De %d h à %d h", f, t)), [this, f, t] {
                Json b = Json::object();
                b.set("bt_night", true);
                b.set("bt_night_from", static_cast<std::int64_t>(f));
                b.set("bt_night_to", static_cast<std::int64_t>(t));
                model_.command("POST", "/api/v2/settings", b.dump(), [this](const std::string& e, const std::string&) { post_toast(e.empty() ? tr("Réglage enregistré") : e); });
            }});
        open_actions(tr("Plein débit la nuit"), std::move(a));
        popup_note_ = tr("Pendant ces heures (heure de la console), les téléchargements vont sans limite ; le reste du temps, le débit maximal réglé. Pendant une partie, le débit de partie reste appliqué.");
    }});
    open_actions(tr("Options BitTorrent"), std::move(acts));
}

void Ui::open_magnet() {
    mag_torrent_.clear();
    mag_open_ = true;
    mag_qr_ = Json();
    mag_count0_ = tor_["torrents"].is_array() ? static_cast<int>(tor_["torrents"].items().size()) : -1;
    model_.command("GET", "/api/v2/web-link?place=magnet", "", [this](const std::string& e, const std::string& body) {
        Json j;
        const bool ok = e.empty() && Json::parse(body, j);
        post_ui([this, ok, j] { if (ok) mag_qr_ = j; });
    });
}

void Ui::input_magnet(std::uint32_t k) {
    if (k & key::circle) { mag_open_ = false; return; }
    if (!mag_torrent_.empty()) return;  // code QR du .torrent : rien d'autre à faire ici
    if (k & key::cross) {
        mag_open_ = false;
        open_keyboard(tr("Lien magnet (magnet:?xt=…)"), "", [](const std::string&) {});
        kb_done_ = [this] {
            const std::string t = kb_text_;
            if (t.rfind("magnet:", 0) != 0) { if (!t.empty()) toast(tr("Ce n'est pas un lien magnet (il commence par « magnet: »)")); return; }
            add_torrent(t);
        };
    }
}

void Ui::draw_magnet(Surface& s) {
    load_torrents();
    const int n = tor_["torrents"].is_array() ? static_cast<int>(tor_["torrents"].items().size()) : -1;
    if (mag_torrent_.empty()) {
        if (mag_count0_ < 0) mag_count0_ = n;
        else if (n > mag_count0_) { mag_open_ = false; toast(tr("Ajouté : il apparaît dans « En cours »")); return; }
    }
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 170));
    const int w = 1300, h = 640, x = (1920 - w) / 2, y = (1080 - h) / 2 - 20;
    window_frame(s, x, y, w, h, 112);
    help_in(x, y, w, h);
    const bool tfile = !mag_torrent_.empty();  // le code QR du .torrent d'un torrent (Actions › Télécharger le .torrent…)
    s.text(font_, x + 40, y + 30, tfile ? std::string(tr("Télécharger le .torrent")) : std::string(tr("+ Lien magnet")), large, color::text, w - 80);
    s.text(font_, x + 40, y + 78, tfile ? mag_torrent_ + ".torrent" : std::string(tr("liens magnet et fichiers .torrent")), small, color::muted, w - 80);
    // À gauche : taper le lien au clavier ; à droite : le téléphone (même code QR que pour les liens de partage).
    const int lw = w - 80 - 420;
    int ty = y + 150;
    if (tfile) {
        for (const auto& l : wrap(tr("Scannez avec le téléphone : la page web de la console s'ouvre et propose d'enregistrer le fichier .torrent, à donner ou à ouvrir dans un autre logiciel."), body, lw)) { s.text(font_, x + 40, ty, l, body, color::text); ty += 42; }
        ty += 8;
        for (const auto& l : wrap(tr("Le téléphone doit être sur le réseau de la maison."), small, lw)) { s.text(font_, x + 40, ty, l, small, color::muted); ty += 32; }
    } else {
    for (const auto& l : wrap(tr("Plus simple : collez le lien depuis le téléphone"), body, lw)) { s.text(font_, x + 40, ty, l, body, color::text); ty += 42; }
    ty += 8;
    for (const auto& l : wrap(tr("Scannez : la page web s'ouvre sur ce formulaire, où vous collez le lien reçu."), small, lw)) { s.text(font_, x + 40, ty, l, small, color::muted); ty += 32; }
    ty += 30;
    const std::string kb = tr("Lien magnet (magnet:?xt=…)");
    const int bw = std::min(lw, Surface::measure(kb, body) + 56);
    s.outline(x + 36, ty - 4, bw + 8, 64, color::accent, 32, 3);
    s.rect(x + 40, ty, bw, 56, color::panel2, 28);
    s.text(font_, x + 68, ty + 12, kb, body, color::text, bw - 56);
    }
    const auto& rows = mag_qr_["qr"].items();
    const int m = static_cast<int>(rows.size()), qs = 380, qx = x + w - 40 - qs, qy = y + 150;
    s.exact_colors(true);
    s.rect(qx, qy, qs, qs, rgb(255, 255, 255), 12);
    if (m) {
        const double cell = qs / (m + 4.0);
        for (int r = 0; r < m; ++r) {
            const std::string& line = rows[static_cast<std::size_t>(r)].as_string();
            for (int c = 0; c < m && c < static_cast<int>(line.size()); ++c)
                if (line[static_cast<std::size_t>(c)] == '1') s.rect(qx + static_cast<int>((c + 2) * cell), qy + static_cast<int>((r + 2) * cell), static_cast<int>((c + 3) * cell) - static_cast<int>((c + 2) * cell), static_cast<int>((r + 3) * cell) - static_cast<int>((r + 2) * cell), rgb(0, 0, 0), 0);
        }
    }
    s.exact_colors(false);
    s.text(font_, qx, qy + qs + 14, mag_qr_["url"].as_string(), small, color::muted, qs);
    if (tfile) draw_footer(s, {{1, tr("Fermer")}});
    else draw_footer(s, {{0, tr("Clavier")}, {1, tr("Fermer")}});
}

void Ui::open_torrent_detail(const std::string& id) {
    td_open_ = true;
    td_id_ = id;
    td_ = Json();
    td_tab_ = td_row_ = 0;
    td_loaded_ = 0;
    load_torrent_detail();
}

void Ui::load_torrent_detail() {
    if (td_loading_ || (td_loaded_ && now_ - td_loaded_ < 1500)) return;
    td_loading_ = true;
    td_loaded_ = now_;
    const std::string id = td_id_;
    model_.command("GET", "/api/v2/torrents/" + id, "", [this, id](const std::string& e, const std::string& body) {
        Json j;
        const bool ok = e.empty() && Json::parse(body, j);
        post_ui([this, ok, j, id, e] {
            td_loading_ = false;
            if (id != td_id_) return;
            if (ok) td_ = j;
            else if (td_open_ && td_.is_null()) { td_open_ = false; toast(e.empty() ? std::string(tr("Torrent introuvable")) : e); }
        });
    });
}

namespace {
const char* tor_state_text(const std::string& st) {
    return st == "metadata" ? tr("recherche des informations du lien") : st == "waiting" ? tr("en attente de sources")
         : st == "queued" ? tr("attend son tour dans la file") : st == "checking" ? tr("vérification de ce qui est déjà là")
         : st == "seeding" ? tr("terminé · partagé") : st == "complete" ? tr("terminé · partage arrêté") : st == "paused" ? tr("en pause") : st == "error" ? tr("erreur") : tr("téléchargement");
}
const char* prio_text(std::int64_t p) { return p == 0 ? tr("ne pas télécharger") : p >= 7 ? tr("haute") : p <= 3 ? tr("basse") : tr("normale"); }
}  // namespace

void Ui::draw_torrent_detail(Surface& s) {
    load_torrent_detail();
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 170));
    const int x = 110, y = 60, w = 1700, h = 950;
    window_frame(s, x, y, w, h, 112);
    if (!popup_) help_in(x, y, w, h);
    const Json& t = td_;
    if (t.is_null()) {
        s.text(font_, x + 40, y + 30, tr("Chargement…"), large, color::text);
        if (!popup_) draw_footer(s, {{1, tr("Fermer")}});
        return;
    }
    const std::string st = t["state"].as_string();
    const std::uint64_t total = static_cast<std::uint64_t>(t["total"].as_int()), done = static_cast<std::uint64_t>(t["done"].as_int());
    s.text(font_, x + 40, y + 26, t["name"].as_string(), large, color::text, w - 80);
    std::string sub = std::string("BitTorrent · ") + tor_state_text(st);
    if (t["forced"].as_bool()) sub += std::string(" · ") + tr("démarrage forcé");
    if (t["queue"].as_int(-1) >= 0 && st != "seeding" && st != "complete") sub += " · " + trf("n° %d dans la file", static_cast<int>(t["queue"].as_int() + 1));
    if (st == "error") sub += " · " + t["error"].as_string();
    s.text(font_, x + 40, y + 76, sub, small, st == "error" ? color::bad : color::muted, w - 80);
    // Onglets (L1 / R1, ou gauche / droite).
    const int nfiles = static_cast<int>(t["files"].items().size()), npeers = static_cast<int>(t["peer_list"].items().size()), ntr = static_cast<int>(t["trackers"].items().size());
    const std::string tabs[4] = {tr("Général"), trf("Fichiers · %d", nfiles), trf("Pairs · %d", npeers), trf("Trackers · %d", ntr)};
    int tx = x + 40;
    const int ty = y + 130;
    for (int i = 0; i < 4; ++i) {
        const int tw = Surface::measure(tabs[i], body) + 48;
        const bool on = td_tab_ == i;
        s.rect(tx, ty, tw, 52, on ? color::accent : color::panel2, 26);
        s.text(font_, tx + 24, ty + 9, tabs[i], body, on ? rgb(10, 14, 28) : color::text);
        tx += tw + 14;
    }
    const int cy = ty + 80, cx = x + 40, cw = w - 80, bottom = y + h - 100;
    if (td_tab_ == 0) {
        const std::int64_t down = t["down"].as_int(), up = t["up"].as_int(), eta = t["eta"].as_int(-1);
        const double ratio = t["ratio"].as_number(), avail = t["availability"].as_number(-1);
        auto rate = [](std::int64_t ko) { return ko <= 0 ? std::string(tr("sans limite")) : ko >= 1024 ? trf("%lld Mo/s", static_cast<long long>(ko / 1024)) : trf("%lld Ko/s", static_cast<long long>(ko)); };
        char rbuf[32], abuf[32];
        std::snprintf(rbuf, sizeof rbuf, "%.2f", ratio);
        std::snprintf(abuf, sizeof abuf, "%.2f", avail);
        const std::vector<std::pair<const char*, std::string>> rows = {
            {N_("Progression"), total ? trf("%s sur %s (%d %%)", size_text(done).c_str(), size_text(total).c_str(), static_cast<int>(100.0 * static_cast<double>(done) / static_cast<double>(total))) : std::string(tr("taille encore inconnue"))},
            {N_("Vitesse"), "↓ " + size_text(static_cast<std::uint64_t>(down)) + "/s   ↑ " + size_text(static_cast<std::uint64_t>(up)) + "/s"},
            {N_("Temps restant"), st == "seeding" || st == "complete" ? std::string(tr("terminé")) : eta >= 0 ? duration_text(static_cast<double>(eta)) : std::string(tr("inconnu (aucune réception)"))},
            {N_("Reçu · envoyé · ratio"), size_text(static_cast<std::uint64_t>(t["downloaded"].as_int())) + " · " + size_text(static_cast<std::uint64_t>(t["uploaded"].as_int())) + " · " + rbuf},
            {N_("Sources · pairs (connectés)"), trf("%d · %d", static_cast<int>(t["seeds"].as_int()), static_cast<int>(t["peers"].as_int()))},
            {N_("Dans le réseau (selon les trackers)"), t["swarm_seeds"].as_int(-1) >= 0 ? trf("%d sources · %d pairs", static_cast<int>(t["swarm_seeds"].as_int()), static_cast<int>(t["swarm_peers"].as_int(0))) : std::string(tr("inconnu"))},
            {N_("Disponibilité (copies complètes)"), avail >= 0 ? std::string(abuf) : std::string(tr("inconnue"))},
            {N_("Morceaux"), std::to_string(t["pieces"].as_int())},
            {N_("Limites de ce torrent"), "↓ " + rate(t["limit_down"].as_int()) + "   ↑ " + rate(t["limit_up"].as_int())},
            {N_("Dans l'ordre"), t["sequential"].as_bool() ? std::string(tr("oui")) : std::string(tr("non"))},
            {N_("Rangé dans"), t["save"].as_string()},
        };
        const int lw = cw - 470;
        int ry = cy;
        for (const auto& r : rows) {
            if (ry + 54 > bottom) break;
            s.rect(cx, ry, lw, 50, rgb(255, 255, 255, 10), 12);
            s.text(font_, cx + 20, ry + 10, tr(r.first), small, color::muted, 420);
            s.text(font_, cx + 460, ry + 10, r.second, small, color::text, lw - 480);
            ry += 56;
        }
        // Le lien magnet en code QR : à scanner depuis un téléphone ou une autre console.
        const int qs = 400, qx = x + w - 40 - qs, qy = cy;
        const Json& q = t["magnet_qr"];
        s.exact_colors(true);
        s.rect(qx, qy, qs, qs, rgb(255, 255, 255), 12);
        const auto& qrows = q.items();
        const int m = static_cast<int>(qrows.size());
        if (m) {
            const double cell = qs / (m + 4.0);
            for (int r = 0; r < m; ++r) {
                const std::string& line = qrows[static_cast<std::size_t>(r)].as_string();
                for (int c = 0; c < m && c < static_cast<int>(line.size()); ++c)
                    if (line[static_cast<std::size_t>(c)] == '1') s.rect(qx + static_cast<int>((c + 2) * cell), qy + static_cast<int>((r + 2) * cell), static_cast<int>((c + 3) * cell) - static_cast<int>((c + 2) * cell), static_cast<int>((r + 3) * cell) - static_cast<int>((r + 2) * cell), rgb(0, 0, 0), 0);
            }
        }
        s.exact_colors(false);
        if (!m) s.text(font_, qx + 30, qy + qs / 2 - 20, tr("Lien trop long pour un code QR"), small, rgb(60, 60, 60), qs - 60);
        int ly = qy + qs + 14;  // la légende sur plusieurs lignes : jamais coupée, quelle que soit la langue
        for (const auto& l : wrap(tr("Lien magnet : scannez-le pour le partager"), small, qs)) { s.text(font_, qx, ly, l, small, color::text); ly += 34; }
        const std::string cap = !t["public"].as_string().empty() ? trf("Marche à la maison et depuis Internet (%s), si le port est ouvert sur la box", t["public"].as_string().c_str())
                              : std::string(tr("Marche sur le réseau de la maison ; depuis Internet, dès que l'adresse publique est connue"));
        for (const auto& l : wrap(cap, small, qs)) { s.text(font_, qx, ly, l, small, color::muted); ly += 34; }
    } else {
        const int rh = td_tab_ == 1 ? 62 : 56, room = std::max(1, (bottom - cy) / (rh + 8));
        const Json& list = td_tab_ == 1 ? t["files"] : td_tab_ == 2 ? t["peer_list"] : t["trackers"];
        const int n = static_cast<int>(list.items().size());
        td_row_ = std::clamp(td_row_, 0, std::max(0, n - 1));
        if (n == 0) s.text(font_, cx, cy + 10, td_tab_ == 1 ? tr("La liste des fichiers arrive avec les informations du lien.") : td_tab_ == 2 ? tr("Aucun pair connecté pour l'instant.") : tr("Aucun tracker : les sources viennent du DHT."), body, color::muted, cw);
        const int first = std::clamp(td_row_ - room + 1, 0, std::max(0, n - room));
        int ry = cy;
        for (int i = first; i < n && i < first + room; ++i) {
            const Json& e = list.items()[static_cast<std::size_t>(i)];
            const bool on = td_row_ == i;
            s.rect(cx, ry, cw, rh, on ? color::panel2 : rgb(255, 255, 255, 10), 14);
            if (on) s.outline(cx, ry, cw, rh, color::accent, 14, 3);
            if (td_tab_ == 1) {
                const std::int64_t sz = e["size"].as_int(), dn = e["done"].as_int(), pr = e["priority"].as_int(4);
                const std::string right = size_text(static_cast<std::uint64_t>(sz)) + " · " + (sz ? std::to_string(static_cast<int>(100.0 * static_cast<double>(dn) / static_cast<double>(sz))) : std::string("0")) + " % · " + prio_text(pr);
                const int rw = Surface::measure(right, small);
                s.text(font_, cx + 20, ry + 8, e["path"].as_string(), small, pr == 0 ? color::muted : color::text, cw - rw - 60);
                s.text(font_, cx + cw - 20 - rw, ry + 8, right, small, pr == 0 ? color::muted : pr >= 7 ? color::accent : color::text);
                s.rect(cx + 20, ry + rh - 14, cw - 40, 6, rgb(255, 255, 255, 20), 3);
                if (sz) s.rect(cx + 20, ry + rh - 14, std::max(6, static_cast<int>((cw - 40) * static_cast<double>(dn) / static_cast<double>(sz))), 6, dn >= sz ? color::ok : color::accent, 3);
            } else if (td_tab_ == 2) {
                const std::string right = "↓ " + size_text(static_cast<std::uint64_t>(e["down"].as_int())) + "/s   ↑ " + size_text(static_cast<std::uint64_t>(e["up"].as_int())) + "/s   " + std::to_string(static_cast<int>(100 * e["progress"].as_number())) + " %";
                const int rw = Surface::measure(right, small);
                std::string who = e["ip"].as_string() + "   " + e["client"].as_string();
                if (e["seed"].as_bool()) who += std::string("   · ") + tr("source");
                if (e["utp"].as_bool()) who += "   · uTP";
                s.text(font_, cx + 20, ry + 12, who, small, color::text, cw - rw - 60);
                s.text(font_, cx + cw - 20 - rw, ry + 12, right, small, color::muted);
            } else {
                const std::string sst = e["status"].as_string();
                std::string right = sst == "ok" ? std::string(tr("répond")) : sst == "updating" ? std::string(tr("contact en cours")) : sst == "error" ? std::string(tr("erreur")) : std::string(tr("pas encore contacté"));
                if (e["seeds"].as_int(-1) >= 0) right += " · " + trf("%d sources · %d pairs", static_cast<int>(e["seeds"].as_int()), static_cast<int>(e["peers"].as_int(0)));
                const int rw = Surface::measure(right, small);
                s.text(font_, cx + 20, ry + 12, e["url"].as_string(), small, color::text, cw - rw - 60);
                s.text(font_, cx + cw - 20 - rw, ry + 12, right, small, sst == "ok" ? color::ok : sst == "error" ? color::bad : color::muted);
            }
            ry += rh + 8;
        }
        if (n > room) s.text(font_, cx, bottom - 4, trf("%d en tout : Haut / bas pour les voir", n), small, color::muted);
    }
    if (popup_) return;
    const bool paused = t["paused"].as_bool();
    if (td_tab_ == 3) draw_footer(s, {{0, td_["trackers"].items().empty() ? tr("Ajouter un tracker…") : tr("Modifier")}, {3, paused ? tr("Reprendre") : tr("Mettre en pause")}, {-1, tr("Options : actions")}, {-1, tr("L1 / R1 : onglets")}, {1, tr("Fermer")}});
    else if (td_tab_ == 1) draw_footer(s, {{0, tr("Priorité")}, {3, paused ? tr("Reprendre") : tr("Mettre en pause")}, {-1, tr("Options : actions")}, {-1, tr("L1 / R1 : onglets")}, {1, tr("Fermer")}});
    else draw_footer(s, {{3, paused ? tr("Reprendre") : tr("Mettre en pause")}, {-1, tr("Options : actions")}, {-1, tr("L1 / R1 : onglets")}, {1, tr("Fermer")}});
}

void Ui::input_torrent_detail(std::uint32_t k) {
    if (k & key::circle) { td_open_ = false; return; }
    if (k & (key::l1 | key::left)) { td_tab_ = (td_tab_ + 3) % 4; td_row_ = 0; return; }
    if (k & (key::r1 | key::right)) { td_tab_ = (td_tab_ + 1) % 4; td_row_ = 0; return; }
    if (td_.is_null()) return;
    const Json& list = td_tab_ == 1 ? td_["files"] : td_tab_ == 2 ? td_["peer_list"] : td_["trackers"];
    const int n = td_tab_ == 0 ? 0 : static_cast<int>(list.items().size());
    if (k & key::up) { td_row_ = std::max(0, td_row_ - 1); return; }
    if (k & key::down) { td_row_ = std::min(std::max(0, n - 1), td_row_ + 1); return; }
    if (k & key::triangle) {
        const bool paused = td_["paused"].as_bool();
        torrent_post(td_id_, paused ? "resume" : "pause", "", paused ? tr("Reprise là où il s'était arrêté") : tr("En pause : il reprendra là où il s'est arrêté"));
        return;
    }
    auto add_tracker = [this] {
        const std::string id = td_id_;
        open_keyboard(tr("Adresse du tracker (udp://…, http://…, https://…)"), "udp://", [](const std::string&) {});
        kb_done_ = [this, id] {
            if (kb_text_.empty() || kb_text_ == "udp://") return;
            Json b = Json::object();
            b.set("url", kb_text_);
            torrent_post(id, "tracker_add", b.dump(), tr("Tracker ajouté"));
        };
    };
    if (k & key::options) {
        if (td_tab_ == 3) {  // sur les trackers : en ajouter un
            const Json t = td_;
            open_actions(t["name"].as_string(), {
                {tr("Ajouter un tracker…"), add_tracker},
                {tr("Toutes les actions du torrent…"), [this, t] { open_torrent_menu(t, true); }},
            });
            return;
        }
        if (td_tab_ == 1 && n > 0) {  // sur les fichiers : aussi la priorité de tous
            const Json t = td_;
            const std::string id = td_id_;
            open_actions(t["name"].as_string(), {
                {tr("Priorité de tous les fichiers…"), [this, id] { torrent_file_priority(id, -1, -1, ""); }},
                {tr("Toutes les actions du torrent…"), [this, t] { open_torrent_menu(t, true); }},
            });
            return;
        }
        open_torrent_menu(td_, true);
        return;
    }
    if ((k & key::cross) && td_tab_ == 3) {  // un tracker : changer son adresse (il a déménagé) ou le retirer
        if (n == 0) { add_tracker(); return; }
        const std::string id = td_id_, url = list.items()[static_cast<std::size_t>(std::min(td_row_, n - 1))]["url"].as_string();
        open_actions(url, {
            {tr("Changer l'adresse…"), [this, id, url] {
                open_keyboard(tr("Nouvelle adresse du tracker"), url, [](const std::string&) {});
                kb_done_ = [this, id, url] {
                    if (kb_text_.empty() || kb_text_ == url) return;
                    Json b = Json::object();
                    b.set("url", url);
                    b.set("to", kb_text_);
                    torrent_post(id, "tracker_edit", b.dump(), tr("Adresse du tracker changée"));
                };
            }},
            {tr("Ajouter un tracker…"), add_tracker},
            {tr("Retirer ce tracker"), [this, id, url] {
                Json b = Json::object();
                b.set("url", url);
                torrent_post(id, "tracker_remove", b.dump(), tr("Tracker retiré"));
            }},
        });
        return;
    }
    if ((k & key::cross) && td_tab_ == 1 && n > 0) {
        const Json& f = list.items()[static_cast<std::size_t>(std::min(td_row_, n - 1))];
        std::string name = f["path"].as_string();
        if (const auto sl = name.rfind('/'); sl != std::string::npos) name = name.substr(sl + 1);
        torrent_file_priority(td_id_, static_cast<int>(f["index"].as_int()), static_cast<int>(f["priority"].as_int(4)), name);
    }
}

void Ui::load_apps_dl() {
    apps_dl_at_ = now_;
    model_.command("GET", "/api/v2/apps-downloads", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) return;
        post_ui([this, j] { apps_dl_ = j; });
    });
}

bool Ui::apps_dl_present() const {
    for (const auto& a : apps_dl_["apps"].items()) if (a["present"].as_bool()) return true;
    return false;
}

std::string Ui::apps_dl_state(const Json& x) const {
    const std::string st = x["state"].as_string();
    const std::int64_t r = x["received"].as_int(), t = x["total"].as_int(), er = x["extracted"].as_int(), et = x["extract_total"].as_int();
    if (st == "done") {
        std::string out = tr("terminé");
        if (x["installed"].as_bool()) out += std::string(" · ") + tr("installé");
        const std::string sm = x["shadow_mount"].as_string();
        if (sm == "confirmed") out += std::string(" · ") + tr("image montée");
        else if (!sm.empty()) out += std::string(" · ") + tr("image pas encore montée");
        return out;
    }
    if (st == "missing") return tr("fichier introuvable");
    if (st == "extracting") return trf("extraction %d %%", et > 0 ? static_cast<int>(er * 100 / et) : 0);
    if (st == "queued") return tr("en attente");
    if (st == "paused") return trf("en pause · %s sur %s", size_text(static_cast<std::uint64_t>(r)).c_str(), size_text(static_cast<std::uint64_t>(t)).c_str());
    if (st == "error") return x["error"].as_string().empty() ? std::string(tr("en échec")) : x["error"].as_string();
    return trf("téléchargement %d %%", t > 0 ? static_cast<int>(r * 100 / t) : 0);
}

std::string Ui::apps_dl_summary() const {
    int n = 0, missing = 0, temps = 0;
    for (const auto& x : apps_dl_["items"].items()) { ++n; if (x["state"].as_string() == "missing") ++missing; }
    for (const auto& t : apps_dl_["temps"].items()) { (void)t; ++temps; }
    if (!n && !temps) return tr("rien de leur part pour l'instant");
    std::string out = trf("%d téléchargement(s)", n);
    if (missing) out += " · " + trf("%d introuvable(s)", missing);
    if (temps) out += " · " + trf("fichiers de passage : %d (%s)", temps, size_text(static_cast<std::uint64_t>(apps_dl_["temp_bytes"].as_int())).c_str());
    return out;
}

void Ui::load_arrivals() {
    arrivals_at_ = now_ ? now_ : 1;
    model_.command("GET", "/api/v2/arrivals", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) return;
        post_ui([this, j] { arrivals_ = j; });
    });
}

// Bulle « Arrivés par Orbit » de l'Accueil (maquette docs/maquettes/orbit-etape3.html, revue le 19/11 : des jeux, pas des
// parties — rien n'oblige à les sauvegarder ; on peut les laisser, les ranger, les compresser) : rien n'est touché chez eux.
void Ui::open_arrivals() {
    int n = 0, images = 0, unsaved = 0;
    std::set<std::string> apps;
    for (const auto& a : arrivals_["items"].items()) {
        if (a["later"].as_bool()) continue;
        ++n;
        apps.insert(a["app_name"].as_string());
        if (!a["saved"].as_bool()) ++unsaved;
        if (!a["pkg"].as_bool() && a["format"].as_string() != "FPKG") ++images;
    }
    const std::string who = apps.size() == 1 ? *apps.begin() : std::string("Orbit / BlackBox");
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    if (unsaved) acts.push_back({tr("Sauvegarder…"), [this] {
        std::vector<std::pair<std::string, std::function<void()>>> dests;
        auto go = [this](const std::string& place) {
            Json b = Json::object();
            b.set("place", place);
            model_.command("POST", "/api/v2/arrivals/backup", b.dump(), [this](const std::string& e, const std::string& body) {
                Json j;
                Json::parse(body, j);
                const int t = static_cast<int>(j["tasks"].size()), k = static_cast<int>(j["skipped"].size());
                std::string msg = !e.empty() ? e : trf("%d sauvegarde(s) ajoutée(s) aux tâches", t);
                if (e.empty() && k) msg += " · " + trf("%d pas sauvegardé(s) : %s", k, j["skipped"].items()[0]["error"].as_string().c_str());
                post_toast(msg);
                post_ui([this] { arrivals_at_ = 0; });
            });
        };
        dests.push_back({trf("Endroit habituel (%s)", where_label(snap_->default_place).c_str()), [go] { go(""); }});
        for (const auto& p : snap_->places) {
            if (p.readonly) continue;
            const std::string id = p.id;
            dests.push_back({p.label, [go, id] { go(id); }});
        }
        dests.push_back({tr("Annuler"), [] {}});
        open_actions(tr("Sauvegarder vers…"), std::move(dests));
        popup_note_ = tr("Un jeu installé se sauvegarde comme les autres ; un .pkg pas installé est copié tel quel vers un NAS, le nuage ou une autre PS5.");
    }});
    acts.push_back({tr("Installer…"), [this] { tab_ = downloads; open_apps_dl(); }});
    acts.push_back({tr("Déplacer vers…"), [this] { tab_ = downloads; open_apps_dl(); }});
    if (images) acts.push_back({tr("Compresser en .pkg (Kraken)…"), [this] { tab_ = downloads; open_apps_dl(); }});
    acts.push_back({tr("Voir dans Téléchargements › Orbit et BlackBox"), [this] { tab_ = downloads; open_apps_dl(); }});
    acts.push_back({tr("Vu"), [this] {
        model_.command("POST", "/api/v2/arrivals/later", "{}", [this](const std::string&, const std::string&) { post_ui([this] { arrivals_at_ = 0; }); });
        arrivals_.set("new_count", 0);
    }});
    open_actions(trf("%d jeu(x) arrivé(s) par %s", n, who.c_str()), std::move(acts));
    popup_note_ = tr("Les laisser où ils sont, les sauvegarder, les ranger ou les compresser : à vous de voir. Rien n'est touché chez Orbit ni chez BlackBox.");
}

void Ui::open_apps_dl() {
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    for (const auto& i : apps_dl_["items"].items()) {
        const Json x = i;
        std::string line = x["title"].as_string() + " · " + x["app_name"].as_string() + " · " + apps_dl_state(x) + " · " + tr(x["place_label"].as_string().c_str());
        if (x.has("bytes")) line += " · " + size_text(static_cast<std::uint64_t>(x["bytes"].as_int()));
        acts.push_back({line, [this, x] { open_apps_dl_item(x); }});
    }
    for (const auto& i : apps_dl_["temps"].items()) {
        const Json t = i;
        const std::string kind = t["kind"].as_string();
        const char* what = kind == "archive" ? N_("archive gardée après l'extraction") : kind == "partial" ? N_("téléchargement en cours") : N_("reste d'un téléchargement");
        const std::string line = trf("Fichier de passage : %s", t["path"].as_string().substr(t["path"].as_string().rfind('/') + 1).c_str()) + " · " + tr(what) + " · " + t["app_name"].as_string()
                               + " · " + tr(t["place_label"].as_string().c_str()) + " · " + size_text(static_cast<std::uint64_t>(t["bytes"].as_int()));
        acts.push_back({line, [this, t] { open_apps_dl_item(t); }});
    }
    if (acts.empty()) { toast(tr("rien de leur part pour l'instant")); return; }
    acts.push_back({tr("Fermer"), [] {}});
    open_actions(tr("Orbit et BlackBox"), std::move(acts));
    popup_note_ = tr("Ce qu'Orbit et BlackBox ont téléchargé sur cette console, lu dans leurs fichiers : rien n'est changé chez eux.");
}

void Ui::open_apps_dl_item(const Json& x) {
    const bool temp = x.has("kind");
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    // Ranger (étape 2) : un jeu terminé se déplace vers un autre disque de la console ; une archive gardée ou un reste
    // peut être supprimé pour libérer la place — toujours avec une confirmation.
    if (!temp && x["state"].as_string() == "done") {
        acts.push_back({tr("Installer…"), [this, x] {
            const std::string p = x["path"].as_string();
            const bool pkg = p.size() > 4 && p.substr(p.size() - 4) == ".pkg";
            open_install(p, x["title"].as_string(), static_cast<std::uint64_t>(x["bytes"].as_int()), "", pkg);
        }});
        acts.push_back({tr("Déplacer vers…"), [this, x] {
            std::vector<std::pair<std::string, std::function<void()>>> dests;
            for (const auto& p : snap_->places) {
                if ((p.type != "internal" && p.type != "usb") || p.id == x["place"].as_string()) continue;
                const std::string pid = p.id, plabel = p.label;
                dests.push_back({plabel + " · " + trf("%s libres", size_text(p.free).c_str()), [this, x, pid, plabel] {
                    open_actions(trf("Déplacer « %s » vers %s ?", x["title"].as_string().c_str(), plabel.c_str()), {
                        {tr("Oui, déplacer (copie vérifiée, puis l'original retiré)"), [this, x, pid] {
                            Json b = Json::object();
                            b.set("app", x["app"]); b.set("id", x["id"]); b.set("place", pid);
                            model_.command("POST", "/api/v2/apps-downloads/move", b.dump(), [this](const std::string& e, const std::string&) {
                                post_toast(e.empty() ? tr("Déplacement lancé : il suit dans Tâches") : e);
                                std::lock_guard<std::mutex> l(async_mu_);
                                apps_dl_at_ = 0;
                            });
                        }},
                        {tr("Annuler"), [] {}}});
                    popup_note_ = trf("%s ne le retrouvera plus à son ancien endroit ; Backup Center le montre à sa nouvelle place.", x["app_name"].as_string().c_str());
                }});
            }
            if (dests.empty()) { toast(tr("Aucun autre disque sur cette console")); return; }
            dests.push_back({tr("Annuler"), [] {}});
            open_actions(tr("Déplacer vers…"), std::move(dests));
        }});
    }
    // Une image .exfat terminée : la compresser à côté, en .pkg (Kraken) ou en .ffpfsc ; l'image d'Orbit reste.
    if (!temp && x["state"].as_string() == "done" && x["path"].as_string().size() > 6 && x["path"].as_string().substr(x["path"].as_string().size() - 6) == ".exfat") {
        auto compress = [this](const std::string& body) {
            model_.command("POST", "/api/v2/images/plan", body, [this, body](const std::string& e, const std::string& resp) {
                Json pl;
                if (!e.empty()) { post_toast(e); return; }
                if (Json::parse(resp, pl) && !pl["ok"].as_bool(true)) { post_toast(pl["error"].as_string()); return; }
                model_.command("POST", "/api/v2/tasks", body, [this](const std::string& e2, const std::string&) {
                    post_toast(e2.empty() ? tr("Ajouté aux tâches : suivez-le dans Transferts › Transferts en cours") : e2);
                });
            });
        };
        const std::string src = x["path"].as_string();
        acts.push_back({tr("Compresser en .pkg (Kraken)"), [compress, src] {
            Json b = Json::object(); b.set("type", "make_pkg"); b.set("source", src); b.set("pkg_level", 1); compress(b.dump());
        }});
        acts.push_back({tr("Compresser en .ffpfsc"), [compress, src] {
            Json b = Json::object(); b.set("type", "compress"); b.set("source", src); compress(b.dump());
        }});
    }
    if (temp && x["kind"].as_string() != "partial") {
        acts.push_back({tr("Libérer la place…"), [this, x] {
            const std::string name = x["path"].as_string().substr(x["path"].as_string().rfind('/') + 1);
            open_actions(trf("Supprimer « %s » ?", name.c_str()), {
                {trf("Oui, supprimer pour de bon (%s libérés)", size_text(static_cast<std::uint64_t>(x["bytes"].as_int())).c_str()), [this, x] {
                    Json b = Json::object();
                    b.set("path", x["path"]); b.set("confirm", "SUPPRIMER");
                    model_.command("POST", "/api/v2/apps-downloads/free", b.dump(), [this](const std::string& e, const std::string&) {
                        post_toast(e.empty() ? tr("Suppression lancée : elle suit dans Tâches") : e);
                        std::lock_guard<std::mutex> l(async_mu_);
                        apps_dl_at_ = 0;
                    });
                }},
                {tr("Annuler"), [] {}}});
            popup_note_ = tr("Un fichier de passage : le jeu n'en a plus besoin. Il ne passe pas par la corbeille.");
        }});
    }
    auto info = [&](const std::string& t) { acts.push_back({t, [this] { open_apps_dl(); }}); };  // une ligne d'information : retour à la liste
    info(trf("Appli : %s", x["app_name"].as_string().c_str()));
    if (!temp) info(trf("État : %s", apps_dl_state(x).c_str()));
    info(trf("Disque : %s", tr(x["place_label"].as_string().c_str())));
    info(trf("Fichier : %s", x["path"].as_string().c_str()));
    if (x.has("bytes")) info(trf("Taille : %s", size_text(static_cast<std::uint64_t>(x["bytes"].as_int())).c_str()));
    if (!temp && !x["format"].as_string().empty())
        info(trf("Format : %s", (x["format"].as_string() + (x["compression"].as_string().empty() ? std::string() : " (" + x["compression"].as_string() + ")")).c_str()));
    if (!temp && !x["title_id"].as_string().empty()) info(x["installed"].as_bool() ? tr("Installé sur cette console") : tr("Pas installé sur cette console"));
    acts.push_back({tr("Fermer"), [] {}});
    open_actions(x["title"].as_string(), std::move(acts));
}

void Ui::draw_downloads(Surface& s) {
    if (now_ - switches_loaded_ > 10000) load_remotes();
    draw_page_head(s, tr("Téléchargements"), tr("Ce qui arrive d'Internet : liens reçus, PS5 à distance."));
    const auto shares = dl_shares();
    const auto active = dl_tasks(true), done = dl_tasks(false);
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    // Deux familles, chacune sa rangée et ses options (propriétaire, 17/11) : les liens de partage (MEGA, Drive, Dropbox,
    // OneDrive : s'ouvrent comme un disque) et BitTorrent (magnet, .torrent) ; puis ce qui vaut pour les deux.
    int y = 226;
    const int cw = 380, ch = 104, gap = 20;
    const int per = (1920 - 2 * margin + gap) / (cw + gap);
    auto label = [&](const char* title, const char* what) {
        s.text(font_, margin, y, tr(title), small, color::muted);
        s.text(font_, margin + Surface::measure(tr(title), small) + 18, y, tr(what), small, color::muted);
        y += 36;
    };
    label(N_("LIENS DE PARTAGE"), N_("MEGA, Google Drive, Dropbox, OneDrive : s'ouvrent comme un disque"));
    const int ncards = static_cast<int>(shares.size()) + 1;
    dl_card_ = std::clamp(dl_card_, 0, ncards - 1);
    const int first = std::max(0, dl_card_ - per + 1);
    for (int i = first; i < ncards && i < first + per; ++i) {
        const int x = margin + (i - first) * (cw + gap);
        const bool on = dl_zone_ == 0 && dl_card_ == i;
        DevCard c;
        if (i == static_cast<int>(shares.size())) {
            c.light = true; c.name = tr("+ Lien de partage"); c.line = tr("MEGA, Google Drive, Dropbox, OneDrive");
            device_card(s, c, x, y, cw, ch, on);
            continue;
        }
        const Json& r = *shares[static_cast<std::size_t>(i)];
        const std::string svc = r["config"]["host"].as_string();
        const std::int64_t exp = r["config"]["expires"].as_int(), left = exp ? (exp - now + 86399) / 86400 : 0;
        c.dev = cloud_dev("share", svc);
        c.name = r["name"].as_string();
        c.line = std::string(svc_name(svc)) + " · " + (!exp ? std::string(tr("gardé")) : left <= 1 ? std::string(tr("expire aujourd'hui")) : trf("expire dans %d j", static_cast<int>(left)));
        c.line_col = left == 1 ? color::warn : 0;
        device_card(s, c, x, y, cw, ch, on);
    }
    y += ch + 22;
    label(N_("BITTORRENT"), N_("liens magnet et fichiers .torrent"));
    {
        struct BtCard { const char* name; const char* line; bool soon; };
        static const BtCard bt[5] = {{N_("+ Lien magnet"), N_("magnet:?xt=… (clavier)"), false}, {N_("+ Fichier .torrent"), N_("sur l'interne ou une clé USB"), false},
                                     {N_("Partager…"), N_("jeu, ROM, appli, sauvegarde"), false}, {N_("Mes liens"), "", false},
                                     {N_("Réglages BitTorrent"), N_("ports, file, connexions, trackers"), false}};
        dl_bt_ = std::clamp(dl_bt_, 0, 4);
        if (now_ - links_at_ > 5000) load_links();
        const int bw = (1920 - 2 * margin - 4 * gap) / 5;  // cinq cartes sur la rangée
        for (int i = 0; i < 5; ++i) {
            DevCard c;
            c.light = i < 4;
            c.name = tr(bt[i].name);
            c.line = tr(bt[i].line);
            if (i == 3) {  // Mes liens : combien, et s'il y en a un en préparation
                int n = 0, prep = 0;
                for (const auto& l : links_.items()) { ++n; if (l["state"].as_string() == "preparing") ++prep; }
                c.line = prep ? trf("%d en préparation", prep) : n ? trf("%d lien(s) donné(s)", n) : std::string(tr("aucun pour l'instant"));
                if (prep) c.line_col = color::warn;
            }
            if (bt[i].soon) c.line_col = color::muted;
            device_card(s, c, margin + i * (bw + gap), y, bw, ch, dl_zone_ == 5 && dl_bt_ == i);
        }
    }
    y += ch + 22;
    // Pour les deux : où ranger et les débits (Paramètres › Réseau), d'un coup d'œil.
    {
        const Json& n = snap_->net;
        auto rate = [](std::int64_t ko) { return ko <= 0 ? std::string(tr("Illimité")) : ko >= 1024 ? trf("%lld Mo/s", static_cast<long long>(ko / 1024)) : trf("%lld Ko/s", static_cast<long long>(ko)); };
        std::string where = tr("Interne");
        for (const auto& p : snap_->places) if (!n["dl_place"].as_string().empty() && p.id == n["dl_place"].as_string()) where = p.label;
        const std::string line = trf("Rangés dans %s", (where + " › " + tr("Téléchargements")).c_str()) + "   ·   " + trf("Débit : %s", rate(n["bt_rate_max"].as_int(0)).c_str())
                               + "   ·   " + trf("Pendant une partie : %s", rate(n["bt_rate_game"].as_int(2048)).c_str());
        const bool on = dl_zone_ == 6;
        const int w = 1920 - 2 * margin;
        card_frame(s, margin, y, w, 58, on, false, true);
        s.text(font_, margin + 24, y + 13, tr("POUR LES DEUX"), small, color::accent);
        const int lx = margin + 24 + Surface::measure(tr("POUR LES DEUX"), small) + 24;
        s.text(font_, lx, y + 13, line + "  ›", small, color::text, margin + w - 24 - lx);
        y += 58 + 26;
    }
    // Orbit et BlackBox : ce qu'ils ont téléchargé sur cette console (lu chez eux, rien n'y est changé) ; seulement s'ils sont là.
    if (now_ - apps_dl_at_ > 5000) load_apps_dl();
    const bool orbit_bar = apps_dl_present();
    if (orbit_bar) {
        const bool on = dl_zone_ == 7;
        const int w = 1920 - 2 * margin;
        card_frame(s, margin, y - 10, w, 58, on, false, true);
        s.text(font_, margin + 24, y + 3, tr("ORBIT ET BLACKBOX"), small, color::accent);
        const int lx = margin + 24 + Surface::measure(tr("ORBIT ET BLACKBOX"), small) + 24;
        const std::string sum = apps_dl_summary();
        bool missing = false;
        for (const auto& x : apps_dl_["items"].items()) if (x["state"].as_string() == "missing") missing = true;
        s.text(font_, lx, y + 3, sum + "  ›", small, missing ? color::warn : color::text, margin + w - 24 - lx);
        y += 58 + 16;
    } else if (dl_zone_ == 7) dl_zone_ = 6;
    // En cours : les torrents (BitTorrent), puis les téléchargements de liens reçus ; débit, temps restant, sources.
    load_torrents();
    const std::vector<const Json*> tors = dl_tors();
    const int ntor = static_cast<int>(tors.size()), nall = ntor + static_cast<int>(active.size());
    s.text(font_, margin, y, trf("EN COURS · %d", nall), small, color::muted);
    if (nall > (orbit_bar ? 1 : 2)) s.text(font_, margin + Surface::measure(trf("EN COURS · %d", nall), small) + 18, y, trf("%d en tout : Haut / bas pour les voir", nall), small, color::muted);
    y += 36;
    const int w = 1920 - 2 * margin;
    if (nall == 0) { s.text(font_, margin, y + 10, tr("Rien en cours. Ouvrez un lien reçu, ou ajoutez un lien magnet ou un .torrent."), body, color::muted, w); y += 70; }
    dl_row_ = std::clamp(dl_row_, 0, std::max(0, nall - 1));
    const int arows = orbit_bar ? 1 : 2;
    const int afirst = std::clamp(dl_row_ - (arows - 1), 0, std::max(0, nall - arows));
    for (int i = afirst; i < nall && i < afirst + arows; ++i) {
        const bool on = dl_zone_ == 1 && dl_row_ == i;
        s.rect(margin, y, w, 94, on ? color::panel2 : color::panel, 18);
        if (on) s.outline(margin, y, w, 94, color::accent, 18, 3);
        std::string title, line2, right;
        std::uint32_t rc = color::accent, bc = color::accent;
        double f = 0;
        if (i < ntor && (*tors[static_cast<std::size_t>(i)]).has("job")) {  // un lien en préparation
            const Json& c = *tors[static_cast<std::size_t>(i)];
            const std::uint64_t total = static_cast<std::uint64_t>(c["total"].as_int()), done = static_cast<std::uint64_t>(c["done"].as_int());
            title = c["name"].as_string();
            line2 = std::string(tr("Créer un lien")) + " · " + tr("calcul des empreintes, rien n'est copié") + " · " + c["path"].as_string();
            if (c["state"].as_string() == "error") { right = c["error"].as_string(); rc = color::bad; }
            else right = total ? trf("%d %% · %s sur %s", static_cast<int>(100 * done / total), size_text(done).c_str(), size_text(total).c_str()) : std::string(tr("liste des fichiers…"));
            f = total ? static_cast<double>(done) / static_cast<double>(total) : 0;
        } else if (i < ntor) {  // un torrent
            const Json& t = *tors[static_cast<std::size_t>(i)];
            const std::string st = t["state"].as_string();
            const std::uint64_t total = static_cast<std::uint64_t>(t["total"].as_int()), done = static_cast<std::uint64_t>(t["done"].as_int());
            const std::int64_t down = t["down"].as_int();
            title = t["name"].as_string();
            std::string what = tor_state_text(st);
            if (st == "queued" && t["queue"].as_int(-1) >= 0) what = trf("attend son tour (n° %d dans la file)", static_cast<int>(t["queue"].as_int() + 1));
            if (t["forced"].as_bool()) what += std::string(" · ") + tr("démarrage forcé");
            // Un lien créé ici qui cherche à télécharger : ses fichiers ne sont plus là où ils étaient (il ne partage rien).
            if (t["created"].as_bool() && (st == "waiting" || st == "downloading" || st == "metadata")) what = tr("fichiers introuvables");
            if (t["created"].as_bool()) what = std::string(tr("lien créé ici")) + " · " + what;
            line2 = std::string("BitTorrent · ") + what + " · " + trf("%d source(s), %d pair(s)", static_cast<int>(t["seeds"].as_int()), static_cast<int>(t["peers"].as_int()))
                  + " · " + t["save"].as_string();
            if (st == "error") { right = t["error"].as_string(); rc = color::bad; }
            else if (st == "paused") { right = trf("en pause · %s sur %s", size_text(done).c_str(), size_text(total).c_str()); rc = bc = color::warn; }
            else if (st == "queued") { right = trf("en attente · %s sur %s", size_text(done).c_str(), size_text(total).c_str()); rc = bc = color::muted; }
            else if (st == "seeding" || st == "complete") { right = trf("✓ %s", size_text(total).c_str()); rc = bc = color::ok; }
            else if (down > 0 && total > done) right = size_text(static_cast<std::uint64_t>(down)) + "/s · " + tr("reste") + " " + duration_text(static_cast<double>(total - done) / static_cast<double>(down));
            else right = total ? trf("%s sur %s", size_text(done).c_str(), size_text(total).c_str()) : std::string(tr("taille encore inconnue"));
            f = total ? static_cast<double>(done) / static_cast<double>(total) : 0;
        } else {  // un lien reçu (tâche)
            const Task& t = *active[static_cast<std::size_t>(i - ntor)];
            title = t.title;
            line2 = dl_place_name(t.from_place) + "  →  " + dl_place_name(t.to_place);
            if (t.state == "paused") { right = trf("en pause · %s sur %s", size_text(t.done).c_str(), size_text(t.total).c_str()); rc = bc = color::warn; }
            else if (t.state == "queued") right = tr("en attente");
            else if (t.speed > 0) right = size_text(static_cast<std::uint64_t>(t.speed)) + "/s · " + tr("reste") + " " + duration_text(static_cast<double>(t.total > t.done ? t.total - t.done : 0) / t.speed);
            else right = trf("%s sur %s", size_text(t.done).c_str(), size_text(t.total).c_str());
            f = t.total ? static_cast<double>(t.done) / static_cast<double>(t.total) : 0;
        }
        s.text(font_, margin + 24, y + 10, title, body, color::text, w - 560);
        s.text(font_, margin + 24, y + 46, line2, small, color::muted, w - 48);
        s.text(font_, margin + w - 24 - std::min(520, Surface::measure(right, body)), y + 10, right, body, rc, 520);
        s.rect(margin + 24, y + 78, w - 48, 8, rgb(255, 255, 255, 20), 4);
        s.rect(margin + 24, y + 78, std::max(8, static_cast<int>((w - 48) * std::min(1.0, f))), 8, bc, 4);
        y += 104;
    }
    y += 12;
    // Terminés : vitesse moyenne.
    s.text(font_, margin, y, tr("TERMINÉS"), small, color::muted);
    if (done.size() > 1) s.text(font_, margin + Surface::measure(tr("TERMINÉS"), small) + 18, y, trf("%d en tout : Haut / bas pour les voir", static_cast<int>(done.size())), small, color::muted);
    y += 36;
    if (done.empty()) s.text(font_, margin, y + 6, tr("Aucun téléchargement terminé pour l'instant."), body, color::muted, w);
    const int room = std::max(1, (990 - y) / 64);
    dl_drow_ = std::clamp(dl_drow_, 0, std::max(0, static_cast<int>(done.size()) - 1));
    const int dfirst = std::max(0, dl_drow_ - room + 1);
    for (int i = dfirst; i < static_cast<int>(done.size()) && i < dfirst + room; ++i) {
        const Task& t = *done[static_cast<std::size_t>(i)];
        const bool on = dl_zone_ == 2 && dl_drow_ == i;
        s.rect(margin, y, w, 56, on ? color::panel2 : rgb(255, 255, 255, 10), 14);
        if (on) s.outline(margin, y, w, 56, color::accent, 14, 3);
        s.text(font_, margin + 24, y + 12, t.title, body, color::text, w - 620);
        std::string right;
        std::uint32_t rc = color::ok;
        if (t.state == "done") {
            const std::int64_t secs = std::max<std::int64_t>(1, t.finished - t.started);
            right = "✓ " + size_text(t.total) + " · " + trf("%s/s moyen", size_text(t.total / static_cast<std::uint64_t>(secs)).c_str());
        } else if (t.state == "cancelled") { right = tr("annulé"); rc = color::muted; }
        else { right = t.error.empty() ? std::string(tr("en échec")) : t.error; rc = color::bad; }
        s.text(font_, margin + w - 24 - std::min(600, Surface::measure(right, small)), y + 16, right, small, rc, 600);
        y += 64;
    }
    s.rect(0, 996, 1920, 84, color::bg, 0);
    if (dl_zone_ == 0) draw_footer(s, {{0, dl_card_ < static_cast<int>(shares.size()) ? tr("Ouvrir ses fichiers") : tr("Ajouter un lien de partage")}, {-1, tr("Options : garder plus longtemps, retirer")}, {1, tr("Retour")}});
    else if (dl_zone_ == 5) draw_footer(s, {{0, tr("Choisir")}, {-1, tr("Gauche / droite")}, {1, tr("Retour")}});
    else if (dl_zone_ == 6) draw_footer(s, {{0, tr("Changer (Paramètres › Réseau)")}, {1, tr("Retour")}});
    else if (dl_zone_ == 7) draw_footer(s, {{0, tr("Voir leurs téléchargements")}, {1, tr("Retour")}});
    else if (dl_zone_ == 1) {
        if (dl_row_ < ntor) draw_footer(s, {{0, tr("Voir la fiche")}, {3, tr("Pause / reprendre")}, {-1, tr("Options : actions")}, {1, tr("Retour")}});
        else draw_footer(s, {{0, tr("Actions")}, {3, tr("Pause / reprendre")}, {1, tr("Retour")}});
    }
    else draw_footer(s, {{-1, tr("Options : retirer de la liste")}, {1, tr("Retour")}});
}

void Ui::input_downloads(std::uint32_t k) {
    bar_ = false;  // Téléchargements n'a pas de rangée de filtres : jamais d'état « barre » fantôme (la surbrillance disparaissait)
    const auto shares = dl_shares();
    const auto active = dl_tasks(true), done = dl_tasks(false);
    const int ncards = static_cast<int>(shares.size()) + 1;
    const bool any_tor = !dl_tors().empty();
    static const int order[6] = {0, 5, 6, 7, 1, 2};
    auto has = [&](int z) { return z == 1 ? (!active.empty() || any_tor) : z == 2 ? !done.empty() : z == 7 ? apps_dl_present() : true; };
    auto at = [&](int z) { for (int i = 0; i < 6; ++i) if (order[i] == z) return i; return 0; };
    auto next_zone_down = [&](int from) { for (int i = at(from) + 1; i < 6; ++i) if (has(order[i])) return order[i]; return from; };
    auto next_zone_up = [&](int from) { for (int i = at(from) - 1; i >= 0; --i) if (has(order[i])) return order[i]; return 0; };
    if (dl_zone_ == 5) {  // BitTorrent : + magnet, + .torrent, créer un lien (bientôt), réglages
        if (k & key::left) dl_bt_ = std::max(0, dl_bt_ - 1);
        if (k & key::right) dl_bt_ = std::min(4, dl_bt_ + 1);
        if (k & key::up) { dl_zone_ = 0; return; }
        if (k & key::down) { dl_zone_ = 6; return; }
        if (k & key::circle) { dl_zone_ = 0; return; }
        if (!(k & key::cross)) return;
        if (dl_bt_ == 0) open_magnet();
        else if (dl_bt_ == 1) {
            open_browser(tr("Choisir un fichier .torrent"), "/data", true, [this](const std::string& path) {
                if (path.size() < 8 || path.substr(path.size() - 8) != ".torrent") { toast(tr("Choisissez un fichier .torrent")); return; }
                add_torrent(path);
            });
        } else if (dl_bt_ == 2) open_share_hub();  // « Partager… » : quoi, où, puis la liste
        else if (dl_bt_ == 3) open_my_shares();     // Mes liens : tous les liens donnés, avec leur code QR
        else { open_network_settings(6); }
        return;
    }
    if (dl_zone_ == 7) {  // Orbit et BlackBox : la liste de leurs téléchargements
        if (k & key::up) { dl_zone_ = 6; return; }
        if (k & key::down) { dl_zone_ = next_zone_down(7); dl_row_ = dl_drow_ = 0; return; }
        if (k & key::circle) { dl_zone_ = 0; return; }
        if (k & key::cross) open_apps_dl();
        return;
    }
    if (dl_zone_ == 6) {  // pour les deux : où ranger, débits
        if (k & key::up) { dl_zone_ = 5; return; }
        if (k & key::down) { dl_zone_ = next_zone_down(6); dl_row_ = dl_drow_ = 0; return; }
        if (k & key::circle) { dl_zone_ = 0; return; }
        if (k & key::cross) { open_network_settings(9); }
        return;
    }
    if (dl_zone_ == 0) {
        if (k & key::left) dl_card_ = std::max(0, dl_card_ - 1);
        if (k & key::right) dl_card_ = std::min(ncards - 1, dl_card_ + 1);
        if (k & key::up) return;  // pas de rangée de filtres ici : rien au-dessus des cartes (L1 / R1, L3 pour les menus)
        if (k & key::down) { dl_zone_ = 5; return; }
        if (k & key::circle) return;
        const bool add = dl_card_ >= static_cast<int>(shares.size());
        if ((k & key::cross) && add) { open_editor("", "share"); return; }
        if (add) return;
        const std::string id = (*shares[static_cast<std::size_t>(dl_card_)])["id"].as_string(), name = (*shares[static_cast<std::size_t>(dl_card_)])["name"].as_string();
        if (k & key::cross) { open_share_files(id); return; }
        if (k & key::options) {
            open_actions(name, {
                {tr("Ouvrir ses fichiers"), [this, id] { open_share_files(id); }},
                {tr("Garder plus longtemps…"), [this, id] { open_editor(id, "share"); }},
                {tr("Retirer ce partage…"), [this, id, name] {
                    open_actions(trf("Retirer « %s » ?", name.c_str()), {{tr("Oui, retirer (rien n'est effacé, ni chez vous ni chez la personne qui partage)"), [this, id] {
                        model_.command("POST", "/api/v2/remotes/" + id + "/forget", "{}", [this](const std::string& e, const std::string&) {
                            post_toast(e.empty() ? tr("Partage retiré") : e);
                            std::lock_guard<std::mutex> l(async_mu_);
                            switches_loaded_ = 0;
                        });
                    }}, {tr("Annuler"), [] {}}});
                }},
            });
        }
        return;
    }
    if (dl_zone_ == 1) {
        const std::vector<const Json*> tors = dl_tors();
        const int ntor = static_cast<int>(tors.size()), n = ntor + static_cast<int>(active.size());
        if (n == 0) { dl_zone_ = 0; return; }
        if (k & key::up) { if (dl_row_ > 0) --dl_row_; else dl_zone_ = next_zone_up(1); return; }
        if (k & key::down) { if (dl_row_ + 1 < n) ++dl_row_; else { const int z = next_zone_down(1); if (z != 1) { dl_zone_ = z; dl_drow_ = 0; } } return; }
        if (k & key::circle) { dl_zone_ = 0; return; }
        const int row = std::min(dl_row_, n - 1);
        if (row < ntor && (*tors[static_cast<std::size_t>(row)]).has("job")) {  // un lien en préparation : l'annuler, ou l'oublier s'il a raté
            const Json c = *tors[static_cast<std::size_t>(row)];
            if (!(k & (key::cross | key::options))) return;
            const std::string job = c["job"].as_string(), name = c["name"].as_string();
            const bool failed = c["state"].as_string() == "error";
            open_actions(name, {{failed ? tr("Retirer de la liste") : tr("Annuler la création du lien"), [this, job, failed] {
                Json b = Json::object();
                b.set("cancel", job);
                tor_jobs_cancelled_.insert(job);
                model_.command("POST", "/api/v2/torrents/create", b.dump(), [this, failed](const std::string& e, const std::string&) {
                    post_toast(!e.empty() ? e : failed ? tr("Retiré de la liste") : tr("Création du lien annulée"));
                    std::lock_guard<std::mutex> l(async_mu_);
                    tor_loaded_ = 0;
                });
            }}, {tr("Fermer"), [] {}}});
            return;
        }
        if (row < ntor) {  // un torrent : Croix ouvre sa fiche, Options toutes ses actions ; Triangle : pause / reprise
            const Json t = *tors[static_cast<std::size_t>(row)];
            if (k & key::cross) { open_torrent_detail(t["id"].as_string()); return; }
            if (k & key::options) { open_torrent_menu(t); return; }
            if (k & key::triangle) {
                const bool paused = t["paused"].as_bool();
                model_.command("POST", "/api/v2/torrents/" + t["id"].as_string() + (paused ? "/resume" : "/pause"), "{}", [this, paused](const std::string& e, const std::string&) {
                    post_toast(!e.empty() ? e : paused ? tr("Reprise là où il s'était arrêté") : tr("En pause : il reprendra là où il s'est arrêté"));
                    std::lock_guard<std::mutex> l(async_mu_);
                    tor_loaded_ = 0;
                });
            }
            return;
        }
        const Task& t = *active[static_cast<std::size_t>(row - ntor)];
        if (k & key::triangle) {
            const char* a = t.state == "paused" ? "resume" : "pause";
            model_.command("POST", "/api/v2/tasks/" + t.id + "/" + a, "{}", [this, a](const std::string& e, const std::string&) {
                post_toast(!e.empty() ? e : std::string(a) == "pause" ? tr("En pause : il reprendra là où il s'est arrêté") : tr("Reprise là où il s'était arrêté"));
            });
            return;
        }
        if (k & (key::cross | key::options)) {
            const std::string id = t.id, title = t.title;
            const bool paused = t.state == "paused";
            open_actions(title, {
                {paused ? tr("Reprendre") : tr("Mettre en pause"), [this, id, paused] {
                    model_.command("POST", "/api/v2/tasks/" + id + (paused ? "/resume" : "/pause"), "{}", [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
                }},
                {tr("Arrêter ce téléchargement…"), [this, id, title] {
                    open_actions(trf("Arrêter « %s » ?", title.c_str()), {{tr("Oui, arrêter (ce qui est déjà arrivé et vérifié reste)"), [this, id] {
                        model_.command("POST", "/api/v2/tasks/" + id + "/cancel", "{}", [this](const std::string& e, const std::string&) { post_toast(e.empty() ? tr("Téléchargement arrêté") : e); });
                    }}, {tr("Annuler"), [] {}}});
                }},
            });
        }
        return;
    }
    // Terminés
    const int n = static_cast<int>(done.size());
    if (n == 0) { dl_zone_ = next_zone_up(2); return; }
    if (k & key::up) { if (dl_drow_ > 0) --dl_drow_; else dl_zone_ = next_zone_up(2); return; }
    if (k & key::down) { dl_drow_ = std::min(n - 1, dl_drow_ + 1); return; }
    if (k & key::circle) { dl_zone_ = 0; return; }
    if (k & key::options) {
        const std::string id = done[static_cast<std::size_t>(std::min(dl_drow_, n - 1))]->id;
        open_actions(tr("Terminés"), {{tr("Retirer de la liste"), [this, id] {
            model_.command("POST", "/api/v2/tasks/" + id + "/remove", "{}", [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
        }}, {tr("Annuler"), [] {}}});
    }
}

}  // namespace app
