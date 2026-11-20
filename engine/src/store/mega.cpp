// MEGA : même interface que les autres appareils. Réglages : FtpConfig — user = e-mail du compte MEGA, password = son
// mot de passe (gardé comme les clés S3, jamais affiché), root = dossier de départ. Pas de connexion par code chez
// MEGA : la console calcule elle-même les clés (PBKDF2), et chaque fichier est chiffré ici avant l'envoi (AES-CTR,
// MAC par morceaux), comme le fait le site de MEGA. MEGA ne voit jamais le contenu.
#include "store/drives.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <map>
#include <mutex>
#include <unistd.h>

#include "core/http_client.hpp"
#include "core/util.hpp"
#include "store/mega_crypto.hpp"
#include "store/kept.hpp"

extern "C" {
#include "bearssl.h"
}

namespace pbc::store {

namespace {
using namespace mega;

const char* const kApi = "https://g.api.mega.co.nz/cs";

// Aléa des clés de fichiers : /dev/urandom, mélangé dans un générateur HMAC-DRBG (avec l'horloge en plus).
Bytes random_bytes(std::size_t n) {
    static std::mutex mu;
    static br_hmac_drbg_context drbg;
    static bool ready = false;
    std::lock_guard<std::mutex> l(mu);
    if (!ready) {
        unsigned char seed[48] = {};
        const int f = ::open("/dev/urandom", O_RDONLY);
        if (f >= 0) { (void)::read(f, seed, 32); ::close(f); }
        const auto t = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::memcpy(seed + 32, &t, sizeof t);
        br_hmac_drbg_init(&drbg, &br_sha256_vtable, seed, sizeof seed);
        ready = true;
    }
    Bytes out(n, '\0');
    br_hmac_drbg_generate(&drbg, &out[0], n);
    return out;
}

Bytes xor_bytes(const Bytes& a, const Bytes& b) { Bytes o = a; for (std::size_t i = 0; i < o.size() && i < b.size(); ++i) o[i] = static_cast<char>(o[i] ^ b[i]); return o; }

// Attributs d'un nœud : « MEGA{"n":"nom"} » complété de zéros, chiffré en AES-CBC (vecteur nul) avec la clé du nœud.
std::string enc_attrs(const Bytes& key, const std::string& name) {
    Json a = Json::object();
    a.set("n", name);
    Bytes plain = "MEGA" + a.dump();
    plain.resize((plain.size() + 15) / 16 * 16, '\0');
    aes_cbc_encrypt0(key, plain);
    return b64url_encode(plain);
}
bool dec_attrs(const Bytes& key, const std::string& b64, std::string& name) {
    Bytes data;
    if (!b64url_decode(b64, data) || data.empty()) return false;
    data.resize(data.size() / 16 * 16);
    if (!aes_cbc_decrypt0(key, data) || data.compare(0, 5, "MEGA{") != 0) return false;
    const auto end = data.rfind('}');
    Json j;
    if (end == std::string::npos || !Json::parse(data.substr(4, end - 3), j)) return false;
    name = j["n"].as_string();
    return true;
}

struct Node {
    std::string h, p, name;
    int t = 0;                 // 0 fichier, 1 dossier, 2 racine, 3 boîte de réception, 4 corbeille de MEGA
    Bytes key, nonce, full;    // clé AES du nœud ; fichier : nonce (8 octets) et clé complète (32 octets)
    std::uint64_t size = 0;
    std::int64_t ts = 0;
};

struct Session { std::string sid, user; Bytes master; };
std::mutex g_sess_mu;
std::map<std::string, Session> g_sessions;  // e-mail → session (une connexion par compte, gardée en mémoire)

std::string error_text(long code) {
    switch (code) {
        case -3: return "MEGA est occupé, réessayez";
        case -4: return "trop de requêtes vers MEGA";
        case -9: return "introuvable (ou e-mail / mot de passe refusés)";
        case -11: return "accès refusé";
        case -15: return "session expirée";
        case -16: return "compte MEGA bloqué";
        case -17: return "quota de MEGA dépassé";
        case -26: return "ce compte demande une double authentification (pas encore prise en charge)";
        default: return "erreur MEGA " + std::to_string(code);
    }
}

class Mega : public Store {
public:
    explicit Mega(FtpConfig cfg) : cfg_(std::move(cfg)) {
        email_ = cfg_.user;
        for (auto& ch : email_) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    bool open_session(std::string& err) {
        {
            std::lock_guard<std::mutex> l(g_sess_mu);
            const auto it = g_sessions.find(email_);
            if (it != g_sessions.end()) s_ = it->second;
        }
        if (s_.sid.empty() && !login(err)) return false;
        if (!load_tree(err)) {
            if (err.find("expirée") == std::string::npos) return false;
            s_ = {};
            err.clear();
            if (!login(err) || !load_tree(err)) return false;
        }
        return true;
    }

    std::string label() const override { return "MEGA"; }

    bool mkdirs(const std::string& dir) override {
        std::string at = top_;  // chemins absolus depuis la racine du compte
        for (const auto& part : split(dir)) {
            const Node* c = child(at, part);
            if (c && c->t != 0) { at = c->h; continue; }
            if (c) return false;  // un fichier du même nom
            const Bytes key = random_bytes(16);
            Json n = Json::object();
            n.set("h", "xxxxxxxx");
            n.set("t", 1);
            n.set("a", enc_attrs(key, part));
            Bytes ek = key;
            aes_ecb_encrypt(s_.master, ek);
            n.set("k", b64url_encode(ek));
            Json list = Json::array();
            list.push(n);
            Json cmd = Json::object();
            cmd.set("a", "p");
            cmd.set("t", at);
            cmd.set("n", list);
            std::string err;
            const Json r = api(cmd, err);
            if (!err.empty() || !r["f"].is_array() || r["f"].size() == 0) { log("mega mkdir %s: %s", part.c_str(), err.c_str()); return false; }
            Node nd;
            nd.h = r["f"][0]["h"].as_string();
            nd.p = at;
            nd.t = 1;
            nd.name = part;
            nd.key = key;
            add(nd);
            at = nd.h;
        }
        return true;
    }

    std::unique_ptr<Writer> create(const std::string& path, std::string& err) override;
    // Reprise d'un envoi arrêté (3.1.41) : l'adresse d'envoi de MEGA, la clé, le nonce et les codes de contrôle des
    // morceaux déjà acceptés sont gardés ; la suite repart au premier morceau manquant.
    bool kept(const std::string& path, Kept& out) override { return kept_load(kkey(path), out); }
    std::unique_ptr<Writer> resume(const std::string& path, std::uint64_t& have, std::string& err) override;
    void drop_partial(const std::string& path) override { kept_drop(kkey(path)); }
    std::string kkey(const std::string& path) const { return kept_key("mega", email_, path); }
    std::unique_ptr<Reader> open(const std::string& path, std::string& err) override { return open_at(path, 0, err); }
    std::unique_ptr<Reader> open_at(const std::string& path, std::uint64_t offset, std::string& err) override;

    bool read_small(const std::string& path, std::string& out, std::uint64_t limit) override {
        std::string err;
        auto r = open(path, err);
        if (!r) return false;
        out.clear();
        char buf[65536];
        for (long n; (n = r->read(buf, sizeof buf)) > 0;) { out.append(buf, static_cast<std::size_t>(n)); if (out.size() > limit) return false; }
        return true;
    }
    bool write_small(const std::string& path, std::string_view data) override {
        std::string err;
        auto w = create(path, err);
        if (!w) return false;
        w->size_hint(data.size());
        return w->write(data.data(), data.size()) && w->commit(err);
    }
    bool stat(const std::string& path, fs::Entry& out) override {
        const Node* n = find(path);
        if (!n) return false;
        out = entry(*n);
        return true;
    }
    bool list(const std::string& dir, std::vector<fs::Entry>& out) override {
        const Node* d = find(dir);
        if (!d || d->t == 0) return false;
        out.clear();
        const auto it = kids_.find(d->h);
        if (it != kids_.end()) for (const auto& h : it->second) { const auto n = nodes_.find(h); if (n != nodes_.end()) out.push_back(entry(n->second)); }
        return true;
    }
    bool space(const std::string&, fs::Space& out) override {
        Json cmd = Json::object();
        cmd.set("a", "uq");
        cmd.set("strg", 1);
        std::string err;
        const Json r = api(cmd, err);
        if (!err.empty() || !r["mstrg"].is_number()) return false;
        out.total = static_cast<std::uint64_t>(r["mstrg"].as_int());
        const std::uint64_t used = static_cast<std::uint64_t>(r["cstrg"].as_int());
        out.free = out.total > used ? out.total - used : 0;
        return true;
    }
    bool remove(const std::string& path) override {
        const Node* n = find(path);
        if (!n) return true;
        return remove_node(n->h);
    }
    bool rename(const std::string& from, const std::string& to) override {
        const Node* n = find(from);
        if (!n) return false;
        const std::string h = n->h;
        const std::string parent_path = std::string(fs::dirname(to)), name(fs::basename(to));
        if (!mkdirs(parent_path)) return false;
        const Node* parent = find(parent_path);
        if (!parent) return false;
        if (const Node* old = child(parent->h, name); old && old->h != h && !remove_node(old->h)) return false;  // remplacé
        std::string err;
        if (nodes_[h].p != parent->h) {
            Json cmd = Json::object();
            cmd.set("a", "m");
            cmd.set("n", h);
            cmd.set("t", parent->h);
            api(cmd, err);
            if (!err.empty()) { log("mega move: %s", err.c_str()); return false; }
            move_node(h, parent->h);
        }
        if (nodes_[h].name != name) {
            Json cmd = Json::object();
            cmd.set("a", "a");
            cmd.set("n", h);
            cmd.set("at", enc_attrs(nodes_[h].key, name));
            api(cmd, err);
            if (!err.empty()) { log("mega rename: %s", err.c_str()); return false; }
            nodes_[h].name = name;
        }
        return true;
    }

    // Pour les envois (MegaWriter) : la requête, la clé maître, le dossier d'arrivée.
    Json api(const Json& cmd, std::string& err) {
        Json list = Json::array();
        list.push(cmd);
        for (int attempt = 0; attempt < 6; ++attempt) {
            http::Call c;
            c.method = "POST";
            c.url = std::string(kApi) + "?id=" + std::to_string(++seq_) + (s_.sid.empty() ? "" : "&sid=" + s_.sid);
            c.headers = {{"Content-Type", "application/json"}};
            c.timeout_s = 60;
            const http::Reply r = http::call(c, list.dump());
            if (!r.error.empty()) { err = "MEGA injoignable : " + r.error; return Json(); }
            Json j;
            if (!Json::parse(r.body, j)) { err = "réponse MEGA illisible (HTTP " + std::to_string(r.status) + ")"; return Json(); }
            const Json one = j.is_array() && j.size() ? j[0] : j;
            if (one.is_number() && one.as_int() < 0) {
                const long code = static_cast<long>(one.as_int());
                if ((code == -3 || code == -4) && attempt < 5) { ::usleep(static_cast<useconds_t>(250000u << attempt)); continue; }
                err = error_text(code);
                if (code == -15) { std::lock_guard<std::mutex> l(g_sess_mu); g_sessions.erase(email_); }
                return Json();
            }
            err.clear();
            return one;
        }
        err = error_text(-3);
        return Json();
    }
    const Bytes& master() const { return s_.master; }
    bool target(const std::string& path, std::string& parent_h, std::string& name, std::string& err) {
        name = std::string(fs::basename(path));
        const std::string dir(fs::dirname(path));
        if (!mkdirs(dir)) { err = "dossier impossible à créer sur MEGA"; return false; }
        const Node* d = find(dir);
        if (!d) { err = "dossier introuvable sur MEGA"; return false; }
        parent_h = d->h;
        return true;
    }
    // Fichier envoyé : il remplace un fichier du même nom.
    bool place(const std::string& parent_h, const std::string& name, const std::string& upload_handle, const Bytes& full_key, std::uint64_t size, std::string& err) {
        const Bytes aes = xor_bytes(full_key.substr(0, 16), full_key.substr(16, 16));
        Json n = Json::object();
        n.set("h", upload_handle);
        n.set("t", 0);
        n.set("a", enc_attrs(aes, name));
        Bytes ek = full_key;
        aes_ecb_encrypt(s_.master, ek);
        n.set("k", b64url_encode(ek));
        Json list = Json::array();
        list.push(n);
        Json cmd = Json::object();
        cmd.set("a", "p");
        cmd.set("t", parent_h);
        cmd.set("n", list);
        const Json r = api(cmd, err);
        if (!err.empty() || !r["f"].is_array() || r["f"].size() == 0) { if (err.empty()) err = "MEGA n'a pas enregistré le fichier"; return false; }
        const std::string old = child(parent_h, name) ? child(parent_h, name)->h : std::string();
        Node nd;
        nd.h = r["f"][0]["h"].as_string();
        nd.p = parent_h;
        nd.t = 0;
        nd.name = name;
        nd.full = full_key;
        nd.key = aes;
        nd.nonce = full_key.substr(16, 8);
        nd.size = size;
        nd.ts = now_unix();
        if (!old.empty()) remove_node(old);
        add(nd);
        return true;
    }

    // ---- Coffre de clés du compte (^!keys, comptes récents) ----
    // Format (lu sur le compte du studio, 18/11) : « 14 00 », IV de 12 octets, puis AES-128-GCM (étiquette de 16 octets) avec
    // la clé HKDF-SHA256(clé principale, sel vide, info 01). Contenu : rubriques étiquette (1 octet) + longueur (3 octets,
    // gros-boutiste) + valeur. Rubrique 4 : génération (4 octets) ; rubrique 48 : clés de partage, 23 octets chacune
    // (identifiant 6 + clé 16 + drapeaux 1 ; 1 = clé de confiance).
    struct Keys { Bytes head; std::vector<std::pair<unsigned, Bytes>> tlv; Json ver; std::string av; };
    Bytes keys_aes() const {
        unsigned char k[16];
        br_hkdf_context hk;
        br_hkdf_init(&hk, &br_sha256_vtable, nullptr, 0);
        br_hkdf_inject(&hk, s_.master.data(), s_.master.size());
        br_hkdf_flip(&hk);
        br_hkdf_produce(&hk, "\x01", 1, k, sizeof k);
        return Bytes(reinterpret_cast<const char*>(k), sizeof k);
    }
    bool keys_load(Keys& out, std::string& err) {
        Json cmd = Json::object();
        cmd.set("a", "uga");
        cmd.set("u", s_.user);
        cmd.set("ua", "^!keys");
        cmd.set("v", 1);
        const Json r = api(cmd, err);
        if (!err.empty()) return false;
        Bytes c;
        out.av = r["av"].as_string();
        out.ver = r["v"];
        if (!b64url_decode(out.av, c) || c.size() < 2 + 12 + 16 || c[0] != 0x14 || c[1] != 0x00) { err = "coffre de clés MEGA absent ou inconnu"; return false; }
        out.head = c.substr(0, 2);
        Bytes data = c.substr(14, c.size() - 14 - 16);
        const Bytes k = keys_aes();
        br_aes_ct_ctr_keys bc;
        br_aes_ct_ctr_init(&bc, k.data(), k.size());
        br_gcm_context gc;
        br_gcm_init(&gc, &bc.vtable, br_ghash_ctmul32);
        br_gcm_reset(&gc, c.data() + 2, 12);
        br_gcm_flip(&gc);
        br_gcm_run(&gc, 0, data.data(), data.size());
        if (br_gcm_check_tag(&gc, c.data() + c.size() - 16) != 1) { err = "coffre de clés MEGA illisible"; return false; }
        out.tlv.clear();
        std::size_t at = 0;
        while (at + 4 <= data.size()) {
            const unsigned tag = static_cast<unsigned char>(data[at]);
            const std::size_t len = (static_cast<std::size_t>(static_cast<unsigned char>(data[at + 1])) << 16) | (static_cast<std::size_t>(static_cast<unsigned char>(data[at + 2])) << 8) | static_cast<unsigned char>(data[at + 3]);
            at += 4;
            if (at + len > data.size()) { err = "coffre de clés MEGA abîmé"; return false; }
            out.tlv.push_back({tag, data.substr(at, len)});
            at += len;
        }
        if (at != data.size()) { err = "coffre de clés MEGA abîmé"; return false; }
        return true;
    }
    // La clé de partage de ce dossier dans le coffre (vide : aucune).
    static Bytes keys_share(const Keys& k, const std::string& handle) {
        Bytes h;
        if (!b64url_decode(handle, h) || h.size() != 6) return {};
        for (const auto& [tag, v] : k.tlv)
            if (tag == 48)
                for (std::size_t i = 0; i + 23 <= v.size(); i += 23)
                    if (v.compare(i, 6, h) == 0) return v.substr(i + 6, 16);
        return {};
    }
    // Ajoute la clé d'un partage, génération + 1, rechiffre (IV neuf) et enregistre. Le coffre d'avant est d'abord gardé
    // sur la console (dossier des envois gardés, lisible par le moteur seulement) ; si MEGA a une version plus récente
    // (autre appareil), on relit et on recommence une fois.
    bool keys_add_share(const std::string& handle, const Bytes& sharekey, std::string& err) {
        Bytes h;
        if (!b64url_decode(handle, h) || h.size() != 6 || sharekey.size() != 16) { err = "clé de partage invalide"; return false; }
        for (int attempt = 0; attempt < 2; ++attempt) {
            Keys k;
            if (!keys_load(k, err)) return false;
            if (keys_share(k, handle) == sharekey) return true;  // déjà là
            const std::string dir = kept_dir();
            if (!dir.empty()) {  // sauvegarde du coffre tel quel (chiffré), avant toute écriture
                ::mkdir(dir.c_str(), 0700);
                const std::string f = dir + "/mega-keys-avant-" + std::to_string(now_unix()) + ".json";
                Json b = Json::object();
                b.set("user", s_.user); b.set("av", k.av); b.set("v", k.ver);
                const std::string txt = b.dump();
                const int fd = ::open(f.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (fd < 0 || ::write(fd, txt.data(), txt.size()) != static_cast<ssize_t>(txt.size())) { if (fd >= 0) ::close(fd); err = "sauvegarde du coffre impossible : rien n'est écrit"; return false; }
                ::close(fd);
            } else { err = "pas de dossier pour sauvegarder le coffre : rien n'est écrit"; return false; }
            bool has48 = false;
            for (auto& [tag, v] : k.tlv) {
                if (tag == 4 && v.size() == 4) {  // génération + 1
                    std::uint32_t g = (static_cast<std::uint32_t>(static_cast<unsigned char>(v[0])) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(v[1])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(v[2])) << 8) | static_cast<unsigned char>(v[3]);
                    ++g;
                    v = Bytes{static_cast<char>(g >> 24), static_cast<char>((g >> 16) & 0xff), static_cast<char>((g >> 8) & 0xff), static_cast<char>(g & 0xff)};
                }
                if (tag == 48) {
                    has48 = true;
                    for (std::size_t i = 0; i + 23 <= v.size();) { if (v.compare(i, 6, h) == 0) v.erase(i, 23); else i += 23; }  // ancienne clé de ce dossier
                    v += h + sharekey + Bytes(1, '\x01');
                }
            }
            if (!has48) { err = "coffre de clés MEGA sans rubrique de partages : rien n'est écrit"; return false; }
            Bytes plain;
            for (const auto& [tag, v] : k.tlv) {
                plain += static_cast<char>(tag);
                plain += static_cast<char>((v.size() >> 16) & 0xff); plain += static_cast<char>((v.size() >> 8) & 0xff); plain += static_cast<char>(v.size() & 0xff);
                plain += v;
            }
            const Bytes iv = random_bytes(12), key = keys_aes();
            br_aes_ct_ctr_keys bc;
            br_aes_ct_ctr_init(&bc, key.data(), key.size());
            br_gcm_context gc;
            br_gcm_init(&gc, &bc.vtable, br_ghash_ctmul32);
            br_gcm_reset(&gc, iv.data(), iv.size());
            br_gcm_flip(&gc);
            br_gcm_run(&gc, 1, plain.data(), plain.size());
            char tag[16];
            br_gcm_get_tag(&gc, tag);
            const Bytes blob = k.head + iv + plain + Bytes(tag, sizeof tag);
            Json val = Json::array();
            val.push(b64url_encode(blob));
            val.push(k.ver);
            Json cmd = Json::object();
            cmd.set("a", "upv");
            cmd.set("^!keys", val);
            std::string e2;
            api(cmd, e2);
            if (e2.empty()) { Keys check; if (keys_load(check, err) && keys_share(check, handle) == sharekey) return true; if (err.empty()) err = "coffre relu sans la clé"; return false; }
            err = "MEGA a refusé le coffre : " + e2;  // version plus récente ailleurs : on relit une fois
        }
        return false;
    }

    // Coffre de clés du compte (attribut « ^!keys », comptes récents) : LECTURE SEULE, pour connaître son format avant
    // d'y inscrire les clés de nos partages. Rien n'est écrit ; aucune clé n'est rendue (formes, tailles, comptes).
    bool keys_info(Json& out, std::string& err) {
        Json cmd = Json::object();
        cmd.set("a", "uga");
        cmd.set("u", s_.user);
        cmd.set("ua", "^!keys");
        cmd.set("v", 1);
        const Json r = api(cmd, err);
        if (!err.empty()) return false;
        Bytes c;
        if (!b64url_decode(r["av"].as_string(), c) || c.size() < 2 + 12 + 16) { err = "pas de coffre ^!keys (compte ancien ?)"; return false; }
        out.set("attr_version", r["v"]);
        out.set("size", static_cast<std::int64_t>(c.size()));
        char hb[8];
        std::snprintf(hb, sizeof hb, "%02x %02x", static_cast<unsigned char>(c[0]), static_cast<unsigned char>(c[1]));
        out.set("head", hb);
        // Clé : HKDF-SHA256 de la clé principale (sel vide), info 0x01, 16 octets. En-tête de 1 ou 2 octets, IV de 12.
        Bytes plain;
        Json tries = Json::array();
        for (const char* info : {"\x01", ""}) {
            unsigned char k[16];
            br_hkdf_context hk;
            br_hkdf_init(&hk, &br_sha256_vtable, nullptr, 0);
            br_hkdf_inject(&hk, s_.master.data(), s_.master.size());
            br_hkdf_flip(&hk);
            br_hkdf_produce(&hk, info, std::strlen(info), k, sizeof k);
            for (std::size_t h : {std::size_t(2), std::size_t(1), std::size_t(0)}) {
                if (c.size() < h + 12 + 16) continue;
                Bytes data = c.substr(h + 12, c.size() - h - 12 - 16);
                const Bytes tag = c.substr(c.size() - 16);
                br_aes_ct_ctr_keys bc;
                br_aes_ct_ctr_init(&bc, k, sizeof k);
                br_gcm_context gc;
                br_gcm_init(&gc, &bc.vtable, br_ghash_ctmul32);
                br_gcm_reset(&gc, c.data() + h, 12);
                br_gcm_flip(&gc);
                br_gcm_run(&gc, 0, data.data(), data.size());
                const bool ok = br_gcm_check_tag(&gc, tag.data()) == 1;
                tries.push(std::string(*info ? "info 01" : "info vide") + ", en-tête " + std::to_string(h) + (ok ? " : OK" : " : non"));
                if (ok && plain.empty()) { plain = data; out.set("header_len", static_cast<std::int64_t>(h)); out.set("info", *info ? "01" : ""); }
            }
        }
        out.set("tries", tries);
        if (plain.empty()) { err = "coffre illisible (format inattendu)"; return false; }
        // Rubriques : étiquette (1 octet), longueur (3 octets, gros-boutiste), valeur.
        Json tags = Json::array();
        std::size_t at = 0;
        while (at + 4 <= plain.size()) {
            const unsigned tag = static_cast<unsigned char>(plain[at]);
            const std::size_t len = (static_cast<std::size_t>(static_cast<unsigned char>(plain[at + 1])) << 16) | (static_cast<std::size_t>(static_cast<unsigned char>(plain[at + 2])) << 8) | static_cast<unsigned char>(plain[at + 3]);
            at += 4;
            if (at + len > plain.size()) { out.set("tlv_error", "longueur au-delà de la fin"); break; }
            Json t = Json::object();
            t.set("tag", static_cast<std::int64_t>(tag));
            t.set("len", static_cast<std::int64_t>(len));
            if (tag == 48) {  // clés de partage : identifiant (6) + clé (16) + drapeaux (1) ?
                t.set("count_if_23", static_cast<std::int64_t>(len % 23 == 0 ? len / 23 : -1));
                Json hs = Json::array();
                for (std::size_t i = 0; len % 23 == 0 && i + 23 <= len && hs.size() < 40; i += 23) {
                    const std::string h = b64url_encode(plain.substr(at + i, 6));
                    const auto n = nodes_.find(h);
                    Json e = Json::object();
                    e.set("h", h);
                    e.set("flags", static_cast<std::int64_t>(static_cast<unsigned char>(plain[at + i + 22])));
                    e.set("name", n != nodes_.end() ? n->second.name : std::string("?"));
                    hs.push(e);
                }
                t.set("shares", hs);
            }
            tags.push(t);
            at += len;
        }
        out.set("tags", tags);
        return true;
    }

    // Lien public vers un fichier ou un dossier du compte (« Partager… ») ; `off` retire le lien.
    bool export_link(const std::string& path, bool off, std::string& link, std::string& err) {
        const Node* n = find(path);
        if (!n || (n->t != 0 && n->t != 1)) { err = "introuvable sur MEGA"; return false; }
        const std::string h = n->h;
        Bytes sharekey;
        bool recorded = false;
        Keys vault;
        const bool has_vault = n->t == 1 && !off && keys_load(vault, err);
        err.clear();
        if (n->t == 1 && !off) {
            // Un dossier se partage avec sa propre clé : chaque élément dedans reçoit aussi sa clé chiffrée par elle.
            // Déjà partagé (clé dans le coffre du compte) : la même clé, pour que les liens déjà donnés restent bons.
            sharekey = has_vault ? keys_share(vault, h) : Bytes();
            recorded = !sharekey.empty();
            if (sharekey.empty()) sharekey = random_bytes(16);
            Json sh = Json::array(), nh = Json::array(), keys = Json::array();
            sh.push(h);
            std::vector<std::string> todo{h};
            for (std::size_t i = 0; i < todo.size(); ++i) {
                const Node& x = nodes_[todo[i]];
                Bytes k = x.t == 0 ? x.full : x.key;
                aes_ecb_encrypt(sharekey, k);
                nh.push(x.h);
                keys.push(0);
                keys.push(static_cast<std::int64_t>(i));
                keys.push(b64url_encode(k));
                const auto it = kids_.find(x.h);
                if (it != kids_.end()) for (const auto& c : it->second) todo.push_back(c);
            }
            Json cr = Json::array();
            cr.push(sh);
            cr.push(nh);
            cr.push(keys);
            Json who = Json::object();
            who.set("u", "EXP");
            who.set("r", 0);
            Json ws = Json::array();
            ws.push(who);
            // Comptes récents (gestionnaire de clés de MEGA) : sans « ok » ni « ha ». Anciens comptes : avec.
            Json cmd = Json::object();
            cmd.set("a", "s2");
            cmd.set("n", h);
            cmd.set("s", ws);
            cmd.set("cr", cr);
            api(cmd, err);
            if (!err.empty()) {
                Bytes ok = sharekey, ha = h + h;
                aes_ecb_encrypt(s_.master, ok);
                aes_ecb_encrypt(s_.master, ha);
                cmd.set("ok", b64url_encode(ok));
                cmd.set("ha", b64url_encode(ha));
                err.clear();
                api(cmd, err);
            }
            if (err.empty()) {  // les clés des éléments, chiffrées par la clé du partage (sans elles, le lien n'ouvre rien)
                Json kc = Json::object();
                kc.set("a", "k");
                kc.set("cr", cr);
                api(kc, err);
            }
            if (!err.empty()) return false;
            // Comptes récents : la clé du partage dans le coffre du compte, sinon le site et l'appli MEGA montrent un lien
            // avec une mauvaise clé (le lien donné ici, lui, marche dans tous les cas).
            if (has_vault && !recorded) {
                std::string e3;
                if (!keys_add_share(h, sharekey, e3)) log("mega: clé du partage non inscrite au coffre : %s", e3.c_str());
            }
        }
        Json cmd = Json::object();
        cmd.set("a", "l");
        cmd.set("n", h);
        if (off) cmd.set("d", 1);
        const Json r = api(cmd, err);
        if (!err.empty()) return false;
        if (off) { link.clear(); return true; }
        const std::string ph = r.as_string();
        if (ph.empty()) { err = "MEGA n'a pas donné de lien"; return false; }
        link = "https://mega.nz/" + std::string(n->t == 1 ? "folder/" : "file/") + ph + "#" + b64url_encode(n->t == 1 ? sharekey : nodes_[h].full);
        return true;
    }

private:
    bool login(std::string& err) {
        Json c = Json::object();
        c.set("a", "us0");
        c.set("user", email_);
        const Json pre = api(c, err);
        if (!err.empty()) return false;
        if (pre["v"].as_int() != 2) { err = "compte MEGA ancien (version 1) : pas encore pris en charge"; return false; }
        Bytes salt;
        b64url_decode(pre["s"].as_string(), salt);
        const Bytes dk = pbkdf2_sha512(cfg_.password, salt, 100000, 32);
        Json l = Json::object();
        l.set("a", "us");
        l.set("user", email_);
        l.set("uh", b64url_encode(dk.substr(16)));
        const Json r = api(l, err);
        if (!err.empty()) { if (err.find("introuvable") != std::string::npos) err = "e-mail ou mot de passe MEGA refusé"; return false; }
        Bytes k, privk, csid;
        b64url_decode(r["k"].as_string(), k);
        b64url_decode(r["privk"].as_string(), privk);
        b64url_decode(r["csid"].as_string(), csid);
        if (!aes_ecb_decrypt(dk.substr(0, 16), k)) { err = "clé MEGA inattendue"; return false; }
        privk.resize(privk.size() / 16 * 16);
        Bytes m;
        if (!aes_ecb_decrypt(k, privk) || !rsa_decrypt_mpi(privk, csid, m) || m.size() < 43) { err = "session MEGA impossible à ouvrir"; return false; }
        s_.sid = b64url_encode(m.substr(0, 43));
        s_.user = r["u"].as_string();
        s_.master = k;
        std::lock_guard<std::mutex> lk(g_sess_mu);
        g_sessions[email_] = s_;
        return true;
    }

    bool load_tree(std::string& err) {
        Json c = Json::object();
        c.set("a", "f");
        c.set("c", 1);
        const Json r = api(c, err);
        if (!err.empty()) return false;
        nodes_.clear();
        kids_.clear();
        std::string top;
        for (const auto& f : r["f"].items()) {
            Node n;
            n.h = f["h"].as_string();
            n.p = f["p"].as_string();
            n.t = static_cast<int>(f["t"].as_int());
            n.size = static_cast<std::uint64_t>(f["s"].as_int());
            n.ts = f["ts"].as_int();
            if (n.t == 2) { top = n.h; nodes_[n.h] = n; continue; }
            if (n.t != 0 && n.t != 1) continue;
            // Clé : « <propriétaire>:<clé> » (plusieurs, séparées par « / ») ; la nôtre est chiffrée avec la clé maître.
            Bytes key;
            for (const auto& part : split_by(f["k"].as_string(), '/')) {
                const auto colon = part.find(':');
                if (colon == std::string::npos || part.substr(0, colon) != s_.user) continue;
                if (b64url_decode(part.substr(colon + 1), key) && (key.size() == 16 || key.size() == 32) && aes_ecb_decrypt(s_.master, key)) break;
                key.clear();
            }
            if (key.empty()) continue;  // partagé par quelqu'un d'autre : pas pour nous
            if (n.t == 0) {
                if (key.size() != 32) continue;
                n.full = key;
                n.key = xor_bytes(key.substr(0, 16), key.substr(16, 16));
                n.nonce = key.substr(16, 8);
            } else n.key = key.substr(0, 16);
            if (!dec_attrs(n.key, f["a"].as_string(), n.name)) continue;
            nodes_[n.h] = n;
        }
        for (auto& [h, n] : nodes_) if (n.t != 2) kids_[n.p].push_back(h);
        top_ = top;  // les chemins reçus sont absolus depuis la racine du compte (« /PBC Backup/… »)
        if (top_.empty()) { err = "racine MEGA introuvable"; return false; }
        return true;
    }

    static std::vector<std::string> split_by(const std::string& s, char sep) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : s) { if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }
    static std::vector<std::string> split(const std::string& path) { return split_by(path, '/'); }

