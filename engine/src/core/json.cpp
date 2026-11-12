#include "core/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pbc {

namespace {
const Json& null_json() { static const Json n; return n; }

struct Parser {
    std::string_view s;
    std::size_t i = 0;
    int depth = 0;

    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool lit(std::string_view w) { if (s.substr(i, w.size()) != w) return false; i += w.size(); return true; }

    static void utf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    bool hex4(std::uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++]; v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10; else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
                case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
                case 'r': out += '\r'; break; case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp; if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00) {
                        std::uint32_t lo; if (!lit("\\u") || !hex4(lo) || lo < 0xDC00 || lo >= 0xE000) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, cp); break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool value(Json& out) {
        if (++depth > 64) return false;
        ws();
        if (i >= s.size()) return false;
        bool ok = true;
        char c = s[i];
        if (c == '{') {
            ++i; out = Json::object(); ws();
            if (i < s.size() && s[i] == '}') { ++i; }
            else for (;;) {
                std::string key; ws();
                if (!string(key)) { ok = false; break; }
                ws(); if (i >= s.size() || s[i] != ':') { ok = false; break; } ++i;
                Json v; if (!value(v)) { ok = false; break; }
                out.set(key, std::move(v)); ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                ok = false; break;
            }
        } else if (c == '[') {
            ++i; out = Json::array(); ws();
            if (i < s.size() && s[i] == ']') { ++i; }
            else for (;;) {
                Json v; if (!value(v)) { ok = false; break; }
                out.push(std::move(v)); ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                ok = false; break;
            }
        } else if (c == '"') {
            std::string str; ok = string(str); out = Json(std::move(str));
        } else if (lit("true")) out = Json(true);
        else if (lit("false")) out = Json(false);
        else if (lit("null")) out = Json();
        else {
            std::size_t start = i;
            if (i < s.size() && s[i] == '-') ++i;
            while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
            if (start == i) ok = false;
            else { std::string num(s.substr(start, i - start)); char* end = nullptr; double d = std::strtod(num.c_str(), &end); ok = end && *end == 0; out = Json(d); }
        }
        --depth;
        return ok;
    }
};

void escape(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break; case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += static_cast<char>(c);
        }
    }
    out += '"';
}
}  // namespace

const Json& Json::operator[](std::string_view key) const {
    if (type_ == Type::object) for (const auto& [k, v] : obj_) if (k == key) return v;
    return null_json();
}
const Json& Json::operator[](std::size_t i) const { return type_ == Type::array && i < arr_.size() ? arr_[i] : null_json(); }
bool Json::has(std::string_view key) const {
    if (type_ == Type::object) for (const auto& kv : obj_) if (kv.first == key) return true;
    return false;
}
Json& Json::set(std::string_view key, Json value) {
    if (type_ != Type::object) *this = object();
    for (auto& kv : obj_) if (kv.first == key) { kv.second = std::move(value); return kv.second; }
    obj_.emplace_back(std::string(key), std::move(value));
    return obj_.back().second;
}

void Json::dump_to(std::string& out, bool pretty, int depth) const {
    auto nl = [&](int d) { if (pretty) { out += '\n'; out.append(static_cast<std::size_t>(d) * 2, ' '); } };
    switch (type_) {
        case Type::null: out += "null"; break;
        case Type::boolean: out += bool_ ? "true" : "false"; break;
        case Type::number: {
            char b[32];
            if (std::isfinite(num_) && num_ == std::floor(num_) && std::fabs(num_) < 9e15) std::snprintf(b, sizeof b, "%lld", static_cast<long long>(num_));
            else std::snprintf(b, sizeof b, "%.17g", std::isfinite(num_) ? num_ : 0.0);
            out += b; break;
        }
        case Type::string: escape(out, str_); break;
        case Type::array:
            out += '[';
            for (std::size_t k = 0; k < arr_.size(); ++k) { if (k) out += ','; nl(depth + 1); arr_[k].dump_to(out, pretty, depth + 1); }
            if (!arr_.empty()) nl(depth);
            out += ']'; break;
        case Type::object:
            out += '{';
            for (std::size_t k = 0; k < obj_.size(); ++k) {
                if (k) out += ',';
                nl(depth + 1); escape(out, obj_[k].first); out += pretty ? ": " : ":";
                obj_[k].second.dump_to(out, pretty, depth + 1);
            }
            if (!obj_.empty()) nl(depth);
            out += '}'; break;
    }
}
std::string Json::dump(bool pretty) const { std::string out; dump_to(out, pretty, 0); return out; }

bool Json::parse(std::string_view text, Json& out) {
    Parser p{text};
    Json v;
    if (!p.value(v)) return false;
    p.ws();
    if (p.i != text.size()) return false;
    out = std::move(v);
    return true;
}

}  // namespace pbc
