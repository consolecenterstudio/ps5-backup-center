// Traductions de l'interface télé. Un seul catalogue partagé avec le web : engine/web/i18n/<code>.json,
// installé avec l'app dans /app0/assets/i18n. Clé = le texte français d'origine (façon gettext) :
//   tr("Sauvegarder")            → texte traduit (ou le français s'il manque)
//   trf("%d jeux", n)            → format printf traduit puis rempli (mêmes % dans le même ordre)
//   N_("Accueil")                → marque un texte de tableau pour l'extraction ; tr() à l'affichage
// Langue : réglage « language » du moteur (« auto » = langue de la console, ou un code).
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace app {

[[nodiscard]] const char* tr(const char* fr) __attribute__((format_arg(1)));
[[nodiscard]] std::string trf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
constexpr const char* N_(const char* s) { return s; }

namespace i18n {

struct Language {
    std::string code, name, english;  // « de », « Deutsch », « German »
    char decimal = '.';
    bool rtl = false;
    bool tv_ok = true;                // l'atlas de police actuel sait l'afficher (voir i18n.cpp)
    std::string fr;                   // « allemand » : traduit à l'affichage (tr) à côté du nom natif
};

// Lit languages.json dans `dir` ; `system_code` : langue détectée de la console (code de catalogue).
void init(std::string dir, std::string system_code);
const std::vector<Language>& languages();
// Étiquette de langue (« pt-BR », « es-MX », « zh-TW », « en-GB »…) → code de catalogue ; anglais à défaut.
std::string resolve(std::string_view tag);
// Paramètre système de la console (sceSystemServiceParamGetInt(1)) → code de catalogue.
std::string from_console(int status, int code);
const std::string& system_code();
// Réglage du moteur (« auto » ou un code) : charge le catalogue. Vrai si la langue affichée change.
bool apply(const std::string& setting);
// Aperçu Mac (--lang) : cette langue, quel que soit le réglage du moteur.
void force(const std::string& code);
const std::string& setting();
const Language& current();     // langue réellement affichée
// « Русский · russe » : nom natif, puis son nom dans la langue affichée (omis s'il est identique).
std::string label(const Language& l);
const Language* find(std::string_view code);
// Vrai quand la langue demandée ne peut pas encore s'afficher sur la télé (police) : anglais à la place.
bool fallback();
bool rtl();                    // droite à gauche (arabe) : pour le futur moteur de texte
char decimal();

}  // namespace i18n
}  // namespace app
