#include "core/tls.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" {
#include "bearssl.h"
extern const br_x509_trust_anchor pbc_trust_anchors[];
extern const std::size_t pbc_trust_anchors_num;
}

namespace pbc::tls {

// Validateur « réseau local » : décode la clé du premier certificat (celui du serveur) sans chaîne
// d'autorité, et calcule son empreinte SHA-256.
struct LocalX509 {
    const br_x509_class* vtable;
    br_x509_decoder_context dc;
    br_sha256_context sha;
    unsigned char fp[32];
    int depth;
    bool ok;
};

struct Session {
    br_ssl_client_context cc;
    br_x509_minimal_context xc;
    LocalX509 local;
    br_sslio_context io;
    unsigned char buf[BR_SSL_BUFSIZE_BIDI];
    int fd = -1;
};

namespace {

int sock_read(void* ctx, unsigned char* buf, std::size_t len) {
    const int fd = *static_cast<int*>(ctx);
    for (;;) {
        const ssize_t n = ::recv(fd, buf, len, 0);
        if (n > 0) return static_cast<int>(n);
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
}

int sock_write(void* ctx, const unsigned char* buf, std::size_t len) {
    const int fd = *static_cast<int*>(ctx);
    for (;;) {
        const ssize_t n = ::send(fd, buf, len, 0);
        if (n > 0) return static_cast<int>(n);
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
}

void lx_start_chain(const br_x509_class** ctx, const char*) {
    auto* x = reinterpret_cast<LocalX509*>(ctx);
    x->depth = 0;
    x->ok = false;
}
void lx_start_cert(const br_x509_class** ctx, std::uint32_t) {
    auto* x = reinterpret_cast<LocalX509*>(ctx);
    if (x->depth == 0) { br_x509_decoder_init(&x->dc, nullptr, nullptr); br_sha256_init(&x->sha); }
}
void lx_append(const br_x509_class** ctx, const unsigned char* buf, std::size_t len) {
    auto* x = reinterpret_cast<LocalX509*>(ctx);
    if (x->depth == 0) { br_x509_decoder_push(&x->dc, buf, len); br_sha256_update(&x->sha, buf, len); }
}
void lx_end_cert(const br_x509_class** ctx) {
    auto* x = reinterpret_cast<LocalX509*>(ctx);
    if (x->depth == 0) { br_sha256_out(&x->sha, x->fp); x->ok = br_x509_decoder_last_error(&x->dc) == 0 && br_x509_decoder_get_pkey(&x->dc); }
    ++x->depth;
}
unsigned lx_end_chain(const br_x509_class** ctx) {
    return reinterpret_cast<LocalX509*>(ctx)->ok ? 0 : BR_ERR_X509_BAD_SERVER_NAME;
}
const br_x509_pkey* lx_get_pkey(const br_x509_class* const* ctx, unsigned* usages) {
    auto* x = reinterpret_cast<LocalX509*>(const_cast<br_x509_class**>(ctx));
    if (!x->ok) return nullptr;
    if (usages) *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    return br_x509_decoder_get_pkey(&x->dc);
}
const br_x509_class local_x509_vtable = {sizeof(LocalX509), lx_start_chain, lx_start_cert, lx_append, lx_end_cert, lx_end_chain, lx_get_pkey};

// Graine du générateur : /dev/urandom si présent, toujours complété par l'horloge et l'adresse.
void seed(br_ssl_engine_context* eng) {
    unsigned char s[48] = {};
    std::size_t got = 0;
    const int f = ::open("/dev/urandom", O_RDONLY);
    if (f >= 0) {
        const ssize_t n = ::read(f, s, 32);
        if (n > 0) got = static_cast<std::size_t>(n);
        ::close(f);
    }
    const auto t = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto sys = std::chrono::system_clock::now().time_since_epoch().count();
    const auto addr = reinterpret_cast<std::uintptr_t>(eng);
    std::memcpy(s + 32, &t, 8);
    std::memcpy(s + 40, &sys, 8);
    for (std::size_t i = 0; i < 8; ++i) s[i] ^= static_cast<unsigned char>(addr >> (i * 8));
    br_ssl_engine_inject_entropy(eng, s, sizeof s);
    (void)got;
}

}  // namespace

Session* open(int fd, const std::string& host, std::string& error, const Options& opt) {
    auto* s = new Session;
    s->fd = fd;
    br_ssl_client_init_full(&s->cc, &s->xc, pbc_trust_anchors, pbc_trust_anchors_num);
    if (!opt.verify) {
        s->local.vtable = &local_x509_vtable;
        br_ssl_engine_set_x509(&s->cc.eng, &s->local.vtable);
    }
    br_ssl_engine_set_buffer(&s->cc.eng, s->buf, sizeof s->buf, 1);
    seed(&s->cc.eng);
    if (opt.resume) {
        br_ssl_session_parameters p;
        br_ssl_engine_get_session_parameters(&opt.resume->cc.eng, &p);
        br_ssl_engine_set_session_parameters(&s->cc.eng, &p);
    }
    if (!br_ssl_client_reset(&s->cc, opt.verify ? host.c_str() : nullptr, opt.resume ? 1 : 0)) {
        error = "TLS : démarrage impossible (" + std::to_string(br_ssl_engine_last_error(&s->cc.eng)) + ")";
        delete s;
        return nullptr;
    }
    br_sslio_init(&s->io, &s->cc.eng, sock_read, &s->fd, sock_write, &s->fd);
    br_sslio_flush(&s->io);  // poignée de main
    const unsigned st = br_ssl_engine_current_state(&s->cc.eng);
    if (st == BR_SSL_CLOSED) {
        const int err = br_ssl_engine_last_error(&s->cc.eng);
        error = "TLS : connexion refusée avec " + host + " (erreur " + std::to_string(err) + (err >= 32 && err < 64 ? ", certificat" : "") + ")";
        delete s;
        return nullptr;
    }
    return s;
}

std::string fingerprint(const Session* s) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s->local.fp) { out += hex[c >> 4]; out += hex[c & 15]; }
    return out;
}

ssize_t read(Session* s, char* buf, std::size_t n) {
    const int r = br_sslio_read(&s->io, buf, n);
    if (r >= 0) return r;
    const int err = br_ssl_engine_last_error(&s->cc.eng);
    return err == BR_ERR_OK ? 0 : -1;  // fermeture propre = fin
}

bool write_all(Session* s, const char* buf, std::size_t n) {
    return br_sslio_write_all(&s->io, buf, n) == 0 && br_sslio_flush(&s->io) == 0;
}

void close(Session* s) {
    if (!s) return;
    br_sslio_close(&s->io);
    delete s;
}

}  // namespace pbc::tls
