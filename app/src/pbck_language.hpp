// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace pbck {
enum class Language { french, english };
enum class LanguagePreference { automatic, french, english };
enum class LanguageSource { manual, system, unavailable_translation, unknown_code, read_error };

struct SystemLanguage {
    std::int32_t status;
    std::int32_t code;
};
struct ResolvedLanguage {
    Language language;
    LanguageSource source;
};

// Community ABI reference: OpenOrbis include/orbis/_types/sys_service.h.
// These codes identify a system locale, not a translation shipped by this app.
// A newer unknown code deliberately remains unknown until its mapping is checked.
inline constexpr std::int32_t system_language_parameter = 1;
inline constexpr std::array<const char*,30> system_locales{
    "ja-JP", "en-US", "fr-FR", "es-ES", "de-DE", "it-IT", "nl-NL", "pt-PT",
    "ru-RU", "ko-KR", "zh-Hant", "zh-Hans", "fi-FI", "sv-SE", "da-DK", "nb-NO",
    "pl-PL", "pt-BR", "en-GB", "tr-TR", "es-419", "ar", "fr-CA", "cs-CZ",
    "hu-HU", "el-GR", "ro-RO", "th-TH", "vi-VN", "id-ID"
};

[[nodiscard]] constexpr const char* system_locale(SystemLanguage value) noexcept {
    if(value.status!=0 || value.code<0 ||
       static_cast<unsigned>(value.code)>=system_locales.size()) return nullptr;
    return system_locales[static_cast<unsigned>(value.code)];
}

// FR/EN resolver used directly by the native UI. A failed system read remains
// available separately for diagnostics even when a manual preference wins.
[[nodiscard]] constexpr ResolvedLanguage resolve_language(
    LanguagePreference preference, SystemLanguage value) noexcept {
    if(preference==LanguagePreference::french) return {Language::french,LanguageSource::manual};
    if(preference==LanguagePreference::english) return {Language::english,LanguageSource::manual};
    if(value.status!=0) return {Language::english,LanguageSource::read_error};
    if(!system_locale(value)) return {Language::english,LanguageSource::unknown_code};
    switch(value.code) {
        case 2: case 22: return {Language::french,LanguageSource::system};
        case 1: case 18: return {Language::english,LanguageSource::system};
        default: return {Language::english,LanguageSource::unavailable_translation};
    }
}

[[nodiscard]] constexpr const char* language_tag(Language value) noexcept {
    return value==Language::french ? "fr" : "en";
}
[[nodiscard]] constexpr const char* language_source_name(LanguageSource value) noexcept {
    switch(value) {
        case LanguageSource::manual: return "manual";
        case LanguageSource::system: return "system";
        case LanguageSource::unavailable_translation: return "unavailable_translation";
        case LanguageSource::unknown_code: return "unknown_code";
        case LanguageSource::read_error: return "read_error";
    }
    return "unknown_source";
}

[[nodiscard]] SystemLanguage read_system_language() noexcept;
}
