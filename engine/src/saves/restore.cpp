#include "saves/restore.hpp"

#include <cinttypes>
#include <set>

#include "accounts/accounts.hpp"
#include "core/fs.hpp"
#include "core/util.hpp"
#include "saves/garlic_core.h"
#include "sqlite3.h"

namespace pbc::saves {

namespace {
bool copy_file(const std::string& from, const std::string& to) {
    FILE* in = std::fopen(fs::host(from).c_str(), "rb");
    if (!in) return false;
    fs::mkdirs(fs::host(std::string(fs::dirname(to))));
    FILE* out = std::fopen(fs::host(to).c_str(), "wb");
    if (!out) { std::fclose(in); return false; }
    char buf[65536];
    bool ok = true;
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, in)) > 0;) if (std::fwrite(buf, 1, n, out) != n) { ok = false; break; }
    ok = ok && !std::ferror(in) && std::fflush(out) == 0;
    std::fclose(in);
    std::fclose(out);
    return ok;
}

bool copy_dir(const std::string& from, const std::string& to) {
    std::vector<fs::Entry> es;
    if (!fs::list(fs::host(from), es)) return true;  // rien à protéger
    for (const auto& e : es) {
        if (e.dir) { if (!copy_dir(fs::join(from, e.name), fs::join(to, e.name))) return false; }
        else if (!copy_file(fs::join(from, e.name), fs::join(to, e.name))) return false;
    }
    return true;
}

std::string hex16(std::uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016" PRIX64, v);
    return b;
}

void garlic_log(const char* line) { log("garlic %s", line); }
}  // namespace