    const Node* child(const std::string& parent, const std::string& name) const {
        const auto it = kids_.find(parent);
        if (it == kids_.end()) return nullptr;
        for (const auto& h : it->second) { const auto n = nodes_.find(h); if (n != nodes_.end() && n->second.name == name) return &n->second; }
        return nullptr;
    }
    const Node* find(const std::string& path) const {
        const auto r = nodes_.find(top_);
        if (r == nodes_.end()) return nullptr;
        const Node* at = &r->second;
        for (const auto& part : split(path)) { at = child(at->h, part); if (!at) return nullptr; }
        return at;
    }
    fs::Entry entry(const Node& n) const {
        fs::Entry e;
        e.name = n.name;
        e.dir = n.t != 0;
        e.size = n.size;
        e.mtime = n.ts;
        return e;
    }
    void add(const Node& n) { nodes_[n.h] = n; kids_[n.p].push_back(n.h); }
    void move_node(const std::string& h, const std::string& to) {
        auto& v = kids_[nodes_[h].p];
        v.erase(std::remove(v.begin(), v.end(), h), v.end());
        nodes_[h].p = to;
        kids_[to].push_back(h);
    }
    bool remove_node(const std::string& h) {
        Json cmd = Json::object();
        cmd.set("a", "d");
        cmd.set("n", h);
        std::string err;
        api(cmd, err);
        if (!err.empty()) { log("mega delete: %s", err.c_str()); return false; }
        auto& v = kids_[nodes_[h].p];
        v.erase(std::remove(v.begin(), v.end(), h), v.end());
        nodes_.erase(h);
        return true;
    }

