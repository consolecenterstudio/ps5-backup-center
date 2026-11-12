#include "store/peer.hpp"

#include <cstring>

#include "core/json.hpp"
#include "link/link.hpp"

namespace pbc::store {

bool parse_peer_id(const std::string& id, std::string& instance, std::string& place) {
    if (id.rfind("peer:", 0) != 0) return false;
    const auto c = id.find(':', 5);
    if (c == std::string::npos) return false;
    instance = id.substr(5, c - 5);
    place = id.substr(c + 1);
    return !instance.empty() && !place.empty();
}

namespace {

std::string rel_of(const std::string& path) {  // « /bundle/x » → « bundle/x »
    std::size_t i = 0;
    while (i < path.size() && path[i] == '/') ++i;
    return path.substr(i);
}

class PeerStore final : public Store {
public:
    PeerStore(link::Link& l, std::string inst, std::string place, std::string label)
        : link_(l), inst_(std::move(inst)), place_(std::move(place)), label_(std::move(label)) {}

    std::string label() const override { return label_; }

    bool op(const char* what, const std::string& path, Json& out, const std::string& to = "") {
        Json b = Json::object();
        b.set("op", what);
        b.set("place", place_);
        b.set("rel", rel_of(path));
        if (!to.empty()) b.set("to", rel_of(to));
        std::string err;
        return link_.call(inst_, "POST", "/api/v2/link/fs", b, out, err);
    }

    bool mkdirs(const std::string& dir) override { Json o; return op("mkdirs", dir, o); }

    struct W final : Writer {
        PeerStore& s;
        std::string rel;
        std::string buf;
        std::uint64_t off = 0;
        bool ok = true, done = false;
        W(PeerStore& st, std::string r) : s(st), rel(std::move(r)) {}
        ~W() override { if (!done) abort(); }
        bool flush() {
            if (buf.empty() && off) return ok;
            std::string err;
            ok = ok && s.link_.put_chunk(s.inst_, s.place_, rel, off, buf, err);
            off += buf.size();
            buf.clear();
            return ok;
        }
        bool write(const char* p, std::size_t n) override {
            if (!ok) return false;
            buf.append(p, n);
            return buf.size() < (8u << 20) || flush();  // morceaux de 8 Mo
        }
        bool commit(std::string& err) override {
            done = true;
            if (!flush()) { err = "envoi à l'autre PS5 interrompu"; return false; }
            Json o;
            if (!s.op("commit", "/" + rel, o)) { err = "l'autre PS5 n'a pas validé le fichier"; return false; }
            return true;
        }
        void abort() override { done = true; Json o; s.op("abort", "/" + rel, o); }
    };

    std::unique_ptr<Writer> create(const std::string& path, std::string& err) override {
        const std::string r = rel_of(path);
        if (r.empty()) { err = "chemin invalide"; return nullptr; }
        return std::make_unique<W>(*this, r);
    }

    struct R final : Reader {
        PeerStore& s;
        std::string rel;
        std::uint64_t off;
        std::string buf;
        std::size_t pos = 0;
        bool eof = false;
        R(PeerStore& st, std::string r, std::uint64_t o) : s(st), rel(std::move(r)), off(o) {}
        long read(char* p, std::size_t n) override {
            if (pos >= buf.size()) {
                if (eof) return 0;
                buf.clear(); pos = 0;
                std::string err;
                if (!s.link_.get_chunk(s.inst_, s.place_, rel, off, 4u << 20, buf, err)) return -1;
                if (buf.empty()) { eof = true; return 0; }
                off += buf.size();
            }
            const std::size_t k = std::min(n, buf.size() - pos);
            std::memcpy(p, buf.data() + pos, k);
            pos += k;
            return static_cast<long>(k);
        }
    };

    std::unique_ptr<Reader> open(const std::string& path, std::string& err) override { return open_at(path, 0, err); }
    std::unique_ptr<Reader> open_at(const std::string& path, std::uint64_t offset, std::string&) override {
        return std::make_unique<R>(*this, rel_of(path), offset);
    }

    bool read_small(const std::string& path, std::string& out, std::uint64_t limit) override {
        std::string err;
        return link_.get_chunk(inst_, place_, rel_of(path), 0, limit, out, err);
    }
    bool write_small(const std::string& path, std::string_view data) override {
        std::string err;
        if (!link_.put_chunk(inst_, place_, rel_of(path), 0, std::string(data), err)) return false;
        Json o;
        return op("commit", path, o);
    }
    bool stat(const std::string& path, fs::Entry& e) override {
        Json o;
        if (!op("stat", path, o) || !o["exists"].as_bool()) return false;
        e.name = o["name"].as_string();
        e.dir = o["dir"].as_bool();
        e.size = static_cast<std::uint64_t>(o["size"].as_int());
        e.mtime = o["mtime"].as_int();
        return true;
    }
    bool list(const std::string& dir, std::vector<fs::Entry>& out) override {
        Json o;
        if (!op("list", dir, o) || !o["entries"].is_array()) return false;
        for (const auto& x : o["entries"].items()) {
            fs::Entry e;
            e.name = x["name"].as_string();
            e.dir = x["dir"].as_bool();
            e.size = static_cast<std::uint64_t>(x["size"].as_int());
            e.mtime = x["mtime"].as_int();
            out.push_back(e);
        }
        return true;
    }
    bool space(const std::string& path, fs::Space& sp) override {
        Json o;
        if (!op("space", path, o)) return false;
        sp.total = static_cast<std::uint64_t>(o["total"].as_int());
        sp.free = static_cast<std::uint64_t>(o["free"].as_int());
        return true;
    }
    bool remove(const std::string& path) override { Json o; return op("remove", path, o); }
    bool rename(const std::string& a, const std::string& b) override { Json o; return op("rename", a, o, b); }

private:
    link::Link& link_;
    std::string inst_, place_, label_;
};

}  // namespace

std::unique_ptr<Store> peer(link::Link& link, const std::string& instance, const std::string& place, const std::string& label) {
    return std::make_unique<PeerStore>(link, instance, place, label);
}

}  // namespace pbc::store
