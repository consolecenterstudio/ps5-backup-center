// Images de jeux PS5 — orchestration des opérations de PS5 Game Compressor dans le moteur.
//
// Adapté de engine/third_party/game-compressor — PS5 Game Compressor v1.0.4, juma-sayeh,
// GPL-3.0. Repris : le format .ffpfsc et tout le calcul (pfs_compress.c, pfs_decompress.c,
// pfs_repair.c, pfs_validate_hash.c, ampr_index.c, miniz), compilés sans modification ;
// la logique de gc_api.c (place libre, mode sûr, suppression différée, réglages
// ShadowMountPlus de gc_shadowmount.c, test de lecture) est réécrite ici, plus stricte :
// on refuse au lieu de fermer le jeu, on vérifie le résultat avant toute suppression.
#include "images/images.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <mutex>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

#include "core/fs.hpp"
#include "core/http_client.hpp"
#include "core/perms.hpp"
#include "core/json.hpp"
#include "core/util.hpp"
#include "images/gc_bridge.h"
#include "scan/recognizers/title_dir.hpp"

extern "C" {
#include "game-compressor/src/miniz.h"
#include "game-compressor/src/pfs_compress.h"
#include "game-compressor/src/pfs_repair.h"
#include "game-compressor/src/pfs_validate_hash.h"
}

#if defined(PBCK_PS5)
extern "C" {
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int app_id, char* title_id);
int sceSystemServiceKillApp(int app_id, int how, int reason, int unk);
int sceSystemServicePowerTick(void);
}
#endif

namespace pbc::images {

namespace {
// Même jeu : « PPSA30279 », « PPSA30279.exfat », « PPSA30279-app », « PPSA30279 Taxi Chaos 2.ffpfsc », « PPSA30279_… ».
bool same_title_name(const std::string& base, const std::string& title_id) {
    return title_id.size() == 9 && base.compare(0, 9, title_id) == 0 &&
           (base.size() == 9 || base[9] == '-' || base[9] == '.' || base[9] == ' ' || base[9] == '_');
}

constexpr std::uint64_t block_size = 65536;                  // bloc PFS / PFSC
constexpr std::uint64_t destructive_min_free = 1ull << 30;   // Game Compressor : 1 Gio minimum
constexpr const char* shadow_dir = "/data/shadowmount";
constexpr const char* shadow_config = "/data/shadowmount/config.ini";
constexpr const char* shadow_autotune = "/data/shadowmount/autotune.ini";
constexpr const char* shadow_manual = "/data/shadowmount/manual.lst";
constexpr const char* shadow_mnt = "/mnt/shadowmnt";
constexpr const char* shadow_pfsc = "/mnt/shadowmnt/pfsc";

std::string H(const std::string& console) { return fs::host(console); }
std::string C(const std::string& host) { return fs::console_path(host); }

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        char a = s[s.size() - suffix.size() + i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
        if (a != b) return false;
    }
    return true;
}

std::uint32_t fnv1a32(const std::string& s) {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

std::string hex8(std::uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "%08x", v); return b; }

std::string stem_of(const std::string& path) {
    std::string base(fs::basename(path));
    const auto dot = base.rfind('.');
    return dot == std::string::npos || dot == 0 ? base : base.substr(0, dot);
}

// PPSA01234 dans un nom de fichier (règle de Game Compressor), sinon "".
std::string title_in_name(const std::string& name) {
    for (std::size_t i = 0; i + 9 <= name.size(); ++i) {
        auto up = [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c; };
        if (up(name[i]) != 'P' || up(name[i + 1]) != 'P' || up(name[i + 2]) != 'S' || up(name[i + 3]) != 'A') continue;
        bool ok = true;
        for (std::size_t j = 4; j < 9; ++j) ok = ok && name[i + j] >= '0' && name[i + j] <= '9';
        if (ok) return "PPSA" + name.substr(i + 4, 5);
    }
    return {};
}

bool read_link(const std::string& console_file, std::string& out) {
    std::string text;
    if (!fs::read(H(console_file), text, 4096)) return false;
    out = text.substr(0, text.find_first_of("\r\n"));
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
    return !out.empty();
}

std::uint64_t tree_bytes(const std::string& host_dir, std::uint64_t* files = nullptr, int depth = 0) {
    if (depth > 32) return 0;
    std::vector<fs::Entry> entries;
    if (!fs::list(host_dir, entries)) return 0;
    std::uint64_t total = 0;
    for (const auto& e : entries) {
        if (e.dir) total += tree_bytes(fs::join(host_dir, e.name), files, depth + 1);
        else { total += e.size; if (files) ++*files; }
    }
    return total;
}

// Place libre au plus proche dossier existant du chemin console donné.
bool free_space(const std::string& console_path, std::uint64_t& out) {
    std::string p = console_path;
    for (int i = 0; i < 64 && !p.empty(); ++i) {
        fs::Space s;
        if (fs::exists(H(p)) && fs::space(H(p), s)) { out = s.free; return true; }
        if (p == "/") break;
        p = std::string(fs::dirname(p));
    }
    return false;
}

// Table des montages du système, lue SANS ATTENTE (getfsstat MNT_NOWAIT) : ne touche à aucun disque, ne peut donc
// pas bloquer, même si un montage est cassé (image retirée sans démontage…). Sur la console, c'est la seule façon
// dont le moteur regarde les montages de ShadowMountPlus — jamais en lisant dans leurs dossiers.
[[maybe_unused]] std::vector<std::string> mount_table() {
    std::vector<std::string> out;
#if defined(PBCK_PS5)
    const int n = ::getfsstat(nullptr, 0, MNT_NOWAIT);
    if (n <= 0) return out;
    std::vector<struct ::statfs> v(static_cast<std::size_t>(n) + 16);
    const int m = ::getfsstat(v.data(), static_cast<long>(v.size() * sizeof(struct ::statfs)), MNT_NOWAIT);
    for (int i = 0; i < m; ++i) out.emplace_back(v[static_cast<std::size_t>(i)].f_mntonname);
#endif
    return out;
}

// Monté : un point de montage a un autre périphérique que son dossier parent (tests sur Mac seulement).
[[maybe_unused]] bool is_mount_point(const std::string& host_dir) {
    struct ::stat a {}, b {};
    if (::stat(host_dir.c_str(), &a) != 0 || !S_ISDIR(a.st_mode)) return false;
    const std::string parent(fs::dirname(host_dir));
    if (::stat(parent.c_str(), &b) != 0) return false;
    return a.st_dev != b.st_dev;
}

// --- état du travail en cours (étape lisible, partagée avec le fil de suivi) ---------
std::mutex g_step_mu;
std::string g_step;
void set_step(const std::string& step) {
    { std::lock_guard<std::mutex> l(g_step_mu); g_step = step; }
    log("images step %s", step.c_str());
}
std::string current_step() { std::lock_guard<std::mutex> l(g_step_mu); return g_step; }

std::mutex g_run_mu;  // une seule opération à la fois (état global de Game Compressor)

std::function<std::string()> g_running_probe;
std::string g_state_dir;
std::mutex g_validation_mu;

void gc_logger(const char* line) { log("gc %s", line); }

// Fil de suivi : transmet la progression, propage pause / annulation, garde la console éveillée.
class Monitor {
public:
    Monitor(const std::function<void(const Progress&)>& cb, std::atomic<bool>& interrupt) : cb_(cb), interrupt_(interrupt) {
        if (interrupt_) { pbc_gc_job_cancel(); cancel_sent_ = true; }
        thread_ = std::thread([this] { loop(); });
    }
    ~Monitor() { stop(); }
    void stop() {
        if (!thread_.joinable()) return;
        quit_ = true;
        thread_.join();
        report();
    }
private:
    void report() {
        pbc_gc_status_t s;
        pbc_gc_job_status(&s);
        Progress p;
        p.bytes_done = s.done_bytes;
        p.bytes_total = s.total_bytes;
        p.files_done = static_cast<unsigned>(s.done_files);
        p.files_total = static_cast<unsigned>(s.total_files);
        p.step = current_step();
        p.current = p.step;
        if (s.destructive_active) p.current += " — suppression pendant l'écriture : annulation impossible";
        if (cb_) cb_(p);
    }
    void loop() {
        int ticks = 0;
        while (!quit_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (interrupt_ && !cancel_sent_) { pbc_gc_job_cancel(); cancel_sent_ = true; }
            if (++ticks % 2 == 0) report();
#if defined(PBCK_PS5)
            if (ticks % 120 == 0) (void)sceSystemServicePowerTick();  // ~30 s : pas de mise en veille auto
#endif
        }
    }
    const std::function<void(const Progress&)>& cb_;
    std::atomic<bool>& interrupt_;
    std::atomic<bool> quit_{false};
    bool cancel_sent_ = false;
    std::thread thread_;
};

bool cancelled(std::atomic<bool>& interrupt) { return interrupt.load() || pbc_gc_job_cancelled(); }

// --- ShadowMountPlus : réglages (fichiers texte), d'après gc_shadowmount.c ----------
std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}
bool iequal(std::string_view a, std::string_view b) {
    return a.size() == b.size() && ends_with_ci(a, b);
}
// « clé=valeur » d'une ligne de réglage (commentaires ignorés).
bool split_setting(const std::string& line, std::string& key, std::string& value) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') return false;
    const auto eq = t.find('=');
    if (eq == std::string::npos) return false;
    key = trim(t.substr(0, eq));
    value = trim(t.substr(eq + 1));
    return true;
}
bool line_is_mode(const std::string& line, const std::string& name, bool ro_only = false) {
    std::string k, v;
    if (!split_setting(line, k, v)) return false;
    if (!iequal(k, "image_ro") && (ro_only || !iequal(k, "image_rw"))) return false;
    return iequal(v, name);
}
bool line_is_sector(const std::string& line, const std::string& name) {
    std::string k, v;
    if (!split_setting(line, k, v) || !iequal(k, "image_sector")) return false;
    const auto colon = v.rfind(':');
    return colon != std::string::npos && iequal(trim(v.substr(0, colon)), name);
}

// Réécrit un fichier de réglages : retire les lignes reconnues, ajoute `add` (si non vide).
bool rewrite_settings(const char* console_file, const std::function<bool(const std::string&)>& drop, const std::string& add, std::string& err) {
    if (!fs::mkdirs(H(shadow_dir))) { err = "création de /data/shadowmount impossible"; return false; }
    std::string text, out;
    fs::read(H(console_file), text, 4u << 20);
    std::size_t pos = 0;
    bool changed = false;
    while (pos < text.size()) {
        auto e = text.find('\n', pos);
        const std::string line = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = e == std::string::npos ? text.size() : e + 1;
        if (drop(line)) { changed = true; continue; }
        out += line;
        out += '\n';
    }
    if (!add.empty()) { out += add; out += '\n'; changed = true; }
    if (!changed) return true;
    if (!fs::write_atomic(H(console_file), out)) { err = std::string("écriture impossible : ") + console_file; return false; }
    ::chmod(H(console_file).c_str(), 0666);
    return true;
}

bool remove_name_hints(const std::string& name, std::string& err) {
    return rewrite_settings(shadow_config, [&](const std::string& l) { return line_is_mode(l, name); }, "", err) &&
           rewrite_settings(shadow_autotune, [&](const std::string& l) { return line_is_sector(l, name); }, "", err);
}
// Avant d'écrire de nouveaux réglages pour un titre, retire ceux d'une image précédente (gc_shadowmount_remove_title_pfsc_hints).
bool remove_title_hints(const std::string& title_id, const std::string& path, std::string& err) {
    if (!path.empty() && !remove_name_hints(std::string(fs::basename(path)), err)) return false;
    if (title_id.empty()) return true;
    for (const char* ext : {".exfat", ".pfs", ".ffpfsc"})
        if (!remove_name_hints(title_id + ext, err)) return false;
    return true;
}
// .ffpfsc : image exFAT imbriquée en lecture seule. AUCUNE taille de secteur imposée au conteneur : ShadowMountPlus
// prend la sienne (4096). Avant, on écrivait « image_sector=<image>:65536 » (repris de PS5 Game Compressor) : sur la
// PS5 en 3.21, le montage échouait alors (« nmount rdonly failed: Invalid argument », 3 essais puis abandon), alors
// que la même image et une autre .ffpfsc se montent sans cette ligne. Une ligne laissée par une version
// d'avant est retirée.
bool write_ffpfsc_hints(const std::string& title_id, const std::string& outer, const std::string& nested, bool nested_exfat, std::string& err) {
    if (!remove_title_hints(title_id, outer, err)) return false;
    const std::string outer_name(fs::basename(outer));
    if (!rewrite_settings(shadow_autotune, [&](const std::string& l) { return line_is_sector(l, outer_name); }, "", err)) return false;
    if (nested_exfat && !nested.empty()) {
        if (!rewrite_settings(shadow_config, [&](const std::string& l) { return line_is_mode(l, nested); }, "image_ro=" + nested, err)) return false;
        if (!rewrite_settings(shadow_autotune, [&](const std::string& l) { return line_is_sector(l, nested); }, "", err)) return false;
    }
    return true;
}
// Image .exfat / .ffpfs : lecture seule (gc_shadowmount_prepare_image_hints_for_title).
bool write_image_hints(const std::string& title_id, const std::string& image, std::string& err) {
    if (!remove_title_hints(title_id, image, err)) return false;
    const std::string name(fs::basename(image));
    return rewrite_settings(shadow_config, [&](const std::string& l) { return line_is_mode(l, name); }, "image_ro=" + name, err) &&
           rewrite_settings(shadow_autotune, [&](const std::string& l) { return line_is_sector(l, name); }, "", err);
}
// API locale de ShadowMountPlus (1.7+, 127.0.0.1:10101) : ajoute la source et met un scan en file. Le scan est
// reporté tant qu'un jeu ou une application tourne (« game_active ») : on le dit à l'utilisateur.
// Réponse vide si l'API ne répond pas (ancienne version : manual.lst seul).
Json smp_add(const std::string& source) {
    Json req = Json::object();
    req.set("path", source);
    std::string out;
    const http::Response r = http::request("POST", "http://127.0.0.1:10101/api/v1/manual/add", "", req.dump(), out, 4);
    Json j;
    if (r.status <= 0 || !Json::parse(out, j) || !j.is_object()) return Json();
    if (!j["scan_queued"].as_bool() && !j["scan_deferred"].as_bool()) {  // déjà connue : redemande un scan
        std::string out2;
        Json s = Json::object();
        s.set("reset_attempts", false);
        Json j2;
        if (http::request("POST", "http://127.0.0.1:10101/api/v1/scan", "", s.dump(), out2, 4).status > 0 && Json::parse(out2, j2) && j2.is_object()) return j2;
    }
    return j;
}
Json g_last_smp;  // dernière réponse de ShadowMountPlus (rapport de la tâche)

