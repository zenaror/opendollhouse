/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace simapp { namespace original {
// Explicit raw STR-3 resource boundary. The active STR language selects its
// code page; these functions never change saved source strings in place.
std::uint8_t TextLanguage();
std::string LegacyToUTF8(std::string_view);
std::string LegacyToUTF8(std::string_view, std::uint8_t sourceLanguage);
std::string PersonalToUTF8(std::string_view, std::uint8_t sourceLanguage, bool sourceUTF8);
// Ambiguity is presentation state, never a replacement stored in CTSS/FAMs.
inline std::string PersonalLabelUTF8(std::string_view bytes, std::uint8_t sourceLanguage,
                                    bool sourceUTF8, bool ambiguous) {
    return ambiguous || (!sourceLanguage && !sourceUTF8) ? "[name unavailable]" : PersonalToUTF8(bytes,sourceLanguage,sourceUTF8);
}
std::string UTF8ToLegacy(std::string_view);
// Only for existing mixed presentation caches: UTF-8 drafts stay UTF-8;
// otherwise decode original bytes. Known raw resources use LegacyToUTF8.
std::string UIString(std::string_view);
}}