    FtpConfig cfg_;
    std::string email_, top_;
    Session s_;
    long seq_ = static_cast<long>(std::time(nullptr) % 100000) * 1000;
    std::map<std::string, Node> nodes_;
    std::map<std::string, std::vector<std::string>> kids_;
};

// Envoi : chaque morceau de MEGA (128 Kio, 256 Kio… puis 1 Mio) est chiffré ici puis posté à l'adresse d'envoi ;
// la réponse au dernier donne le « jeton d'envoi » qui devient le fichier.
class MegaWriter : public Writer {
public:
    MegaWriter(Mega& m, std::string parent, std::string name, std::string kept) : m_(m), parent_(std::move(parent)), name_(std::move(name)), kept_(std::move(kept)) {
        key_ = random_bytes(16);
        nonce_ = random_bytes(8);
    }
    ~MegaWriter() override { if (!done_ && !kept_saved_) kept_drop(kept_); }
    // Reprise : l'état gardé par keep() ; la suite écrit à partir de `have` (une limite de morceau).
    bool reopen(const Kept& k, std::string& err) {
        const Json& x = k.extra;
        Bytes key, nonce;
        if (k.session.empty() || k.total == 0 || !b64url_decode(x["key"].as_string(), key) || key.size() != 16 || !b64url_decode(x["nonce"].as_string(), nonce) || nonce.size() != 8
            || !acked_.restore(k)) { err = "envoi MEGA gardé illisible"; return false; }
        std::vector<Bytes> macs;
        for (const auto& m : x["macs"].items()) { Bytes b; if (!b64url_decode(m.as_string(), b) || b.size() != 16) { err = "envoi MEGA gardé illisible"; return false; } macs.push_back(b); }
        total_ = k.total;
        bounds_ = chunk_bounds(total_);
        if (macs.size() > bounds_.size() || (macs.size() < bounds_.size() ? bounds_[macs.size()] : total_) != k.have) { err = "envoi MEGA gardé incohérent"; return false; }
        key_ = key; nonce_ = nonce; macs_ = std::move(macs); url_ = k.session;
        next_ = macs_.size(); sent_ = resume_at_ = k.have;
        hinted_ = started_ = resumed_ = true;
        return true;
    }
    // Arrêt : l'adresse d'envoi reste valable chez MEGA un certain temps ; on note où on en est.
    bool keep() override {
        if (!started_ || next_ == 0 || failed_ || kept_.empty()) { abort(); return false; }
        Kept k;
        acked_.fill(k);
        k.total = total_;
        k.session = url_;
        k.extra = Json::object();
        k.extra.set("key", b64url_encode(key_));
        k.extra.set("nonce", b64url_encode(nonce_));
        Json m = Json::array();
        for (const auto& b : macs_) m.push(b64url_encode(b));
        k.extra.set("macs", m);
        buf_.clear();
        done_ = true;
        if (!kept_save(kept_, k)) return false;
        kept_saved_ = true;
        return true;
    }
    void size_hint(std::uint64_t n) override { if (!started_) { total_ = n; hinted_ = true; } }
    bool write(const char* p, std::size_t n) override {
        if (failed_) return false;
        buf_.append(p, n);
        if (hinted_ && total_ > 0) return flush(false);
        return true;
    }
    bool commit(std::string& err) override {
        if (failed_) { err = err_.empty() ? "envoi interrompu vers MEGA" : err_; return false; }
        if (!hinted_ || sent_ + buf_.size() != total_) { if (started_) { err = "taille annoncée fausse pour MEGA"; return false; } total_ = buf_.size(); }
        if (!flush(true)) { err = err_; return false; }
        if (handle_.empty()) { err = "MEGA n'a pas confirmé l'envoi"; return false; }
        Bytes mm = file_meta_mac(key_, macs_);
        Bytes full = xor_bytes(key_, nonce_ + mm) + nonce_ + mm;  // [k0^n0, k1^n1, k2^mm0, k3^mm1, n0, n1, mm0, mm1]
        done_ = true;
        kept_drop(kept_);
        return m_.place(parent_, name_, handle_, full, total_, err);
    }
    void abort() override { failed_ = true; buf_.clear(); done_ = true; kept_drop(kept_); }

private:
    bool start() {
        Json cmd = Json::object();
        cmd.set("a", "u");
        cmd.set("s", static_cast<std::int64_t>(total_));
        const Json r = m_.api(cmd, err_);
        url_ = r["p"].as_string();
        if (!err_.empty() || url_.empty()) { if (err_.empty()) err_ = "MEGA ne donne pas d'adresse d'envoi"; failed_ = true; return false; }
        bounds_ = chunk_bounds(total_);
        started_ = true;
        return true;
    }
    // Envoie les morceaux complets (et le dernier si `last`).
    bool flush(bool last) {
        if (!started_ && !start()) return false;
        while (next_ < bounds_.size()) {
            const std::uint64_t from = bounds_[next_], to = next_ + 1 < bounds_.size() ? bounds_[next_ + 1] : total_;
            const std::size_t len = static_cast<std::size_t>(to - from);
            if (buf_.size() < len) {  // morceau pas encore complet
                if (!last) return true;
                err_ = "envoi MEGA incomplet";
                failed_ = true;
                return false;
            }
            std::string chunk = buf_.substr(0, len);
            buf_.erase(0, len);
            const std::string plain = chunk;  // pour le suivi de ce que MEGA a accepté (empreinte du début, reprise)
            ChunkMac mac;
            mac.start(key_, nonce_);
            mac.update(chunk.data(), chunk.size());
            macs_.push_back(mac.finish());
            aes_ctr(key_, nonce_, from, &chunk[0], chunk.size());
            http::Call c;
            c.method = "POST";
            c.url = url_ + "/" + std::to_string(from);
            c.timeout_s = 180;
            const http::Reply r = http::call(c, chunk);
            const bool refused = !r.ok() || (!r.body.empty() && r.body[0] == '-');
            if (refused && resumed_ && sent_ == resume_at_) {  // l'adresse d'envoi gardée a expiré chez MEGA
                err_ = "l'envoi gardé a expiré chez MEGA : relancez, la copie repartira du début";
                kept_drop(kept_);
                failed_ = true;
                return false;
            }
            if (!r.ok()) { err_ = "envoi refusé par MEGA (HTTP " + std::to_string(r.status) + ")" + (r.error.empty() ? "" : " : " + r.error); failed_ = true; return false; }
            if (!r.body.empty()) {
                if (r.body[0] == '-') { err_ = error_text(std::strtol(r.body.c_str(), nullptr, 10)); failed_ = true; return false; }
                handle_ = r.body;  // réponse au dernier morceau
            }
            acked_.add(plain.data(), plain.size());
            sent_ += len;
            ++next_;
        }
        return true;
    }
    Mega& m_;
    std::string parent_, name_, kept_, url_, handle_, err_;
    Bytes key_, nonce_, buf_;
    std::vector<Bytes> macs_;
    std::vector<std::uint64_t> bounds_;
    std::size_t next_ = 0;
    std::uint64_t total_ = 0, sent_ = 0, resume_at_ = 0;
    Acked acked_;
    bool hinted_ = false, started_ = false, failed_ = false, resumed_ = false, done_ = false, kept_saved_ = false;
};

class MegaReader : public Reader {
public:
    long read(char* p, std::size_t n) override {
        const long got = dl.read(p, n);
        if (got > 0) { aes_ctr(key, nonce, at, p, static_cast<std::size_t>(got)); at += static_cast<std::uint64_t>(got); }
        return got;
    }
    http::Download dl;
    Bytes key, nonce;
    std::uint64_t at = 0;
};

std::unique_ptr<Writer> Mega::create(const std::string& path, std::string& err) {
    std::string parent, name;
    if (!target(path, parent, name, err)) return nullptr;
    kept_drop(kkey(path));  // nouvel envoi depuis le début : un envoi gardé est oublié
    return std::make_unique<MegaWriter>(*this, parent, name, kkey(path));
}

std::unique_ptr<Writer> Mega::resume(const std::string& path, std::uint64_t& have, std::string& err) {
    have = 0;
    Kept k;
    if (!kept_load(kkey(path), k)) { err = "rien à reprendre"; return nullptr; }
    std::string parent, name;
    if (!target(path, parent, name, err)) return nullptr;
    auto w = std::make_unique<MegaWriter>(*this, parent, name, kkey(path));
    if (!w->reopen(k, err)) { kept_drop(kkey(path)); return nullptr; }
    have = k.have;
    return w;
}

std::unique_ptr<Reader> Mega::open_at(const std::string& path, std::uint64_t offset, std::string& err) {
    const Node* n = find(path);
    if (!n || n->t != 0) { err = "fichier introuvable sur MEGA"; return nullptr; }
    Json cmd = Json::object();
    cmd.set("a", "g");
    cmd.set("g", 1);
    cmd.set("n", n->h);
    const Json r = api(cmd, err);
    const std::string url = r["g"].as_string();
    if (!err.empty() || url.empty()) { if (err.empty()) err = "MEGA ne donne pas d'adresse de lecture"; return nullptr; }
    auto rd = std::make_unique<MegaReader>();
    rd->key = n->key;
    rd->nonce = n->nonce;
    rd->at = offset;
    http::Call c;
    c.url = offset ? url + "/" + std::to_string(offset) + "-" : url;
    c.timeout_s = 120;
    http::Reply head;
    if (!rd->dl.begin(c, head)) { err = "lecture refusée par MEGA (HTTP " + std::to_string(head.status) + ")"; return nullptr; }
    return rd;
}


// ---- Lien de partage MEGA (lecture seule) ----------------------------------------------------------------------
// « https://mega.nz/folder/<h>#<clé>[/folder/<sous-dossier>] », « https://mega.nz/file/<h>#<clé> », et les anciennes
// formes « #F!h!clé » / « #!h!clé ». Le lien porte la clé : tout se déchiffre ici, sans compte.
struct ShareTree {
    std::int64_t at = 0;
    std::string top;
    std::map<std::string, Node> nodes;
    std::map<std::string, std::vector<std::string>> kids;
};
std::mutex g_share_mu;
std::map<std::string, ShareTree> g_shares;  // lien → arbre lu (gardé 2 minutes : chaque écran recrée l'appareil)

class MegaShare : public Store {
public:
    explicit MegaShare(MegaLink link) : link_(std::move(link)) {}
    std::string label() const override { return "MEGA"; }