// Demande de montage : chemin ajouté à manual.lst (remplace l'entrée du même titre), puis date touchée.
bool request_mount(const std::string& title_id, const std::string& source, std::string& err) {
    const auto same_title = [&](const std::string& l) {
        const std::string t = trim(l);
        if (t.empty() || t == source) return true;
        if (title_id.size() != 9) return false;
        // « PPSA30279.exfat », « PPSA30279-app », « PPSA30279 Taxi Chaos 2.ffpfsc », « PPSA30279_… » : même titre.
        // Avant : l'espace n'était pas reconnu, l'ancienne image restait inscrite à côté de la nouvelle.
        return same_title_name(std::string(fs::basename(t)), title_id);
    };
    if (!rewrite_settings(shadow_manual, same_title, source, err)) return false;
    struct timeval tv[2];
    ::gettimeofday(&tv[0], nullptr);
    struct ::stat st {};
    if (::stat(H(shadow_manual).c_str(), &st) == 0 && tv[0].tv_sec <= st.st_mtime) { tv[0].tv_sec = st.st_mtime + 1; tv[0].tv_usec = 0; }
    tv[1] = tv[0];
    ::utimes(H(shadow_manual).c_str(), tv);
    ::chmod(H(shadow_manual).c_str(), 0666);
    g_last_smp = smp_add(source);
    return true;
}
// Ce que ShadowMountPlus a répondu : inscription immédiate, ou reportée (jeu / application en cours).
void report_smp(Json& rep) {
    if (!g_last_smp.is_object()) return;
    rep.set("shadowmount_api", true);
    const std::string why = g_last_smp["scan_deferred_reason"].as_string();
    if (g_last_smp["scan_deferred"].as_bool() || !why.empty()) rep.set("mount_deferred", why.empty() ? std::string("busy") : why);
}

// --- lecture d'un conteneur .ffpfsc (d'après pfsc_open de pfs_repair.c) -------------
std::uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
std::uint64_t rd64(const unsigned char* p) { return rd32(p) | (static_cast<std::uint64_t>(rd32(p + 4)) << 32); }

bool pread_all(int fd, void* buf, std::size_t n, std::uint64_t off) {
    auto* p = static_cast<unsigned char*>(buf);
    while (n) {
        const ssize_t r = ::pread(fd, p, n, static_cast<off_t>(off));
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        p += r; n -= static_cast<std::size_t>(r); off += static_cast<std::uint64_t>(r);
    }
    return true;
}

struct Pfsc {
    int fd = -1;
    std::uint64_t file_start = 0, stored_size = 0, logical_size = 0, nested_size = 0, blocks = 0;
    std::vector<std::uint64_t> offsets;
    std::string nested_name;
    int nested_type = PFS_NESTED_UNKNOWN;
    ~Pfsc() { if (fd >= 0) ::close(fd); }
};

bool open_pfsc(const std::string& host_path, Pfsc& img, std::string& err) {
    pfs_decompress_info_t dec{};
    char e[256] = {0};
    if (pfs_decompress_detect_nested(host_path.c_str(), &dec, e, sizeof e) != 0) { err = std::string("conteneur illisible : ") + e; return false; }
    img.nested_name = dec.nested_name;
    img.nested_type = dec.nested_type;
    img.fd = ::open(host_path.c_str(), O_RDONLY);
    if (img.fd < 0) { err = std::string("ouverture impossible : ") + std::strerror(errno); return false; }
    unsigned char inode[0xA8], hdr[0x30];
    if (!pread_all(img.fd, inode, sizeof inode, block_size + 3 * 0xA8)) { err = "en-tête PFS illisible"; return false; }
    img.stored_size = rd64(inode + 0x08);
    img.nested_size = rd64(inode + 0x10);
    const std::int32_t db0 = static_cast<std::int32_t>(rd32(inode + 0x64));
    if ((rd32(inode + 0x04) & 1u) == 0 || db0 < 0 || !img.stored_size || !img.nested_size) { err = "pas d'image compressée dans le conteneur"; return false; }
    img.file_start = static_cast<std::uint64_t>(db0) * block_size;
    if (!pread_all(img.fd, hdr, sizeof hdr, img.file_start) || rd32(hdr) != 0x43534650u || rd32(hdr + 0x0c) != block_size) { err = "en-tête PFSC invalide"; return false; }
    const std::uint64_t table = rd64(hdr + 0x18);
    img.logical_size = rd64(hdr + 0x28);
    if (table != 0x400 || !img.logical_size || img.logical_size % block_size || img.nested_size > img.logical_size) { err = "disposition PFSC non prise en charge"; return false; }
    img.blocks = img.logical_size / block_size;
    if (img.blocks > (1ull << 32)) { err = "trop de blocs"; return false; }
    std::vector<unsigned char> raw((img.blocks + 1) * 8);
    if (!pread_all(img.fd, raw.data(), raw.size(), img.file_start + table)) { err = "table des blocs illisible"; return false; }
    img.offsets.resize(img.blocks + 1);
    for (std::uint64_t i = 0; i <= img.blocks; ++i) {
        img.offsets[i] = rd64(raw.data() + i * 8);
        if (i && img.offsets[i] < img.offsets[i - 1]) { err = "table des blocs incohérente"; return false; }
    }
    if (img.offsets[img.blocks] > img.stored_size) { err = "table des blocs hors du conteneur"; return false; }
    return true;
}

struct VerifyStats {
    std::uint64_t blocks = 0, checked = 0, bad = 0, hashed = 0;
    std::string hash_mode = "missing";
    std::vector<std::uint64_t> first_bad;
};

// Vérification logicielle complète : chaque bloc est décompressé (miniz) et comparé à son
// empreinte SHA-256 enregistrée à la compression (<conteneur>.vhash), sans montage.
bool verify_ffpfsc(const std::string& host_path, VerifyStats& st, std::atomic<bool>& interrupt, std::string& err) {
    Pfsc img;
    if (!open_pfsc(host_path, img, err)) return false;
    st.blocks = img.blocks;
    pfs_vhash_reader_t vr{};
    vr.fd = -1;
    pfs_vhash_mode_t mode = PFS_VHASH_MODE_MISSING;
    char e[256] = {0};
    const int hrc = pfs_vhash_reader_open_for_image(host_path.c_str(), img.logical_size, img.nested_size, img.blocks,
                                                    img.nested_name.c_str(), img.nested_type, &vr, &mode, e, sizeof e);
    if (hrc < 0) { err = std::string("empreintes illisibles : ") + e; return false; }
    const bool hashes = hrc == 0;
    st.hash_mode = pfs_vhash_mode_name(mode);
    constexpr std::uint64_t batch = 128;
    std::vector<unsigned char> stored, decoded(block_size), hbuf(batch * PFS_VHASH_HASH_SIZE);
    pbc_gc_job_set_progress(0, img.nested_size);
    bool ok = true;
    for (std::uint64_t i = 0; i < img.blocks && ok; i += batch) {
        if (cancelled(interrupt)) { err = "annulé"; ok = false; break; }
        const std::uint64_t n = std::min(batch, img.blocks - i);
        const std::uint64_t a = img.offsets[i], b = img.offsets[i + n];
        stored.resize(static_cast<std::size_t>(b - a));
        if (!pread_all(img.fd, stored.data(), stored.size(), img.file_start + a)) { err = "lecture impossible (support défectueux ?)"; ok = false; break; }
        if (hashes && pfs_vhash_reader_read_hashes(&vr, i, n, hbuf.data(), e, sizeof e) != 0) { err = std::string("empreintes illisibles : ") + e; ok = false; break; }
        for (std::uint64_t k = 0; k < n; ++k) {
            const std::uint64_t idx = i + k, s = img.offsets[idx] - a, len = img.offsets[idx + 1] - img.offsets[idx];
            bool good = len > 0 && len <= block_size;
            if (good && len == block_size) std::memcpy(decoded.data(), stored.data() + s, block_size);
            else if (good) good = tinfl_decompress_mem_to_mem(decoded.data(), block_size, stored.data() + s, len, TINFL_FLAG_PARSE_ZLIB_HEADER) == block_size;
            if (good && hashes) {
                const std::uint64_t useful = std::min<std::uint64_t>(block_size, img.nested_size > idx * block_size ? img.nested_size - idx * block_size : 0);
                unsigned char h[PFS_VHASH_HASH_SIZE];
                pfs_sha256(decoded.data(), static_cast<std::size_t>(useful), h);
                good = std::memcmp(h, hbuf.data() + k * PFS_VHASH_HASH_SIZE, PFS_VHASH_HASH_SIZE) == 0;
                ++st.hashed;
            }
            ++st.checked;
            if (!good) { ++st.bad; if (st.first_bad.size() < 16) st.first_bad.push_back(idx); }
        }
        pbc_gc_job_set_progress(std::min(img.nested_size, (i + n) * block_size), img.nested_size);
    }
    pfs_vhash_reader_close(&vr);
    return ok;
}

// L'image décompressée correspond-elle bloc à bloc aux empreintes du conteneur d'origine ?
bool verify_image_against(const std::string& image_host, const std::string& ffpfsc_host, std::atomic<bool>& interrupt, std::string& verdict, std::string& err) {
    Pfsc img;
    if (!open_pfsc(ffpfsc_host, img, err)) return false;
    pfs_vhash_reader_t vr{};
    vr.fd = -1;
    pfs_vhash_mode_t mode = PFS_VHASH_MODE_MISSING;
    char e[256] = {0};
    const int hrc = pfs_vhash_reader_open_for_image(ffpfsc_host.c_str(), img.logical_size, img.nested_size, img.blocks,
                                                    img.nested_name.c_str(), img.nested_type, &vr, &mode, e, sizeof e);
    if (hrc != 0) { verdict = "sans empreintes"; return true; }  // rien à comparer : la décompression a déjà décodé chaque bloc
    const int fd = ::open(image_host.c_str(), O_RDONLY);
    if (fd < 0) { pfs_vhash_reader_close(&vr); err = "image produite illisible"; return false; }
    std::vector<unsigned char> buf(block_size);
    bool ok = true;
    for (std::uint64_t i = 0; i < img.blocks && ok; ++i) {
        if (cancelled(interrupt)) { err = "annulé"; ok = false; break; }
        const std::uint64_t useful = std::min<std::uint64_t>(block_size, img.nested_size > i * block_size ? img.nested_size - i * block_size : 0);
        if (!useful) break;
        unsigned char want[PFS_VHASH_HASH_SIZE], got[PFS_VHASH_HASH_SIZE];
        if (!pread_all(fd, buf.data(), static_cast<std::size_t>(useful), i * block_size) || pfs_vhash_reader_read_hash(&vr, i, want, e, sizeof e) != 0) { err = "lecture de vérification impossible"; ok = false; break; }
        pfs_sha256(buf.data(), static_cast<std::size_t>(useful), got);
        if (std::memcmp(want, got, sizeof want) != 0) { err = "l'image produite diffère de l'original (bloc " + std::to_string(i) + ")"; ok = false; break; }
        if (i % 64 == 0) pbc_gc_job_set_progress(i * block_size, img.nested_size);
    }
    ::close(fd);
    pfs_vhash_reader_close(&vr);
    if (ok) verdict = "identique (SHA-256 par bloc)";
    return ok;
}

