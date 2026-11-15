/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "files/SafeFile.h"
#include "odh/game_languages.hpp"
#include <cstdio>
#include <string>

namespace simapp {

// A handheld profile may already have neighborhoods and options from builds
// that never offered a language choice. Only a saved choice answers the prompt.
inline std::optional<std::uint8_t> ReadHandheldLanguage(const std::string& saveRoot) {
    const auto primary = saveRoot + "/language.txt";
    for (const char* suffix : {"", ".new", ".tmp", ".bak"}) {
        auto* f = std::fopen((primary + suffix).c_str(), "rb");
        if (!f) continue;
        char data[5];
        const auto size = std::fread(data, 1, sizeof(data), f);
        const bool complete = !std::ferror(f) && size < sizeof(data);
        std::fclose(f);
        if (!complete) continue;
        std::string_view value(data, size);
        if (!value.empty() && value.back() == '\n') {
            value.remove_suffix(1);
            if (!value.empty() && value.back() == '\r') value.remove_suffix(1);
        }
        if (value.empty() || value.size() > 2 || value.front() < '1' || value.front() > '9')
            continue;
        const auto code = odh_host::parse_game_language(value);
        if (code) return code;
    }
    return {};
}
inline bool WriteHandheldLanguage(const std::string& saveRoot, std::uint8_t code) {
    return odh_host::valid_game_language(code) &&
        FSO::Files::SafeFile::WriteText(saveRoot + "/language.txt", std::to_string(code) + "\n");
}
}
