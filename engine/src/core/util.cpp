#include "core/util.hpp"

#include <cstdarg>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#if defined(PBCK_PS5)
#include <sys/types.h>
#include <sys/sysctl.h>
extern "C" int sceKernelDebugOutText(int, const char*);
#endif

namespace pbc {

std::string firmware() {
    if (const char* e = std::getenv("PBC_FIRMWARE")) return e;
#if defined(PBCK_PS5)
    unsigned int v = 0;
    size_t n = sizeof v;
    if (sysctlbyname("kern.sdk_version", &v, &n, nullptr, 0) == 0 && v) {
        char t[16];
        std::snprintf(t, sizeof t, "%x.%02x", (v >> 24) & 0xff, (v >> 16) & 0xff);
        return t;
    }
#endif
    return "";
}

int firmware_number(const std::string& v) {
    const auto dot = v.find('.');
    if (dot == std::string::npos || dot == 0) return 0;
    return std::atoi(v.substr(0, dot).c_str()) * 100 + std::atoi(v.substr(dot + 1, 2).c_str());
}

void log_line(const char* line) {
#if defined(PBCK_PS5)
    sceKernelDebugOutText(0, line);
#else
    std::fputs(line, stderr);
#endif
}

void log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    char line[1040];
    std::snprintf(line, sizeof line, "PBC2 %s\n", buf);
    log_line(line);
}

std::string sha256_hex(std::string_view data) { Sha256 h; h.add(data.data(), data.size()); return h.hex(); }

std::string random_id(unsigned bytes) {
    unsigned char b[32] = {};
    if (bytes > sizeof b) bytes = sizeof b;
    int fd = ::open("/dev/urandom", O_RDONLY);
    bool ok = fd >= 0 && ::read(fd, b, bytes) == static_cast<ssize_t>(bytes);
    if (fd >= 0) ::close(fd);
    if (!ok) {  // repli : temps + pid, suffisant pour un identifiant local
        std::uint64_t x = static_cast<std::uint64_t>(std::time(nullptr)) ^ (static_cast<std::uint64_t>(::getpid()) << 32);
        for (unsigned i = 0; i < bytes; ++i) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; b[i] = static_cast<unsigned char>(x); }
    }
    static const char hexd[] = "0123456789abcdef";
    std::string out;
    for (unsigned i = 0; i < bytes; ++i) { out += hexd[b[i] >> 4]; out += hexd[b[i] & 15]; }
    return out;
}

std::int64_t now_unix() { return static_cast<std::int64_t>(std::time(nullptr)); }

std::string iso_time(std::int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm {};
    gmtime_r(&tt, &tm);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return b;
}

std::string safe_name(std::string_view s, std::size_t max) {
    std::string out;
    for (unsigned char c : s) {
        if (out.size() >= max) break;
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') out += '_';
        else out += static_cast<char>(c);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? std::string("element") : out;
}

int compare_versions(const std::string& a, const std::string& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && a[i] != '.') { if (a[i] >= '0' && a[i] <= '9') x = x * 10 + (a[i] - '0'); ++i; }
        while (j < b.size() && b[j] != '.') { if (b[j] >= '0' && b[j] <= '9') y = y * 10 + (b[j] - '0'); ++j; }
        if (x != y) return x < y ? -1 : 1;
        ++i; ++j;
    }
    return 0;
}

}  // namespace pbc