// Lecture complète d'un fichier ou d'un dossier : compte les blocs de 64 Kio illisibles.
struct ReadCheck { std::uint64_t bytes = 0, files = 0, bad_blocks = 0; std::vector<std::string> bad_files; };
bool read_check_file(const std::string& host_file, ReadCheck& rc, std::uint64_t total, std::atomic<bool>& interrupt) {
    const int fd = ::open(host_file.c_str(), O_RDONLY);
    if (fd < 0) { ++rc.bad_blocks; if (rc.bad_files.size() < 16) rc.bad_files.push_back(C(host_file)); return true; }
    std::vector<unsigned char> buf(4u << 20);
    std::uint64_t off = 0;
    bool had_error = false;
    for (;;) {
        if (cancelled(interrupt)) { ::close(fd); return false; }
        const ssize_t r = ::pread(fd, buf.data(), buf.size(), static_cast<off_t>(off));
        if (r == 0) break;
        if (r < 0) {
            if (errno == EINTR) continue;
            ++rc.bad_blocks;
            had_error = true;
            off += block_size;  // saute le bloc fautif
            struct ::stat st {};
            if (::fstat(fd, &st) != 0 || off >= static_cast<std::uint64_t>(st.st_size)) break;
            continue;
        }
        off += static_cast<std::uint64_t>(r);
        rc.bytes += static_cast<std::uint64_t>(r);
        pbc_gc_job_set_progress(rc.bytes, total);
    }
    ::close(fd);
    ++rc.files;
    if (had_error && rc.bad_files.size() < 16) rc.bad_files.push_back(C(host_file));
    return true;
}
bool read_check_tree(const std::string& host, ReadCheck& rc, std::uint64_t total, std::atomic<bool>& interrupt, int depth = 0) {
    if (depth > 32) return true;
    std::vector<fs::Entry> entries;
    if (!fs::list(host, entries)) return true;
    for (const auto& e : entries) {
        const std::string p = fs::join(host, e.name);
        if (!(e.dir ? read_check_tree(p, rc, total, interrupt, depth + 1) : read_check_file(p, rc, total, interrupt))) return false;
    }
    return true;
}

// Copie d'arborescence (extraction d'une image montée), d'après copy_tree_gc.
bool copy_tree(const std::string& src, const std::string& dst, std::uint64_t& copied, std::uint64_t total, std::atomic<bool>& interrupt, std::string& err, int depth = 0) {
    if (depth > 32) { err = "arborescence trop profonde"; return false; }
    if (::mkdir(dst.c_str(), 0777) != 0 && errno != EEXIST) { err = "création impossible : " + C(dst); return false; }
    std::vector<fs::Entry> entries;
    if (!fs::list(src, entries)) { err = "lecture impossible : " + C(src); return false; }
    std::vector<unsigned char> buf(4u << 20);
    for (const auto& e : entries) {
        if (cancelled(interrupt)) { err = "annulé"; return false; }
        const std::string s = fs::join(src, e.name), d = fs::join(dst, e.name);
        if (e.dir) { if (!copy_tree(s, d, copied, total, interrupt, err, depth + 1)) return false; continue; }
        const int in = ::open(s.c_str(), O_RDONLY);
        if (in < 0) { err = "lecture impossible : " + C(s); return false; }
        const int out = ::open(d.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (out < 0) { ::close(in); err = "écriture impossible : " + C(d); return false; }
        {   // mêmes droits que la source (un eboot.bin 0777 reste exécutable)
            struct ::stat sm {};
            if (::fstat(in, &sm) == 0) ::fchmod(out, sm.st_mode & 07777);
        }
        bool ok = true;
        for (;;) {
            if (cancelled(interrupt)) { err = "annulé"; ok = false; break; }
            const ssize_t r = ::read(in, buf.data(), buf.size());
            if (r == 0) break;
            if (r < 0) { if (errno == EINTR) continue; err = "lecture impossible : " + C(s); ok = false; break; }
            const unsigned char* p = buf.data();
            std::size_t left = static_cast<std::size_t>(r);
            while (left) {
                const ssize_t w = ::write(out, p, left);
                if (w < 0 && errno == EINTR) continue;
                if (w <= 0) { err = "écriture impossible : " + C(d); ok = false; break; }
                p += w; left -= static_cast<std::size_t>(w);
            }
            if (!ok) break;
            copied += static_cast<std::uint64_t>(r);
            pbc_gc_job_set_progress(copied, total);
        }
        if (ok && ::fsync(out) != 0) { err = "écriture impossible : " + C(d); ok = false; }
        ::close(in);
        ::close(out);
        if (!ok) return false;
    }
    return true;
}

// --- mémoire des validations -----------------------------------------------------------
std::string validation_file() { return g_state_dir.empty() ? std::string() : fs::join(g_state_dir, "images-validation.json"); }
Json load_validations() {
    Json j = Json::object();
    std::string text;
    const std::string f = validation_file();
    if (!f.empty() && fs::read(H(f), text) && Json::parse(text, j) && j.is_object()) return j;
    return Json::object();
}
void remember_validation(const std::string& console_path, Json v) {
    const std::string f = validation_file();
    if (f.empty()) return;
    std::lock_guard<std::mutex> l(g_validation_mu);
    Json all = load_validations();
    fs::Entry e;
    if (fs::stat(H(console_path), e)) { v.set("size", e.size); v.set("mtime", e.mtime); }
    v.set("checked", now_unix());
    all.set(console_path, v);
    fs::mkdirs(H(g_state_dir));
    fs::write_atomic(H(f), all.dump(true) + "\n");
}
// Dernière validation, si le fichier n'a pas changé depuis.
Json known_validation(const std::string& console_path) {
    std::lock_guard<std::mutex> l(g_validation_mu);
    const Json all = load_validations();
    const Json& v = all[console_path];
    if (!v.is_object()) return Json();
    fs::Entry e;
    if (fs::stat(H(console_path), e) && !e.dir && (static_cast<std::uint64_t>(v["size"].as_int()) != e.size || v["mtime"].as_int() != e.mtime)) {
        Json stale = v;
        stale.set("state", "stale");
        return stale;
    }
    return v;
}

std::string location_of(const std::string& p) {
    if (p.rfind("/mnt/usb", 0) == 0) return "usb";
    if (p.rfind("/mnt/ext", 0) == 0) return "ext";
    return "internal";
}

// Sortie par défaut : dans `target_dir` si donné, sinon à côté de la source ; un jeu installé
// (/user/app, /system_ex) va dans /data/homebrew (jamais d'écriture dans les dossiers système).
std::string default_dir(const Options& o) {
    if (!o.target_dir.empty()) return o.target_dir;
    const std::string parent(fs::dirname(o.source));
    if (parent.rfind("/user/", 0) == 0 || parent.rfind("/system", 0) == 0) return "/data/homebrew";
    return parent;
}

bool safe_console_path(const std::string& p) {
    return !p.empty() && p[0] == '/' && p.find("/../") == std::string::npos && p.find("/./") == std::string::npos &&
           (p.size() < 3 || p.compare(p.size() - 3, 3, "/..") != 0);
}

}  // namespace

// ===================================================================================
const char* to_string(Kind k) {
    switch (k) {
        case Kind::folder: return "folder";
        case Kind::exfat: return "exfat";
        case Kind::ffpfs: return "ffpfs";
        case Kind::ffpfsc: return "ffpfsc";
        case Kind::ffpkg: return "ffpkg";
        case Kind::native: return "native";
        default: return "unknown";
    }
}
const char* storage_of(Kind k) {
    switch (k) {
        case Kind::native: return "native";
        case Kind::folder: return "folder";
        case Kind::unknown: return "";
        default: return "image";
    }
}
const char* container_of(Kind k) {
    switch (k) {
        case Kind::ffpfsc: return "ffpfsc";
        case Kind::exfat: return "exfat";
        case Kind::ffpfs: return "ffpfs";
        case Kind::ffpkg: return "ffpkg";
        default: return "";
    }
}

Kind kind_of(const std::string& p) {
    fs::Entry e;
    if (!fs::stat(H(p), e)) return Kind::unknown;
    if (e.dir) {
        // Installation native : le système garde le paquet (app.pkg) ; aucune inscription ShadowMountPlus.
        std::string lnk;
        if (fs::exists(H(fs::join(p, "app.pkg"))) && !read_link(fs::join(p, "mount.lnk"), lnk) && !read_link(fs::join(p, "mount_img.lnk"), lnk))
            return Kind::native;
        return Kind::folder;
    }
    if (ends_with_ci(p, ".ffpfsc")) return Kind::ffpfsc;
    if (ends_with_ci(p, ".exfat")) return Kind::exfat;
    if (ends_with_ci(p, ".ffpfs") || ends_with_ci(p, ".pfs")) return Kind::ffpfs;
    if (ends_with_ci(p, ".ffpkg")) return Kind::ffpkg;
    return Kind::unknown;
}

void set_state_dir(const std::string& d) { g_state_dir = d; }
void set_running_title_probe(std::function<std::string()> p) { g_running_probe = std::move(p); }

bool foreground_app(int& app_id, std::string& title_id) {
    app_id = -1;
    title_id.clear();
#if defined(PBCK_PS5)
    app_id = sceSystemServiceGetAppIdOfRunningBigApp();
    if (app_id < 0) return false;
    char tid[32] = {0};
    if (sceSystemServiceGetAppTitleId(app_id, tid) != 0 || !tid[0]) return false;
    title_id = tid;
    return true;
#else
    return false;
#endif
}

int kill_app(int app_id) {
#if defined(PBCK_PS5)
    return sceSystemServiceKillApp(app_id, -1, 0, 0);
#else
    (void)app_id;
    return -1;
#endif
}

std::string running_title() {
    if (g_running_probe) return g_running_probe();
#if defined(PBCK_PS5)
    const int app = sceSystemServiceGetAppIdOfRunningBigApp();
    if (app < 0) return {};
    char tid[32] = {0};
    if (sceSystemServiceGetAppTitleId(app, tid) != 0 || !tid[0]) return "?";  // inconnu : prudence
    return tid;
#else
    return {};
#endif
}

namespace {
// Inscrit : /user/app/<ID>/mount.lnk (dossier) ou mount_img.lnk (image) désigne ce chemin.
bool registered_at(const std::string& p, Kind kind, const std::string& title_id) {
    if (kind == Kind::native) return true;
    std::string lnk;
    if (title_id.empty()) return false;
    if (kind == Kind::folder) return read_link(fs::join("/user/app/" + title_id, "mount.lnk"), lnk) && lnk == p;
    return read_link(fs::join("/user/app/" + title_id, "mount_img.lnk"), lnk) && lnk == p;
}
// Point de montage actif du contenu (sans tenir compte de l'inscription).
bool live_mount(const std::string& p, Kind kind, const std::string& title_id, std::string& where);
}  // namespace

bool mounted(const std::string& p, Kind kind, const std::string& title_id, std::string* mount_path) {
    std::string where;
    const bool live = live_mount(p, kind, title_id, where);
    if (mount_path) *mount_path = where;
    return live || registered_at(p, kind, title_id);
}

namespace {
bool live_mount(const std::string& p, Kind kind, const std::string& title_id, std::string& where) {
    const bool linked = kind != Kind::native && registered_at(p, kind, title_id);
    bool live = false;
#if defined(PBCK_PS5)
    // Console : uniquement la table des montages (sans attente) — on ne lit jamais dans un dossier monté.
    const auto table = mount_table();
    auto has = [&](const std::string& m) { return std::find(table.begin(), table.end(), m) != table.end(); };
    if (kind == Kind::native || ((kind == Kind::folder || kind == Kind::ffpkg) && linked)) {
        const std::string m = "/system_ex/app/" + title_id;
        live = !title_id.empty() && has(m);
        if (live || kind == Kind::folder) where = kind == Kind::folder ? p : m;
        return live;
    }
    if (kind == Kind::ffpfsc) {
        const std::string outer = std::string(shadow_pfsc) + "/" + stem_of(p) + "_" + hex8(fnv1a32(p));
        live = has(outer);
        // Image imbriquée montée à son tour : /mnt/shadowmnt/<ID>_<fnv> (trouvée dans la table, pas en listant).
        if (live && !title_id.empty())
            for (const auto& m : table)
                if (m.rfind(std::string(shadow_mnt) + "/" + title_id + "_", 0) == 0) { where = m; break; }
    } else if (kind == Kind::exfat || kind == Kind::ffpfs) {
        const std::string m = std::string(shadow_mnt) + "/" + stem_of(p) + "_" + hex8(fnv1a32(p));
        live = has(m);
        if (live) where = m;
    }
    return live;
#else
    if (kind == Kind::native || ((kind == Kind::folder || kind == Kind::ffpkg) && linked)) {
        // Contenu exposé au système dans /system_ex/app/<ID> (installation native ou montage ShadowMountPlus).
        const std::string m = "/system_ex/app/" + title_id;
        live = !title_id.empty() && is_mount_point(H(m));
        if (live || kind == Kind::folder) where = kind == Kind::folder ? p : m;
        return live;
    }
    if (kind == Kind::ffpfsc) {
        // ShadowMountPlus : conteneur dans /mnt/shadowmnt/pfsc/<nom>_<fnv>, image imbriquée montée à son tour.
        const std::string outer = std::string(shadow_pfsc) + "/" + stem_of(p) + "_" + hex8(fnv1a32(p));
        live = is_mount_point(H(outer));
        std::vector<fs::Entry> inside;
        if (live && fs::list(H(outer), inside))
            for (const auto& e : inside) {
                if (e.dir) continue;
                const std::string nested = fs::join(outer, e.name);
                const std::string m = std::string(shadow_mnt) + "/" + stem_of(nested) + "_" + hex8(fnv1a32(nested));
                if (fs::is_dir(H(m))) { where = m; break; }
            }
    } else if (kind == Kind::exfat || kind == Kind::ffpfs) {
        const std::string m = std::string(shadow_mnt) + "/" + stem_of(p) + "_" + hex8(fnv1a32(p));
        live = is_mount_point(H(m));
        if (live) where = m;
    }
    return live;
#endif
}
}  // namespace

