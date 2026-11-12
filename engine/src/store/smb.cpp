// SPDX-License-Identifier: GPL-3.0-or-later
// Connecteur SMB (partages Windows, Synology, QNAP…) : SMB 2 / 3 par libsmb2 (LGPL-2.1, third_party/libsmb2).
// Les chemins vus par le moteur sont « /<partage>/<dossier>/… » (comme le dossier de départ réglé sur l'appareil) ;
// le premier élément est le partage auquel on se connecte, le reste est relatif à ce partage.
// Écriture : <fichier>.part, rendu visible par un renommage (comme le FTP) — jamais de fichier à moitié écrit.
#include "store/smb.hpp"

#include <fcntl.h>

#include <algorithm>
#include <cstring>

#include "core/fs.hpp"
#include "core/util.hpp"

extern "C" {
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
}

namespace pbc::store {

namespace {

std::string share_of(const std::string& root) {  // « /Partage/Jeux » → « Partage »
    std::string r = root;
    while (!r.empty() && r[0] == '/') r.erase(0, 1);
    const auto s = r.find('/');
    return s == std::string::npos ? r : r.substr(0, s);
}

class Smb final : public Store {
public:
    explicit Smb(FtpConfig cfg) : cfg_(std::move(cfg)), share_(share_of(cfg_.root)) {}
    ~Smb() override {
        if (ctx_) { if (connected_) smb2_disconnect_share(ctx_); smb2_destroy_context(ctx_); }
    }

    bool open(std::string& err) {
        if (share_.empty()) { err = "partage manquant : indiquez-le comme premier dossier (ex. /Partage)"; return false; }
        ctx_ = smb2_init_context();
        if (!ctx_) { err = "mémoire insuffisante (SMB)"; return false; }
        smb2_set_timeout(ctx_, 20);
        smb2_set_security_mode(ctx_, SMB2_NEGOTIATE_SIGNING_ENABLED);
        if (!cfg_.user.empty() && cfg_.user != "anonymous") smb2_set_user(ctx_, cfg_.user.c_str());
        if (!cfg_.password.empty()) smb2_set_password(ctx_, cfg_.password.c_str());
        if (!cfg_.domain.empty()) smb2_set_domain(ctx_, cfg_.domain.c_str());
        const std::string server = cfg_.port && cfg_.port != 445 ? cfg_.host + ":" + std::to_string(cfg_.port) : cfg_.host;
        if (smb2_connect_share(ctx_, server.c_str(), share_.c_str(), cfg_.user.empty() || cfg_.user == "anonymous" ? nullptr : cfg_.user.c_str()) != 0) {
            err = std::string("connexion SMB impossible : ") + smb2_get_error(ctx_);
            return false;
        }
        connected_ = true;
        return true;
    }

    std::string label() const override { return "smb://" + cfg_.host + "/" + share_; }

    bool mkdirs(const std::string& dir) override {
        const std::string rel = to_rel(dir);
        if (rel.empty()) return true;
        std::string cur;
        for (std::size_t i = 0; i <= rel.size(); ++i) {
            if (i == rel.size() || rel[i] == '\\') {
                if (!cur.empty()) {
                    struct smb2_stat_64 st{};
                    if (smb2_stat(ctx_, cur.c_str(), &st) != 0 && smb2_mkdir(ctx_, cur.c_str()) != 0) {
                        if (smb2_stat(ctx_, cur.c_str(), &st) != 0) return false;  // créé entre-temps ? sinon refus
                    }
                }
            }
            if (i < rel.size()) cur += rel[i];
        }
        return true;
    }

    struct W final : Writer {
        Smb& s;
        std::string path, part;
        struct smb2fh* fh = nullptr;
        bool ok = true;
        W(Smb& st, std::string p) : s(st), path(std::move(p)), part(path + ".part") {}
        ~W() override { if (fh) abort(); }
        bool write(const char* p, std::size_t n) override {
            const std::size_t chunk = std::max<std::size_t>(65536, static_cast<std::size_t>(smb2_get_max_write_size(s.ctx_)));
            while (ok && n) {
                const int w = smb2_write(s.ctx_, fh, reinterpret_cast<const std::uint8_t*>(p), static_cast<std::uint32_t>(std::min(n, chunk)));
                if (w <= 0) { ok = false; break; }
                p += w; n -= static_cast<std::size_t>(w);
            }
            return ok;
        }
        bool commit(std::string& err) override {
            if (!fh) { err = "fichier fermé"; return false; }
            const bool closed = smb2_close(s.ctx_, fh) == 0;
            fh = nullptr;
            if (!ok || !closed) { smb2_unlink(s.ctx_, part.c_str()); err = std::string("écriture SMB interrompue : ") + smb2_get_error(s.ctx_); return false; }
            struct smb2_stat_64 st{};
            if (smb2_stat(s.ctx_, path.c_str(), &st) == 0) smb2_unlink(s.ctx_, path.c_str());  // remplacement (fichier de la même copie)
            if (smb2_rename(s.ctx_, part.c_str(), path.c_str()) != 0) { err = std::string("renommage SMB impossible : ") + smb2_get_error(s.ctx_); return false; }
            return true;
        }
        void abort() override {
            if (fh) { smb2_close(s.ctx_, fh); fh = nullptr; }
            smb2_unlink(s.ctx_, part.c_str());
        }
    };

