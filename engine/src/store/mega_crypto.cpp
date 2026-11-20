#include "store/mega_crypto.hpp"

#include <cstring>

extern "C" {
#include "bearssl.h"
// Entiers de BearSSL (br_i31_*, compilés avec BearSSL ; « inner.h » ne se lit pas en C++) : le RSA de MEGA.
void br_i31_decode(uint32_t* x, const void* src, size_t len);
uint32_t br_i31_decode_mod(uint32_t* x, const void* src, size_t len, const uint32_t* m);
void br_i31_decode_reduce(uint32_t* x, const void* src, size_t len, const uint32_t* m);
void br_i31_encode(void* dst, size_t len, const uint32_t* x);
uint32_t br_i31_bit_length(uint32_t* x, size_t xlen);
uint32_t br_i31_ninv31(uint32_t x);
uint32_t br_i31_modpow_opt(uint32_t* x, const unsigned char* e, size_t elen, const uint32_t* m, uint32_t m0i, uint32_t* tmp, size_t twlen);
void br_i31_mulacc(uint32_t* d, const uint32_t* a, const uint32_t* b);
}

namespace pbc::store::mega {

namespace {
void br_i31_zero(uint32_t* x, uint32_t bit_len) { *x++ = bit_len; std::memset(x, 0, ((bit_len + 31) >> 5) * sizeof *x); }
const char* const kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
int b64v(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-' || c == '+') return 62;
    if (c == '_' || c == '/') return 63;
    return -1;
}
// Un bloc AES chiffré seul (ECB) : CBC avec un vecteur nul sur 16 octets.
void enc_block(const br_aes_ct64_cbcenc_keys& k, unsigned char* b) {
    unsigned char iv[16] = {0};
    br_aes_ct64_cbcenc_run(&k, iv, b, 16);
}
void xor16(unsigned char* a, const unsigned char* b) { for (int i = 0; i < 16; ++i) a[i] ^= b[i]; }
}  // namespace

std::string b64url_encode(std::string_view in) {
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) | static_cast<unsigned char>(in[i + 2]);
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if (i + 1 == in.size()) {
        const unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63];
    } else if (i + 2 == in.size()) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63];
    }
    return out;
}

bool b64url_decode(std::string_view in, Bytes& out) {
    out.clear();
    unsigned v = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == ',') break;
        const int d = b64v(c);
        if (d < 0) return false;
        v = (v << 6) | static_cast<unsigned>(d);
        bits += 6;
        if (bits >= 8) { bits -= 8; out += static_cast<char>((v >> bits) & 0xFF); }
    }
    return true;
}

bool aes_ecb_encrypt(const Bytes& key, Bytes& data) {
    if (key.size() != 16 || data.size() % 16) return false;
    br_aes_ct64_cbcenc_keys k;
    br_aes_ct64_cbcenc_init(&k, key.data(), 16);
    for (std::size_t i = 0; i < data.size(); i += 16) enc_block(k, reinterpret_cast<unsigned char*>(&data[i]));
    return true;
}

bool aes_ecb_decrypt(const Bytes& key, Bytes& data) {
    if (key.size() != 16 || data.size() % 16) return false;
    br_aes_ct64_cbcdec_keys k;
    br_aes_ct64_cbcdec_init(&k, key.data(), 16);
    for (std::size_t i = 0; i < data.size(); i += 16) {
        unsigned char iv[16] = {0};
        br_aes_ct64_cbcdec_run(&k, iv, &data[i], 16);
    }
    return true;
}

bool aes_cbc_encrypt0(const Bytes& key, Bytes& data) {
    if (key.size() != 16 || data.size() % 16) return false;
    br_aes_ct64_cbcenc_keys k;
    br_aes_ct64_cbcenc_init(&k, key.data(), 16);
    unsigned char iv[16] = {0};
    if (!data.empty()) br_aes_ct64_cbcenc_run(&k, iv, data.data(), data.size());
    return true;
}

bool aes_cbc_decrypt0(const Bytes& key, Bytes& data) {
    if (key.size() != 16 || data.size() % 16) return false;
    br_aes_ct64_cbcdec_keys k;
    br_aes_ct64_cbcdec_init(&k, key.data(), 16);
    unsigned char iv[16] = {0};
    if (!data.empty()) br_aes_ct64_cbcdec_run(&k, iv, data.data(), data.size());
    return true;
}