Json Info::to_json() const {
    Json j = Json::object();
    j.set("path", path);
    j.set("kind", to_string(kind));
    j.set("storage", storage_of(kind));
    j.set("container", container_of(kind));
    j.set("title_id", title_id);
    j.set("title", title);
    j.set("location", location_of(path));
    j.set("size", size);
    j.set("data_size", data_size);
    if (kind == Kind::ffpfsc) {
        j.set("compressed_size", size);
        j.set("inner", inner);
        j.set("inner_name", inner_name);
        j.set("has_hashes", has_hashes);
        if (data_size) j.set("ratio", static_cast<double>(size) / static_cast<double>(data_size));
    }
    if (kind == Kind::folder) j.set("apr_indexed", apr_indexed);
    j.set("mounted", mounted);
    j.set("registered", registered);
    if (!mount_path.empty()) j.set("mount_path", mount_path);
    j.set("running", running);
    j.set("validation", validation.is_null() ? Json("unknown") : validation["state"]);
    if (validation.is_object()) j.set("validation_detail", validation);
    return j;
}

bool inspect(const std::string& p, Info& out, std::string& error, bool measure) {
    out = Info{};
    out.path = p;
    if (!safe_console_path(p)) { error = "chemin invalide"; return false; }
    out.kind = kind_of(p);
    fs::Entry e;
    if (!fs::stat(H(p), e)) { error = "introuvable : " + p; return false; }
    char err[256] = {0};
    switch (out.kind) {
        case Kind::folder: {
            int cat = 0;
            if (!scan::read_param(fs::join(p, "sce_sys/param.json"), out.title_id, out.title, cat)) { error = "dossier sans sce_sys/param.json"; return false; }
            out.apr_indexed = fs::exists(H(fs::join(p, "ampr_emu.index")));
            if (measure) out.size = out.data_size = tree_bytes(H(p));
            break;
        }
        case Kind::ffpfsc: {
            pfs_decompress_info_t d{};
            out.size = e.size;
            if (pfs_decompress_detect_nested(H(p).c_str(), &d, err, sizeof err) != 0) { error = std::string("conteneur .ffpfsc illisible : ") + err; return false; }
            out.inner = d.nested_type == PFS_NESTED_EXFAT ? "exfat" : "pfs";
            out.inner_name = d.nested_name;
            out.data_size = d.nested_size;
            out.has_hashes = fs::exists(H(p + ".vhash"));
            out.title_id = title_in_name(std::string(fs::basename(p)));
            if (out.title_id.empty()) out.title_id = title_in_name(out.inner_name);
            break;
        }
        case Kind::exfat:
        case Kind::ffpfs: {
            pfs_app_info_t a{};
            out.size = out.data_size = e.size;
            if (pfs_image_probe(H(p).c_str(), &a, err, sizeof err) != 0) { error = std::string("image invalide : ") + err; return false; }
            out.title_id = a.title_id;
            break;
        }
        case Kind::ffpkg:  // paquet monté par ShadowMountPlus : format non décodé par le moteur
            out.size = out.data_size = e.size;
            out.title_id = title_in_name(std::string(fs::basename(p)));
            break;
        case Kind::native: {
            int cat = 0;
            if (!scan::read_param(fs::join(p, "sce_sys/param.json"), out.title_id, out.title, cat)) out.title_id = title_in_name(std::string(fs::basename(p)));
            if (measure) out.size = out.data_size = tree_bytes(H(p));
            break;
        }
        default: error = "ni dossier de jeu, ni image .exfat / .ffpfs / .ffpfsc / .ffpkg, ni jeu installé"; return false;
    }
    if (out.title.empty() && !out.title_id.empty()) {
        std::string tid, name;
        int cat = 0;
        if (scan::read_param(fs::join("/user/app/" + out.title_id, "sce_sys/param.json"), tid, name, cat)) out.title = name;
    }
    out.registered = registered_at(p, out.kind, out.title_id);
    out.mounted = live_mount(p, out.kind, out.title_id, out.mount_path);
    const std::string run = running_title();
    out.running = !out.title_id.empty() && run == out.title_id;
    out.validation = known_validation(p);
    return true;
}

std::vector<Info> discover(bool measure_folders) {
    std::vector<std::string> roots = {"/data/homebrew", "/data/etaHEN/games"};
    for (const char* base : {"/mnt/ext0", "/mnt/ext1", "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3", "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7"})
        for (const char* sub : {"", "/homebrew", "/etaHEN/games"}) roots.push_back(std::string(base) + sub);
    std::vector<std::string> paths;
    auto add = [&](const std::string& p) { if (std::find(paths.begin(), paths.end(), p) == paths.end()) paths.push_back(p); };
    for (const auto& r : roots) {
        std::vector<fs::Entry> entries;
        if (!fs::list(H(r), entries)) continue;
        for (const auto& e : entries) {
            if (e.name.empty() || e.name[0] == '.') continue;  // fichiers temporaires cachés
            const std::string p = fs::join(r, e.name);
            if (e.dir ? fs::exists(H(fs::join(p, "sce_sys/param.json"))) && (fs::exists(H(fs::join(p, "eboot.bin"))) || fs::exists(H(fs::join(p, "iboot.bin"))))
                      : (ends_with_ci(e.name, ".ffpfsc") || ends_with_ci(e.name, ".exfat") || ends_with_ci(e.name, ".ffpfs") || ends_with_ci(e.name, ".ffpkg")))
                add(p);
        }
    }
    // Inscriptions ShadowMountPlus (image ou dossier ailleurs), installations natives (SSD interne,
    // et stockage étendu M.2 / USB s'il expose user/app), dossiers de jeu inscrits.
    for (const char* app_root : {"/user/app", "/mnt/ext0/user/app", "/mnt/ext1/user/app"}) {
        std::vector<fs::Entry> apps;
        if (!fs::list(H(app_root), apps)) continue;
        for (const auto& a : apps) {
            if (!a.dir) continue;
            const std::string dir = fs::join(app_root, a.name);
            std::string lnk;
            if (read_link(fs::join(dir, "mount_img.lnk"), lnk) && lnk[0] == '/' && fs::exists(H(lnk))) add(lnk);
            else if (read_link(fs::join(dir, "mount.lnk"), lnk) && lnk[0] == '/' && fs::is_dir(H(lnk))) add(lnk);
            // installation native : seulement les jeux (PPSA… PS5, CUSA… PS4), pas les applis système (NPXS…)
            else if ((a.name.rfind("PPSA", 0) == 0 || a.name.rfind("CUSA", 0) == 0) && fs::exists(H(fs::join(dir, "app.pkg")))) add(dir);
            else if (fs::exists(H(fs::join(dir, "eboot.bin"))) && fs::exists(H(fs::join(dir, "sce_sys/param.json")))) add(dir);
        }
    }
    std::vector<Info> out;
    for (const auto& p : paths) {
        Info i;
        std::string err;
        if (inspect(p, i, err, measure_folders)) out.push_back(std::move(i));
    }
    return out;
}

// --- options -------------------------------------------------------------------------------
namespace {
constexpr const char* op_names[] = {"compress", "uncompress", "make_image", "extract_image", "validate", "read_speed", "ampr_index", "set_read_only", "transfer"};
constexpr const char* task_names[] = {"compress", "uncompress", "make_image", "extract_image", "validate_image", "read_speed_test", "build_ampr_index", "set_read_only", "transfer"};
constexpr int op_count = 9;
}
const char* to_string(Op o) { return op_names[static_cast<int>(o)]; }
const char* task_type(Op o) { return task_names[static_cast<int>(o)]; }
bool parse_op(std::string_view s, Op& out) {
    for (int i = 0; i < op_count; ++i) if (s == op_names[i]) { out = static_cast<Op>(i); return true; }
    return false;
}
bool parse_task_type(std::string_view s, Op& out) {
    for (int i = 0; i < op_count; ++i) if (s == task_names[i]) { out = static_cast<Op>(i); return true; }
    return false;
}

std::string last_mount_deferred() {
    Json r = Json::object();
    report_smp(r);
    return r["mount_deferred"].as_string();
}

namespace {
// Appel JSON à l'API locale de ShadowMountPlus ; vrai si « status » vaut 0.
bool smp_post(const std::string& route, const Json& body, Json& out, std::string& err) {
    // Occupé (son scan ou une autre opération de stockage, EBUSY = 16) : on attend et on réessaie, 3 min au plus.
    // Avant : retirer le jeu de la liste lançait un scan, et la désinstallation juste après était refusée.
    for (int attempt = 0;; ++attempt) {
        std::string text;
        const http::Response r = http::request("POST", "http://127.0.0.1:10101" + route, "", body.dump(), text, 10);
        if (r.status <= 0) { err = "ShadowMountPlus ne répond pas (API 10101)"; return false; }
        if (!Json::parse(text, out) || !out.is_object()) { err = "réponse illisible de ShadowMountPlus"; return false; }
        if (out["status"].as_int(-1) == 0) return true;
        const std::string why = out["error"].as_string();
        const bool busy = out["status"].as_int(-1) == 16 || why.find("busy") != std::string::npos;
        if (busy && attempt < 60) { std::this_thread::sleep_for(std::chrono::seconds(3)); continue; }
        err = "ShadowMountPlus refuse : " + (why.empty() ? std::to_string(out["status"].as_int(-1)) : why);
        return false;
    }
}
}  // namespace

std::vector<std::string> manual_sources(const std::string& title_id) {
    std::vector<std::string> out;
    std::string text;
    if (!fs::read(H(shadow_manual), text, 1u << 20)) return out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == '#') continue;
        if (same_title_name(std::string(fs::basename(line)), title_id)) out.push_back(line);
    }
    return out;
}

std::vector<std::string> smp_game_sources(const std::string& title_id) {
    std::vector<std::string> out;
    Json b = Json::object(), r;
    b.set("title_id", title_id);
    std::string err;
    if (!smp_post("/api/v1/games/info", b, r, err)) return out;
    // Forme de la réponse non documentée en détail : on garde toute valeur texte qui ressemble à un chemin de ce jeu.
    std::function<void(const Json&)> walk = [&](const Json& j) {
        if (j.is_object()) { for (const auto& [k, v] : j.members()) walk(v); return; }
        if (j.is_array()) { for (const auto& v : j.items()) walk(v); return; }
        const std::string s = j.as_string();
        if ((s.rfind("/mnt/usb", 0) == 0 || s.rfind("/mnt/ext", 0) == 0 || s.rfind("/data/", 0) == 0) &&
            same_title_name(std::string(fs::basename(s)), title_id) && std::find(out.begin(), out.end(), s) == out.end())
            out.push_back(s);
    };
    walk(r);
    return out;
}

bool smp_manual_add(const std::string& source) { return smp_add(source).is_object(); }

bool smp_manual_remove(const std::string& source, std::string& err) {
    Json b = Json::object(), out;
    b.set("path", source);
    return smp_post("/api/v1/manual/remove", b, out, err);
}

bool smp_uninstall(const std::string& title_id, std::string& err) {
    Json b = Json::object(), out;
    b.set("title_id", title_id);
    return smp_post("/api/v1/games/uninstall", b, out, err);
}

bool register_folder(const std::string& title_id, const std::string& dir, std::string& err) {
    if (title_id.size() != 9) { err = "identifiant du jeu inconnu"; return false; }
    return request_mount(title_id, dir, err);
}

bool register_image(const std::string& title_id, const std::string& path, std::string& err) {
    if (title_id.size() != 9) { err = "identifiant du jeu inconnu"; return false; }
    if (kind_of(path) == Kind::ffpfsc) {
        Pfsc img;
        if (!open_pfsc(H(path), img, err)) return false;
        if (!write_ffpfsc_hints(title_id, path, img.nested_name, img.nested_type == PFS_NESTED_EXFAT, err)) return false;
    } else if (!write_image_hints(title_id, path, err)) return false;
    return request_mount(title_id, path, err);
}