    std::unique_ptr<Writer> create(const std::string& path, std::string& err) override {
        const std::string rel = to_rel(path);
        mkdirs(std::string(fs::dirname(path)));
        auto w = std::make_unique<W>(*this, rel);
        w->fh = smb2_open(ctx_, w->part.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if (!w->fh) { err = std::string("création SMB impossible : ") + smb2_get_error(ctx_); return nullptr; }
        return w;
    }

    struct R final : Reader {
        Smb& s;
        struct smb2fh* fh = nullptr;
        explicit R(Smb& st) : s(st) {}
        ~R() override { if (fh) smb2_close(s.ctx_, fh); }
        long read(char* p, std::size_t n) override {
            const std::size_t chunk = std::max<std::size_t>(65536, static_cast<std::size_t>(smb2_get_max_read_size(s.ctx_)));
            const int r = smb2_read(s.ctx_, fh, reinterpret_cast<std::uint8_t*>(p), static_cast<std::uint32_t>(std::min(n, chunk)));
            return r < 0 ? -1 : r;
        }
    };

    std::unique_ptr<Reader> open(const std::string& path, std::string& err) override { return open_at(path, 0, err); }
    std::unique_ptr<Reader> open_at(const std::string& path, std::uint64_t offset, std::string& err) override {
        auto r = std::make_unique<R>(*this);
        r->fh = smb2_open(ctx_, to_rel(path).c_str(), O_RDONLY);
        if (!r->fh) { err = std::string("lecture SMB impossible : ") + smb2_get_error(ctx_); return nullptr; }
        if (offset) {
            std::uint64_t cur = 0;
            if (smb2_lseek(ctx_, r->fh, static_cast<std::int64_t>(offset), SEEK_SET, &cur) < 0) { err = "position SMB impossible"; return nullptr; }
        }
        return r;
    }

    bool read_small(const std::string& path, std::string& out, std::uint64_t limit) override {
        std::string err;
        auto r = open(path, err);
        if (!r) return false;
        out.clear();
        char buf[65536];
        for (long n; (n = r->read(buf, sizeof buf)) > 0;) {
            out.append(buf, static_cast<std::size_t>(n));
            if (out.size() > limit) return false;
        }
        return true;
    }

    bool write_small(const std::string& path, std::string_view data) override {
        std::string err;
        auto w = create(path, err);
        return w && w->write(data.data(), data.size()) && w->commit(err);
    }

    bool stat(const std::string& p, fs::Entry& out) override {
        struct smb2_stat_64 st{};
        if (smb2_stat(ctx_, to_rel(p).c_str(), &st) != 0) return false;
        out = {};
        out.name = std::string(fs::basename(p));
        out.dir = st.smb2_type == SMB2_TYPE_DIRECTORY;
        out.size = st.smb2_size;
        out.mtime = static_cast<std::int64_t>(st.smb2_mtime);
        return true;
    }

    bool list(const std::string& dir, std::vector<fs::Entry>& out) override {
        out.clear();
        struct smb2dir* d = smb2_opendir(ctx_, to_rel(dir).c_str());
        if (!d) return false;
        while (struct smb2dirent* e = smb2_readdir(ctx_, d)) {
            if (!std::strcmp(e->name, ".") || !std::strcmp(e->name, "..")) continue;
            fs::Entry x;
            x.name = e->name;
            x.dir = e->st.smb2_type == SMB2_TYPE_DIRECTORY;
            x.size = e->st.smb2_size;
            x.mtime = static_cast<std::int64_t>(e->st.smb2_mtime);
            out.push_back(x);
        }
        smb2_closedir(ctx_, d);
        std::sort(out.begin(), out.end(), [](const fs::Entry& a, const fs::Entry& b) { return a.name < b.name; });
        return true;
    }

    bool space(const std::string& p, fs::Space& out) override {
        struct smb2_statvfs v{};
        if (smb2_statvfs(ctx_, to_rel(p).c_str(), &v) != 0) return false;
        out.free = static_cast<std::uint64_t>(v.f_bavail) * v.f_bsize;
        out.total = static_cast<std::uint64_t>(v.f_blocks) * v.f_bsize;
        return true;
    }

    bool remove(const std::string& p) override {
        const std::string rel = to_rel(p);
        struct smb2_stat_64 st{};
        if (smb2_stat(ctx_, rel.c_str(), &st) != 0) return false;
        if (st.smb2_type == SMB2_TYPE_DIRECTORY) {
            std::vector<fs::Entry> l;
            if (list(p, l)) for (const auto& e : l) remove(p + "/" + e.name);
            return smb2_rmdir(ctx_, rel.c_str()) == 0;
        }
        return smb2_unlink(ctx_, rel.c_str()) == 0;
    }

    bool rename(const std::string& from, const std::string& to) override {
        return smb2_rename(ctx_, to_rel(from).c_str(), to_rel(to).c_str()) == 0;
    }

private:
    // « /Partage/Mes Jeux/PBC Backup/x » → « Mes Jeux/PBC Backup/x » (relatif au partage)
    std::string to_rel(const std::string& p) const {
        std::string r = p;
        while (!r.empty() && r[0] == '/') r.erase(0, 1);
        if (r == share_) return "";
        if (r.rfind(share_ + "/", 0) == 0) r = r.substr(share_.size() + 1);
        while (!r.empty() && r.back() == '/') r.pop_back();
        std::replace(r.begin(), r.end(), '/', '\\');  // séparateur SMB (libsmb2 ne le convertit que pour le renommage)
        return r;
    }

    FtpConfig cfg_;
    std::string share_;
    struct smb2_context* ctx_ = nullptr;
    bool connected_ = false;
};

}  // namespace

std::unique_ptr<Store> smb(const FtpConfig& cfg, std::string& err) {
    auto s = std::make_unique<Smb>(cfg);
    if (!s->open(err)) { log("smb open failed host=%s error=%s", cfg.host.c_str(), err.c_str()); return nullptr; }
    return s;
}

}  // namespace pbc::store