void aes_ctr(const Bytes& key, const Bytes& nonce8, std::uint64_t offset, char* data, std::size_t n) {
    if (key.size() != 16 || nonce8.size() != 8 || !n) return;
    br_aes_ct64_ctr_keys k;
    br_aes_ct64_ctr_init(&k, key.data(), 16);
    auto iv_for = [&](std::uint64_t block, unsigned char* iv12) {
        std::memcpy(iv12, nonce8.data(), 8);
        const std::uint32_t hi = static_cast<std::uint32_t>(block >> 32);
        iv12[8] = static_cast<unsigned char>(hi >> 24); iv12[9] = static_cast<unsigned char>(hi >> 16);
        iv12[10] = static_cast<unsigned char>(hi >> 8); iv12[11] = static_cast<unsigned char>(hi);
    };
    // Début au milieu d'un bloc : on chiffre un bloc entier à part et on garde la bonne partie.
    if (offset % 16) {
        const std::uint64_t block = offset / 16;
        const std::size_t skip = static_cast<std::size_t>(offset % 16), take = std::min<std::size_t>(16 - skip, n);
        unsigned char buf[16] = {0}, iv[12];
        std::memcpy(buf + skip, data, take);
        iv_for(block, iv);
        br_aes_ct64_ctr_run(&k, iv, static_cast<std::uint32_t>(block), buf, 16);
        std::memcpy(data, buf + skip, take);
        data += take; n -= take; offset += take;
    }
    while (n) {
        const std::uint64_t block = offset / 16;
        const std::uint32_t lo = static_cast<std::uint32_t>(block);
        // Le compteur de BearSSL tient sur 32 bits : on s'arrête avant qu'il ne fasse le tour.
        const std::uint64_t room = (0x100000000ULL - lo) * 16;
        const std::size_t len = static_cast<std::size_t>(std::min<std::uint64_t>(n, room));
        unsigned char iv[12];
        iv_for(block, iv);
        br_aes_ct64_ctr_run(&k, iv, lo, data, len);
        data += len; n -= len; offset += len;
    }
}

void ChunkMac::start(const Bytes& file_key, const Bytes& nonce8) {
    key = file_key;
    iv = nonce8 + nonce8;
    mac = iv;
    pending.clear();
}

void ChunkMac::update(const char* p, std::size_t n) {
    br_aes_ct64_cbcenc_keys k;
    br_aes_ct64_cbcenc_init(&k, key.data(), 16);
    auto block = [&](const unsigned char* b) {
        xor16(reinterpret_cast<unsigned char*>(&mac[0]), b);
        enc_block(k, reinterpret_cast<unsigned char*>(&mac[0]));
    };
    if (!pending.empty()) {
        const std::size_t take = std::min(16 - pending.size(), n);
        pending.append(p, take); p += take; n -= take;
        if (pending.size() < 16) return;
        block(reinterpret_cast<const unsigned char*>(pending.data()));
        pending.clear();
    }
    while (n >= 16) { block(reinterpret_cast<const unsigned char*>(p)); p += 16; n -= 16; }
    if (n) pending.assign(p, n);
}

Bytes ChunkMac::finish() {
    if (!pending.empty()) {
        pending.resize(16, '\0');
        br_aes_ct64_cbcenc_keys k;
        br_aes_ct64_cbcenc_init(&k, key.data(), 16);
        xor16(reinterpret_cast<unsigned char*>(&mac[0]), reinterpret_cast<const unsigned char*>(pending.data()));
        enc_block(k, reinterpret_cast<unsigned char*>(&mac[0]));
        pending.clear();
    }
    return mac;
}

std::vector<std::uint64_t> chunk_bounds(std::uint64_t size) {
    std::vector<std::uint64_t> starts;
    std::uint64_t at = 0;
    for (std::uint64_t i = 1; at < size; ++i) {
        starts.push_back(at);
        at += (i < 8 ? i : 8) * 131072;
    }
    if (starts.empty()) starts.push_back(0);
    return starts;
}

