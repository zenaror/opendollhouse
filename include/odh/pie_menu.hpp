#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace odh {

// One already-eligible interaction offered by a selected object.
// Keep the TTAB row so choosing a displayed leaf can return to the exact action.
struct PieMenuChoice {
    std::size_t interaction_row = 0;
    std::optional<std::string> label;
};

// A node can be an action, a category, or both when labels share a path.
struct PieMenuItem {
    std::string label;
    std::optional<std::size_t> interaction_row;
    std::vector<PieMenuItem> children;
};

// Builds the original game's slash-separated category paths. Items and
// categories retain first-seen order; duplicate action labels remain distinct.
std::vector<PieMenuItem> make_pie_menu_model(const std::vector<PieMenuChoice>& choices);

} // namespace odh
