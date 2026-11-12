#include "places/places.hpp"

#include <cstdio>
#include <sys/stat.h>

#include "core/fs.hpp"
#include "core/util.hpp"

namespace pbc::places {

namespace {
bool read_marker(const std::string& mount, std::string& id) {
    std::string text;
    Json j;
    if (!fs::read(fs::host(fs::join(mount, marker_name)), text, 4096) || !Json::parse(text, j)) return false;
    id = j["disk_id"].as_string();
    return !id.empty();
}
void fill_space(Place& p) {
    fs::Space s;
    if (fs::space(fs::host(p.mount), s)) { p.free = s.free; p.total = s.total; }
}
}  // namespace

std::vector<Place> detect() {
    std::vector<Place> out;
    Place internal;
    internal.id = "internal";
    internal.type = Place::Type::internal;
    internal.mount = "/data";
    internal.label = "Interne";
    fill_space(internal);
    out.push_back(internal);

    for (int n = 0; n < 8; ++n) {
        char mount[32];
        std::snprintf(mount, sizeof mount, "/mnt/usb%d", n);
        std::vector<fs::Entry> entries;
        if (!fs::is_dir(fs::host(mount)) || !fs::list(fs::host(mount), entries)) continue;
        Place p;
        p.type = Place::Type::usb;
        p.mount = mount;
        char label[16];
        std::snprintf(label, sizeof label, "USB %d", n);
        p.label = label;
        // Sans témoin, l'identifiant reste provisoire (lié au montage) jusqu'au premier usage.
        if (!read_marker(mount, p.id)) p.id = std::string("mount:") + mount;
        // Point de montage vide : même périphérique que /mnt (aucun disque branché).
        struct ::stat parent {}, self {};
        if (::stat(fs::host("/mnt").c_str(), &parent) == 0 && ::stat(fs::host(mount).c_str(), &self) == 0 && parent.st_dev == self.st_dev &&
            fs::host("") .empty()) continue;
        fill_space(p);
        if (p.total == 0) continue;
        out.push_back(std::move(p));
    }
    return out;
}

bool ensure_marker(Place& place) {
    if (place.type == Place::Type::internal) return true;
    std::string id;
    if (read_marker(place.mount, id)) { place.id = id; return true; }
    Json j = Json::object();
    id = "disk-" + random_id(8);
    j.set("disk_id", id);
    j.set("created", iso_time(now_unix()));
    j.set("by", "PS5 Backup Center");
    if (!fs::write_atomic(fs::host(fs::join(place.mount, marker_name)), j.dump(true) + "\n")) return false;
    log("place marker created mount=%s disk_id=%s", place.mount.c_str(), id.c_str());
    place.id = id;
    return true;
}

bool backup_folder(const Place& place, const std::string& custom, std::string& out) {
    out = custom.empty() ? fs::join(place.mount, default_folder) : custom;
    return fs::mkdirs(fs::host(out));
}

const Place* find(const std::vector<Place>& all, const std::string& id) {
    for (const auto& p : all) if (p.id == id) return &p;
    return nullptr;
}

}  // namespace pbc::places