std::string games_root(const std::string& mount) {
    if (mount.empty() || mount == "/data" || mount == "/") return "/data/homebrew";
    return fs::join(mount, "homebrew");
}

bool Options::from_json(const Json& b, Options& o, std::string& error) {
    o = Options{};
    const std::string t = b["type"].as_string().empty() ? b["op"].as_string() : b["type"].as_string();
    if (!parse_task_type(t, o.op) && !parse_op(t, o.op)) { error = "opération inconnue : " + t; return false; }
    o.source = b["source"].as_string();
    o.target_dir = b["target_dir"].as_string();
    o.output = b["output"].as_string();
    o.level = static_cast<int>(b["level"].as_int(6));
    o.inner = b["inner"].as_string().empty() ? "exfat" : b["inner"].as_string();
    // « to » : format de sortie d'une décompression (folder / exfat) ; pour un transfert c'est l'emplacement
    // de destination, déjà converti en target_dir par le moteur.
    {
        const std::string to = b["to"].as_string();
        o.to = to == "exfat" || to == "image" ? to : "folder";
    }
    o.delete_source = b["delete_source"].as_bool();
    o.destructive = b["destructive"].as_bool();
    o.repair = b["repair"].as_bool();
    o.shadowmount_hints = b["shadowmount_hints"].as_bool(true);
    o.mount = b["mount"].as_bool();
    o.seconds = static_cast<int>(b["seconds"].as_int(20));
    o.mode = b["mode"].as_string().empty() ? "copy" : b["mode"].as_string();
    o.verify = b["verify"].as_bool(true);
    o.previous = b["previous"].as_string();
    o.compress = b["compress"].as_bool();
    if (o.op == Op::transfer && o.mode != "copy" && o.mode != "move") { error = "transfert : mode copy ou move"; return false; }
    if (o.op == Op::transfer && o.target_dir.empty() && o.output.empty()) { error = "transfert : destination manquante"; return false; }
    while (o.source.size() > 1 && o.source.back() == '/') o.source.pop_back();
    while (o.target_dir.size() > 1 && o.target_dir.back() == '/') o.target_dir.pop_back();
    if (!safe_console_path(o.source)) { error = "source invalide"; return false; }
    if (!o.target_dir.empty() && !safe_console_path(o.target_dir)) { error = "dossier de destination invalide"; return false; }
    if (!o.output.empty() && !safe_console_path(o.output)) { error = "chemin de sortie invalide"; return false; }
    if (o.level < 0 || o.level > 9) { error = "niveau de compression : 0 à 9"; return false; }
    if (o.inner != "exfat" && o.inner != "pfs") { error = "format interne : exfat ou pfs"; return false; }
    if (o.to != "folder" && o.to != "exfat" && o.to != "image") { error = "décompression vers : folder ou exfat"; return false; }
    if (o.to == "image") o.to = "exfat";
    if (o.seconds < 3 || o.seconds > 600) o.seconds = 20;
    if (o.destructive && o.op != Op::compress) { error = "le mode destructif ne concerne que la compression"; return false; }
    return true;
}

Json Options::to_json() const {
    Json j = Json::object();
    j.set("op", to_string(op));
    j.set("source", source);
    if (!target_dir.empty()) j.set("target_dir", target_dir);
    if (!output.empty()) j.set("output", output);
    j.set("level", level);
    j.set("inner", inner);
    j.set("to", to);
    j.set("delete_source", delete_source);
    j.set("destructive", destructive);
    j.set("repair", repair);
    j.set("shadowmount_hints", shadowmount_hints);
    j.set("mount", mount);
    j.set("seconds", seconds);
    if (op == Op::transfer) { j.set("mode", mode); j.set("verify", verify); j.set("compress", compress); if (!previous.empty()) j.set("previous", previous); }
    return j;
}

Json Plan::to_json() const {
    Json j = Json::object();
    j.set("ok", ok);
    if (!error.empty()) j.set("error", error);
    if (!code.empty()) j.set("code", code);
    j.set("title_id", title_id);
    if (!output.empty()) j.set("output", output);
    j.set("source_bytes", source_bytes);
    j.set("required_bytes", required_bytes);
    j.set("free_bytes", free_bytes);
    j.set("missing_bytes", free_bytes >= required_bytes ? std::uint64_t{0} : required_bytes - free_bytes);
    j.set("estimate", estimate);
    return j;
}

// --- plan : sortie prévue, place nécessaire, garde-fous ---------------------------------
namespace {
// Opération qui modifie ou supprime la source (ou l'image elle-même).
bool writes_source(const Options& o) {
    return o.delete_source || o.destructive || (o.op == Op::validate && o.repair) || o.op == Op::ampr_index || o.op == Op::set_read_only ||
           (o.op == Op::transfer && o.mode == "move");
}

// Transfert avec compression = compression vers la destination (suppression vérifiée pour un déplacement).
Options normalize(const Options& in) {
    Options o = in;
    if (o.op == Op::transfer && o.compress && kind_of(o.source) != Kind::ffpfsc) {
        o.op = Op::compress;
        o.delete_source = in.mode == "move";
        o.mount = in.mode == "move";  // l'instance déplacée reprend la place de l'ancienne dans ShadowMountPlus
    }
    return o;
}

bool installed_path(const std::string& p) { return p.rfind("/user/", 0) == 0 || p.rfind("/system", 0) == 0; }

Plan make_plan(const Options& o, Info& src) {
    Plan p;
    auto fail = [&](const char* code, std::string msg) { p.ok = false; p.code = code; p.error = std::move(msg); return p; };
    std::string err;
    const bool need_size = o.op == Op::compress || o.op == Op::make_image || o.op == Op::extract_image || o.op == Op::transfer;
    if (!inspect(o.source, src, err, need_size)) return fail(fs::exists(H(o.source)) ? "unsupported" : "not_found", err);
    p.title_id = src.title_id;
    p.source_bytes = src.data_size;
    // Jamais toucher l'image d'un jeu en cours d'exécution ; une source montée seulement en lecture.
    const std::string run = running_title();
    if (run == "?" && writes_source(o)) return fail("running", "impossible d'identifier le jeu en cours : fermez le jeu puis réessayez");
    if (src.running && (writes_source(o) || o.op != Op::read_speed)) return fail("running", "le jeu " + src.title_id + " est en cours d'exécution : fermez-le d'abord");
    // Exceptions : la réparation compare à l'image montée ; le réglage lecture seule ne touche pas l'image ;
    // un déplacement réinscrit le jeu à son nouvel emplacement (comme Game Compressor) ; une compression
    // « déplacée » aussi.
    // Chaque façon d'exister a ses opérations : une installation native n'est pas une image.
    if (src.kind == Kind::native && o.op != Op::validate && o.op != Op::read_speed) {
        switch (o.op) {
            case Op::compress:
            case Op::make_image:
                return fail("native", "jeu installé nativement par la PS5 (app.pkg) : il faut d'abord l'extraire en dossier "
                                      "(dump avec un outil de dump), puis compresser ou créer l'image depuis ce dossier");
            case Op::transfer:
                return fail("native", "jeu installé nativement : sa copie ou son déplacement se fait par le menu Stockage de la PS5 "
                                      "(stockage étendu M.2 / USB) ; le moteur ne déplace que les images et dossiers ShadowMountPlus");
            default:
                return fail("native", "opération réservée aux images et dossiers montés par ShadowMountPlus, pas aux jeux installés nativement");
        }
    }
    if (src.kind == Kind::ffpkg && o.op != Op::validate && o.op != Op::read_speed && o.op != Op::transfer)
        return fail("unsupported", "image .ffpkg : copie, déplacement, contrôle de lecture et test de vitesse seulement "
                                   "(format non compressible ni extractible par le moteur)");
    if (o.op == Op::transfer && o.compress && src.kind == Kind::ffpkg)
        return fail("unsupported", "image .ffpkg : transfert sans compression seulement");
    const bool relinks = o.op == Op::transfer || (o.op == Op::compress && o.mount && o.delete_source && !o.destructive);
    if ((src.mounted || src.registered) && writes_source(o) && !(o.op == Op::validate && o.repair) && o.op != Op::set_read_only && !relinks)
        return fail("mounted", "la source est montée par ShadowMountPlus : opération en lecture seule uniquement");
    if ((o.delete_source || o.destructive || (o.op == Op::transfer && o.mode == "move")) && installed_path(o.source))
        return fail("unsupported", "jeu installé par le système : copie seulement, l'original reste en place");

    const std::string dir = default_dir(o);
    switch (o.op) {
        case Op::compress: {
            if (src.kind == Kind::ffpfsc) return fail("unsupported", "déjà compressé (.ffpfsc)");
            if (o.destructive && src.kind != Kind::folder) return fail("unsupported", "mode destructif : dossier seulement");
            const std::string name = src.kind == Kind::folder ? src.title_id : stem_of(o.source);
            p.output = o.output.empty() ? fs::join(dir, name + ".ffpfsc") : o.output;
            if (!ends_with_ci(p.output, ".ffpfsc")) return fail("bad_output", "la sortie doit finir par .ffpfsc");
            if (o.destructive && std::string(fs::dirname(p.output)) != std::string(fs::dirname(o.source)))
                return fail("unsupported", "mode destructif : seulement sur le même stockage, à côté de la source");
            // Pire cas (données incompressibles) : la taille de la source, plus les empreintes (32 o / 64 Kio).
            p.required_bytes = o.destructive ? std::min(src.data_size, destructive_min_free) : src.data_size + src.data_size / 2048 + (1u << 20);
            break;
        }
        case Op::make_image: {
            if (src.kind != Kind::folder) return fail("unsupported", "seul un dossier de jeu devient une image");
            const char* ext = o.inner == "pfs" ? ".ffpfs" : ".exfat";
            p.output = o.output.empty() ? fs::join(dir, src.title_id + ext) : o.output;
            if (!ends_with_ci(p.output, ext)) return fail("bad_output", std::string("la sortie doit finir par ") + ext);
            p.required_bytes = src.data_size + src.data_size / 50 + (64u << 20);  // tables du système de fichiers
            p.estimate = true;
            break;
        }
        case Op::uncompress: {
            if (src.kind != Kind::ffpfsc) return fail("unsupported", "seul un .ffpfsc se décompresse");
            const std::string stem = stem_of(o.source);
            if (o.to == "folder") p.output = o.output.empty() ? fs::join(dir, stem + "-app") : o.output;
            else p.output = o.output.empty() ? fs::join(dir, stem + (src.inner == "exfat" ? ".exfat" : ".ffpfs")) : o.output;
            p.required_bytes = src.data_size;  // image imbriquée : majorant du contenu
            p.estimate = o.to == "folder";
            break;
        }
        case Op::extract_image: {
            if (src.kind == Kind::ffpfsc) {  // logiciel, sans montage
                p.output = o.output.empty() ? fs::join(dir, stem_of(o.source) + "-app") : o.output;
                p.required_bytes = src.data_size;
                p.estimate = true;
                break;
            }
            if (src.kind != Kind::exfat && src.kind != Kind::ffpfs) return fail("unsupported", "seule une image s'extrait");
            if (!src.mounted || src.mount_path.empty()) return fail("not_mounted", "l'image doit être montée par ShadowMountPlus pour être extraite");
            p.output = o.output.empty() ? fs::join(dir, src.title_id + "-app") : o.output;
            p.required_bytes = tree_bytes(H(src.mount_path));
            break;
        }
        case Op::ampr_index:
            if (src.kind != Kind::folder) return fail("unsupported", "l'index AMPR se construit sur un dossier");
            p.output = fs::join(o.source, "ampr_emu.index");
            p.required_bytes = 64u << 20;
            p.estimate = true;
            break;
        case Op::set_read_only:
            if (src.kind != Kind::exfat && src.kind != Kind::ffpfs) return fail("unsupported", "réglage réservé aux images .exfat / .ffpfs");
            break;
        case Op::transfer: {
            p.output = o.output.empty() ? fs::join(dir, std::string(fs::basename(o.source))) : o.output;
            if (p.output == o.source || std::string(fs::dirname(p.output)) == std::string(fs::dirname(o.source)))
                return fail("same_place", "le jeu est déjà à cet emplacement");
            p.required_bytes = src.kind == Kind::folder ? src.data_size : src.size;
            fs::Entry vh;
            if (src.kind == Kind::ffpfsc && fs::stat(H(o.source + ".vhash"), vh)) p.required_bytes += vh.size;
            break;
        }
        case Op::validate:
        case Op::read_speed:
            break;
    }
    if (!p.output.empty() && p.output != fs::join(o.source, "ampr_emu.index") && fs::exists(H(p.output)))
        return fail("output_exists", "la sortie existe déjà : " + p.output);
    if (p.required_bytes) {
        const std::string where = p.output.empty() ? o.source : std::string(fs::dirname(p.output));
        if (!free_space(where, p.free_bytes)) return fail("no_space", "place libre inconnue sur " + where);
        if (p.free_bytes < p.required_bytes)
            return fail("no_space", "place insuffisante : il manque " + std::to_string((p.required_bytes - p.free_bytes) >> 20) + " Mio");
    }
    p.ok = true;
    return p;
}
}  // namespace

