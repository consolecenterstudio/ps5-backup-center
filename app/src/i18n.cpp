#include "i18n.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <unistd.h>

#include "core/json.hpp"
#include "fonts.hpp"
#include "native_assets.hpp"  // glyphes de l'atlas de police actuel (voir tv_renderable)

namespace app {

namespace {

struct Catalog {
    std::string code;
    std::map<std::string, std::string, std::less<>> text;
};

std::string dir_, system_ = "fr", setting_ = "auto", forced_;
std::vector<i18n::Language> langs_;
std::mutex mu_;
std::map<std::string, std::unique_ptr<Catalog>> loaded_;  // jamais libérés : tr() renvoie des pointeurs dedans
std::atomic<const Catalog*> current_{nullptr};             // nul : français (la source)
std::atomic<const i18n::Language*> lang_{nullptr};
std::atomic<bool> fallback_{false};

const i18n::Language& french() {
    static const i18n::Language fr{"fr", "Français", "French", ',', false, true, "français"};
    return fr;
}

bool read_file(const std::string& path, std::string& out) {
    const int f = ::open(path.c_str(), O_RDONLY);
    if (f < 0) return false;
    char buf[16384];
    for (ssize_t n; (n = ::read(f, buf, sizeof buf)) > 0;) out.append(buf, static_cast<std::size_t>(n));
    ::close(f);
    return !out.empty();
}

unsigned next_rune(std::string_view s, std::size_t& i) {
    const auto c = static_cast<unsigned char>(s[i]);
    unsigned v = c, n = 1;
    if (c >= 0xF0) { v = c & 7; n = 4; } else if (c >= 0xE0) { v = c & 15; n = 3; } else if (c >= 0xC0) { v = c & 31; n = 2; }
    for (unsigned k = 1; k < n && i + k < s.size(); ++k) v = (v << 6) | (static_cast<unsigned char>(s[i + k]) & 63);
    i += n;
    return v;
}

// L'atlas de police actuel ne connaît que le latin (Latin-1) : le cyrillique, le grec, le chinois, le japonais,
// le coréen, l'arabe, et même le polonais ou le turc (ł, ş, ğ…) n'y sont pas. Une langue qu'il ne sait pas
// afficher reste proposée, mais la télé montre l'anglais à la place (le web, lui, affiche tout).
// À lever quand le moteur de texte TrueType (gfx.*) arrive : il suffira de renvoyer vrai ici.
bool tv_renderable(const Catalog& c) {
    // Les polices de secours (Inter étendu, arabe, chinois / japonais / coréen) sont chargées : la télé sait tout
    // afficher. Sinon (mémoire refusée au démarrage), on garde le test sur l'atlas latin.
    if (fonts::available(fonts::inter_medium) && fonts::available(fonts::arabic) && fonts::available(fonts::cjk_sc)) return true;
    static const std::set<unsigned> have = [] {
        std::set<unsigned> s;
        for (const auto& g : pbck::assets::glyphs) if (g.font == 1) s.insert(g.code);
        return s;
    }();
    std::size_t total = 0, missing = 0;
    for (const auto& [k, v] : c.text)
        for (std::size_t i = 0; i < v.size();) {
            const unsigned r = next_rune(v, i);
            if (r < 0x80 || (r >= 0x2000 && r < 0x2C00)) continue;  // ponctuation, flèches (« „ › ») : pas un critère de langue
            ++total;
            if (!have.count(r)) ++missing;
        }
    return missing * 200 <= total;  // un symbole isolé manquant ne condamne pas une langue entière
}

const Catalog* load(const std::string& code) {
    std::lock_guard<std::mutex> l(mu_);
    if (auto it = loaded_.find(code); it != loaded_.end()) return it->second.get();
    std::string text;
    pbc::Json j;
    if (!read_file(dir_ + "/" + code + ".json", text) || !pbc::Json::parse(text, j) || !j.is_object()) return nullptr;
    auto c = std::make_unique<Catalog>();
    c->code = code;
    for (const auto& [k, v] : j.members())
        if (!k.empty() && k[0] != '@' && v.is_string() && !v.as_string().empty()) c->text.emplace(k, v.as_string());
    return (loaded_[code] = std::move(c)).get();
}

// Choisit et charge la langue affichée pour un code voulu.
bool show(const std::string& want) {
    const i18n::Language* before = lang_.load();
    const i18n::Language* l = i18n::find(want);
    if (!l) l = i18n::find("en");
    bool fb = false;
    const Catalog* c = nullptr;
    if (l && l->code != "fr") {
        c = load(l->code);
        // Lisibilité sur la télé : vérifiée seulement pour la langue choisie (jamais pour toutes au démarrage,
        // le tas de l'app est petit — 2.29.0 plantait en chargeant les 17 catalogues).
        if (c) for (auto& x : langs_) if (&x == l) x.tv_ok = tv_renderable(*c);
        if (c && !l->tv_ok) c = nullptr;
        if (!c) { fb = true; l = i18n::find("en"); c = l ? load("en") : nullptr; }
        if (!c) l = nullptr;  // aucun catalogue lisible : français
    }
    current_ = c;
    lang_ = l && l->code != "fr" ? l : nullptr;
    fallback_ = fb;
    // Polices : la bonne variante des idéogrammes (chinois simplifié / traditionnel, japonais, coréen) et le sens
    // de lecture par défaut (arabe : de droite à gauche).
    fonts::set_language(l ? l->code : "fr");
    fonts::set_rtl(l && l->rtl);
    return before != lang_.load();
}

}  // namespace

const char* tr(const char* fr) {
    const Catalog* c = current_.load();
    if (!c || !fr) return fr;
    const auto it = c->text.find(std::string_view(fr));
    return it == c->text.end() ? fr : it->second.c_str();
}

std::string trf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    const int n = std::vsnprintf(buf, sizeof buf, tr(fmt), ap);
#pragma clang diagnostic pop
    va_end(ap);
    if (n < 0) return tr(fmt);
    return std::string(buf, static_cast<std::size_t>(std::min<int>(n, static_cast<int>(sizeof buf) - 1)));
}

