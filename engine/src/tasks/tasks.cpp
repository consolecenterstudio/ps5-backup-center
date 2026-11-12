#include "tasks/tasks.hpp"
#include "files/files.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <unistd.h>

#include "images/images.hpp"
#include "saves/inspect.hpp"
#include "saves/restore.hpp"
#include "saves/switch_import.hpp"
#include "core/fs.hpp"
#include "core/util.hpp"

namespace pbc::tasks {

namespace {
constexpr const char* states[] = {"queued", "running", "paused", "done", "failed", "cancelled"};
bool parse_state(std::string_view s, State& out) {
    for (int i = 0; i < 6; ++i) if (s == states[i]) { out = static_cast<State>(i); return true; }
    return false;
}
Json progress_json(const backup::Progress& p) {
    Json j = Json::object();
    j.set("bytes_done", p.bytes_done);
    j.set("bytes_total", p.bytes_total);
    j.set("files_done", static_cast<std::uint64_t>(p.files_done));
    j.set("files_total", static_cast<std::uint64_t>(p.files_total));
    j.set("current", p.current);
    if (!p.phase.empty()) {
        j.set("phase", p.phase);
        j.set("phase_done", p.phase_done);
        j.set("phase_total", p.phase_total);
    }
    return j;
}
std::vector<Part> parts_from(const Json& a) {
    std::vector<Part> out;
    for (const auto& v : a.items()) { Part p; if (parse_part(v.as_string(), p)) out.push_back(p); }
    return out;
}
Json parts_to(const std::vector<Part>& v) {
    Json a = Json::array();
    for (auto p : v) a.push(to_string(p));
    return a;
}
}  // namespace

const char* to_string(State s) { return states[static_cast<int>(s)]; }

Json Task::to_json(bool with_request) const {
    Json j = Json::object();
    j.set("id", id);
    if (type == Type::image) {
        images::Op op = images::Op::compress;
        images::parse_op(request["op"].as_string(), op);
        j.set("type", images::task_type(op));
        j.set("source", request["source"]);
        if (state == State::running) j.set("step", progress.current);
    } else if (type == Type::inspect_save) j.set("type", "inspect_save");
    else if (type == Type::files) j.set("type", request["op"].as_string() == "delete" ? "files_delete" : request["move"].as_bool() ? "files_move" : "files_copy");
    else j.set("type", type == Type::backup ? "backup" : type == Type::restore ? "restore" : type == Type::restore_save ? "restore_save" : type == Type::import_switch ? "import_switch" : "export_switch");
    if (!code.empty()) j.set("code", code);
    // Élément concerné (image dans la liste des tâches).
    if (request["item"].is_object()) j.set("item", request["item"]["key"]);
    else if (!request["item_key"].as_string().empty()) j.set("item", request["item_key"]);
    else if (!request["title_id"].as_string().empty()) j.set("title_id", request["title_id"]);
    if (report.is_object()) j.set("report", report);
    j.set("state", to_string(state));
    j.set("title", title);
    j.set("error", error);
    j.set("created", created);
    j.set("started", started);
    j.set("finished", finished);
    j.set("progress", progress_json(progress));
    j.set("bytes_per_second", bytes_per_second);
    j.set("bundle", bundle);
    if (with_request) j.set("request", request);
    return j;
}

std::string Queue::task_path(const std::string& id) const { return fs::join(dir_, "task-" + id + ".json"); }
std::string Queue::journal_path(const std::string& id) const { return fs::join(dir_, "task-" + id + ".journal"); }

bool Queue::save(const Task& t) { return fs::write_atomic(task_path(t.id), t.to_json(true).dump(true) + "\n"); }

Task* Queue::find(const std::string& id) {
    for (auto& t : tasks_) if (t.id == id) return &t;
    return nullptr;
}

bool Queue::load() {
    std::lock_guard<std::mutex> l(mu_);
    tasks_.clear();
    if (!fs::mkdirs(dir_)) return false;
    std::vector<fs::Entry> entries;
    fs::list(dir_, entries);
    for (const auto& e : entries) {
        if (e.dir || e.name.rfind("task-", 0) != 0 || e.name.size() < 6 || e.name.substr(e.name.size() - 5) != ".json") continue;
        std::string text;
        Json j;
        if (!fs::read(fs::join(dir_, e.name), text) || !Json::parse(text, j)) continue;
        Task t;
        t.id = j["id"].as_string();
        t.type = j["type"].as_string() == "restore" ? Type::restore : j["type"].as_string() == "restore_save" ? Type::restore_save : j["type"].as_string() == "import_switch" ? Type::import_switch : j["type"].as_string() == "export_switch" ? Type::export_switch : Type::backup;
        if (images::Op op; images::parse_task_type(j["type"].as_string(), op)) t.type = Type::image;
        if (j["type"].as_string() == "files_copy" || j["type"].as_string() == "files_move" || j["type"].as_string() == "files_delete") t.type = Type::files;
        if (j["type"].as_string() == "inspect_save") t.type = Type::inspect_save;
        t.code = j["code"].as_string();
        if (j["report"].is_object()) t.report = j["report"];
        if (!parse_state(j["state"].as_string(), t.state)) continue;
        t.title = j["title"].as_string();
        t.error = j["error"].as_string();
        t.created = j["created"].as_int();
        t.started = j["started"].as_int();
        t.finished = j["finished"].as_int();
        t.bundle = j["bundle"].as_string();
        t.request = j["request"];
        const Json& p = j["progress"];
        t.progress.bytes_done = static_cast<std::uint64_t>(p["bytes_done"].as_int());
        t.progress.bytes_total = static_cast<std::uint64_t>(p["bytes_total"].as_int());
        t.progress.files_done = static_cast<unsigned>(p["files_done"].as_int());
        t.progress.files_total = static_cast<unsigned>(p["files_total"].as_int());
        if (t.state == State::running) {  // interrompue par un arrêt : on la reprendra
            t.state = State::queued;
            save(t);
            log("task resume-after-restart id=%s", t.id.c_str());
        }
        tasks_.push_back(std::move(t));
    }
    std::sort(tasks_.begin(), tasks_.end(), [](const Task& a, const Task& b) { return a.created < b.created; });
    ++revision_;
    log("tasks loaded count=%zu", tasks_.size());
    return true;
}

std::string Queue::add_backup(const backup::Request& req) {
    Task t;
    t.id = random_id(6);
    t.type = Type::backup;
    t.title = req.item.title;
    t.created = now_unix();
    Json r = Json::object();
    r.set("item", req.item.to_json());
    r.set("parts", parts_to(req.parts));
    r.set("folder", req.folder);
    r.set("disk_id", req.disk_id);
    if (!req.remote_id.empty()) r.set("remote", req.remote_id);
    if (req.keep > 0) r.set("keep", static_cast<std::int64_t>(req.keep));
    if (req.flat) r.set("flat", true);
    if (!req.bundle_name.empty()) r.set("bundle_name", req.bundle_name);  // fixé à l'avance (rangement par plateforme)
    t.request = r;
    {
        std::lock_guard<std::mutex> l(mu_);
        save(t);
        tasks_.push_back(t);
        ++revision_;
    }
    log("task added id=%s type=backup item=%s", t.id.c_str(), req.item.key.c_str());
    cv_.notify_all();
    return t.id;
}

std::string Queue::add_import_switch(const Json& request, const std::string& title) { return add_typed(Type::import_switch, request, title); }
std::string Queue::add_restore_save(const Json& request, const std::string& title) { return add_typed(Type::restore_save, request, title); }

std::string Queue::add_typed(Type type, const Json& request, const std::string& title) {
    Task t;
    t.id = random_id(6);
    t.type = type;
    t.title = title;
    t.created = now_unix();
    t.request = request;
    {
        std::lock_guard<std::mutex> l(mu_);
        save(t);
        tasks_.push_back(t);
        ++revision_;
    }
    log("task added id=%s type=%d title=%s", t.id.c_str(), static_cast<int>(type), title.c_str());
    cv_.notify_all();
    return t.id;
}

std::string Queue::add_restore(const backup::RestoreRequest& req, const std::string& title) {
    Task t;
    t.id = random_id(6);
    t.type = Type::restore;
    t.title = title;
    t.created = now_unix();
    Json r = Json::object();
    r.set("bundle", req.bundle);
    r.set("parts", parts_to(req.parts));
    r.set("target", req.target);
    r.set("overwrite", req.overwrite);
    if (!req.item_key.empty()) r.set("item_key", req.item_key);
    if (!req.remote_id.empty()) r.set("remote", req.remote_id);
    t.request = r;
    {
        std::lock_guard<std::mutex> l(mu_);
        save(t);
        tasks_.push_back(t);
        ++revision_;
    }
    log("task added id=%s type=restore bundle=%s", t.id.c_str(), req.bundle.c_str());
    cv_.notify_all();
    return t.id;
}

bool Queue::pause(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    Task* t = find(id);
    if (!t || (t->state != State::queued && t->state != State::running)) return false;
    if (t->state == State::running) interrupt_ = true;
    t->state = State::paused;
    save(*t);
    ++revision_;
    return true;
}

bool Queue::resume(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    Task* t = find(id);
    if (!t || (t->state != State::paused && t->state != State::failed)) return false;
    t->state = State::queued;
    t->error.clear();
    save(*t);
    ++revision_;
    cv_.notify_all();
    return true;
}

bool Queue::cancel(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    Task* t = find(id);
    if (!t || t->state == State::done || t->state == State::cancelled) return false;
    if (t->state == State::running) interrupt_ = true;
    t->state = State::cancelled;
    t->finished = now_unix();
    save(*t);
    ++revision_;
    return true;
}

bool Queue::cleanup_files(const std::string& id, std::string& error) {
    Json request;
    {
        std::lock_guard<std::mutex> l(mu_);
        Task* t = find(id);
        if (!t || t->type != Type::files || t->request["op"].as_string() == "delete") { error = "tâche inconnue"; return false; }
        if (t->state != State::cancelled && t->state != State::failed) { error = "seulement une copie annulée ou en échec"; return false; }
        request = t->request;
        if (t->report["cleaning"].as_bool()) { error = "déjà en cours"; return false; }
        if (!t->report.is_object()) t->report = Json::object();
        t->report.set("cleaning", true);
        ++revision_;
    }
    std::thread([this, id, request] {
        std::string err;
        Json rep;
        auto dst = open_place ? open_place(request["dst_place"].as_string(), err) : nullptr;
        const bool ok = dst && files::cleanup(*dst, journal_path(id) + ".files", rep, err);
        std::lock_guard<std::mutex> l(mu_);
        if (Task* t = find(id)) {
            if (!t->report.is_object()) t->report = Json::object();
            t->report.set("cleaning", false);
            if (ok) { t->report.set("cleaned", rep); ::unlink((journal_path(id) + ".files").c_str()); }
            else t->report.set("cleanup_error", err);
            save(*t);
            ++revision_;
        }
    }).detach();
    return true;
}

bool Queue::clear(const std::string& state, int& removed, int& kept) {
    removed = kept = 0;
    State st;
    if (!parse_state(state, st) || (st != State::done && st != State::failed && st != State::cancelled)) return false;
    std::lock_guard<std::mutex> l(mu_);
    for (auto it = tasks_.begin(); it != tasks_.end();) {
        if (it->state != st) { ++it; continue; }
        const std::string files_journal = journal_path(it->id) + ".files";
        if (it->type == Type::files && st != State::done && ::access(files_journal.c_str(), F_OK) == 0 && !it->report["cleaned"].is_object()) {
            ++kept; ++it; continue;
        }
        ::unlink(task_path(it->id).c_str());
        ::unlink(journal_path(it->id).c_str());
        ::unlink(files_journal.c_str());
        it = tasks_.erase(it);
        ++removed;
    }
    if (removed) ++revision_;
    return true;
}

bool Queue::remove(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = std::find_if(tasks_.begin(), tasks_.end(), [&](const Task& t) { return t.id == id; });
    if (it == tasks_.end() || it->state == State::running || it->state == State::queued || it->state == State::paused) return false;
    ::unlink(task_path(id).c_str());
    ::unlink(journal_path(id).c_str());
    ::unlink((journal_path(id) + ".files").c_str());
    tasks_.erase(it);
    ++revision_;
    return true;
}

std::vector<Task> Queue::list() const { std::lock_guard<std::mutex> l(mu_); return tasks_; }
bool Queue::running(std::string& id, std::string& title) const {
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& t : tasks_) if (t.state == State::running) { id = t.id; title = t.title; return true; }
    return false;
}
bool Queue::has_work() const {
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& t : tasks_) if (t.state == State::running || t.state == State::queued) return true;
    return false;
}
bool Queue::get(const std::string& id, Task& out) const {
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& t : tasks_) if (t.id == id) { out = t; return true; }
    return false;
}