RestoreSaveResult restore_save(const RestoreSaveRequest& req, const backup::ProgressFn& progress, const std::atomic<bool>& stop) {
    RestoreSaveResult res;
    auto fail = [&](const std::string& code, const std::string& msg) { res.code = code; res.error = msg; log("restore-save failed code=%s %s", code.c_str(), msg.c_str()); return res; };
    gc_set_log(garlic_log);
    char err[512] = {0};

    // 1. Le backup : images principales de chaque jeu.
    std::string text;
    Json m;
    struct Image { std::string title_id, path, name; std::uint64_t size = 0; };
    std::vector<Image> images;
    if (!req.images_dir.empty()) {  // images seules (ancienne sauvegarde) : le jeu est donné par la demande
        if (req.title_id.empty()) return fail("bad_bundle", "jeu inconnu pour cette sauvegarde");
        std::vector<fs::Entry> es;
        if (!fs::list(fs::host(req.images_dir), es)) return fail("bad_bundle", "dossier d'images illisible");
        for (const auto& e : es)
            if (!e.dir && e.name.rfind("sdimg_", 0) == 0 && e.name.rfind("sdimg_sce_bu_", 0) != 0) images.push_back({req.title_id, fs::join(req.images_dir, e.name), e.name, e.size});
    } else {
    if (!fs::read(fs::host(fs::join(req.bundle, "manifest.json")), text) || !Json::parse(text, m)) return fail("bad_bundle", "manifeste illisible");
    if (!fs::exists(fs::host(fs::join(req.bundle, "complete")))) return fail("bad_bundle", "sauvegarde incomplète");
    for (const auto& c : m["components"].items()) {
        const Json& meta = c["meta"];
        if (meta["format"].as_string() == "ps4-savedata") return fail("ps4", "la restauration des sauvegardes PS4 arrive bientôt");
        if (meta["format"].as_string() != "ps5-savedata") continue;
        const std::string src_uid = meta["user_id"].as_string();
        const std::string tid = m["item"]["title_id"].as_string();
        if (!req.title_id.empty() && req.title_id != tid) continue;
        const std::string dir = fs::join(req.bundle, "data/save/ps5/" + src_uid + "/" + tid + "/images");
        std::vector<fs::Entry> es;
        if (!fs::list(fs::host(dir), es)) continue;
        for (const auto& e : es)
            if (!e.dir && e.name.rfind("sdimg_", 0) == 0 && e.name.rfind("sdimg_sce_bu_", 0) != 0) images.push_back({tid, fs::join(dir, e.name), e.name, e.size});
    }
    }
    if (images.empty()) return fail("bad_bundle", "aucune image de sauvegarde PS5 dans ce backup");

    // 2. Le compte cible doit exister et avoir un identifiant non nul.
    std::string target_aid;
    bool seen = false;
    for (const auto& a : accounts::list()) if (a.user_id == req.user_id) { seen = true; target_aid = a.account_id; }
    if (!seen) return fail("no_user", "compte " + req.user_id + " introuvable sur la console");
    if (target_aid.empty() || target_aid.find_first_not_of("0x") == std::string::npos)
        return fail("no_account_id", "le compte cible n'a pas d'identifiant : activez-le d'abord (sinon le jeu verrait une sauvegarde corrompue)");

    // 3. Jeu fermé.
    std::set<std::string> titles;
    for (const auto& i : images) titles.insert(i.title_id);
    char running[16] = {0};
    if (gc_running_title(running) && titles.count(running)) return fail("game_running", std::string("fermez d'abord le jeu ") + running);

    // 4. Copie de sécurité des images déjà présentes sur la console (inutile en vérification seule).
    const std::string safety = fs::join(fs::join(req.state_dir, "restore-safety"), iso_time(now_unix()).substr(0, 19) + "-" + random_id(3));
    for (const auto& tid : titles) {
        if (req.check_only) break;
        const std::string home = fs::join("/user/home", req.user_id);
        if (!copy_dir(fs::join(home, "savedata_prospero/" + tid), fs::join(safety, tid + "/images")) ||
            !copy_dir(fs::join(home, "savedata_prospero_meta/user/" + tid), fs::join(safety, tid + "/meta")))
            return fail("safety", "copie de sécurité impossible : rien n'a été modifié");
    }
    res.report.set("safety_copy", safety);

    // 5. Import image par image (cœur Garlic compilé dans le moteur).
    backup::Progress pr;
    pr.files_total = static_cast<unsigned>(images.size());
    for (const auto& i : images) pr.bytes_total += i.size;
    Json imported = Json::array();
    bool any_resign = false;
    for (const auto& i : images) {
        if (stop) break;
        pr.current = i.title_id + "/" + i.name;
        if (progress) progress(pr);
        gc_import_info pre{};
        if (gc_import_encrypted(fs::host(i.path).c_str(), nullptr, 0, req.user_id.c_str(), &pre, err, sizeof err) != 0) {
            gc_unmount();
            return fail("import", i.name + " : lecture de l'image impossible (" + err + ")");
        }
        const std::string dir = i.name.substr(6);
        if (pre.sfo_zeroed || pre.title_id != i.title_id || pre.dir_name != dir || !pre.user_account_known || !pre.user_account_id) {
            gc_unmount();
            return fail("preflight", i.name + " : vérification refusée (titre " + pre.title_id + ", dossier " + pre.dir_name + ", param.sfo " +
                        (pre.sfo_zeroed ? "à zéro" : "lu") + ", compte cible " + hex16(pre.user_account_id) + ")");
        }
        if (req.check_only) {
            gc_unmount();
            Json one = Json::object();
            one.set("title_id", i.title_id);
            one.set("name", i.name);
            one.set("main_title", std::string(pre.main_title));
            one.set("match", pre.match != 0);
            one.set("save_account", hex16(pre.save_account_id));
            one.set("target_account", hex16(pre.user_account_id));
            one.set("exists", pre.exists != 0);
            imported.push(one);
            ++pr.files_done;
            pr.bytes_done += i.size;
            if (progress) progress(pr);
            continue;
        }
        if (!pre.match) {
            any_resign = true;
            if (!req.allow_resign) {
                gc_unmount();
                res.report.set("save_account", hex16(pre.save_account_id));
                res.report.set("target_account", hex16(pre.user_account_id));
                return fail("account_mismatch", "cette sauvegarde appartient à un autre compte : confirmez le portage vers ce compte");
            }
        }
        gc_finish_info fin{};
        if (gc_import_finish(req.user_id.c_str(), nullptr, &fin, err, sizeof err) != 0 || fin.sfo_zeroed) {
            gc_unmount();
            return fail("finish", i.name + " : import non terminé (" + err + ")");
        }
        if (fin.warning[0]) log("restore-save warning name=%s %s", i.name.c_str(), fin.warning);
        Json one = Json::object();
        one.set("title_id", i.title_id);
        one.set("name", i.name);
        one.set("resigned", !pre.match);
        one.set("database", fin.db_ok != 0);
        if (fin.warning[0]) one.set("warning", std::string(fin.warning));
        imported.push(one);
        ++pr.files_done;
        pr.bytes_done += i.size;
        if (progress) progress(pr);
        log("restore-save imported title=%s name=%s match=%d owner=%d db=%d blob=%d", i.title_id.c_str(), i.name.c_str(), pre.match, fin.owner_result, fin.db_ok, fin.blob_written);
    }
    gc_unmount();
    if (stop) return fail("stopped", "interrompue");
    if (req.check_only) { res.report.set("checked", imported); res.ok = true; log("restore-save check done images=%zu", images.size()); return res; }
    res.report.set("imported", imported);
    res.report.set("resigned", any_resign);

    // 6. Vérification en montant chaque image installée (sa copie de travail : l'original n'est pas réécrit).
    std::set<std::uint64_t> ids;
    unsigned verified = 0;
    for (const auto& i : images) {
        const std::string installed = fs::join(fs::join("/user/home", req.user_id), "savedata_prospero/" + i.title_id + "/" + i.name);
        gc_mount_info mnt{};
        const bool ok = gc_mount_save(fs::host(installed).c_str(), &mnt, err, sizeof err) == 0;
        gc_unmount();
        if (!ok || mnt.sfo_zeroed || !mnt.sfo_ok || !mnt.files || !mnt.account_id)
            return fail("verify", i.name + " : vérification au montage refusée" + (ok ? std::string() : " (" + std::string(err) + ")"));
        ids.insert(mnt.account_id);
        ++verified;
    }
    if (verified != images.size() || ids.size() != 1)
        return fail("verify", "vérification incomplète : " + std::to_string(verified) + "/" + std::to_string(images.size()) + " images, " + std::to_string(ids.size()) + " identifiant(s)");
    res.report.set("verified", static_cast<std::uint64_t>(verified));
    res.report.set("account_id", "0x" + hex16(*ids.begin()));
    res.ok = true;
    log("restore-save done images=%u resigned=%d", verified, any_resign ? 1 : 0);
    return res;
}