    bool load(std::string& err) {
        {
            std::lock_guard<std::mutex> l(g_share_mu);
            const auto it = g_shares.find(link_.raw);
            if (it != g_shares.end() && now_unix() - it->second.at < 120) { t_ = it->second; return true; }
        }
        Bytes key;
        if (!b64url_decode(link_.key, key)) { err = "clé du lien MEGA illisible"; return false; }
        if (link_.folder) {
            if (key.size() != 16) { err = "clé du lien MEGA illisible"; return false; }
            Json c = Json::object();
            c.set("a", "f");
            c.set("c", 1);
            c.set("r", 1);
            c.set("ca", 1);
            const Json r = api(c, err);
            if (!err.empty()) { if (err.find("introuvable") != std::string::npos) err = "lien MEGA expiré ou retiré"; return false; }
            for (const auto& f : r["f"].items()) {
                Node n;
                n.h = f["h"].as_string();
                n.p = f["p"].as_string();
                n.t = static_cast<int>(f["t"].as_int());
                n.size = static_cast<std::uint64_t>(f["s"].as_int());
                n.ts = f["ts"].as_int();
                if (n.t != 0 && n.t != 1) continue;
                Bytes k;
                const std::string ks = f["k"].as_string();
                for (std::size_t i = 0; i <= ks.size();) {  // « <poignée>:<clé> », plusieurs séparées par « / »
                    const std::size_t e = std::min(ks.find('/', i), ks.size());
                    const std::string part = ks.substr(i, e - i);
                    i = e + 1;
                    const auto colon = part.find(':');
                    if (colon == std::string::npos) continue;
                    if (b64url_decode(part.substr(colon + 1), k) && (k.size() == 16 || k.size() == 32) && aes_ecb_decrypt(key, k)) break;
                    k.clear();
                }
                if (k.empty()) continue;
                if (n.t == 0) {
                    if (k.size() != 32) continue;
                    n.full = k;
                    n.key = xor_bytes(k.substr(0, 16), k.substr(16, 16));
                    n.nonce = k.substr(16, 8);
                } else n.key = k.substr(0, 16);
                if (!dec_attrs(n.key, f["a"].as_string(), n.name)) continue;
                t_.nodes[n.h] = n;
            }
            t_.top = link_.sub.empty() ? link_.handle : link_.sub;
            if (!t_.nodes.count(t_.top)) {  // le dossier partagé lui-même : celui dont le parent n'est pas dans la liste
                for (const auto& [h, n] : t_.nodes) if (n.t == 1 && !t_.nodes.count(n.p)) { t_.top = h; break; }
            }
            if (!t_.nodes.count(t_.top)) { err = "lien MEGA vide ou clé fausse"; return false; }
            for (auto& [h, n] : t_.nodes) if (h != t_.top && t_.nodes.count(n.p)) t_.kids[n.p].push_back(h);
        } else {
            if (key.size() != 32) { err = "clé du lien MEGA illisible"; return false; }
            Json c = Json::object();
            c.set("a", "g");
            c.set("p", link_.handle);
            const Json r = api(c, err);
            if (!err.empty()) { if (err.find("introuvable") != std::string::npos) err = "lien MEGA expiré ou retiré"; return false; }
            Node n;
            n.h = link_.handle;
            n.p = "*";
            n.full = key;
            n.key = xor_bytes(key.substr(0, 16), key.substr(16, 16));
            n.nonce = key.substr(16, 8);
            n.size = static_cast<std::uint64_t>(r["s"].as_int());
            n.ts = now_unix();
            if (!dec_attrs(n.key, r["at"].as_string(), n.name)) { err = "lien MEGA : clé fausse"; return false; }
            Node top;  // un fichier seul : vu comme un dossier qui le contient
            top.h = "*";
            top.t = 1;
            t_.top = "*";
            t_.nodes["*"] = top;
            t_.nodes[n.h] = n;
            t_.kids["*"].push_back(n.h);
        }
        t_.at = now_unix();
        std::lock_guard<std::mutex> l(g_share_mu);
        g_shares[link_.raw] = t_;
        return true;
    }