namespace i18n {

void init(std::string dir, std::string system_code) {
    dir_ = std::move(dir);
    system_ = std::move(system_code);
    std::string text;
    pbc::Json j;
    if (read_file(dir_ + "/languages.json", text) && pbc::Json::parse(text, j))
        for (const auto& x : j.items()) {
            Language l;
            l.code = x["code"].as_string();
            l.name = x["name"].as_string();
            l.english = x["english"].as_string();
            l.fr = x["fr"].as_string();
            l.decimal = x["decimal"].as_string() == "," ? ',' : '.';
            l.rtl = x["rtl"].as_bool();
            if (l.code.empty()) continue;
            langs_.push_back(std::move(l));
        }
    if (langs_.empty()) langs_.push_back(french());
    system_ = i18n::resolve(system_);
    // Pas de préchargement : seule la langue affichée (et l'anglais en secours) est lue.
    for (auto& l : langs_) l.tv_ok = true;
    show(system_);
}

const std::vector<Language>& languages() { return langs_; }

const Language* find(std::string_view code) {
    for (const auto& l : langs_) if (l.code == code) return &l;
    return nullptr;
}

std::string resolve(std::string_view tag) {
    std::string t(tag);
    for (auto& ch : t) if (ch == '_') ch = '-';
    auto lower = [](std::string s) { for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); return s; };
    const std::string lt = lower(t);
    for (const auto& l : langs_) if (lower(l.code) == lt) return l.code;
    const std::string base = lt.substr(0, lt.find('-'));
    const std::string region = lt.size() > base.size() ? lt.substr(base.size() + 1) : "";
    if (base == "es" && !region.empty() && region != "es") return "es-419";
    if (base == "pt") return region == "br" ? "pt-BR" : "pt-PT";
    if (base == "zh") return region == "tw" || region == "hk" || region == "mo" || region == "hant" ? "zh-Hant" : "zh-Hans";
    for (const auto& l : langs_) if (lower(l.code) == base) return l.code;
    return "en";
}

std::string from_console(int status, int code) {
    // Codes de l'ancienne app (app/src/pbck_language.hpp) : 30 langues système de la PS5.
    static const char* const locales[] = {"ja-JP", "en-US", "fr-FR", "es-ES", "de-DE", "it-IT", "nl-NL", "pt-PT", "ru-RU", "ko-KR",
                                          "zh-Hant", "zh-Hans", "fi-FI", "sv-SE", "da-DK", "nb-NO", "pl-PL", "pt-BR", "en-GB", "tr-TR",
                                          "es-419", "ar", "fr-CA", "cs-CZ", "hu-HU", "el-GR", "ro-RO", "th-TH", "vi-VN", "id-ID"};
    // Étiquette brute : init() la résout une fois la liste des langues lue (appelée avant, elle donnait « en »).
    if (status != 0 || code < 0 || code >= static_cast<int>(sizeof locales / sizeof *locales)) return "en";
    return locales[code];
}

const std::string& system_code() { return system_; }
const std::string& setting() { return setting_; }

bool apply(const std::string& s) {
    setting_ = s.empty() ? "auto" : s;
    if (!forced_.empty()) return false;
    return show(setting_ == "auto" ? system_ : setting_);
}

void force(const std::string& code) { forced_ = code; show(code); }

const Language& current() { const Language* l = lang_.load(); return l ? *l : french(); }

std::string label(const Language& l) {
    if (l.fr.empty() || l.code == current().code) return l.name;
    const std::string here = tr(l.fr.c_str());
    auto low = [](std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return s; };
    return low(here) == low(l.name) ? l.name : l.name + " · " + here;
}
bool fallback() { return fallback_.load(); }
bool rtl() { return current().rtl; }
char decimal() { return current().decimal; }

}  // namespace i18n
}  // namespace app
