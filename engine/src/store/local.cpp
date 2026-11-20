#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

#include "store/store.hpp"

namespace pbc::store {

namespace {

class LocalWriter : public Writer {
public:
    LocalWriter(std::string final_path, int fd) : final_(std::move(final_path)), tmp_(final_ + ".part"), fd_(fd) {}
    ~LocalWriter() override { abort(); }
    bool write(const char* p, std::size_t n) override {
        while (n) {
            const ssize_t w = ::write(fd_, p, n);
            if (w < 0) { if (errno == EINTR) continue; return false; }
            p += w;
            n -= static_cast<std::size_t>(w);
        }
        return true;
    }
    bool commit(std::string& err) override {
        if (fd_ < 0) { err = "déjà fermé"; return false; }
        const bool ok = ::fsync(fd_) == 0;
        ::close(fd_);
        fd_ = -1;
        if (!ok || std::rename(tmp_.c_str(), final_.c_str()) != 0) { err = "écriture finale impossible"; ::unlink(tmp_.c_str()); return false; }
        return true;
    }
    void abort() override { if (fd_ >= 0) { ::close(fd_); fd_ = -1; ::unlink(tmp_.c_str()); } }
    bool keep() override { if (fd_ < 0) return false; ::fsync(fd_); ::close(fd_); fd_ = -1; return true; }
private:
    std::string final_, tmp_;
    int fd_;
};

class LocalReader : public Reader {
public:
    explicit LocalReader(int fd) : fd_(fd) {}
    ~LocalReader() override { ::close(fd_); }
    long read(char* p, std::size_t n) override {
        for (;;) { const ssize_t r = ::read(fd_, p, n); if (r < 0 && errno == EINTR) continue; return static_cast<long>(r); }
    }
private:
    int fd_;
};

class Local : public Store {
public:
    std::string label() const override { return "disque de la console"; }
    bool mkdirs(const std::string& d) override { return fs::mkdirs(fs::host(d)); }
    std::unique_ptr<Writer> create(const std::string& p, std::string& err) override {
        const std::string h = fs::host(p);
        if (!fs::mkdirs(std::string(fs::dirname(h)))) { err = "dossier impossible à créer"; return nullptr; }
        const int fd = ::open((h + ".part").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) { err = "écriture impossible"; return nullptr; }
        return std::make_unique<LocalWriter>(h, fd);
    }
    std::unique_ptr<Writer> resume(const std::string& p, std::uint64_t& have, std::string& err) override {
        have = 0;
        const std::string h = fs::host(p);
        fs::Entry e;
        if (!fs::stat(h + ".part", e) || e.dir || e.size == 0) return nullptr;
        const int fd = ::open((h + ".part").c_str(), O_WRONLY | O_APPEND);
        if (fd < 0) { err = "reprise impossible"; return nullptr; }
        have = e.size;
        return std::make_unique<LocalWriter>(h, fd);
    }
    std::unique_ptr<Reader> open_partial(const std::string& p, std::string& err) override { return open(p + ".part", err); }
    std::unique_ptr<Reader> open(const std::string& p, std::string& err) override {
        const int fd = ::open(fs::host(p).c_str(), O_RDONLY);
        if (fd < 0) { err = "lecture impossible"; return nullptr; }
        return std::make_unique<LocalReader>(fd);
    }
    // Lecture à partir d'un décalage : un saut, pas une lecture de tout ce qui précède (l'en-tête d'un .pkg est parfois à la fin).
    std::unique_ptr<Reader> open_at(const std::string& p, std::uint64_t offset, std::string& err) override {
        const int fd = ::open(fs::host(p).c_str(), O_RDONLY);
        if (fd < 0) { err = "lecture impossible"; return nullptr; }
        if (offset && ::lseek(fd, static_cast<off_t>(offset), SEEK_SET) < 0) { ::close(fd); err = "lecture impossible"; return nullptr; }
        return std::make_unique<LocalReader>(fd);
    }
    bool read_small(const std::string& p, std::string& out, std::uint64_t limit) override { return fs::read(fs::host(p), out, limit); }
    bool write_small(const std::string& p, std::string_view data) override { return fs::write_atomic(fs::host(p), data); }
    bool stat(const std::string& p, fs::Entry& out) override { return fs::stat(fs::host(p), out); }
    bool list(const std::string& d, std::vector<fs::Entry>& out) override { return fs::list(fs::host(d), out); }
    bool space(const std::string& p, fs::Space& out) override { return fs::space(fs::host(p), out); }
    bool remove(const std::string& p) override { return fs::remove_tree(fs::host(p)); }
    bool rename(const std::string& a, const std::string& b) override { return ::rename(fs::host(a).c_str(), fs::host(b).c_str()) == 0; }
};

}  // namespace

std::unique_ptr<Store> local() { return std::make_unique<Local>(); }

}  // namespace pbc::store
