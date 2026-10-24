#include "odh/pie_menu.hpp"

#include <algorithm>
#include <utility>

namespace odh {
namespace {

std::vector<std::string> path_components(const std::optional<std::string>& label) {
    if (!label || label->empty()) return {"???"};

    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto slash = label->find('/', start);
        parts.emplace_back(label->substr(start, slash == std::string::npos
            ? std::string::npos : slash - start));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    if (std::all_of(parts.begin(), parts.end(), [](const auto& part) { return part.empty(); }))
        return {"???"};
    return parts;
}

PieMenuItem& find_or_add_category(std::vector<PieMenuItem>& items, const std::string& label) {
    const auto found = std::find_if(items.rbegin(), items.rend(), [&](const auto& item) {
        return item.label == label;
    });
    // Simitone's name lookup points to the most recently inserted child. It
    // can therefore turn an item into a parent if a later label extends it.
    if (found != items.rend()) return *found;
    items.push_back(PieMenuItem{label, std::nullopt, {}});
    return items.back();
}

} // namespace

std::vector<PieMenuItem> make_pie_menu_model(const std::vector<PieMenuChoice>& choices) {
    std::vector<PieMenuItem> result;
    for (const auto& choice : choices) {
        auto parts = path_components(choice.label);
        auto* siblings = &result;
        for (std::size_t index = 0; index + 1 < parts.size(); ++index) {
            auto& category = find_or_add_category(*siblings, parts[index]);
            siblings = &category.children;
        }
        // Actions with duplicate labels are intentionally separate choices.
        siblings->push_back(PieMenuItem{std::move(parts.back()), choice.interaction_row, {}});
    }
    return result;
}

} // namespace odh
