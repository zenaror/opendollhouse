// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace FSO { namespace SimAntics { namespace Model {
// Dialog fragments retain the encoding of their own resource until presentation.
// Legacy users can still request the original concatenated byte string.
struct VMDialogText {
    struct Part { std::string Bytes; uint8_t Language = 1; bool UTF8 = false; };
    std::vector<Part> Parts;
    uint8_t Language = 1;
    bool UTF8 = false;
    bool LegacyOnly = false; // An opaque dynamic value keeps the entire dialog on its legacy path.
    VMDialogText(uint8_t language=1, bool utf8=false) : Language(language), UTF8(utf8) {}
    void Append(const std::string& bytes, uint8_t language, bool utf8) {
        if (bytes.empty()) return;
        if (!Parts.empty() && Parts.back().Language == language && Parts.back().UTF8 == utf8) Parts.back().Bytes += bytes;
        else Parts.push_back({bytes, language, utf8});
    }
    VMDialogText& operator+=(const std::string& bytes) { Append(bytes, Language, UTF8); return *this; }
    VMDialogText& operator+=(char byte) { return *this += std::string(1, byte); }
    VMDialogText& operator+=(const VMDialogText& text) { LegacyOnly = LegacyOnly || text.LegacyOnly; for (const auto& p : text.Parts) Append(p.Bytes,p.Language,p.UTF8); return *this; }
    std::string LegacyBytes() const { std::string bytes; for (const auto& p : Parts) bytes += p.Bytes; return bytes; }
    void ExpandParagraphs() {
        bool cr = false;
        for (auto& p : Parts) {
            std::string bytes;
            for (char c : p.Bytes) { bytes += c; if (cr && c == '\n') bytes += "\r\n"; cr = c == '\r'; }
            p.Bytes.swap(bytes);
        }
    }
};
}}}
