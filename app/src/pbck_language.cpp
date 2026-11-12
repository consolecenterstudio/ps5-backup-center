// SPDX-License-Identifier: GPL-3.0-or-later
#include "pbck_language.hpp"

// Present in the locked community SDK 0.42 libSceSystemService.so.
extern "C" std::int32_t sceSystemServiceParamGetInt(std::int32_t, std::int32_t*);

namespace pbck {
SystemLanguage read_system_language() noexcept {
    std::int32_t code=-1;
    const auto status=sceSystemServiceParamGetInt(system_language_parameter,&code);
    // On failure the output argument is not a valid measurement, even if written.
    return {status,status==0 ? code : -1};
}
}
