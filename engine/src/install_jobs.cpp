// Installer par étapes (maquette docs/maquettes/installer-etapes.html) : une installation = une tâche, découpée en
// étapes numérotées (copie, vérification, conversion en .pkg, installation), chacune avec sa barre, sa vitesse et le
// temps restant ; un temps total estimé dès le départ (vitesses mesurées des fois d'avant, sinon prudentes).
// Les tâches de la file qui font le travail (copie, conversion) restent cachées derrière elle.
#include "engine.hpp"

#include <unistd.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <thread>

#include "core/fs.hpp"
#include "core/notify.hpp"
#include "images/images.hpp"
#include "images/pkg_head.hpp"
#include "link/link.hpp"
#include "store/peer.hpp"
#include "store/store.hpp"
#include "tasks/tasks.hpp"

namespace pbc {

namespace {

std::int64_t now_s() { return static_cast<std::int64_t>(std::time(nullptr)); }
std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Vitesses prudentes (octets/s) tant qu'on n'a rien mesuré.
double default_speed(const std::string& step, const std::string& src) {
    if (step == "copy") return src.rfind("remote:", 0) == 0 ? 15e6 : src.rfind("peerfiles:", 0) == 0 ? 20e6 : 60e6;
    if (step == "verify") return 120e6;
    if (step == "convert") return 18e6;
    return 0;
}

Json step(const char* key) {
    Json s = Json::object();
    s.set("key", key); s.set("state", "pending"); s.set("done", 0); s.set("total", 0);
    return s;
}

}  // namespace

std::string Engine::speeds_file() const { return fs::host(fs::join(dir_, "install-speeds.json")); }

double Engine::install_speed(const std::string& step, const std::string& src) {
    std::lock_guard<std::mutex> l(speeds_mu_);  // à part : appelé aussi pendant qu'une installation se met à jour
    if (!speeds_.is_object()) {
        std::string t;
        Json j;
        speeds_ = fs::read(speeds_file(), t, 1u << 20) && Json::parse(t, j) && j.is_object() ? j : Json::object();
    }
    const std::string k = step == "copy" ? "copy:" + src : step;
    const double v = speeds_[k].as_number();
    return v > 1e5 ? v : default_speed(step, src);
}

void Engine::learn_speed(const std::string& step, const std::string& src, double bps) {
    if (bps < 1e5) return;
    std::lock_guard<std::mutex> l(speeds_mu_);  // à part : appelé aussi pendant qu'une installation se met à jour
    if (!speeds_.is_object()) speeds_ = Json::object();
    const std::string k = step == "copy" ? "copy:" + src : step;
    const double old = speeds_[k].as_number();
    speeds_.set(k, old > 1e5 ? old * 0.6 + bps * 0.4 : bps);  // moyenne glissante : une mesure ne change pas tout
    fs::write_atomic(speeds_file(), speeds_.dump());
}

// Ce que la PS5 cible sait installer (un .pkg demande kstuff et Kraken Mount ou notre version intégrée).
bool Engine::pkg_ok_at(const std::string& dst, std::string& why) {
    const bool peer = dst.rfind("peerfiles:", 0) == 0;
    Json m;
    if (!peer) m = mounting();
    else {
        std::string err, inst = dst.substr(10);
        inst = inst.substr(0, inst.find(':'));
        if (!link_ || !link_->call(inst, "GET", "/api/v2/mounting", Json(), m, err)) { why = "l'autre PS5 ne répond pas"; return false; }
        if (!m.has("pkg_ok")) { why = "l'autre PS5 a une version trop ancienne pour les .pkg"; return false; }
    }
    if (!m["pkg_ok"].as_bool()) { why = m["pkg_why"].as_string(); return false; }
    return true;
}

// Les façons d'installer ce jeu sur cette cible, avec leurs étapes, leur temps estimé et la place qu'il faut.
// body : {src_place, dst_place, image_path?, image_size?, image_dir?, pkg_path?, pkg_size?, free?}
Json Engine::install_options(const Json& b) {
    const std::string sp = b["src_place"].as_string();
    std::string dst = b["dst_place"].as_string();
    if (dst.rfind("peer:", 0) == 0) { std::string inst, place; if (store::parse_peer_id(dst, inst, place)) dst = "peerfiles:" + inst + ":" + place; }
    const bool peer = dst.rfind("peerfiles:", 0) == 0;
    const std::uint64_t isz = static_cast<std::uint64_t>(b["image_size"].as_int()), psz = static_cast<std::uint64_t>(b["pkg_size"].as_int());
    const std::uint64_t free = static_cast<std::uint64_t>(b["free"].as_int());
    std::string why;
    const bool has_img = !b["image_path"].as_string().empty(), has_pkg = !b["pkg_path"].as_string().empty();
    const bool pkg_ok = (has_pkg || has_img) && pkg_ok_at(dst, why);
    // Ce que le .pkg dit de lui-même (en-tête seul) : Kraken ne lance qu'un jeu PS5 en fake PKG.
    pkghead::Head ph;
    if (has_pkg) {
        std::string e2;
        if (auto st = open_files_place(sp, e2)) pkghead::read(*st, b["pkg_path"].as_string(), ph);
    }
    const std::string refused = pkghead::kraken_refusal(ph);
    // Un jeu PS4 (CUSA…) ne se convertit pas : le .pkg que l'on fabrique est un paquet PS5.
    const std::string ipath = b["image_path"].as_string();
    const bool ps4 = b["platform"].as_string() == "ps4" || ph.platform == "ps4" || ipath.find("CUSA") != std::string::npos;
    auto secs = [&](const char* st, std::uint64_t bytes) { const double v = install_speed(st, sp); return v > 0 ? static_cast<std::int64_t>(static_cast<double>(bytes) / v) + 1 : 0; };
    Json out = Json::array();
    auto add = [&](const char* as, const std::vector<std::pair<const char*, std::uint64_t>>& st, std::uint64_t need, bool ok, const std::string& reason) {
        Json o = Json::object(), steps = Json::array();
        std::int64_t total = 0;
        for (const auto& [k, bytes] : st) {
            Json s = Json::object();
            s.set("key", k);
            const std::int64_t e = std::string(k) == "install" ? 10 : std::string(k) == "cleanup" ? 2 : secs(k, bytes);
            s.set("eta", e);
            total += e;
            steps.push(s);
        }
        o.set("as", as); o.set("steps", steps); o.set("eta", total); o.set("need", static_cast<std::int64_t>(need));
        std::string r = reason;
        if (ok && free && need > free) { ok = false; r = "pas assez de place"; }
        o.set("ok", ok);
        if (!r.empty()) o.set("why", r);
        if (std::string(as) == "pkg" || std::string(as) == "console") {
            if (ph.ok) o.set("pkg", pkghead::to_json(ph));
            if (std::string(as) == "console" && ph.fake == 0) o.set("note", "paquet officiel : il lui faut sa licence pour se lancer");
        }
        out.push(o);
    };
    const std::uint64_t slack = 512ull << 20;
    if (has_img) add("image", {{"copy", isz}, {"verify", isz}, {"install", 0}}, isz + slack, true, "");
    if (has_pkg) {
        add("pkg", {{"copy", psz}, {"verify", psz}, {"install", 0}}, psz + slack, pkg_ok && refused.empty(), !refused.empty() ? refused : why);
        add("console", {{"copy", psz}, {"verify", psz}, {"install", 0}}, psz + slack, true, "");
    }
    if (has_img && !b["image_dir"].as_bool())  // un dossier se convertit aussi, mais la copie d'un dossier distant est plus lente : plus tard
        add("convert", {{"copy", isz}, {"verify", isz}, {"convert", isz}, {"install", 0}, {"cleanup", 0}}, isz * 2 + isz / 50 + slack,
            pkg_ok && !peer && !ps4, ps4 ? "jeu PS4 : la conversion fabrique un .pkg PS5" : !pkg_ok ? why : peer ? "conversion : seulement sur cette PS5 pour l'instant" : "");
    Json r = Json::object();
    r.set("options", out);
    if (ph.ok) r.set("pkg", pkghead::to_json(ph));
    return r;
}

Json Engine::install_jobs() {
    std::lock_guard<std::mutex> l(jobs_mu_);
    Json a = Json::array();
    const std::int64_t now = now_s();
    for (const auto& [id, j] : jobs_.members()) {
        if (j["finished"].as_int() != 0 && now - j["finished"].as_int() > 2 * 3600) continue;
        // Comme une tâche de la file (barre, vitesse) pour les écrans qui ne connaissent pas les étapes ; le détail
        // (étapes, étape en cours, temps restant de tout) dans « report ».
        Json x = Json::object(), p = Json::object(), rep = Json::object();
        for (const char* k : {"id", "type", "title", "state", "error", "created", "started", "finished"}) x.set(k, j[k]);
        if (j["state"].as_string() == "waiting") x.set("state", "running");
        const Json& cur = j["steps"][static_cast<std::size_t>(j["cur"].as_int())];
        p.set("bytes_done", cur["done"]); p.set("bytes_total", cur["total"]);
        x.set("progress", p);
        x.set("bytes_per_second", cur["rate"]);
        x.set("step", cur["key"]);
        for (const char* k : {"steps", "cur", "eta", "note", "as", "target", "source"}) rep.set(k, j[k]);
        x.set("report", rep);
        x.set("target", j["target"]);
        x.set("steps", j["steps"]);
        a.push(x);
    }
    return a;
}

// « Vider la liste » : les installations finies (state : « done », « failed », « cancelled », ou vide = toutes les finies).
int Engine::install_jobs_clear(const std::string& state) {
    std::lock_guard<std::mutex> l(jobs_mu_);
    Json keep = Json::object();
    int n = 0;
    for (const auto& [id, x] : jobs_.members()) {
        const bool fin = x["finished"].as_int() != 0;
        if (fin && (state.empty() || x["state"].as_string() == state)) { ++n; continue; }
        keep.set(id, x);
    }
    jobs_ = keep;
    fs::write_atomic(fs::host(fs::join(dir_, "installs.json")), jobs_.dump());
    return n;
}

std::set<std::string> Engine::install_children() {
    std::lock_guard<std::mutex> l(jobs_mu_);
    std::set<std::string> s;
    for (const auto& [id, j] : jobs_.members())
        for (const auto& c : j["children"].items()) s.insert(c.as_string());
    return s;
}

bool Engine::install_cancel(const std::string& id, std::string& error) {
    std::string child;
    {
        std::lock_guard<std::mutex> l(jobs_mu_);
        if (!jobs_.has(id)) { error = "installation inconnue"; return false; }
        Json j = jobs_[id];
        if (j["finished"].as_int()) { error = "déjà terminée"; return false; }
        j.set("cancel", true);
        child = j["child"].as_string();
        jobs_.set(id, j);
    }
    if (!child.empty()) queue_->cancel(child);
    ++revision_;
    return true;
}

// Mise à jour d'une installation ; enregistrée sur disque seulement quand une étape ou l'état change (pas à chaque
// relevé de la barre) : après un redémarrage du moteur, elle reprend là où elle en était.
void Engine::job_update(const std::string& id, const std::function<void(Json&)>& f, bool persist) {
    std::lock_guard<std::mutex> l(jobs_mu_);
    Json j = jobs_[id];
    f(j);
    jobs_.set(id, j);
    if (persist) fs::write_atomic(fs::host(fs::join(dir_, "installs.json")), jobs_.dump());
}

namespace {
Json spec_json(const Engine::InstallJobSpec& s) {
    Json j = Json::object();
    j.set("title", s.title); j.set("as", s.as); j.set("how", s.how); j.set("src_place", s.src_place); j.set("src_label", s.src_label); j.set("dst", s.dst);
    j.set("dst_dir", s.dst_dir); j.set("name", s.name); j.set("target_label", s.target_label); j.set("copy_task", s.copy_task);
    j.set("bytes", static_cast<std::int64_t>(s.bytes)); j.set("peer", s.peer);
    return j;
}
Engine::InstallJobSpec spec_of(const Json& j) {
    Engine::InstallJobSpec s;
    s.title = j["title"].as_string(); s.as = j["as"].as_string(); s.how = j["how"].as_string(); s.src_place = j["src_place"].as_string();
    s.src_label = j["src_label"].as_string(); s.dst = j["dst"].as_string(); s.dst_dir = j["dst_dir"].as_string(); s.name = j["name"].as_string();
    s.target_label = j["target_label"].as_string(); s.copy_task = j["copy_task"].as_string(); s.bytes = static_cast<std::uint64_t>(j["bytes"].as_int());
    s.peer = j["peer"].as_bool();
    return s;
}
}  // namespace

// Au démarrage : les installations d'avant ; celles pas finies reprennent (leur copie ou conversion a repris dans la file).
void Engine::install_jobs_resume() {
    std::string t;
    Json j;
    if (!fs::read(fs::host(fs::join(dir_, "installs.json")), t, 8u << 20) || !Json::parse(t, j) || !j.is_object()) return;
    std::vector<std::pair<std::string, InstallJobSpec>> go;
    {
        std::lock_guard<std::mutex> l(jobs_mu_);
        jobs_ = j;
        for (const auto& [id, x] : j.members())
            if (x["finished"].as_int() == 0 && x["spec"].is_object()) go.push_back({id, spec_of(x["spec"])});
    }
    for (auto& [id, sp] : go) {
        log("installer par étapes : reprise de %s (%s)", sp.title.c_str(), id.c_str());
        std::thread([this, id = id, sp = sp] { install_job_run(id, sp); }).detach();
    }
}

// Lance une installation par étapes : la copie (tâche de la file, déjà ajoutée) puis le reste, suivi ici.
std::string Engine::install_job_start(const InstallJobSpec& sp) {
    const std::string id = "inst-" + std::to_string(now_s()) + "-" + std::to_string(++jobs_seq_);
    Json j = Json::object(), steps = Json::array();
    for (const char* k : {"copy", "verify"}) steps.push(step(k));
    if (sp.as == "convert") steps.push(step("convert"));
    steps.push(step("install"));
    if (sp.as == "convert") steps.push(step("cleanup"));
    j.set("id", id); j.set("type", "install_job"); j.set("title", sp.title); j.set("target", sp.target_label); j.set("source", sp.src_label);
    j.set("as", sp.as); j.set("state", "running"); j.set("created", now_s()); j.set("started", now_s()); j.set("finished", 0);
    j.set("cur", 0); j.set("steps", steps); j.set("child", sp.copy_task);
    Json ch = Json::array();
    ch.push(sp.copy_task);
    j.set("children", ch);
    j.set("spec", spec_json(sp));  // pour reprendre après un redémarrage du moteur
    // Estimation de départ, recalculée pendant.
    std::int64_t eta = 0;
    for (const char* k : {"copy", "verify"}) { const double v = install_speed(k, sp.src_place); if (v > 0) eta += static_cast<std::int64_t>(static_cast<double>(sp.bytes) / v); }
    if (sp.as == "convert") eta += static_cast<std::int64_t>(static_cast<double>(sp.bytes) / install_speed("convert", sp.src_place));
    j.set("eta", eta + 10);
    job_update(id, [&](Json& x) { x = j; }, true);
    ++revision_;
    std::thread([this, id, sp] { install_job_run(id, sp); }).detach();
    return id;
}

void Engine::install_job_run(const std::string& id, InstallJobSpec sp) {
    auto cancelled = [&] { std::lock_guard<std::mutex> l(jobs_mu_); return jobs_[id]["cancel"].as_bool(); };
    auto finish = [&](const char* state, const std::string& err, const std::string& note) {
        job_update(id, [&](Json& j) {
            j.set("state", state); j.set("finished", now_s()); j.set("eta", 0);
            if (!err.empty()) j.set("error", err);
            if (!note.empty()) j.set("note", note);
        }, true);
        ++revision_;
    };
    // Temps estimé de chaque étape (lu une fois ici : jamais sous le verrou des installations, que install_speed prend).
    std::vector<std::int64_t> est;
    {
        std::vector<std::string> keys = {"copy", "verify"};
        if (sp.as == "convert") keys.push_back("convert");
        keys.push_back("install");
        if (sp.as == "convert") keys.push_back("cleanup");
        for (const auto& k : keys)
            est.push_back(k == "install" ? 10 : k == "cleanup" ? 2 : static_cast<std::int64_t>(static_cast<double>(sp.bytes) / install_speed(k, sp.src_place)));
    }
    // L'avancement d'une étape : fait, total, vitesse lissée, temps restant de l'étape et de tout ce qui suit.
    struct Meter { std::int64_t t0 = 0, last_ms = 0; std::uint64_t last = 0; double rate = 0; } m;
    auto set_step = [&](int idx, const char* state, std::uint64_t done, std::uint64_t total) {
        const std::int64_t t = now_ms();
        if (m.last_ms && t > m.last_ms && done >= m.last) {
            const double inst = static_cast<double>(done - m.last) * 1000.0 / static_cast<double>(t - m.last_ms);
            m.rate = m.rate > 0 ? m.rate * 0.7 + inst * 0.3 : inst;  // lissée (~10 s à une lecture toutes les 2 s)
        }
        m.last_ms = t; m.last = done;
        bool changed = false;
        job_update(id, [&](Json& j) {
            Json steps = j["steps"];
            Json s = steps[static_cast<std::size_t>(idx)];
            changed = s["state"].as_string() != state;
            if (s["state"].as_string() == "pending") s.set("started", now_s());
            s.set("state", state); s.set("done", static_cast<std::int64_t>(done)); s.set("total", static_cast<std::int64_t>(total));
            const std::int64_t left = m.rate > 1 && total > done ? static_cast<std::int64_t>(static_cast<double>(total - done) / m.rate) : 0;
            s.set("rate", static_cast<std::int64_t>(m.rate)); s.set("eta", left);
            if (std::strcmp(state, "done") == 0) { s.set("ended", now_s()); s.set("eta", 0); }
            // Reste des étapes suivantes : leur estimation.
            std::int64_t later = 0;
            for (std::size_t k = static_cast<std::size_t>(idx) + 1; k < steps.size() && k < est.size(); ++k) later += est[k];
            Json ns = Json::array();
            for (std::size_t k = 0; k < steps.size(); ++k) ns.push(k == static_cast<std::size_t>(idx) ? s : steps[k]);
            j.set("steps", ns); j.set("cur", idx); j.set("eta", left + later);
        });
        if (changed) job_update(id, [](Json&) {}, true);
    };
    auto step_done = [&](int idx, const char* key) {  // apprend la vitesse de l'étape finie
        std::int64_t t0 = 0, t1 = now_s();
        std::uint64_t total = 0;
        { std::lock_guard<std::mutex> l(jobs_mu_); const Json& s = jobs_[id]["steps"][static_cast<std::size_t>(idx)]; t0 = s["started"].as_int(); total = static_cast<std::uint64_t>(s["total"].as_int()); }
        if (t1 > t0 + 5 && total > (64ull << 20)) learn_speed(key, sp.src_place, static_cast<double>(total) / static_cast<double>(t1 - t0));
        set_step(idx, "done", total, total);
        m = Meter{};
    };
    // Une tâche de la file (copie ou conversion) suivie jusqu'au bout ; false si elle échoue ou est annulée.
    auto follow = [&](const std::string& task, int idx_main, int idx_verify, std::string& err) -> bool {
        bool in_verify = false;
        int missing = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (quit_) { err = "moteur arrêté"; return false; }
            tasks::Task t;
            if (!queue_->get(task, t)) {  // retirée de la liste (« Vider la liste ») : finie si son étape l'était
                if (++missing < 5) continue;
                bool was_done = false;
                { std::lock_guard<std::mutex> l(jobs_mu_); was_done = jobs_[id]["steps"][static_cast<std::size_t>(idx_verify >= 0 ? idx_verify : idx_main)]["state"].as_string() == "done"; }
                if (was_done) return true;
                err = "tâche introuvable";
                return false;
            }
            if (t.state == tasks::State::cancelled) { err = "annulée"; return false; }
            if (t.state == tasks::State::failed) { err = t.error.empty() ? std::string("échec") : t.error; return false; }
            const auto& p = t.progress;
            if (idx_verify >= 0 && p.phase == "verify") {
                if (!in_verify) { step_done(idx_main, "copy"); in_verify = true; }
                set_step(idx_verify, "running", p.phase_done, p.phase_total);
            } else if (t.state == tasks::State::queued) {
                set_step(idx_main, "waiting", p.bytes_done, p.bytes_total ? p.bytes_total : sp.bytes);
            } else if (t.state != tasks::State::done) {
                set_step(idx_main, "running", p.bytes_done, p.bytes_total ? p.bytes_total : sp.bytes);
            }
            if (t.state == tasks::State::done) {
                if (idx_verify >= 0) { if (!in_verify) step_done(idx_main, "copy"); step_done(idx_verify, "verify"); }
                else step_done(idx_main, "convert");
                return true;
            }
        }
    };
    const std::string file = fs::join(sp.dst_dir, sp.name);
    std::string err;
    if (!follow(sp.copy_task, 0, 1, err)) {
        finish(cancelled() || err == "annulée" ? "cancelled" : "failed", err == "annulée" ? std::string() : err, "");
        if (err != "annulée") notify::send("Installation interrompue", sp.title + " : copie — " + err);
        return;
    }
    int idx = 2;
    std::string to_install = file, how = sp.how;
    // Conversion en .pkg (sur cette PS5) : le paquet est écrit à côté, l'image copiée est retirée à la fin.
    if (sp.as == "convert") {
        if (cancelled()) { if (!sp.peer) ::unlink(fs::host(file).c_str()); finish("cancelled", "", ""); return; }
        Json body = Json::object();
        body.set("type", "make_pkg"); body.set("source", file); body.set("target_dir", sp.dst_dir); body.set("pkg_level", 1);
        const Json plan = image_plan(body, err);
        std::string conv;
        std::string conv_out;
        { std::lock_guard<std::mutex> l(jobs_mu_); conv = jobs_[id]["conv_task"].as_string(); conv_out = jobs_[id]["conv_output"].as_string(); }
        if (!conv.empty()) {  // reprise : la conversion est déjà dans la file
        } else if (plan.is_null() || !plan["ok"].as_bool(true) || !image_task(body, conv, err)) {
            if (err.empty()) err = plan["error"].as_string();
            finish("failed", "conversion — " + err, "");
            notify::send("Installation interrompue", sp.title + " : conversion — " + err);
            return;
        }
        if (conv_out.empty()) conv_out = plan["output"].as_string();
        job_update(id, [&](Json& j) {
            if (j["conv_task"].as_string() == conv) return;
            j.set("child", conv); j.set("conv_task", conv); j.set("conv_output", conv_out);
            Json c = j["children"]; c.push(conv); j.set("children", c);
        }, true);
        if (!follow(conv, 2, -1, err)) {
            ::unlink(fs::host(file).c_str());  // l'image copiée n'a plus lieu d'être ; la source, elle, n'est jamais touchée
            finish(err == "annulée" ? "cancelled" : "failed", err == "annulée" ? std::string() : "conversion — " + err, "");
            if (err != "annulée") notify::send("Installation interrompue", sp.title + " : conversion — " + err);
            return;
        }
        to_install = conv_out;
        idx = 3;
    }
    if (cancelled()) {
        if (!sp.peer) { ::unlink(fs::host(file).c_str()); if (to_install != file) ::unlink(fs::host(to_install).c_str()); }
        finish("cancelled", "", "");
        return;
    }
    // Installation : montage (ShadowMount, Kraken) ou par la console ; ici ou sur l'autre PS5.
    set_step(idx, "running", 0, 1);
    Json req = Json::object(), out;
    bool ok = false;
    err.clear();
    if (!sp.peer) {
        req.set("path", to_install);
        out = how == "console" ? package_install(req, err) : install_here(req, err);
        ok = !out.is_null() && err.empty();
    } else if (link_) {
        const std::string rest = sp.dst.substr(10);
        const auto c = rest.find(':');
        const std::string inst = rest.substr(0, c), place = c == std::string::npos ? std::string() : rest.substr(c + 1);
        Json places;
        std::string pm;
        if (link_->call(inst, "GET", "/api/v2/places", Json(), places, err))
            for (const auto& p : places.items()) if (p["id"].as_string() == place) pm = p["mount"].as_string();
        if (pm.empty() || pm == "/user") pm = "/data";
        req.set("path", fs::join(images::games_root(pm), sp.name));
        ok = err.empty() && link_->call(inst, "POST", how == "console" ? "/api/v2/packages/install" : "/api/v2/install/here", req, out, err);
    } else err = "liaison indisponible";
    if (!ok) {
        set_step(idx, "failed", 0, 1);
        finish("failed", "installation — " + err, "");
        notify::send("Copié, mais pas installé", sp.title + " : " + err);
        log("installer par étapes : %s — %s", sp.title.c_str(), err.c_str());
        return;
    }
    set_step(idx, "done", 1, 1);
    if (sp.as == "convert") {  // l'image copiée : le paquet la remplace
        set_step(idx + 1, "running", 0, 1);
        ::unlink(fs::host(file).c_str());
        set_step(idx + 1, "done", 1, 1);
    }
    const std::string by = out["by"].as_string();
    const std::string note = how == "console" ? "installation demandée à la console" : by == "smp_theirs" ? "ShadowMountPlus l'inscrit dès que l'application est fermée" : "";
    finish("done", "", note);
    notify::send(how == "console" ? "Copié, installation demandée à la console" : by == "smp_theirs" ? "Copié : ShadowMountPlus l'inscrit à la fermeture de l'application" : "Installé",
                 sp.title + " → " + sp.target_label);
    log("installer par étapes : %s → %s (%s) — fait", sp.title.c_str(), sp.target_label.c_str(), sp.as.c_str());
}

}  // namespace pbc