void Queue::start() {
    quit_ = false;
    thread_ = std::thread([this] { worker(); });
}

void Queue::stop() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard<std::mutex> l(mu_);
        quit_ = true;
        interrupt_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

void Queue::worker() {
    for (;;) {
        Task current;
        {
            std::unique_lock<std::mutex> l(mu_);
            cv_.wait(l, [&] {
                return quit_.load() || std::any_of(tasks_.begin(), tasks_.end(), [](const Task& t) { return t.state == State::queued; });
            });
            if (quit_) return;
            Task* t = nullptr;
            for (auto& x : tasks_) if (x.state == State::queued) { t = &x; break; }
            t->state = State::running;
            if (!t->started) t->started = now_unix();
            t->error.clear();
            save(*t);
            ++revision_;
            running_id_ = t->id;
            interrupt_ = false;
            current = *t;
        }
        run_one(current);
        {
            std::lock_guard<std::mutex> l(mu_);
            running_id_.clear();
            Task* t = find(current.id);
            if (t) {
                // Une pause ou une annulation demandée pendant l'exécution garde le dessus.
                // Une opération d'image terminée l'est vraiment (ex. compression destructive, qui ne s'annule plus) :
                // elle ne doit pas rester « en pause » et se relancer sur une source déjà supprimée.
                if (t->state == State::running || (t->type == Type::image && current.state == State::done)) { t->state = current.state; t->error = current.error; }
                t->progress = current.progress;
                t->bundle = current.bundle;
                t->code = current.code;
                t->report = current.report;
                t->finished = (t->state == State::done || t->state == State::failed || t->state == State::cancelled) ? now_unix() : 0;
                t->request = current.request;  // nom du dossier .pbck fixé à la première exécution
                save(*t);
                ++revision_;
                if (t->state == State::done && on_finished) on_finished(*t);
            }
        }
    }
}

