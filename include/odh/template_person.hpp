#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <vector>

namespace odh {

struct TemplateBodyString {
    std::uint16_t index;
    std::string bytes; // Game-encoded bytes; no conversion or lossy replacement.
};

struct TemplatePersonSource {
    std::uint32_t guid=0;
    std::uint16_t definition_chunk_id=0,body_strings_id=0;
};
struct TemplatePersonRequest {
    std::uint32_t guid;
    std::string object_label; // IFF's fixed 64-byte label field, without NUL.
    std::uint8_t language; // TS1 STR# language code, 1..20.
    std::string name;      // Already encoded in the selected game's character set.
    std::string biography; // Already encoded in the selected game's character set.
    std::vector<TemplateBodyString> body_strings;
    std::uint64_t maximum_iff_bytes = 16 * 1024 * 1024;
    // Authenticated source OBJD/BodySTR for Make19 pet templates or other
    // explicit person templates. An authenticated all-English template with at
    // least27 source strings may grow only its missing wardrobe suffix through
    // index34. Absence preserves legacy TemplatePerson128/200 and strict bounds.
    std::optional<TemplatePersonSource> source;
};

// Produces a new private character IFF in memory. The template and all unknown
// chunks remain untouched; this function never reads or writes game paths.
std::vector<std::uint8_t> clone_template_person(
    const std::vector<std::uint8_t>& template_iff,
    const TemplatePersonRequest& request);

} // namespace odh
