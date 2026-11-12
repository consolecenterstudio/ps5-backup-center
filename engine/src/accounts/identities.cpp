#include "accounts/identities.hpp"

#include <algorithm>
#include <cctype>

#include "core/fs.hpp"

namespace pbc::accounts {

std::string Identities::normalize(std::string id) {
    if (id.rfind("0x", 0) == 0 || id.rfind("0X", 0) == 0) id = id.substr(2);
    for (auto& c : id) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (id.empty() || id.size() > 16 || id.find_first_not_of("0123456789ABCDEF") != std::string::npos) return "";
    while (id.size() < 16) id = "0" + id;
    return id == "0000000000000000" ? "" : id;
}

Identities::Identities(std::string host_path) : path_(std::move(host_path)) {
    std::string text;
    Json j;
    if (fs::read(path_, text) && Json::parse(text, j)) { merge(j); dirty_ = false; }
}

bool Identities::note(const std::string& account_id, const std::string& name, const std::string& user_id, std::int64_t when, const std::string& console) {
    const std::string id = normalize(account_id);
    if (id.empty()) return false;
    std::lock_guard<std::mutex> l(mu_);
    Identity& e = ids_[id];
    bool changed = false;
    // Le nom le plus récent l'emporte (un compte peut être renommé) ; un nom vide n'efface jamais un nom connu.
    if (!name.empty() && name != e.name && when >= e.last_seen) { e.name = name; changed = true; }
    if (!user_id.empty() && e.user_id.empty()) { e.user_id = user_id; changed = true; }
    if (!console.empty() && e.console.empty()) { e.console = console; changed = true; }
    if (when > 0 && (!e.first_seen || when < e.first_seen)) { e.first_seen = when; changed = true; }
    if (when > e.last_seen) { e.last_seen = when; changed = true; }
    if (changed) dirty_ = true;
    return changed;
}

bool Identities::merge(const Json& other) {
    bool changed = false;
    for (const auto& [id, v] : other["identities"].members())
    {
        const bool a = note(id, v["name"].as_string(), v["user_id"].as_string(), v["last_seen"].as_int(), v["console"].as_string());
        const bool b = note(id, "", "", v["first_seen"].as_int());
        changed = changed || a || b;
    }
    return changed;
}

std::string Identities::name(const std::string& account_id) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = ids_.find(normalize(account_id));
    return it == ids_.end() ? std::string() : it->second.name;
}

Json Identities::to_json() const {
    std::lock_guard<std::mutex> l(mu_);
    Json all = Json::object();
    for (const auto& [id, e] : ids_) {
        Json j = Json::object();
        j.set("name", e.name);
        j.set("user_id", e.user_id);
        j.set("console", e.console);
        j.set("first_seen", e.first_seen);
        j.set("last_seen", e.last_seen);
        all.set(id, j);
    }
    Json out = Json::object();
    out.set("format", "pbc-identities");
    out.set("version", static_cast<std::int64_t>(1));
    out.set("identities", all);
    return out;
}

bool Identities::save() {
    if (!dirty_) return true;
    if (!fs::write_atomic(path_, to_json().dump(true) + "\n")) return false;
    dirty_ = false;
    return true;
}

}  // namespace pbc::accounts