Plan plan(const Options& in) {
    Info src;
    return make_plan(normalize(in), src);
}

// --- exécution ---------------------------------------------------------------------------
namespace {
struct Ctx {
    const Options& o;
    Info& src;
    Plan& plan;
    std::atomic<bool>& interrupt;
    Result r;
};

Result fail_r(Result r, const char* code, std::string msg) { r.ok = false; r.code = code; r.error = std::move(msg); return r; }

void remove_output(const std::string& host_path) {
    fs::remove_tree(host_path);
    ::unlink((host_path + ".vhash").c_str());
}

// Suppression de la source, après vérification complète (mode sûr).
bool delete_source_after(Ctx& c, std::string& err) {
    if (running_title() == c.src.title_id && !c.src.title_id.empty()) { err = "le jeu a été lancé entre-temps : source conservée"; return false; }
    // Déplacement (o.mount) : l'instance montée est remplacée par la nouvelle, ShadowMountPlus est relancé dessus.
    if (!c.o.mount && mounted(c.o.source, c.src.kind, c.src.title_id)) { err = "la source est maintenant montée : conservée"; return false; }
    set_step("Suppression de la source (vérifiée)");
    if (!fs::remove_tree(H(c.o.source))) { err = "suppression de la source impossible"; return false; }
    ::unlink(H(c.o.source + ".vhash").c_str());
    return true;
}

void after_output(Ctx& c, Json& rep, bool ffpfsc, const std::string& nested, bool nested_exfat) {
    if (c.o.shadowmount_hints && !c.src.title_id.empty()) {
        std::string err;
        const bool ok = ffpfsc ? write_ffpfsc_hints(c.src.title_id, c.r.output, nested, nested_exfat, err) : write_image_hints(c.src.title_id, c.r.output, err);
        rep.set("shadowmount_hints", ok ? Json(true) : Json(err));
    }
    if (c.o.mount && !c.src.title_id.empty()) {
        std::string err;
        rep.set("mount_requested", request_mount(c.src.title_id, c.r.output, err) ? Json(true) : Json(err));
        report_smp(rep);
    }
}

Result run_compress(Ctx& c) {
    Result& r = c.r;
    const std::string out_h = H(c.plan.output);
    pbc_gc_zlib_level = c.o.level;
    set_step(c.src.kind == Kind::folder ? "Analyse du dossier" : "Ouverture de l'image");
    pfs_compress_plan_t* gp = nullptr;
    pfs_app_info_t info{};
    char err[256] = {0};
    pfs_stream_options_t stream{PFS_STREAM_DEFAULT_BUDGET_BYTES, 0, PFS_STREAM_ORDER_BUDGETED_GAIN};
    const int policy = c.o.destructive ? PFS_DELETE_STREAM : PFS_DELETE_KEEP;
    const int format = c.o.inner == "pfs" ? PFS_COMPRESS_FORMAT_PFS : PFS_COMPRESS_FORMAT_EXFAT;
    int rc = pfs_compress_prepare_source_to_ffpfsc_opts_output_ex(H(c.o.source).c_str(), 0, format, policy, 0, out_h.c_str(),
                                                                  c.o.destructive ? &stream : nullptr, &gp, &info, err, sizeof err);
    if (rc != 0) return fail_r(r, rc == -2 ? "output_exists" : cancelled(c.interrupt) ? "cancelled" : "failed", std::string("préparation : ") + err);
    set_step(c.o.destructive ? "Compression (suppression pendant l'écriture)" : "Compression");
    rc = pfs_compress_execute_prepared_to_ffpfsc(gp, PFS_COMPRESS_DEFAULT_WORKERS, &info, err, sizeof err);
    pfs_compress_plan_free(gp);
    if (rc != 0) {
        if (!c.o.destructive) remove_output(out_h);
        return fail_r(r, cancelled(c.interrupt) ? "cancelled" : "failed", std::string("compression : ") + err);
    }
    r.output = C(info.output_path);
    pbc_gc_status_t s;
    pbc_gc_job_status(&s);
    Json rep = Json::object();
    rep.set("source", c.o.source);
    rep.set("output", r.output);
    rep.set("source_bytes", c.src.data_size);
    fs::Entry e;
    const std::uint64_t outsz = fs::stat(out_h, e) ? e.size : 0;
    rep.set("output_bytes", outsz);
    if (c.src.data_size) rep.set("ratio", static_cast<double>(outsz) / static_cast<double>(c.src.data_size));
    rep.set("saved_bytes", c.src.data_size > outsz ? c.src.data_size - outsz : std::uint64_t{0});
    rep.set("inner", c.o.inner);
    rep.set("inner_name", info.nested_name);
    rep.set("level", c.o.level);
    rep.set("compressed_blocks", s.compressed_blocks);
    rep.set("raw_blocks", s.raw_blocks);
    rep.set("apr_indexed", info.apr_indexed != 0);
    rep.set("destructive", c.o.destructive);

    // Vérification complète avant de garder le résultat (et avant toute suppression).
    set_step("Vérification du .ffpfsc");
    VerifyStats vs;
    std::string verr;
    const bool vok = verify_ffpfsc(out_h, vs, c.interrupt, verr);
    Json v = Json::object();
    v.set("blocks", vs.blocks);
    v.set("checked", vs.checked);
    v.set("bad_blocks", vs.bad);
    v.set("hash_mode", vs.hash_mode);
    rep.set("verification", v);
    if (!vok || vs.bad) {
        if (c.o.destructive) {  // la source est déjà partie : on garde tout ce qui existe
            r.report = rep;
            return fail_r(r, "verify_failed", "vérification échouée après compression destructive : " + (vok ? std::to_string(vs.bad) + " bloc(s) faux" : verr));
        }
        remove_output(out_h);
        r.report = rep;
        if (!vok && cancelled(c.interrupt)) return fail_r(r, "cancelled", "annulé");
        return fail_r(r, "verify_failed", "vérification échouée, résultat supprimé, source intacte : " + (vok ? std::to_string(vs.bad) + " bloc(s) faux" : verr));
    }
    Json vr = Json::object();
    vr.set("state", "ok");
    vr.set("method", "software");
    vr.set("blocks", vs.blocks);
    vr.set("bad_blocks", std::uint64_t{0});
    remember_validation(r.output, vr);
    if (c.o.delete_source && !c.o.destructive) {
        std::string derr;
        rep.set("source_deleted", delete_source_after(c, derr));
        if (!derr.empty()) rep.set("source_kept_reason", derr);
    } else rep.set("source_deleted", c.o.destructive);
    after_output(c, rep, true, info.nested_name, info.nested_type == PFS_NESTED_EXFAT);
    r.report = rep;
    r.ok = true;
    return r;
}

Result run_make_image(Ctx& c) {
    Result& r = c.r;
    const std::string out_h = H(c.plan.output);
    set_step("Analyse du dossier");
    pfs_compress_plan_t* gp = nullptr;
    pfs_app_info_t info{};
    char err[256] = {0};
    const int format = c.o.inner == "pfs" ? PFS_COMPRESS_FORMAT_PFS : PFS_COMPRESS_FORMAT_EXFAT;
    int rc = pfs_make_image_prepare_source_opts_output_ex(H(c.o.source).c_str(), 0, format, out_h.c_str(), &gp, &info, err, sizeof err);
    if (rc != 0) return fail_r(r, rc == -2 ? "output_exists" : cancelled(c.interrupt) ? "cancelled" : "failed", std::string("préparation : ") + err);
    // Place exacte connue maintenant (taille de l'image).
    std::uint64_t free_b = 0;
    if (free_space(std::string(fs::dirname(c.plan.output)), free_b) && free_b < info.nested_size) {
        pfs_compress_plan_free(gp);
        return fail_r(r, "no_space", "place insuffisante : il manque " + std::to_string((info.nested_size - free_b) >> 20) + " Mio");
    }
    set_step(c.o.inner == "pfs" ? "Création de l'image PFS" : "Création de l'image exFAT");
    rc = pfs_make_image_execute_prepared(gp, &info, err, sizeof err);
    pfs_compress_plan_free(gp);
    if (rc != 0) { ::unlink(out_h.c_str()); return fail_r(r, cancelled(c.interrupt) ? "cancelled" : "failed", std::string("création : ") + err); }
    r.output = C(info.output_path);
    set_step("Vérification de l'image");
    pfs_app_info_t probe{};
    if (pfs_image_probe(info.output_path, &probe, err, sizeof err) != 0 || probe.title_id != c.src.title_id) {
        ::unlink(info.output_path);
        return fail_r(r, "verify_failed", std::string("image produite invalide, supprimée : ") + err);
    }
    Json rep = Json::object();
    rep.set("source", c.o.source);
    rep.set("output", r.output);
    rep.set("source_bytes", c.src.data_size);
    rep.set("output_bytes", info.nested_size);
    rep.set("inner", c.o.inner);
    if (c.o.delete_source) {
        std::string derr;
        rep.set("source_deleted", delete_source_after(c, derr));
        if (!derr.empty()) rep.set("source_kept_reason", derr);
    }
    after_output(c, rep, false, "", false);
    r.report = rep;
    r.ok = true;
    return r;
}

Result run_uncompress(Ctx& c, bool to_folder) {
    Result& r = c.r;
    const std::string out_h = H(c.plan.output);
    pfs_decompress_info_t info{};
    char err[256] = {0};
    set_step(to_folder ? "Décompression vers un dossier" : "Décompression vers une image");
    const int rc = to_folder
        ? pfs_decompress_ffpfsc_to_app_opts_output(H(c.o.source).c_str(), 0, PFS_DECOMPRESS_DEFAULT_WORKERS, PFS_DELETE_KEEP, out_h.c_str(), &info, err, sizeof err)
        : pfs_decompress_ffpfsc_to_image_opts_output(H(c.o.source).c_str(), 0, PFS_DECOMPRESS_DEFAULT_WORKERS, PFS_DELETE_KEEP, out_h.c_str(), &info, err, sizeof err);
    if (rc != 0) return fail_r(r, rc == -2 ? "output_exists" : cancelled(c.interrupt) ? "cancelled" : "failed", std::string("décompression : ") + err);
    r.output = C(info.output_path);
    Json rep = Json::object();
    rep.set("source", c.o.source);
    rep.set("output", r.output);
    rep.set("to", to_folder ? "folder" : "exfat");
    set_step("Vérification du résultat");
    if (to_folder) {
        pfs_app_info_t probe{};
        if (pfs_app_probe(info.output_path, &probe, err, sizeof err) != 0 || (!c.src.title_id.empty() && probe.title_id != c.src.title_id)) {
            fs::remove_tree(info.output_path);
            return fail_r(r, "verify_failed", std::string("dossier produit invalide, supprimé : ") + err);
        }
        rep.set("output_bytes", tree_bytes(info.output_path));
        rep.set("verification", "structure (param.json, eboot.bin) ; chaque bloc décodé sans erreur");
    } else {
        pfs_app_info_t probe{};
        std::string verdict, verr;
        if (pfs_image_probe(info.output_path, &probe, err, sizeof err) != 0) {
            ::unlink(info.output_path);
            return fail_r(r, "verify_failed", std::string("image produite invalide, supprimée : ") + err);
        }
        if (!verify_image_against(info.output_path, H(c.o.source), c.interrupt, verdict, verr)) {
            ::unlink(info.output_path);
            return fail_r(r, cancelled(c.interrupt) ? "cancelled" : "verify_failed", "image produite incorrecte, supprimée : " + verr);
        }
        rep.set("output_bytes", info.nested_size);
        rep.set("verification", verdict);
    }
    if (c.o.delete_source) {
        std::string derr;
        const bool del = delete_source_after(c, derr);
        rep.set("source_deleted", del);
        if (!derr.empty()) rep.set("source_kept_reason", derr);
        if (del && c.o.shadowmount_hints) {  // réglages du conteneur supprimé (gc_shadowmount_remove_pfsc_hints)
            std::string herr;
            remove_name_hints(std::string(fs::basename(c.o.source)), herr);
        }
    }
    if (!to_folder) after_output(c, rep, false, "", false);
    else if (c.o.mount && !c.src.title_id.empty()) {
        std::string merr;
        rep.set("mount_requested", request_mount(c.src.title_id, r.output, merr) ? Json(true) : Json(merr));
    }
    r.report = rep;
    r.ok = true;
    return r;
}

Result run_extract(Ctx& c) {
    if (c.src.kind == Kind::ffpfsc) return run_uncompress(c, true);
    Result& r = c.r;
    const std::string out_h = H(c.plan.output);
    const std::string tmp = fs::join(std::string(fs::dirname(out_h)), ".pbc-extracting-" + std::string(fs::basename(out_h)));
    fs::remove_tree(tmp);
    set_step("Extraction de l'image montée");
    std::uint64_t copied = 0;
    std::string err;
    if (!copy_tree(H(c.src.mount_path), tmp, copied, c.plan.required_bytes, c.interrupt, err)) {
        fs::remove_tree(tmp);
        return fail_r(r, cancelled(c.interrupt) ? "cancelled" : "failed", "extraction : " + err);
    }
    if (::rename(tmp.c_str(), out_h.c_str()) != 0) { fs::remove_tree(tmp); return fail_r(r, "failed", "renommage final impossible"); }
    set_step("Vérification du dossier");
    pfs_app_info_t probe{};
    char e[256] = {0};
    if (pfs_app_probe(out_h.c_str(), &probe, e, sizeof e) != 0 || probe.title_id != c.src.title_id) {
        fs::remove_tree(out_h);
        return fail_r(r, "verify_failed", std::string("dossier extrait invalide, supprimé : ") + e);
    }
    r.output = c.plan.output;
    Json rep = Json::object();
    rep.set("source", c.o.source);
    rep.set("output", r.output);
    rep.set("output_bytes", copied);
    r.report = rep;
    r.ok = true;
    return r;
}

Result run_validate(Ctx& c) {
    Result& r = c.r;
    Json rep = Json::object();
    rep.set("source", c.o.source);
    rep.set("kind", to_string(c.src.kind));
    Json mem = Json::object();
    std::string err;
    bool good = true;
    if (c.src.kind == Kind::ffpfsc) {
        set_step("Vérification logicielle des blocs");
        VerifyStats vs;
        if (!verify_ffpfsc(H(c.o.source), vs, c.interrupt, err)) return fail_r(r, cancelled(c.interrupt) ? "cancelled" : "failed", err);
        Json sw = Json::object();
        sw.set("blocks", vs.blocks);
        sw.set("checked", vs.checked);
        sw.set("hashed", vs.hashed);
        sw.set("bad_blocks", vs.bad);
        sw.set("hash_mode", vs.hash_mode);
        Json fb = Json::array();
        for (auto b : vs.first_bad) fb.push(b);
        sw.set("first_bad", fb);
        rep.set("software", sw);
        good = vs.bad == 0;
        mem.set("method", "software");
        mem.set("bad_blocks", vs.bad);
        // Contrôle « matériel » de Game Compressor : compare le décodage de la console (image montée)
        // au décodage logiciel ; la réparation remplace les blocs fautifs par des blocs bruts.
        if (c.src.mounted) {
            set_step(c.o.repair ? "Validation et réparation (image montée)" : "Validation (image montée)");
            pfs_repair_info_t ri{};
            char e[256] = {0};
            const int rc = c.o.repair ? pfs_repair_ffpfsc_auto(H(c.o.source).c_str(), &ri, e, sizeof e)
                                      : pfs_repair_ffpfsc_scan_only(H(c.o.source).c_str(), &ri, e, sizeof e);
            Json hw = Json::object();
            hw.set("ok", rc == 0);
            if (rc != 0) hw.set("error", e);
            hw.set("scanned_blocks", ri.scanned_blocks);
            hw.set("matched_blocks", ri.matched_blocks);
            hw.set("bad_blocks", ri.repaired_blocks);
            hw.set("hash_mode", ri.hash_mode);
            if (c.o.repair) { hw.set("repaired", rc == 0 && ri.repaired_blocks > 0); hw.set("noop", ri.noop != 0); }
            rep.set("mounted_check", hw);
            if (rc != 0 && cancelled(c.interrupt)) return fail_r(r, "cancelled", "annulé");
            if (rc != 0 || (!c.o.repair && ri.repaired_blocks > 0)) good = false;
            mem.set("method", "software+mounted");
            mem.set("mounted_bad_blocks", ri.repaired_blocks);
            if (c.o.repair && rc == 0) mem.set("repaired_blocks", ri.repaired_blocks);
        } else {
            rep.set("mounted_check", "image non montée : contrôle par la console non fait (montez-la avec ShadowMountPlus)");
            if (c.o.repair) rep.set("repair", "réparation impossible sans montage : elle compare au décodage de la console");
        }
    } else {
        // Image .exfat / .ffpfs ou dossier : structure, puis lecture complète (blocs illisibles).
        char e[256] = {0};
        pfs_app_info_t probe{};
        set_step("Contrôle de la structure");
        const bool tree = c.src.kind == Kind::folder || c.src.kind == Kind::native;
        if (c.src.kind == Kind::folder || c.src.kind == Kind::exfat || c.src.kind == Kind::ffpfs) {
            const int prc = c.src.kind == Kind::folder ? pfs_app_probe(H(c.o.source).c_str(), &probe, e, sizeof e) : pfs_image_probe(H(c.o.source).c_str(), &probe, e, sizeof e);
            rep.set("structure", prc == 0 ? Json("ok") : Json(e));
            good = prc == 0;
        } else rep.set("structure", "non contrôlée (format du système ou .ffpkg) : lecture seule");
        set_step("Lecture complète (blocs illisibles)");
        ReadCheck rc;
        const std::uint64_t total = tree ? tree_bytes(H(c.o.source)) : c.src.size;
        const bool done = tree ? read_check_tree(H(c.o.source), rc, total, c.interrupt) : read_check_file(H(c.o.source), rc, total, c.interrupt);
        if (!done) return fail_r(r, "cancelled", "annulé");
        rep.set("read_bytes", rc.bytes);
        rep.set("files", rc.files);
        rep.set("unreadable_blocks", rc.bad_blocks);
        Json bf = Json::array();
        for (const auto& f : rc.bad_files) bf.push(f);
        rep.set("unreadable_files", bf);
        if (rc.bad_blocks) good = false;
        mem.set("method", "read");
        mem.set("bad_blocks", rc.bad_blocks);
    }
    mem.set("state", good ? "ok" : "bad");
    remember_validation(c.o.source, mem);
    rep.set("state", good ? "ok" : "bad");
    r.report = rep;
    r.output = c.o.source;
    if (!good) return fail_r(r, "invalid", "image défectueuse (voir le rapport)");
    r.ok = true;
    return r;
}

Result run_read_speed(Ctx& c) {
    Result& r = c.r;
    // Ce que lit le jeu : le contenu monté (décompressé par la console) si possible, sinon la source.
    std::string root = c.src.mounted && !c.src.mount_path.empty() ? c.src.mount_path : c.o.source;
    if (c.src.kind == Kind::ffpfsc && root == c.o.source)
        return fail_r(r, "not_mounted", "un .ffpfsc doit être monté pour mesurer la lecture du jeu");
    set_step("Test de vitesse de lecture");
    const auto t0 = std::chrono::steady_clock::now();
    const auto deadline = t0 + std::chrono::seconds(c.o.seconds);
    std::uint64_t bytes = 0, files = 0;
    std::vector<unsigned char> buf(4u << 20);
    std::function<bool(const std::string&, int)> walk = [&](const std::string& h, int depth) -> bool {
        if (depth > 32) return true;
        struct ::stat st {};
        if (::stat(h.c_str(), &st) != 0) return true;
        if (!S_ISDIR(st.st_mode)) {
            const int fd = ::open(h.c_str(), O_RDONLY);
            if (fd < 0) return true;
            ++files;
            for (;;) {
                if (cancelled(c.interrupt) || std::chrono::steady_clock::now() >= deadline) { ::close(fd); return false; }
                const ssize_t n = ::read(fd, buf.data(), buf.size());
                if (n <= 0) break;
                bytes += static_cast<std::uint64_t>(n);
                const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                pbc_gc_job_set_progress(static_cast<std::uint64_t>(el * 1000), static_cast<std::uint64_t>(c.o.seconds) * 1000);
            }
            ::close(fd);
            return true;
        }
        std::vector<fs::Entry> entries;
        if (!fs::list(h, entries)) return true;
        for (const auto& e : entries) if (!walk(fs::join(h, e.name), depth + 1)) return false;
        return true;
    };
    // Une seule lecture complète au plus : relire les mêmes fichiers mesurerait le cache, pas le disque
    // (un petit jeu de 10 Mo annonçait 4,7 Go/s). Petit jeu → mesure courte, signalée comme peu fiable.
    const bool whole = walk(H(root), 0);
    if (cancelled(c.interrupt)) return fail_r(r, "cancelled", "annulé");
    if (!bytes) return fail_r(r, "failed", "aucune donnée lisible");
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    Json rep = Json::object();
    rep.set("source", c.o.source);
    rep.set("read_root", root);
    rep.set("complete_pass", whole);
    if (whole && (bytes < (256ull << 20) || secs < 1.0)) rep.set("note", "jeu trop petit : mesure peu fiable (lecture complète en moins d'une seconde ou moins de 256 Mo)");
    rep.set("bytes", bytes);
    rep.set("files", files);
    rep.set("seconds", secs);
    rep.set("bytes_per_second", secs > 0 ? static_cast<double>(bytes) / secs : 0.0);
    r.report = rep;
    r.ok = true;
    return r;
}

Result run_ampr(Ctx& c) {
    Result& r = c.r;
    set_step("Construction de l'index AMPR (APR-EMU)");
    pfs_app_info_t info{};
    char err[256] = {0};
    if (pfs_build_ampr_index_for_folder(H(c.o.source).c_str(), &info, err, sizeof err) != 0)
        return fail_r(r, cancelled(c.interrupt) ? "cancelled" : "failed", std::string("index AMPR : ") + err);
    r.output = fs::join(c.o.source, "ampr_emu.index");
    Json rep = Json::object();
    rep.set("source", c.o.source);
    rep.set("output", r.output);
    rep.set("apr_indexed", info.apr_indexed != 0);
    rep.set("files", info.scan_files);
    r.report = rep;
    r.ok = true;
    return r;
}

Result run_set_read_only(Ctx& c) {
    Result& r = c.r;
    set_step("Réglage ShadowMountPlus : lecture seule");
    const std::string name(fs::basename(c.o.source));
    std::string text, err;
    fs::read(H(shadow_config), text, 4u << 20);
    bool present = false;
    std::size_t pos = 0;
    while (pos < text.size() && !present) {
        auto e = text.find('\n', pos);
        present = line_is_mode(text.substr(pos, e == std::string::npos ? std::string::npos : e - pos), name, true);
        pos = e == std::string::npos ? text.size() : e + 1;
    }
    Json rep = Json::object();
    rep.set("source", c.o.source);
    rep.set("already_read_only", present);
    if (!present && !rewrite_settings(shadow_config, [&](const std::string& l) { return line_is_mode(l, name); }, "image_ro=" + name, err))
        return fail_r(r, "failed", err);
    if (!present && c.o.mount && !c.src.title_id.empty())
        rep.set("mount_requested", request_mount(c.src.title_id, c.o.source, err) ? Json(true) : Json(err));
    r.output = c.o.source;
    r.report = rep;
    r.ok = true;
    return r;
}

// --- copie / déplacement entre stockages (d'après run_move_op de gc_api.c) ----------------
bool hash_file(const std::string& host_file, std::string& hex, std::atomic<bool>& interrupt, std::uint64_t& done, std::uint64_t total) {
    const int fd = ::open(host_file.c_str(), O_RDONLY);
    if (fd < 0) return false;
    Sha256 h;
    std::vector<unsigned char> buf(4u << 20);
    for (;;) {
        if (cancelled(interrupt)) { ::close(fd); return false; }
        const ssize_t n = ::read(fd, buf.data(), buf.size());
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ::close(fd); return false; }
        if (n == 0) break;
        h.add(buf.data(), static_cast<std::size_t>(n));
        done += static_cast<std::uint64_t>(n);
        pbc_gc_job_set_progress(done, total);
    }
    ::close(fd);
    hex = h.hex();
    return true;
}
// Relit source et copie : même liste de fichiers, mêmes empreintes SHA-256.
bool same_tree(const std::string& a, const std::string& b, std::atomic<bool>& interrupt, std::uint64_t& done, std::uint64_t total,
               std::uint64_t& files, std::string& err, int depth = 0) {
    struct ::stat sa {}, sb {};
    if (::stat(a.c_str(), &sa) != 0 || ::stat(b.c_str(), &sb) != 0) { err = "fichier manquant : " + C(b); return false; }
    if (S_ISDIR(sa.st_mode) != S_ISDIR(sb.st_mode)) { err = "nature différente : " + C(b); return false; }
    if (!S_ISDIR(sa.st_mode)) {
        if (sa.st_size != sb.st_size) { err = "taille différente : " + C(b); return false; }
        std::string ha, hb;
        if (!hash_file(a, ha, interrupt, done, total) || !hash_file(b, hb, interrupt, done, total)) { err = cancelled(interrupt) ? "annulé" : "relecture impossible : " + C(b); return false; }
        if (ha != hb) { err = "contenu différent : " + C(b); return false; }
        ++files;
        return true;
    }
    if (depth > 32) { err = "arborescence trop profonde"; return false; }
    std::vector<fs::Entry> ea, eb;
    if (!fs::list(a, ea) || !fs::list(b, eb) || ea.size() != eb.size()) { err = "contenu de dossier différent : " + C(b); return false; }
    for (const auto& e : ea)
        if (!same_tree(fs::join(a, e.name), fs::join(b, e.name), interrupt, done, total, files, err, depth + 1)) return false;
    return true;
}
bool copy_one_file(const std::string& src, const std::string& dst, std::uint64_t& copied, std::uint64_t total, std::atomic<bool>& interrupt, std::string& err) {
    // Réutilise copy_tree sur un dossier temporaire d'un seul fichier serait détourné : copie directe.
    const int in = ::open(src.c_str(), O_RDONLY);
    if (in < 0) { err = "lecture impossible : " + C(src); return false; }
    const int out = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) { ::close(in); err = "écriture impossible : " + C(dst); return false; }
    {
        struct ::stat sm {};
        if (::fstat(in, &sm) == 0) ::fchmod(out, sm.st_mode & 07777);
    }
    std::vector<unsigned char> buf(4u << 20);
    bool ok = true;
    for (;;) {
        if (cancelled(interrupt)) { err = "annulé"; ok = false; break; }
        const ssize_t r = ::read(in, buf.data(), buf.size());
        if (r == 0) break;
        if (r < 0) { if (errno == EINTR) continue; err = "lecture impossible : " + C(src); ok = false; break; }
        const unsigned char* p = buf.data();
        std::size_t left = static_cast<std::size_t>(r);
        while (ok && left) {
            const ssize_t w = ::write(out, p, left);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) { err = "écriture impossible : " + C(dst); ok = false; break; }
            p += w; left -= static_cast<std::size_t>(w);
        }
        if (!ok) break;
        copied += static_cast<std::uint64_t>(r);
        pbc_gc_job_set_progress(copied, total);
    }
    if (ok && ::fsync(out) != 0) { err = "écriture impossible : " + C(dst); ok = false; }
    ::close(in);
    ::close(out);
    return ok;
}
// Inscription ShadowMountPlus (/user/app/<ID>/mount.lnk ou mount_img.lnk) déplacée vers le nouveau chemin.
bool relink(const std::string& title_id, const std::string& from, const std::string& to, std::string& which) {
    if (title_id.empty()) return false;
    for (const char* name : {"mount_img.lnk", "mount.lnk"}) {
        const std::string f = fs::join("/user/app/" + title_id, name);
        std::string cur;
        if (read_link(f, cur) && cur == from) {
            which = name;
            const bool ok = fs::write_atomic(H(f), to + "\n");
            ::chmod(H(f).c_str(), 0666);
            return ok;
        }
    }
    return false;
}
// Demande de rescan sans changer la liste (copie : comme gc_shadowmount_request_scan).
void touch_scan() {
    std::string err;
    rewrite_settings(shadow_manual, [](const std::string&) { return false; }, "", err);
    const std::string h = H(shadow_manual);
    if (!fs::exists(h)) fs::write_atomic(h, "");
    struct timeval tv[2];
    ::gettimeofday(&tv[0], nullptr);
    struct ::stat st {};
    if (::stat(h.c_str(), &st) == 0 && tv[0].tv_sec <= st.st_mtime) { tv[0].tv_sec = st.st_mtime + 1; tv[0].tv_usec = 0; }
    tv[1] = tv[0];
    ::utimes(h.c_str(), tv);
    ::chmod(h.c_str(), 0666);
}

