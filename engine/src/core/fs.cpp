#include "core/fs.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pbc::fs {

namespace {
std::string g_root;
thread_local int g_error = 0;
bool fail() { g_error = errno; return false; }
}  // namespace

void set_host_root(std::string root) {
    while (!root.empty() && root.back() == '/') root.pop_back();
    g_root = std::move(root);
}
std::string host(std::string_view p) { return g_root + std::string(p); }
std::string console_path(std::string_view p) {
    if (!g_root.empty() && p.substr(0, g_root.size()) == g_root) p.remove_prefix(g_root.size());
    return std::string(p);
}
int last_error() { return g_error; }

std::string join(std::string_view a, std::string_view b) {
    std::string out(a);
    if (out.empty() || out.back() != '/') out += '/';
    while (!b.empty() && b.front() == '/') b.remove_prefix(1);
    out += b;
    return out;
}
std::string_view basename(std::string_view p) {
    while (p.size() > 1 && p.back() == '/') p.remove_suffix(1);
    auto k = p.rfind('/');
    return k == std::string_view::npos ? p : p.substr(k + 1);
}
std::string_view dirname(std::string_view p) {
    while (p.size() > 1 && p.back() == '/') p.remove_suffix(1);
    auto k = p.rfind('/');
    if (k == std::string_view::npos) return ".";
    return k == 0 ? std::string_view("/") : p.substr(0, k);
}

bool stat(const std::string& p, Entry& out) {
    struct ::stat st {};
    if (::stat(p.c_str(), &st) != 0) return fail();
    out.name = std::string(basename(p));
    out.dir = S_ISDIR(st.st_mode);
    out.size = static_cast<std::uint64_t>(st.st_size);
    out.mtime = static_cast<std::int64_t>(st.st_mtime);
    return true;
}
bool exists(const std::string& p) { struct ::stat st {}; return ::stat(p.c_str(), &st) == 0; }
bool is_dir(const std::string& p) { struct ::stat st {}; return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }

bool list(const std::string& dir, std::vector<Entry>& out) {
    out.clear();
    DIR* d = ::opendir(dir.c_str());
    if (!d) return fail();
    while (dirent* e = ::readdir(d)) {
        std::string_view n(e->d_name);
        if (n == "." || n == "..") continue;
        Entry en;
        if (stat(join(dir, n), en)) out.push_back(std::move(en));
    }
    ::closedir(d);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
    return true;
}

bool mkdirs(const std::string& dir) {
    if (dir.empty() || is_dir(dir)) return true;
    std::string parent(dirname(dir));
    if (parent != dir && !mkdirs(parent)) return false;
    if (::mkdir(dir.c_str(), 0777) != 0 && errno != EEXIST) return fail();
    return true;
}

bool read(const std::string& p, std::string& out, std::uint64_t limit) {
    out.clear();
    int fd = ::open(p.c_str(), O_RDONLY);
    if (fd < 0) return fail();
    char buf[65536];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) { if (errno == EINTR) continue; int e = errno; ::close(fd); errno = e; return fail(); }
        if (n == 0) break;
        if (out.size() + static_cast<std::size_t>(n) > limit) { ::close(fd); errno = EFBIG; return fail(); }
        out.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);
    return true;
}

bool write_atomic(const std::string& p, std::string_view data) {
    std::string dir(dirname(p));
    if (!mkdirs(dir)) return false;
    char suffix[32];
    std::snprintf(suffix, sizeof suffix, ".tmp-%d", static_cast<int>(::getpid()));
    std::string tmp = p + suffix;
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return fail();
    std::size_t done = 0;
    while (done < data.size()) {
        ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n < 0) { if (errno == EINTR) continue; int e = errno; ::close(fd); ::unlink(tmp.c_str()); errno = e; return fail(); }
        done += static_cast<std::size_t>(n);
    }
    if (::fsync(fd) != 0 || ::close(fd) != 0) { int e = errno; ::unlink(tmp.c_str()); errno = e; return fail(); }
    if (::rename(tmp.c_str(), p.c_str()) != 0) { int e = errno; ::unlink(tmp.c_str()); errno = e; return fail(); }
    int dfd = ::open(dir.c_str(), O_RDONLY);
    if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }
    return true;
}

bool remove_tree(const std::string& p) {
    struct ::stat st {};
    if (::lstat(p.c_str(), &st) != 0) return errno == ENOENT ? true : fail();
    if (S_ISDIR(st.st_mode)) {
        std::vector<Entry> entries;
        if (!list(p, entries)) return false;
        for (const auto& e : entries) if (!remove_tree(join(p, e.name))) return false;
        if (::rmdir(p.c_str()) != 0) return fail();
        return true;
    }
    if (::unlink(p.c_str()) != 0) return fail();
    return true;
}

bool space(const std::string& p, Space& out) {
    struct ::statfs s {};
    if (::statfs(p.c_str(), &s) != 0) return fail();
    out.free = static_cast<std::uint64_t>(s.f_bavail) * static_cast<std::uint64_t>(s.f_bsize);
    out.total = static_cast<std::uint64_t>(s.f_blocks) * static_cast<std::uint64_t>(s.f_bsize);
    return true;
}

}  // namespace pbc::fs