RestoreSaveResult delete_console_save(const std::string& state_dir, const std::string& user_id, const std::string& title_id) {
    RestoreSaveResult res;
    auto fail = [&](const std::string& code, const std::string& msg) { res.code = code; res.error = msg; log("delete-save failed code=%s %s", code.c_str(), msg.c_str()); return res; };
    auto hexid = [](const std::string& v, std::size_t n) { return v.size() == n && v.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos; };
    auto tid_ok = [](const std::string& t) {
        if (t.size() != 9) return false;
        for (int i = 0; i < 4; ++i) if (t[static_cast<std::size_t>(i)] < 'A' || t[static_cast<std::size_t>(i)] > 'Z') return false;
        for (int i = 4; i < 9; ++i) if (t[static_cast<std::size_t>(i)] < '0' || t[static_cast<std::size_t>(i)] > '9') return false;
        return true;
    };
    if (!hexid(user_id, 8) || !tid_ok(title_id)) return fail("bad_request", "compte ou jeu invalide");
    char running[16] = {0};
    if (gc_running_title(running) && title_id == running) return fail("game_running", std::string("fermez d'abord le jeu ") + running);

    const std::string home = fs::join("/user/home", user_id);
    // PS5 (savedata_prospero) ou PS4 (savedata) : celle qui a des fichiers.
    struct Layout { const char* images; const char* meta; const char* db; };
    const Layout ps5{"savedata_prospero/", "savedata_prospero_meta/user/", "/system_data/savedata_prospero/"};
    const Layout ps4{"savedata/", "savedata_meta/user/", "/system_data/savedata/"};
    std::vector<fs::Entry> es;
    const bool is_ps5 = fs::list(fs::host(fs::join(home, std::string(ps5.images) + title_id)), es) && !es.empty();
    const Layout& L = is_ps5 ? ps5 : ps4;
    const std::string images = fs::join(home, std::string(L.images) + title_id), meta = fs::join(home, std::string(L.meta) + title_id);
    if (!fs::is_dir(fs::host(images)) && !fs::is_dir(fs::host(meta))) return fail("not_found", "aucune sauvegarde de ce jeu sur ce compte");
    const std::string db_path = std::string(L.db) + user_id + "/db/user/savedata.db";

    // 1. Copie de sécurité (fichiers + lignes de la base). Échec → rien n'est touché.
    const std::string safety = fs::join(fs::join(state_dir, "delete-safety"), iso_time(now_unix()).substr(0, 19) + "-" + title_id + "-" + user_id);
    if (!copy_dir(images, fs::join(safety, "images")) || !copy_dir(meta, fs::join(safety, "meta")))
        return fail("safety", "copie de sécurité impossible : rien n'a été supprimé");
    sqlite3* db = nullptr;
    Json rows = Json::array();
    const bool have_db = fs::exists(fs::host(db_path));
    if (have_db) {
        if (sqlite3_open(fs::host(db_path).c_str(), &db) != SQLITE_OK) { if (db) sqlite3_close(db); return fail("db", "base des sauvegardes illisible : rien n'a été supprimé"); }
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT * FROM savedata WHERE title_id = ?1", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, title_id.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(st) == SQLITE_ROW) {
                Json r = Json::object();
                for (int c = 0; c < sqlite3_column_count(st); ++c) {
                    const unsigned char* t = sqlite3_column_text(st, c);
                    r.set(sqlite3_column_name(st, c), t ? std::string(reinterpret_cast<const char*>(t)) : std::string());
                }
                rows.push(r);
            }
            sqlite3_finalize(st);
        }
    }
    Json note = Json::object();
    note.set("title_id", title_id);
    note.set("user_id", user_id);
    note.set("images", images);
    note.set("meta", meta);
    note.set("db", db_path);
    note.set("db_rows", rows);
    if (!fs::write_atomic(fs::host(fs::join(safety, "delete.json")), note.dump())) {
        if (db) sqlite3_close(db);
        return fail("safety", "copie de sécurité impossible : rien n'a été supprimé");
    }
    res.report.set("safety_copy", safety);

    // 2. La base d'abord (la console ne liste plus la sauvegarde), puis les fichiers.
    if (db) {
        sqlite3_stmt* st = nullptr;
        bool ok = sqlite3_prepare_v2(db, "DELETE FROM savedata WHERE title_id = ?1", -1, &st, nullptr) == SQLITE_OK;
        if (ok) { sqlite3_bind_text(st, 1, title_id.c_str(), -1, SQLITE_TRANSIENT); ok = sqlite3_step(st) == SQLITE_DONE; sqlite3_finalize(st); }
        sqlite3_close(db);
        if (!ok) return fail("db", "base des sauvegardes non modifiable : rien n'a été supprimé");
        res.report.set("db_rows", static_cast<std::int64_t>(rows.size()));
    }
    const bool gone = fs::remove_tree(fs::host(images)) && fs::remove_tree(fs::host(meta));
    if (!gone) return fail("files", "base mise à jour mais fichiers pas tous retirés (copie de sécurité : " + safety + ")");
    res.ok = true;
    log("delete-save ok %s %s safety=%s", user_id.c_str(), title_id.c_str(), safety.c_str());
    return res;
}

}  // namespace pbc::saves