Result run_transfer(Ctx& c) {
    Result& r = c.r;
    const bool move = c.o.mode == "move";
    const std::string out_h = H(c.plan.output);
    const std::string tmp = fs::join(std::string(fs::dirname(out_h)), std::string(move ? ".pbc-moving-" : ".pbc-copying-") + std::string(fs::basename(out_h)));
    if (!fs::mkdirs(std::string(fs::dirname(out_h)))) return fail_r(r, "failed", "dossier de destination impossible à créer");
    fs::remove_tree(tmp);
    set_step(move ? "Déplacement : copie" : "Copie");
    std::uint64_t copied = 0;
    std::string err;
    const bool dir = c.src.kind == Kind::folder;
    const std::uint64_t total = c.plan.required_bytes;
    const bool ok = dir ? copy_tree(H(c.o.source), tmp, copied, total, c.interrupt, err) : copy_one_file(H(c.o.source), tmp, copied, total, c.interrupt, err);
    if (!ok) { fs::remove_tree(tmp); return fail_r(r, cancelled(c.interrupt) ? "cancelled" : "failed", "copie : " + err); }
    // Empreintes de validation d'un .ffpfsc : elles suivent le conteneur.
    const bool vhash = c.src.kind == Kind::ffpfsc && fs::exists(H(c.o.source + ".vhash"));
    if (vhash && !copy_one_file(H(c.o.source + ".vhash"), tmp + ".vhash", copied, total, c.interrupt, err)) {
        fs::remove_tree(tmp); ::unlink((tmp + ".vhash").c_str());
        return fail_r(r, cancelled(c.interrupt) ? "cancelled" : "failed", "copie : " + err);
    }
    Json rep = Json::object();
    rep.set("mode", c.o.mode);
    rep.set("source", c.o.source);
    rep.set("bytes", copied);
    if (c.o.verify || move) {
        set_step("Vérification (SHA-256 de chaque fichier)");
        std::uint64_t done = 0, files = 0;
        if (!same_tree(H(c.o.source), tmp, c.interrupt, done, copied * 2, files, err)) {
            fs::remove_tree(tmp); ::unlink((tmp + ".vhash").c_str());
            return fail_r(r, cancelled(c.interrupt) ? "cancelled" : "verify_failed", "vérification échouée, copie supprimée, original intact : " + err);
        }
        rep.set("verified_files", files);
    }
    rep.set("verified", c.o.verify || move);
    rep.set("copy_verified", c.o.verify || move);  // contenu (SHA-256) — ne dit rien des droits
    if (dir && perms::is_app_folder(tmp)) {
        // Paquet d'appli ou de jeu : exécutables et modules exécutables, dossiers traversables, RELUS avant de finir.
        set_step("Droits des exécutables");
        const perms::Report pr = perms::apply_app(tmp);
        rep.set("perms_checked", pr.exec_files);
        rep.set("perms_fixed", pr.fixed);
        rep.set("perms_verified", pr.ok());
        if (!pr.ok()) {
            r.report = rep;
            return fail_r(r, "perms_failed", "copie vérifiée, mais droits d'exécution impossibles à appliquer : " + pr.summary() + " — l'appli ne pourrait pas démarrer");
        }
    }
    rep.set("launch_validated", false);  // seul un vrai lancement le dira
    if (::rename(tmp.c_str(), out_h.c_str()) != 0) { fs::remove_tree(tmp); ::unlink((tmp + ".vhash").c_str()); return fail_r(r, "failed", "renommage final impossible"); }
    if (vhash) ::rename((tmp + ".vhash").c_str(), (out_h + ".vhash").c_str());
    r.output = c.plan.output;
    rep.set("output", r.output);
    {   // validation connue : suit le fichier
        const Json v = known_validation(c.o.source);
        if (v.is_object() && v["state"].as_string() == "ok") remember_validation(r.output, v);
    }
    if (!move) {
        // Copie à installer (autre console, copie de backup) : réglages ShadowMountPlus puis demande de montage.
        if (c.o.mount && !c.src.title_id.empty() && fs::is_dir(H(r.output))) {  // dossier : inscription seule, pas de réglages d'image
            std::string merr;
            rep.set("mount_requested", request_mount(c.src.title_id, r.output, merr) ? Json(true) : Json(merr));
            report_smp(rep);
        } else if (c.o.mount && !c.src.title_id.empty()) {
            std::string nested;
            bool nested_exfat = false;
            const bool ffpfsc = kind_of(r.output) == Kind::ffpfsc;
            if (ffpfsc) { Pfsc img; std::string e2; if (open_pfsc(out_h, img, e2)) { nested = img.nested_name; nested_exfat = img.nested_type == PFS_NESTED_EXFAT; } }
            after_output(c, rep, ffpfsc, nested, nested_exfat);
        }
        touch_scan();
        rep.set("source_deleted", false);
        r.report = rep;
        r.ok = true;
        return r;
    }
    // Déplacement : l'original ne part qu'après la vérification, et jamais si le jeu vient d'être lancé.
    const std::string run = running_title();
    if (run == "?" || (!c.src.title_id.empty() && run == c.src.title_id)) {
        rep.set("source_deleted", false);
        rep.set("source_kept_reason", "jeu lancé pendant le transfert : original conservé");
        r.report = rep;
        r.ok = true;
        return r;
    }
    set_step("Déplacement : suppression de l'original (vérifié)");
    const bool del = fs::remove_tree(H(c.o.source));
    if (del) ::unlink(H(c.o.source + ".vhash").c_str());
    rep.set("source_deleted", del);
    std::string which;
    const bool relinked = relink(c.src.title_id, c.o.source, c.plan.output, which);
    if (relinked) rep.set("relinked", which);
    // Inscription ShadowMountPlus seulement si on a déplacé LE jeu inscrit (ou si on l'a demandé) : déplacer une
    // simple copie de travail ne doit jamais remplacer l'inscription du vrai jeu (même Title ID).
    if (!c.src.title_id.empty() && (relinked || c.o.mount)) {
        std::string merr;
        rep.set("mount_requested", request_mount(c.src.title_id, c.plan.output, merr) ? Json(true) : Json(merr));
        report_smp(rep);
    }
    r.report = rep;
    r.ok = true;
    return r;
}
}  // namespace

