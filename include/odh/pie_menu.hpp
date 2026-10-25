#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace odh {

// One already-eligible interaction offered by a selected object.
// Keep the TTAB row so choosing a displayed leaf can return to the exact action.
struct PieMenuChoice {
    std::size_t interaction_row = 0;
    std::optional<std::string> label;
    std::uint8_t label_language = 1;
};

// A node can be an action, a category, or both when labels share a path.
struct PieMenuItem {
    std::string label;
    std::optional<std::size_t> interaction_row;
    std::uint8_t label_language = 1;
    std::vector<PieMenuItem> children;
};

// Builds the original game's slash-separated category paths. Items and
// categories retain first-seen order; duplicate action labels remain distinct.
std::vector<PieMenuItem> make_pie_menu_model(const std::vector<PieMenuChoice>& choices);

struct PieMenuRect {
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    bool contains(std::int32_t px, std::int32_t py) const noexcept {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

struct PieMenuLayout {
    std::array<std::int32_t, 2> center{0, 0};
    std::vector<PieMenuRect> items;
    std::optional<PieMenuRect> back_button;
};

// Places up to eight choices 60 pixels around the pointer like Simitone's pie;
// additional choices alternate in columns above and below its center.
std::optional<PieMenuLayout> layout_pie_menu(std::uint32_t frame_width,
    std::uint32_t frame_height, std::array<std::int32_t, 2> anchor,
    std::size_t item_count, bool has_parent);

} // namespace odh