    bool mkdirs(const std::string&) override { return false; }
    std::unique_ptr<Writer> create(const std::string&, std::string& err) override { err = "partage en lecture seule"; return nullptr; }
    std::unique_ptr<Reader> open(const std::string& path, std::string& err) override { return open_at(path, 0, err); }
    std::unique_ptr<Reader> open_at(const std::string& path, std::uint64_t offset, std::string& err) override {
        const Node* n = find(path);
        if (!n || n->t != 0) { err = "fichier introuvable dans le partage"; return nullptr; }
        Json cmd = Json::object();
        cmd.set("a", "g");
        cmd.set("g", 1);
        cmd.set("ssl", 2);
        if (link_.folder) cmd.set("n", n->h); else cmd.set("p", n->h);
        const Json r = api(cmd, err);
        const std::string url = r["g"].as_string();
        if (!err.empty() || url.empty()) { if (err.empty()) err = "MEGA ne donne pas d'adresse de lecture"; return nullptr; }
        auto rd = std::make_unique<MegaReader>();
        rd->key = n->key;
        rd->nonce = n->nonce;
        rd->at = offset;
        http::Call c;
        c.url = offset ? url + "/" + std::to_string(offset) + "-" : url;
        c.timeout_s = 120;
        http::Reply head;
        if (!rd->dl.begin(c, head)) { err = "lecture refusée par MEGA (HTTP " + std::to_string(head.status) + ")"; return nullptr; }
        return rd;
    }
    bool read_small(const std::string& path, std::string& out, std::uint64_t limit) override {
        std::string err;
        auto r = open(path, err);
        if (!r) return false;
        out.clear();
        char buf[65536];
        for (long n; (n = r->read(buf, sizeof buf)) > 0;) { out.append(buf, static_cast<std::size_t>(n)); if (out.size() > limit) return false; }
        return true;
    }
    bool write_small(const std::string&, std::string_view) override { return false; }
    bool stat(const std::string& path, fs::Entry& out) override {
        const Node* n = find(path);
        if (!n) return false;
        out = entry(*n);
        return true;
    }
    bool list(const std::string& dir, std::vector<fs::Entry>& out) override {
        const Node* d = find(dir);
        if (!d || d->t == 0) return false;
        out.clear();
        const auto it = t_.kids.find(d->h);
        if (it != t_.kids.end()) for (const auto& h : it->second) { const auto n = t_.nodes.find(h); if (n != t_.nodes.end()) out.push_back(entry(n->second)); }
        return true;
    }
    bool space(const std::string&, fs::Space&) override { return false; }
    bool remove(const std::string&) override { return false; }
    // Nom affiché : celui du dossier partagé, ou du fichier seul.
    std::string title() const {
        const auto it = t_.nodes.find(t_.top);
        if (it == t_.nodes.end()) return {};
        if (!it->second.name.empty()) return it->second.name;
        const auto k = t_.kids.find(t_.top);
        if (k != t_.kids.end() && k->second.size() == 1) { const auto n = t_.nodes.find(k->second[0]); if (n != t_.nodes.end()) return n->second.name; }
        return {};
    }
    // Ce que contient le partage : nombre de fichiers, de dossiers, taille totale.
    void totals(std::size_t& files, std::size_t& dirs, std::uint64_t& bytes) const {
        files = dirs = 0;
        bytes = 0;
        for (const auto& [h, n] : t_.nodes) { if (h == t_.top) continue; if (n.t == 0) { ++files; bytes += n.size; } else ++dirs; }
    }

private:
    Json api(const Json& cmd, std::string& err) {
        Json list = Json::array();
        list.push(cmd);
        for (int attempt = 0; attempt < 6; ++attempt) {
            http::Call c;
            c.method = "POST";
            c.url = std::string(kApi) + "?id=" + std::to_string(++seq_) + (link_.folder ? "&n=" + link_.handle : "");
            c.headers = {{"Content-Type", "application/json"}};
            c.timeout_s = 60;
            const http::Reply r = http::call(c, list.dump());
            if (!r.error.empty()) { err = "MEGA injoignable : " + r.error; return Json(); }
            Json j;
            if (!Json::parse(r.body, j)) { err = "réponse MEGA illisible (HTTP " + std::to_string(r.status) + ")"; return Json(); }
            const Json one = j.is_array() && j.size() ? j[0] : j;
            if (one.is_number() && one.as_int() < 0) {
                const long code = static_cast<long>(one.as_int());
                if ((code == -3 || code == -4) && attempt < 5) { ::usleep(static_cast<useconds_t>(250000u << attempt)); continue; }
                err = code == -2 || code == -9 || code == -11 ? "introuvable" : error_text(code);
                return Json();
            }
            err.clear();
            return one;
        }
        err = error_text(-3);
        return Json();
    }
    const Node* find(const std::string& path) const {
        const auto r = t_.nodes.find(t_.top);
        if (r == t_.nodes.end()) return nullptr;
        const Node* at = &r->second;
        std::string part;
        auto step = [&]() -> bool {
            if (part.empty()) return true;
            const auto it = t_.kids.find(at->h);
            const Node* next = nullptr;
            if (it != t_.kids.end()) for (const auto& h : it->second) { const auto n = t_.nodes.find(h); if (n != t_.nodes.end() && n->second.name == part) { next = &n->second; break; } }
            part.clear();
            at = next;
            return next != nullptr;
        };
        for (char c : path) { if (c == '/') { if (!step()) return nullptr; } else part += c; }
        return step() ? at : nullptr;
    }
    static fs::Entry entry(const Node& n) {
        fs::Entry e;
        e.name = n.name;
        e.dir = n.t != 0;
        e.size = n.size;
        e.mtime = n.ts;
        return e;
    }
    MegaLink link_;
    ShareTree t_;
    long seq_ = static_cast<long>(std::time(nullptr) % 100000) * 1000;
};

}  // namespace

std::unique_ptr<Store> mega_store(const FtpConfig& cfg, std::string& err) {
    if (cfg.user.empty() || cfg.password.empty()) { err = "e-mail et mot de passe MEGA nécessaires"; return nullptr; }
    auto m = std::make_unique<Mega>(cfg);
    if (!m->open_session(err)) return nullptr;
    return m;
}

namespace {
bool b64ch(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; }
std::string take(const std::string& s, std::size_t& i) { const std::size_t b = i; while (i < s.size() && b64ch(s[i])) ++i; return s.substr(b, i - b); }
}  // namespace

bool parse_mega_link(const std::string& in, MegaLink& out) {
    std::string s;
    for (char c : in) if (!std::isspace(static_cast<unsigned char>(c))) s += c;
    const auto host = s.find("mega.nz/") != std::string::npos ? s.find("mega.nz/") + 8 : s.find("mega.co.nz/") != std::string::npos ? s.find("mega.co.nz/") + 11 : std::string::npos;
    if (host == std::string::npos) return false;
    out = {};
    std::size_t i = host;
    auto starts = [&](const char* p) { const std::size_t n = std::strlen(p); if (s.compare(i, n, p) != 0) return false; i += n; return true; };
    if (starts("folder/") || starts("file/")) {
        out.folder = s.compare(host, 7, "folder/") == 0;
        out.handle = take(s, i);
        if (i >= s.size() || s[i] != '#') return false;
        ++i;
        out.key = take(s, i);
        if (out.folder && (starts("/folder/") || starts("/file/"))) out.sub = take(s, i);
    } else if (starts("#F!") || starts("#!")) {
        out.folder = s.compare(host, 3, "#F!") == 0;
        out.handle = take(s, i);
        if (i >= s.size() || s[i] != '!') return false;
        ++i;
        out.key = take(s, i);
        if (out.folder && i < s.size() && (s[i] == '!' || s[i] == '@')) { ++i; out.sub = take(s, i); }
    } else return false;
    if (out.handle.size() != 8 || out.key.size() < 22) return false;
    out.raw = std::string("https://mega.nz/") + (out.folder ? "folder/" : "file/") + out.handle + "#" + out.key + (out.sub.empty() ? "" : "/folder/" + out.sub);
    return true;
}

std::unique_ptr<Store> mega_share(const std::string& url, std::string& err) {
    MegaLink l;
    if (!parse_mega_link(url, l)) { err = "lien MEGA non reconnu"; return nullptr; }
    auto m = std::make_unique<MegaShare>(l);
    if (!m->load(err)) return nullptr;
    return m;
}

bool mega_keys_info(const FtpConfig& cfg, Json& out, std::string& err) {
    Mega m(cfg);
    if (!m.open_session(err)) return false;
    return m.keys_info(out, err);
}

bool mega_export(const FtpConfig& cfg, const std::string& path, bool off, std::string& link, std::string& err) {
    Mega m(cfg);
    if (!m.open_session(err)) return false;
    return m.export_link(path, off, link, err);
}

void mega_share_forget(const std::string& url) {
    MegaLink l;
    if (!parse_mega_link(url, l)) return;
    std::lock_guard<std::mutex> g(g_share_mu);
    g_shares.erase(l.raw);
}

bool mega_share_probe(const std::string& url, Json& out, std::string& err) {
    MegaLink l;
    if (!parse_mega_link(url, l)) { err = "lien MEGA non reconnu"; return false; }
    MegaShare m(l);
    if (!m.load(err)) return false;
    std::size_t files = 0, dirs = 0;
    std::uint64_t bytes = 0;
    m.totals(files, dirs, bytes);
    std::vector<fs::Entry> top;
    m.list("/", top);
    out = Json::object();
    out.set("service", "mega");
    out.set("kind", l.folder ? "folder" : "file");
    out.set("name", m.title());
    out.set("files", static_cast<std::int64_t>(files));
    out.set("dirs", static_cast<std::int64_t>(dirs));
    out.set("bytes", static_cast<std::int64_t>(bytes));
    out.set("link", l.raw);
    return true;
}

}  // namespace pbc::store
