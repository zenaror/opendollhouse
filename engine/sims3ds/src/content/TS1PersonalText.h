/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "files/formats/iff/chunks/STR.h"

namespace FSO { namespace Content { namespace TS1 {
// A whole personal record: name and biography always resolve from the same set.
// No strings or language tags are rewritten by resolving another UI language.
struct TS1PersonalText {
    uint8_t SourceLanguage = 0;
    bool UTF8 = false, Ambiguous = false;
    std::vector<std::string> Values;
    std::string Value(size_t index) const { return index < Values.size() ? Values[index] : std::string(); }
};
inline TS1PersonalText ResolvePersonalText(const FSO::Files::Formats::IFF::Chunks::STR* chunk) {
    TS1PersonalText result;
    if (!chunk) return result;
    auto present = [&](size_t index) { return index < chunk->LanguageSets.size() && !chunk->LanguageSets[index].Strings.empty(); };
    size_t language = uint8_t(FSO::Files::Formats::IFF::Chunks::STR::DefaultLangCode);
    if (!language) language = 1;
    size_t selected = language - 1;
    if (!present(selected)) {
        if (present(0)) selected = 0;
        else {
            size_t count = 0;
            for (size_t i = 0; i < chunk->LanguageSets.size(); ++i) if (present(i)) { selected = i; ++count; }
            if (count != 1) { result.Ambiguous = count > 1; return result; }
        }
    }
    result.SourceLanguage = uint8_t(selected + 1);
    result.UTF8 = chunk->SourceFormat == -1 || chunk->SourceFormat == -4;
    for (const auto& item : chunk->LanguageSets[selected].Strings) result.Values.push_back(item.Value);
    return result;
}
}}}
