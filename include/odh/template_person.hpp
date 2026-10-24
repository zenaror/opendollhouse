#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace odh {

struct TemplateBodyString {
    std::uint16_t index;
    std::string bytes; // Game-encoded bytes; no conversion or lossy replacement.
};

struct TemplatePersonRequest {
    std::uint32_t guid;
    std::string object_label; // IFF's fixed 64-byte label field, without NUL.
    std::uint8_t language; // TS1 STR# language code, 1..20.
    std::string name;      // Already encoded in the selected game's character set.
    std::string biography; // Already encoded in the selected game's character set.
    std::vector<TemplateBodyString> body_strings;
    std::uint64_t maximum_iff_bytes = 16 * 1024 * 1024;
};

// Produces a new private character IFF in memory. The template and all unknown
// chunks remain untouched; this function never reads or writes game paths.
std::vector<std::uint8_t> clone_template_person(
    const std::vector<std::uint8_t>& template_iff,
    const TemplatePersonRequest& request);

} // namespace odh