Bytes file_meta_mac(const Bytes& file_key, const std::vector<Bytes>& chunk_macs) {
    br_aes_ct64_cbcenc_keys k;
    br_aes_ct64_cbcenc_init(&k, file_key.data(), 16);
    unsigned char mac[16] = {0};
    for (const auto& cm : chunk_macs) { xor16(mac, reinterpret_cast<const unsigned char*>(cm.data())); enc_block(k, mac); }
    Bytes out(8, '\0');
    for (int i = 0; i < 4; ++i) { out[i] = static_cast<char>(mac[i] ^ mac[i + 4]); out[i + 4] = static_cast<char>(mac[i + 8] ^ mac[i + 12]); }
    return out;
}

Bytes pbkdf2_sha512(std::string_view password, const Bytes& salt, unsigned iterations, std::size_t len) {
    br_hmac_key_context kc;
    br_hmac_key_init(&kc, &br_sha512_vtable, password.data(), password.size());
    Bytes out;
    for (std::uint32_t blk = 1; out.size() < len; ++blk) {
        unsigned char u[64], t[64];
        br_hmac_context hc;
        br_hmac_init(&hc, &kc, 0);
        br_hmac_update(&hc, salt.data(), salt.size());
        const unsigned char be[4] = {static_cast<unsigned char>(blk >> 24), static_cast<unsigned char>(blk >> 16), static_cast<unsigned char>(blk >> 8), static_cast<unsigned char>(blk)};
        br_hmac_update(&hc, be, 4);
        br_hmac_out(&hc, u);
        std::memcpy(t, u, 64);
        for (unsigned i = 1; i < iterations; ++i) {
            br_hmac_init(&hc, &kc, 0);
            br_hmac_update(&hc, u, 64);
            br_hmac_out(&hc, u);
            for (int j = 0; j < 64; ++j) t[j] ^= u[j];
        }
        out.append(reinterpret_cast<char*>(t), 64);
    }
    out.resize(len);
    return out;
}

bool read_mpi(const Bytes& in, std::size_t& at, Bytes& out) {
    if (at + 2 > in.size()) return false;
    const std::size_t bits = (static_cast<unsigned char>(in[at]) << 8) | static_cast<unsigned char>(in[at + 1]);
    const std::size_t len = (bits + 7) / 8;
    at += 2;
    if (at + len > in.size()) return false;
    out = in.substr(at, len);
    at += len;
    return true;
}

bool rsa_decrypt_mpi(const Bytes& privk, const Bytes& c_mpi, Bytes& out) {
    std::size_t at = 0;
    Bytes p, q, d, u, c;
    if (!read_mpi(privk, at, p) || !read_mpi(privk, at, q) || !read_mpi(privk, at, d) || !read_mpi(privk, at, u)) return false;
    at = 0;
    if (!read_mpi(c_mpi, at, c)) return false;
    if (p.size() > 300 || q.size() > 300 || d.size() > 600) return false;  // clés de 4096 bits au plus
    static constexpr std::size_t W = 160;  // mots de 31 bits : 4960 bits
    std::uint32_t xp[W] = {0}, xq[W] = {0}, n[W] = {0}, x[W] = {0}, tmp[W * 6] = {0};
    br_i31_decode(xp, p.data(), p.size());
    br_i31_decode(xq, q.data(), q.size());
    // n = p × q (BearSSL : n démarre avec la longueur annoncée de p, puis on garde sa longueur réelle).
    br_i31_zero(n, xp[0]);
    br_i31_mulacc(n, xp, xq);
    n[0] = br_i31_bit_length(n + 1, (n[0] + 31) >> 5);
    if (!br_i31_decode_mod(x, c.data(), c.size(), n)) {  // c ≥ n : on réduit
        br_i31_decode_reduce(x, c.data(), c.size(), n);
    }
    const std::uint32_t m0i = br_i31_ninv31(n[1]);
    if (!br_i31_modpow_opt(x, reinterpret_cast<const unsigned char*>(d.data()), d.size(), n, m0i, tmp, sizeof tmp / sizeof tmp[0])) return false;
    const std::size_t nbytes = ((n[0] - (n[0] >> 5)) + 7) >> 3;
    Bytes m(nbytes, '\0');
    br_i31_encode(&m[0], nbytes, x);
    std::size_t z = 0;
    while (z + 1 < m.size() && m[z] == 0) ++z;  // sans les zéros de tête (comme le fait MEGA)
    out = m.substr(z);
    return true;
}

}  // namespace pbc::store::mega