void Queue::run_one(Task& t) {
    using clock = std::chrono::steady_clock;
    auto last_save = clock::now();
    const auto t0 = clock::now();
    std::uint64_t base = 0;
    bool first = true;
    auto progress = [&](const backup::Progress& p) {
        if (first) { base = p.bytes_done; first = false; }
        const auto now = clock::now();
        const double secs = std::chrono::duration<double>(now - t0).count();
        std::lock_guard<std::mutex> l(mu_);
        Task* live = find(t.id);
        if (!live) return;
        live->progress = p;
        live->bytes_per_second = secs > 0.5 ? static_cast<double>(p.bytes_done - base) / secs : 0;
        t.progress = p;
        ++revision_;
        if (now - last_save > std::chrono::seconds(2)) { save(*live); last_save = now; }
    };

    if (t.type == Type::backup) {
        backup::Request req;
        if (!Item::from_json(t.request["item"], req.item)) { t.state = State::failed; t.error = "demande illisible"; return; }
        req.parts = parts_from(t.request["parts"]);
        req.folder = t.request["folder"].as_string();
        req.disk_id = t.request["disk_id"].as_string();
        req.bundle_name = t.request["bundle_name"].as_string();
        req.flat = t.request["flat"].as_bool();
        if (req.bundle_name.empty()) {
            req.bundle_name = backup::make_bundle_name(req);
            t.request.set("bundle_name", req.bundle_name);
            std::lock_guard<std::mutex> l(mu_);
            if (Task* live = find(t.id)) { live->request = t.request; save(*live); }
        }
        std::unique_ptr<store::Store> remote;
        if (!t.request["remote"].as_string().empty()) {
            std::string err;
            remote = open_remote ? open_remote(t.request["remote"].as_string(), err) : nullptr;
            if (!remote) { t.state = State::failed; t.error = "appareil injoignable : " + err; return; }
            req.store = remote.get();
        }
        backup::Journal journal(journal_path(t.id));
        auto r = backup::run(req, journal, progress, interrupt_);
        t.bundle = r.bundle;
        if (r.ok) { t.state = State::done; ::unlink(journal_path(t.id).c_str()); }
        else if (interrupt_) t.state = State::running;  // pause/annulation/arrêt : l'état est fixé par l'appelant
        else { t.state = State::failed; t.error = r.error; }
    } else if (t.type == Type::image) {
        images::Options o;
        std::string err;
        Json req = t.request;
        req.set("type", req["op"]);
        if (!images::Options::from_json(req, o, err)) { t.state = State::failed; t.error = "demande illisible : " + err; return; }
        auto r = images::run(o, [&](const images::Progress& ip) {
            backup::Progress p;
            p.bytes_done = ip.bytes_done;
            p.bytes_total = ip.bytes_total;
            p.files_done = ip.files_done;
            p.files_total = ip.files_total;
            p.current = ip.current;
            progress(p);
        }, interrupt_);
        t.report = r.report;
        t.code = r.code;
        t.bundle = r.output;  // résultat (chemin console)
        if (r.ok) t.state = State::done;
        else if (interrupt_ && r.code == "cancelled") t.state = State::running;  // pause/annulation : l'état est fixé par l'appelant
        else { t.state = State::failed; t.error = r.error; }
    } else if (t.type == Type::files && t.request["op"].as_string() == "delete") {
        std::string err;
        auto st = open_place ? open_place(t.request["place"].as_string(), err) : nullptr;
        if (!st) { t.state = State::failed; t.error = "endroit injoignable : " + err; return; }
        std::vector<std::string> names;
        for (const auto& n : t.request["names"].items()) names.push_back(n.as_string());
        auto r = files::remove_items(*st, t.request["dir"].as_string(), names, progress, interrupt_);
        t.report = r.report;
        if (r.ok) t.state = State::done;
        else if (interrupt_) t.state = State::running;  // pause/annulation : l'état est fixé par l'appelant
        else { t.state = State::failed; t.error = r.error; }
    } else if (t.type == Type::files) {
        std::string err;
        auto src = open_place ? open_place(t.request["src_place"].as_string(), err) : nullptr;
        if (!src) { t.state = State::failed; t.error = "départ injoignable : " + err; return; }
        auto dst = open_place ? open_place(t.request["dst_place"].as_string(), err) : nullptr;
        if (!dst) { t.state = State::failed; t.error = "destination injoignable : " + err; return; }
        files::Request fr;
        fr.src = src.get();
        fr.dst = dst.get();
        fr.src_dir = t.request["src_dir"].as_string();
        fr.dst_dir = t.request["dst_dir"].as_string();
        fr.move = t.request["move"].as_bool();
        for (const auto& n : t.request["names"].items()) fr.names.push_back(n.as_string());
        fr.journal = journal_path(t.id) + ".files";
        auto r = files::run(fr, progress, interrupt_);
        t.report = r.report;
        if (r.ok) t.state = State::done;
        else if (interrupt_) t.state = State::running;  // pause/annulation : l'état est fixé par l'appelant
        else { t.state = State::failed; t.error = r.error; }
    } else if (t.type == Type::export_switch) {
        std::string err;
        auto dst = open_remote ? open_remote(t.request["remote"].as_string(), err) : nullptr;
        if (!dst) { t.state = State::failed; t.error = "Switch injoignable : " + err; return; }
        saves::SwitchExportRequest req;
        req.eden_root = t.request["eden_root"].as_string();
        req.profile = t.request["profile"].as_string();
        req.title_id = t.request["title_id"].as_string();
        req.game_folder = t.request["game_folder"].as_string();
        req.profile_name = t.request["profile_name"].as_string();
        req.check_only = t.request["check_only"].as_bool();
        auto r = saves::export_switch(*dst, req, interrupt_);
        t.report = r.report;
        t.code = r.code;
        if (r.ok) t.state = State::done;
        else if (interrupt_) t.state = State::running;
        else { t.state = State::failed; t.error = r.error; }
    } else if (t.type == Type::import_switch) {
        std::string err;
        auto src = open_remote ? open_remote(t.request["remote"].as_string(), err) : nullptr;
        if (!src) { t.state = State::failed; t.error = "Switch injoignable : " + err; return; }
        saves::SwitchImportRequest req;
        req.game = t.request["game"].as_string();
        req.file = t.request["file"].as_string();
        req.title_id = t.request["title_id"].as_string();
        req.eden_root = t.request["eden_root"].as_string();
        req.profile = t.request["profile"].as_string();
        req.state_dir = t.request["state_dir"].as_string();
        req.check_only = t.request["check_only"].as_bool();
        req.force_older = t.request["force_older"].as_bool();
        for (const auto& a : t.request["eden_apps"].items()) req.eden_apps.push_back(a.as_string());
        auto r = saves::import_switch(*src, req, progress, interrupt_);
        t.report = r.report;
        t.code = r.code;
        if (r.ok) t.state = State::done;
        else if (interrupt_) t.state = State::running;
        else { t.state = State::failed; t.error = r.error; }
    } else if (t.type == Type::inspect_save) {
        auto r = saves::inspect_save(t.id, t.request, open_remote, progress, interrupt_);
        t.report = r.report;
        t.code = r.code;
        if (r.ok) t.state = State::done;
        else if (interrupt_) t.state = State::running;
        else { t.state = State::failed; t.error = r.error; }
    } else if (t.type == Type::restore_save) {
        saves::RestoreSaveRequest req;
        req.bundle = t.request["bundle"].as_string();
        req.images_dir = t.request["images_dir"].as_string();  // dossier d'images seules (ancienne sauvegarde)
        // Sauvegarde sur un appareil réseau (NAS) : on télécharge d'abord ses images sdimg_* dans un dossier
        // de travail de la console, puis la restauration suit la procédure habituelle (vérification, copie
        // de sécurité, jeu fermé…). Le dossier de travail est effacé à la fin.
        std::string staging;
        if (!t.request["remote"].as_string().empty()) {
            std::string err;
            auto st = open_remote ? open_remote(t.request["remote"].as_string(), err) : nullptr;
            if (!st) { t.state = State::failed; t.error = "appareil injoignable : " + err; return; }
            staging = fs::join(t.request["state_dir"].as_string(), "remote-restore/" + t.id);
            fs::remove_tree(fs::host(staging));
            fs::mkdirs(fs::host(staging));
            // Images trouvées jusqu'à 6 niveaux (.pbck ou dossier daté), jamais sdimg_sce_bu_* (saves/inspect.hpp).
            std::string e2;
            const auto files = saves::fetch_images(*st, req.bundle, staging, t.request["title_id"].as_string(), t.request["user_id"].as_string(), progress, interrupt_, e2);
            if (interrupt_) { fs::remove_tree(fs::host(staging)); return; }
            if (files.empty()) { fs::remove_tree(fs::host(staging)); t.state = State::failed; t.error = "NAS : " + e2; return; }
            req.images_dir = staging;
            req.bundle.clear();
        }
        req.user_id = t.request["user_id"].as_string();
        req.title_id = t.request["title_id"].as_string();
        req.allow_resign = t.request["allow_resign"].as_bool();
        req.check_only = t.request["check_only"].as_bool();
        req.state_dir = t.request["state_dir"].as_string();
        auto r = saves::restore_save(req, progress, interrupt_);
        if (!staging.empty()) fs::remove_tree(fs::host(staging));
        t.report = r.report;
        t.code = r.code;
        if (r.ok) t.state = State::done;
        else if (interrupt_) t.state = State::running;
        else { t.state = State::failed; t.error = r.error; }
    } else {
        backup::RestoreRequest req;
        req.bundle = t.request["bundle"].as_string();
        req.parts = parts_from(t.request["parts"]);
        req.target = t.request["target"].as_string();
        req.overwrite = t.request["overwrite"].as_bool();
        std::unique_ptr<store::Store> remote;
        if (!t.request["remote"].as_string().empty()) {
            std::string err;
            remote = open_remote ? open_remote(t.request["remote"].as_string(), err) : nullptr;
            if (!remote) { t.state = State::failed; t.error = "appareil injoignable : " + err; return; }
            req.store = remote.get();
        }
        auto r = backup::restore(req, progress, interrupt_);
        if (r.ok) t.state = State::done;
        else if (interrupt_) t.state = State::running;
        else { t.state = State::failed; t.error = r.error; }
    }
    log("task end id=%s state=%s error=%s", t.id.c_str(), to_string(t.state), t.error.c_str());
}

}  // namespace pbc::tasks
