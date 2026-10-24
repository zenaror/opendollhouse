#include "odh/pie_menu.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

PieMenuItem& find_or_add_category(std::vector<PieMenuItem>& items, const std::string& label,
                                  std::uint8_t language) {
    const auto found = std::find_if(items.rbegin(), items.rend(), [&](const auto& item) {
        return item.label == label;
    });
    // Simitone's name lookup points to the most recently inserted child. It
    // can therefore turn an item into a parent if a later label extends it.
    if (found != items.rend()) return *found;
    items.push_back(PieMenuItem{label, std::nullopt, language, {}});
    return items.back();
}

} // namespace

std::vector<PieMenuItem> make_pie_menu_model(const std::vector<PieMenuChoice>& choices) {
    std::vector<PieMenuItem> result;
    for (const auto& choice : choices) {
        auto parts = path_components(choice.label);
        auto* siblings = &result;
        for (std::size_t index = 0; index + 1 < parts.size(); ++index) {
            auto& category = find_or_add_category(*siblings, parts[index], choice.label_language);
            siblings = &category.children;
        }
        // Actions with duplicate labels are intentionally separate choices.
        siblings->push_back(PieMenuItem{std::move(parts.back()), choice.interaction_row,
            choice.label_language, {}});
    }
    return result;
}

std::optional<PieMenuLayout> layout_pie_menu(std::uint32_t frame_width,
    std::uint32_t frame_height, std::array<std::int32_t, 2> anchor,
    std::size_t item_count, bool has_parent) {
    if (frame_width < 128 || frame_height < 120 || item_count == 0 ||
        frame_width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        frame_height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return std::nullopt;

    constexpr std::int32_t button_height = 24;
    constexpr std::int32_t radius = 40;
    const auto frame_w = static_cast<std::int32_t>(frame_width);
    const auto frame_h = static_cast<std::int32_t>(frame_height);
    const auto max_radial_width = (frame_w - 2 * radius - 8) / 2;
    const auto width = std::min<std::int32_t>({156, max_radial_width, frame_w / 3});
    if (width < 80) return std::nullopt;
    const auto center_x = std::clamp(anchor[0], radius + width + 4,
        frame_w - radius - width - 4);
    const auto center_y = std::clamp(anchor[1], radius + button_height + 4,
        frame_h - radius - button_height - 4);

    PieMenuLayout result;
    result.items.reserve(item_count);
    const auto direction_count = item_count > 4 ? std::size_t{8} :
        item_count > 2 ? std::size_t{4} : std::size_t{2};
    for (std::size_t i = 0; i < item_count; ++i) {
        std::int32_t x = center_x - width / 2;
        std::int32_t y = center_y - button_height / 2;
        if (i < 8) {
            constexpr double pi = 3.14159265358979323846;
            const auto angle = (static_cast<double>(i) / static_cast<double>(direction_count)) *
                (2.0 * pi) - (pi / 2.0);
            const auto item_x = center_x + static_cast<std::int32_t>(std::lround(std::cos(angle) * radius));
            const auto item_y = center_y + static_cast<std::int32_t>(std::lround(std::sin(angle) * radius));
            if (i == 0) {
                x = center_x - width / 2;
                y = item_y - button_height;
            } else if (i == direction_count / 2) {
                x = center_x - width / 2;
                y = item_y;
            } else if (i < direction_count / 2) {
                x = item_x;
            } else {
                x = item_x - width;
            }
            if (i != 0 && i != direction_count / 2) y = item_y - button_height / 2;
        } else {
            const auto overflow = i - 8;
            const auto rows = static_cast<std::int32_t>(overflow / 2 + 1);
            x = center_x - width / 2;
            y = (overflow % 2 == 0)
                ? center_y - radius - button_height * (rows + 1)
                : center_y + radius + button_height * rows;
        }
        x = std::clamp(x, 4, frame_w - width - 4);
        y = std::clamp(y, 4, frame_h - button_height - 4);
        result.items.push_back({x, y, width, button_height});
    }
    if (has_parent) {
        const auto back_width = std::min<std::int32_t>(112, frame_w - 8);
        result.back_button = PieMenuRect{center_x - back_width / 2,
            center_y - button_height / 2, back_width, button_height};
    }
    return result;
}

} // namespace odh
