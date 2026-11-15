// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/bitmap.hpp"

namespace odh_host {
// Authored character images, in original strip order. This loader does not
// assign mood/relationship meanings to frames or infer a transparency key.
struct CharacterUiImages {
    std::vector<odh::BitmapImage> faces,relations;
    std::optional<odh::BitmapImage> thumbnail,speech_medium,speech_large;
};
struct CharacterUiImageLimits {
    std::uint64_t iff_bytes=16*1024*1024,bitmap_bytes=1024*1024;
    std::uint32_t chunks=4096,cell_extent=128,pixels=200000;
};
// The caller authenticates the character IFF before passing its bounded View.
// Missing image resources are optional; malformed or duplicate selected IDs fail.
CharacterUiImages read_character_ui_images(const odh::View&,CharacterUiImageLimits={});
// Current resource providers can retain an authoritative IFF in a FAR or in
// memory. This DTO edge shares the exact same bounded image decoder and strip
// splitting code; it never guesses a physical path from a logical IFF name.
struct CharacterUiImageResource {std::uint16_t id;std::vector<std::uint8_t> bitmap;};
CharacterUiImages read_character_ui_images(const std::vector<CharacterUiImageResource>&,CharacterUiImageLimits={});
// Original relation portrait uses long-term (matrix index2), not daily(index0).
// Missing data selects the neutral picture without manufacturing a stored score.
std::size_t character_relationship_frame(std::optional<std::int16_t> long_term) noexcept;
}
