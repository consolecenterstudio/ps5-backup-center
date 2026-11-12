#include "images/images.hpp"
#include "saves/switch_import.hpp"

#include <cstdio>
#include <functional>
#include <cstring>
#include <ctime>

#include "core/fs.hpp"
#include "core/http_client.hpp"
#include "core/util.hpp"
#include "core/zip.hpp"

namespace pbc::saves {

std::vector<EdenProfile> eden_profiles(const std::vector<std::string>& roots) {
    std::vector<EdenProfile> out;
    for (const auto& root : roots) {
        std::string d;
        if (!fs::read(fs::host(fs::join(root, "user/nand/system/save/8000000000000010/su/avators/profiles.dat")), d, 1u << 16)) continue;
        // 16 octets d'en-tête puis 8 entrées de 0xC8 : identifiant (2 × 64 bits), date, nom (32 octets).
        for (int i = 0; i < 8; ++i) {
            const std::size_t o = 0x10 + static_cast<std::size_t>(i) * 0xC8;
            if (o + 0x48 > d.size()) break;
            std::uint64_t lo = 0, hi = 0;
            std::memcpy(&lo, d.data() + o, 8);
            std::memcpy(&hi, d.data() + o + 8, 8);
            if (!lo && !hi) continue;
            char folder[40];
            std::snprintf(folder, sizeof folder, "%016llX%016llX", static_cast<unsigned long long>(hi), static_cast<unsigned long long>(lo));
            out.push_back({root, folder, std::string(d.c_str() + o + 0x28, strnlen(d.c_str() + o + 0x28, 32))});
        }
        out.push_back({root, "00000000000000000000000000000000", "Appareil (commun)"});
    }
    return out;
}

// « 2024-12-31_19-01-25 » dans le nom JKSV → heure Unix (approximative : heure locale de la Switch).
std::int64_t jksv_time(const std::string& file) {
    int y, mo, d, h, mi, s;
    const auto p = file.rfind(" - ");
    // JKSV récent : « 2024-12-31_19-01-25 » ; ancien : « 2024.06.28 @ 14.30.27 ».
    if (p == std::string::npos || (std::sscanf(file.c_str() + p + 3, "%d-%d-%d_%d-%d-%d", &y, &mo, &d, &h, &mi, &s) != 6 &&
                                   std::sscanf(file.c_str() + p + 3, "%d.%d.%d @ %d.%d.%d", &y, &mo, &d, &h, &mi, &s) != 6)) return 0;
    std::tm t{};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = s;
    return static_cast<std::int64_t>(timegm(&t));
}

namespace {

std::int64_t newest(const std::string& dir, unsigned& files) {
    std::int64_t m = 0;
    std::vector<fs::Entry> es;
    if (!fs::list(fs::host(dir), es)) return 0;
    for (const auto& e : es) {
        if (e.dir) m = std::max(m, newest(fs::join(dir, e.name), files));
        else { m = std::max(m, e.mtime); ++files; }
    }
    return m;
}

bool copy_tree(const std::string& from, const std::string& to) {
    std::vector<fs::Entry> es;
    if (!fs::list(fs::host(from), es)) return true;
    if (!fs::mkdirs(fs::host(to))) return false;
    for (const auto& e : es) {
        if (e.dir) { if (!copy_tree(fs::join(from, e.name), fs::join(to, e.name))) return false; continue; }
        std::string data;
        if (!fs::read(fs::host(fs::join(from, e.name)), data, 1u << 30) || !fs::write_atomic(fs::host(fs::join(to, e.name)), data)) return false;
    }
    return true;
}
}  // namespace

SwitchImportResult import_switch(store::Store& src, const SwitchImportRequest& req, const backup::ProgressFn& progress, const std::atomic<bool>& stop) {
    SwitchImportResult res;
    auto fail = [&](const std::string& code, const std::string& msg) { res.code = code; res.error = msg; log("switch-import failed code=%s %s", code.c_str(), msg.c_str()); return res; };
    if (req.title_id.size() != 16 || req.profile.size() != 32 || req.file.find('/') != std::string::npos || req.game.find('/') != std::string::npos)
        return fail("bad_request", "demande invalide");
    const std::string target = fs::join(req.eden_root, "user/nand/user/save/0000000000000000/" + req.profile + "/" + req.title_id);

    // 1. Téléchargement du zip JKSV depuis la Switch.
    const std::string work = fs::join(req.state_dir, "switch-import/" + random_id(4));
    const std::string zip_host = fs::host(fs::join(work, "save.zip"));
    fs::mkdirs(fs::host(work));
    {
        std::string err;
        auto r = src.open("/JKSV/" + req.game + "/" + req.file, err);
        if (!r) return fail("download", "lecture sur la Switch impossible : " + err);
        FILE* out = std::fopen(zip_host.c_str(), "wb");
        if (!out) return fail("download", "écriture temporaire impossible");
        char buf[65536];
        long n = 0;
        while (!stop && (n = r->read(buf, sizeof buf)) > 0) std::fwrite(buf, 1, static_cast<std::size_t>(n), out);
        std::fclose(out);
        if (stop) { fs::remove_tree(fs::host(work)); return fail("stopped", "interrompue"); }
        if (n < 0) { fs::remove_tree(fs::host(work)); return fail("download", "transfert interrompu"); }
    }
    std::vector<zip::Entry> entries;
    if (!zip::list(zip_host, entries)) { fs::remove_tree(fs::host(work)); return fail("bad_zip", "archive JKSV illisible"); }
    std::vector<zip::Entry> files;
    std::uint64_t total = 0;
    for (const auto& e : entries) {
        if (e.dir() || e.name == ".nx_save_meta.bin" || e.name.find("..") != std::string::npos || e.name[0] == '/') continue;
        files.push_back(e);
        total += e.size;
    }
    if (files.empty()) { fs::remove_tree(fs::host(work)); return fail("empty", "sauvegarde vide"); }
    // JKSV récent : .nx_save_meta.bin = « JKSV », 1 octet de version, puis l'identifiant du jeu (64 bits, octet 5).
    for (const auto& e : entries) {
        if (e.name != ".nx_save_meta.bin") continue;
        std::string meta, err;
        if (zip::read(zip_host, e, meta, err) && meta.size() >= 14 && meta.compare(0, 4, "JKSV") == 0) {
            std::uint64_t tid = 0;
            std::memcpy(&tid, meta.data() + 5, 8);
            char hex[20];
            std::snprintf(hex, sizeof hex, "%016llX", static_cast<unsigned long long>(tid));
            res.report.set("meta_title_id", std::string(hex));
            std::string want = req.title_id;
            for (auto& c : want) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (want != hex) { fs::remove_tree(fs::host(work)); return fail("wrong_game", "cette sauvegarde est celle du jeu " + std::string(hex) + ", pas de " + req.title_id); }
        }
    }

    // 2. Comparaison avec la sauvegarde déjà dans Eden.
    unsigned existing = 0;
    const std::int64_t eden_time = newest(target, existing), switch_time = jksv_time(req.file);
    const bool eden_newer = existing && switch_time && eden_time > switch_time + 86400;  // marge : fuseaux horaires
    Json files_j = Json::array();
    for (const auto& e : files) { Json f = Json::object(); f.set("name", e.name); f.set("size", e.size); files_j.push(f); }
    res.report.set("target", target);
    res.report.set("files", files_j);
    res.report.set("bytes", total);
    res.report.set("existing_files", static_cast<std::uint64_t>(existing));
    res.report.set("eden_time", eden_time);
    res.report.set("switch_time", switch_time);
    res.report.set("eden_newer", eden_newer);
    if (req.check_only) { fs::remove_tree(fs::host(work)); res.ok = true; return res; }
    if (eden_newer && !req.force_older) { fs::remove_tree(fs::host(work)); return fail("eden_newer", "la sauvegarde dans Eden est plus récente que celle de la Switch : confirmez pour la remplacer"); }

    // 3. L'émulateur doit être fermé (appli au premier plan, lue par nous-mêmes : aucune appli externe).
    {
        int app_id = -1;
        std::string running;
        if (images::foreground_app(app_id, running))
            for (const auto& a : req.eden_apps)
                if (running == a) { fs::remove_tree(fs::host(work)); return fail("eden_running", "fermez d'abord Eden"); }
    }

    // Copie de sécurité de la sauvegarde Eden actuelle.
    if (existing) {
        const std::string safety = fs::join(req.state_dir, "restore-safety/" + iso_time(now_unix()).substr(0, 19) + "-eden-" + req.title_id + "-" + random_id(2));
        if (!copy_tree(target, safety)) { fs::remove_tree(fs::host(work)); return fail("safety", "copie de sécurité impossible : rien n'a été modifié"); }
        res.report.set("safety_copy", safety);
    }

    // 4. Nouvelle version écrite à côté, vérifiée, puis échange des dossiers.
    const std::string fresh = target + ".pbc-new", old = target + ".pbc-old";
    fs::remove_tree(fs::host(fresh));
    backup::Progress pr;
    pr.files_total = static_cast<unsigned>(files.size());
    pr.bytes_total = total;
    for (const auto& e : files) {
        if (stop) { fs::remove_tree(fs::host(fresh)); fs::remove_tree(fs::host(work)); return fail("stopped", "interrompue"); }
        std::string data, err;
        if (!zip::read(zip_host, e, data, err)) { fs::remove_tree(fs::host(fresh)); fs::remove_tree(fs::host(work)); return fail("bad_zip", err); }
        const std::string dst = fs::join(fresh, e.name);
        fs::mkdirs(fs::host(std::string(fs::dirname(dst))));
        if (!fs::write_atomic(fs::host(dst), data)) { fs::remove_tree(fs::host(fresh)); fs::remove_tree(fs::host(work)); return fail("write", "écriture impossible dans Eden"); }
        fs::Entry check;
        if (!fs::stat(fs::host(dst), check) || check.size != e.size) { fs::remove_tree(fs::host(fresh)); fs::remove_tree(fs::host(work)); return fail("verify", "relecture différente : " + e.name); }
        ++pr.files_done;
        pr.bytes_done += e.size;
        pr.current = e.name;
        if (progress) progress(pr);
    }
    fs::remove_tree(fs::host(old));
    const bool had = fs::exists(fs::host(target));
    fs::mkdirs(fs::host(std::string(fs::dirname(target))));
    if (had && std::rename(fs::host(target).c_str(), fs::host(old).c_str()) != 0) { fs::remove_tree(fs::host(fresh)); fs::remove_tree(fs::host(work)); return fail("swap", "remplacement impossible : rien n'a été modifié"); }
    if (std::rename(fs::host(fresh).c_str(), fs::host(target).c_str()) != 0) {
        if (had) std::rename(fs::host(old).c_str(), fs::host(target).c_str());
        fs::remove_tree(fs::host(fresh));
        fs::remove_tree(fs::host(work));
        return fail("swap", "remplacement impossible : l'ancienne sauvegarde est remise");
    }
    fs::remove_tree(fs::host(old));
    fs::remove_tree(fs::host(work));
    res.ok = true;
    log("switch-import done title=%s profile=%s files=%zu", req.title_id.c_str(), req.profile.c_str(), files.size());
    return res;
}

SwitchImportResult export_switch(store::Store& dst, const SwitchExportRequest& req, const std::atomic<bool>& stop) {
    SwitchImportResult res;
    auto fail = [&](const std::string& code, const std::string& msg) { res.code = code; res.error = msg; log("switch-export failed code=%s %s", code.c_str(), msg.c_str()); return res; };
    if (req.title_id.size() != 16 || req.profile.size() != 32 || req.game_folder.empty() || req.game_folder.find('/') != std::string::npos || req.profile_name.find('/') != std::string::npos)
        return fail("bad_request", "demande invalide");
    const std::string src = fs::join(req.eden_root, "user/nand/user/save/0000000000000000/" + req.profile + "/" + req.title_id);
    std::vector<std::pair<std::string, std::string>> files;
    std::uint64_t total = 0;
    std::function<bool(const std::string&, const std::string&)> add = [&](const std::string& dir, const std::string& rel) {
        std::vector<fs::Entry> es;
        if (!fs::list(fs::host(dir), es)) return false;
        for (const auto& e : es) {
            if (stop) return false;
            const std::string r = rel.empty() ? e.name : rel + "/" + e.name;
            if (e.dir) { if (!add(fs::join(dir, e.name), r)) return false; continue; }
            std::string data;
            if (!fs::read(fs::host(fs::join(dir, e.name)), data, 256u << 20)) return false;
            total += data.size();
            files.emplace_back(r, std::move(data));
        }
        return true;
    };
    if (!add(src, "") || files.empty()) return fail("empty", "aucune sauvegarde Eden pour ce jeu et ce profil");
    char stamp[32];
    const std::time_t now = static_cast<std::time_t>(now_unix());
    std::tm t{};
    gmtime_r(&now, &t);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &t);
    const std::string name = (req.profile_name.empty() ? "Eden" : req.profile_name) + " - " + stamp + " PBC.zip";
    const std::string path = "/JKSV/" + req.game_folder + "/" + name;
    Json list = Json::array();
    for (const auto& [n, d] : files) { Json f = Json::object(); f.set("name", n); f.set("size", static_cast<std::uint64_t>(d.size())); list.push(f); }
    res.report.set("target", path);
    res.report.set("files", list);
    res.report.set("bytes", total);
    if (req.check_only) { res.ok = true; return res; }
    const std::string zip = zip::build(files);
    if (!dst.mkdirs("/JKSV/" + req.game_folder) || !dst.write_small(path, zip)) return fail("write", "écriture impossible sur la Switch");
    fs::Entry back;
    if (!dst.stat(path, back) || back.size != zip.size()) return fail("verify", "relecture différente sur la Switch");
    res.ok = true;
    log("switch-export done title=%s file=%s bytes=%zu", req.title_id.c_str(), path.c_str(), zip.size());
    return res;
}

}  // namespace pbc::saves
