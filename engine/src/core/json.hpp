// Valeur JSON minimale (sans exceptions) : lecture, écriture, accès typés.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pbc {

class Json {
public:
    enum class Type : std::uint8_t { null, boolean, number, string, array, object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::boolean), bool_(b) {}
    Json(int v) : type_(Type::number), num_(v) {}
    Json(std::int64_t v) : type_(Type::number), num_(static_cast<double>(v)) {}
    Json(std::uint64_t v) : type_(Type::number), num_(static_cast<double>(v)) {}
    Json(double v) : type_(Type::number), num_(v) {}
    Json(const char* s) : type_(Type::string), str_(s) {}
    Json(std::string s) : type_(Type::string), str_(std::move(s)) {}
    Json(std::string_view s) : type_(Type::string), str_(s) {}

    static Json array() { Json j; j.type_ = Type::array; return j; }
    static Json object() { Json j; j.type_ = Type::object; return j; }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::null; }
    bool is_object() const { return type_ == Type::object; }
    bool is_array() const { return type_ == Type::array; }
    bool is_string() const { return type_ == Type::string; }
    bool is_number() const { return type_ == Type::number; }

    // Accès tolérants : valeur par défaut si le type ne correspond pas.
    bool as_bool(bool d = false) const { return type_ == Type::boolean ? bool_ : d; }
    double as_number(double d = 0) const { return type_ == Type::number ? num_ : d; }
    std::int64_t as_int(std::int64_t d = 0) const { return type_ == Type::number ? static_cast<std::int64_t>(num_) : d; }
    const std::string& as_string() const { static const std::string empty; return type_ == Type::string ? str_ : empty; }

    // Objet
    const Json& operator[](std::string_view key) const;
    Json& set(std::string_view key, Json value);
    bool has(std::string_view key) const;
    const std::vector<std::pair<std::string, Json>>& members() const { return obj_; }

    // Tableau
    const Json& operator[](std::size_t i) const;
    Json& push(Json value) { if (type_ != Type::array) { *this = array(); } arr_.push_back(std::move(value)); return arr_.back(); }
    std::size_t size() const { return type_ == Type::array ? arr_.size() : type_ == Type::object ? obj_.size() : 0; }
    const std::vector<Json>& items() const { return arr_; }

    std::string dump(bool pretty = false) const;
    static bool parse(std::string_view text, Json& out);

private:
    void dump_to(std::string& out, bool pretty, int depth) const;
    Type type_ = Type::null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};

}  // namespace pbc
