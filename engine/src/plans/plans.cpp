#include "plans/plans.hpp"

#include <algorithm>

#include "core/fs.hpp"
#include "core/util.hpp"

namespace pbc::plans {

namespace {
const char* timing_name(Timing t) { return t == Timing::once ? "once" : t == Timing::daily ? "daily" : t == Timing::weekly ? "weekly" : "manual"; }
Timing timing_from(const std::string& s) { return s == "once" ? Timing::once : s == "daily" ? Timing::daily : s == "weekly" ? Timing::weekly : Timing::manual; }
}

Json Plan::to_json() const {
    Json j = Json::object();
    j.set("id", id);
    j.set("name", name);
    j.set("enabled", enabled);
    j.set("timing", timing_name(timing));
    j.set("minute_of_day", static_cast<std::int64_t>(minute_of_day));
    j.set("weekdays", static_cast<std::uint64_t>(weekdays));
    j.set("once_at", once_at);
    Json it = Json::array();
    for (const auto& k : items) it.push(k);
    j.set("items", it);
    j.set("rule", rule);
    j.set("rule_arg", rule_arg);
    Json ps = Json::array();
    for (const auto& p : parts) ps.push(p);
    j.set("parts", ps);
    j.set("place", place);
    j.set("keep", static_cast<std::int64_t>(keep));
    j.set("last_run", last_run);
    j.set("next_run", next_run);
    j.set("last_result", last_result);
    return j;
}

Plan Plan::from_json(const Json& j) {
    Plan p;
    p.id = j["id"].as_string();
    p.name = j["name"].as_string();
    p.enabled = j["enabled"].as_bool(true);
    p.timing = timing_from(j["timing"].as_string());
    p.minute_of_day = static_cast<int>(std::clamp<std::int64_t>(j["minute_of_day"].as_int(8 * 60), 0, 24 * 60 - 1));
    p.weekdays = static_cast<unsigned>(j["weekdays"].as_int(0)) & 127u;
    p.once_at = j["once_at"].as_int();
    for (const auto& k : j["items"].items()) p.items.push_back(k.as_string());
    p.rule = j["rule"].as_string();
    p.rule_arg = j["rule_arg"].as_string();
    for (const auto& x : j["parts"].items()) p.parts.push_back(x.as_string());
    p.place = j["place"].as_string();
    p.keep = static_cast<int>(std::clamp<std::int64_t>(j["keep"].as_int(5), 0, 100));
    p.last_run = j["last_run"].as_int();
    p.next_run = j["next_run"].as_int();
    p.last_result = j["last_result"].as_string();
    return p;
}

std::int64_t next_run(const Plan& p, std::int64_t after, std::int64_t off) {
    if (p.timing == Timing::manual) return 0;
    if (p.timing == Timing::once) return p.once_at > after ? p.once_at : 0;
    // Jour local courant à minuit (heure Unix), puis jours suivants.
    const std::int64_t local = after + off, day0 = local - ((local % 86400) + 86400) % 86400;
    for (int d = 0; d < 8; ++d) {
        const std::int64_t day = day0 + d * 86400LL;
        const int weekday = static_cast<int>(((day / 86400) + 3) % 7);  // 1970-01-01 était un jeudi → lundi = 0
        if (p.timing == Timing::weekly && !(p.weekdays & (1u << weekday))) continue;
        const std::int64_t at = day + p.minute_of_day * 60LL - off;
        if (at > after) return at;
    }
    return 0;
}

void Store::load() {
    std::lock_guard<std::mutex> l(mu_);
    list_.clear();
    std::string text;
    Json j;
    if (fs::read(path_, text) && Json::parse(text, j)) for (const auto& p : j.items()) list_.push_back(Plan::from_json(p));
}

bool Store::save() const {
    Json a = Json::array();
    for (const auto& p : list_) a.push(p.to_json());
    return fs::write_atomic(path_, a.dump(true) + "\n");
}

std::vector<Plan> Store::list() const { std::lock_guard<std::mutex> l(mu_); return list_; }

bool Store::get(const std::string& id, Plan& out) const {
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& p : list_) if (p.id == id) { out = p; return true; }
    return false;
}

std::string Store::put(Plan p) {
    std::lock_guard<std::mutex> l(mu_);
    if (p.id.empty()) p.id = "plan-" + random_id(4);
    bool replaced = false;
    for (auto& x : list_) if (x.id == p.id) { p.last_run = x.last_run; p.last_result = x.last_result; x = p; replaced = true; }
    if (!replaced) list_.push_back(p);
    save();
    return p.id;
}

bool Store::remove(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    const auto n = list_.size();
    std::erase_if(list_, [&](const Plan& p) { return p.id == id; });
    return list_.size() != n && save();
}

void Store::mark_run(const std::string& id, std::int64_t when, const std::string& result, std::int64_t off) {
    std::lock_guard<std::mutex> l(mu_);
    for (auto& p : list_)
        if (p.id == id) { p.last_run = when; p.last_result = result; p.next_run = next_run(p, when, off); }
    save();
}

std::vector<Plan> Store::due(std::int64_t now) const {
    std::lock_guard<std::mutex> l(mu_);
    std::vector<Plan> out;
    for (const auto& p : list_) if (p.enabled && p.next_run && p.next_run <= now) out.push_back(p);
    return out;
}

void Store::refresh_next(std::int64_t now, std::int64_t off) {
    std::lock_guard<std::mutex> l(mu_);
    bool changed = false;
    for (auto& p : list_) {
        const std::int64_t n = p.next_run && p.next_run <= now && p.enabled ? p.next_run : next_run(p, std::max(now, p.last_run), off);
        if (n != p.next_run) { p.next_run = n; changed = true; }
    }
    if (changed) save();
}

}  // namespace pbc::plans
