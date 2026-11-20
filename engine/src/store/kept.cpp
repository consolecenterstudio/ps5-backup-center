#include "store/kept.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ctime>
#include <mutex>

#include "core/fs.hpp"

namespace pbc::store {

namespace {
std::mutex g_mu;
std::string g_dir;
constexpr std::size_t kTail = 65536;
constexpr std::int64_t kMaxAge = 6 * 24 * 3600;
std::string file_of(const std::string& key) { return g_dir + "/" + key + ".json"; }
}  // namespace

void set_kept_dir(const std::string& host_dir) { std::lock_guard<std::mutex> l(g_mu); g_dir = host_dir; }
std::string kept_dir() { std::lock_guard<std::mutex> l(g_mu); return g_dir; }

std::string kept_key(const std::string& service, const std::string& account_secret, const std::string& path) {
    return sha256_hex(service + '\n' + account_secret + '\n' + path).substr(0, 40);
}

bool kept_save(const std::string& key, const Kept& k) {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_dir.empty() || k.have == 0 || k.session.empty()) return false;
    ::mkdir(g_dir.c_str(), 0700);
    Json j = Json::object();
    j.set("have", static_cast<std::int64_t>(k.have));
    j.set("total", static_cast<std::int64_t>(k.total));
    j.set("sha_state", k.sha_state);
    j.set("tail_sha", k.tail_sha);
    j.set("session", k.session);
    j.set("extra", k.extra);
    j.set("at", static_cast<std::int64_t>(std::time(nullptr)));
    const std::string body = j.dump(), f = file_of(key), tmp = f + ".tmp";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);  // la session vaut autorisation d'envoi
    if (fd < 0) return false;
    const bool ok = ::write(fd, body.data(), body.size()) == static_cast<ssize_t>(body.size());
    ::fsync(fd);
    ::close(fd);
    if (!ok || std::rename(tmp.c_str(), f.c_str()) != 0) { ::unlink(tmp.c_str()); return false; }
    return true;
}

bool kept_load(const std::string& key, Kept& k) {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_dir.empty()) return false;
    std::string body;
    if (!fs::read(file_of(key), body, 1u << 20)) return false;
    Json j;
    if (!Json::parse(body, j) || !j.is_object()) return false;
    const std::int64_t at = j["at"].as_int();
    if (at <= 0 || std::time(nullptr) - at > kMaxAge) { ::unlink(file_of(key).c_str()); return false; }
    k.have = static_cast<std::uint64_t>(j["have"].as_int());
    k.total = static_cast<std::uint64_t>(j["total"].as_int());
    k.sha_state = j["sha_state"].as_string();
    k.tail_sha = j["tail_sha"].as_string();
    k.session = j["session"].as_string();
    k.extra = j["extra"];
    return k.have > 0 && !k.session.empty() && !k.sha_state.empty();
}

void kept_drop(const std::string& key) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_dir.empty()) ::unlink(file_of(key).c_str());
}

void Acked::add(const char* p, std::size_t n) {
    sha_.add(p, n);
    if (tail_.empty() && n < kTail && n_ > 0 && !tail_sha_.empty()) tail_sha_.clear();  // fin d'avant inconnue : plus de comparaison possible
    n_ += n;
    if (n >= kTail) tail_.assign(p + n - kTail, kTail);
    else { tail_.append(p, n); if (tail_.size() > kTail) tail_.erase(0, tail_.size() - kTail); }
}

void Acked::fill(Kept& k) const {
    k.have = n_;
    k.sha_state = sha_.state();
    k.tail_sha = tail_.empty() ? tail_sha_ : sha256_hex(tail_);
}

bool Acked::restore(const Kept& k) {
    if (!sha_.restore(k.sha_state)) return false;
    n_ = k.have;
    tail_.clear();  // la fin n'est pas gardée en clair : celle d'avant sert tant qu'aucun nouveau morceau n'est accepté
    tail_sha_ = k.tail_sha;
    return true;
}

}  // namespace pbc::store