Result run(const Options& in, const std::function<void(const Progress&)>& progress, std::atomic<bool>& interrupt) {
    std::lock_guard<std::mutex> once(g_run_mu);
    const Options o = normalize(in);
    Info src;
    Plan p = make_plan(o, src);
    Result r;
    if (!p.ok) {
        r.code = p.code;
        r.error = p.error;
        r.report = p.to_json();
        // Mise à jour refusée avant de commencer : l'ancienne version reprend sa place.
        const std::string back = p.output.empty() ? o.output : p.output;
        if (!o.previous.empty() && fs::exists(H(o.previous)) && !back.empty() && !fs::exists(H(back))) ::rename(H(o.previous).c_str(), H(back).c_str());
        return r;
    }
    if (!p.output.empty() && o.op != Op::ampr_index && !fs::mkdirs(H(std::string(fs::dirname(p.output))))) {
        r.code = "failed";
        r.error = "dossier de destination impossible à créer : " + std::string(fs::dirname(p.output));
        return r;
    }
    pbc_gc_set_logger(gc_logger);
    pbc_gc_job_begin(to_string(o.op));
    set_step("Préparation");
    log("images start op=%s source=%s output=%s level=%d inner=%s delete=%d destructive=%d", to_string(o.op), o.source.c_str(),
        p.output.c_str(), o.level, o.inner.c_str(), o.delete_source ? 1 : 0, o.destructive ? 1 : 0);
    Ctx c{o, src, p, interrupt, Result{}};
    {
        Monitor m(progress, interrupt);
        switch (o.op) {
            case Op::compress: r = run_compress(c); break;
            case Op::make_image: r = run_make_image(c); break;
            case Op::uncompress: r = run_uncompress(c, o.to == "folder"); break;
            case Op::extract_image: r = run_extract(c); break;
            case Op::validate: r = run_validate(c); break;
            case Op::read_speed: r = run_read_speed(c); break;
            case Op::ampr_index: r = run_ampr(c); break;
            case Op::set_read_only: r = run_set_read_only(c); break;
            case Op::transfer: r = run_transfer(c); break;
        }
        set_step(r.ok ? "Terminé" : "Arrêté");
        m.stop();
    }
    pbc_gc_job_end();
    if (r.report.is_null()) r.report = Json::object();
    if (!o.previous.empty() && fs::exists(H(o.previous))) {
        // Mise à jour d'une appli : l'ancienne version n'est jamais gardée en double ; elle part
        // seulement une fois la nouvelle copiée, vérifiée et ses droits relus — sinon elle reprend sa place.
        if (r.ok) { fs::remove_tree(H(o.previous)); r.report.set("previous_removed", true); }
        else if (!fs::exists(H(p.output)) && ::rename(H(o.previous).c_str(), H(p.output).c_str()) == 0) r.report.set("previous_restored", true);
    }
    r.report.set("op", to_string(o.op));
    if (in.op == Op::transfer) { r.report.set("transfer", in.mode); r.report.set("compressed", o.op == Op::compress); }
    log("images end op=%s ok=%d code=%s error=%s output=%s", to_string(o.op), r.ok ? 1 : 0, r.code.c_str(), r.error.c_str(), r.output.c_str());
    return r;
}

}  // namespace pbc::images
