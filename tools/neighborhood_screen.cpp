#include "neighborhood_screen.hpp"

#include "odh/family.hpp"
#include "odh/neighborhood_picker.hpp"
#include "odh/resident_selection.hpp"
#include "resource.hpp"
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
#include "linux_text_renderer.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

namespace odh_host {

PreparedNeighborhoodMap prepare_neighborhood_map(
        const std::filesystem::path& user_data,
        const std::filesystem::path& ui_graphics, unsigned mode) {
    const auto config = odh::neighborhood_map_config(mode);
    if (!config) throw odh::FormatError("neighborhood map is not available for this mode");

    std::optional<odh::BitmapImage> image;
    with_far_entry(ui_graphics.string(), config->resource,
        [&](const auto& view) { image = odh::read_bmp(view); });

    std::optional<odh::LotLocations> locations;
    with_iff_file((user_data / "LotLocations.iff").string(), [&](const auto& view) {
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type != "STR#" || chunk.id != mode) return;
            if (locations) throw odh::FormatError("ambiguous LotLocations STR# mode");
            locations = odh::read_lot_locations(chunk.payload);
        });
    });
    if (!locations) throw odh::FormatError("LotLocations mode STR# not found");

    std::vector<odh::FamilyEntry> entries;
    with_iff_file((user_data / "Neighborhood.iff").string(), [&](const auto& view) {
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type != "FAMI") return;
            if (entries.size() >= 4096) throw odh::FormatError("too many FAMI chunks");
            entries.push_back({chunk.id, odh::read_fami(chunk.payload)});
        });
    });
    const odh::FamilyContext families(entries);
    std::vector<odh::NeighborhoodMarker> markers;
    for (const auto& loc : locations->lots) {
        std::ostringstream filename;
        filename << "House" << std::setw(2) << std::setfill('0') << loc.house << ".iff";
        markers.push_back({loc, std::filesystem::is_regular_file(user_data / "Houses" / filename.str()),
            families.by_house(static_cast<std::int16_t>(loc.house)) != nullptr});
    }
    std::stable_sort(markers.begin(), markers.end(), [](const auto& a, const auto& b) {
        return a.location.y < b.location.y;
    });

    return {odh::draw_neighborhood_map(*image, markers, config->marker_scale),
            std::move(markers), config->marker_scale};
}

std::optional<std::uint8_t> show_language_picker(LinuxWindow& window,
        std::uint8_t initial_language) {
    static constexpr std::array<const char*,20> names{{
        "English (US)", "English (UK)", "Fran\u00e7ais", "Deutsch", "Italiano",
        "Espa\u00f1ol", "Nederlands", "Dansk", "Svenska", "Norsk", "Suomi",
        "\u05e2\u05d1\u05e8\u05d9\u05ea", "\u0420\u0443\u0441\u0441\u043a\u0438\u0439", "Portugu\u00eas",
        "\u65e5\u672c\u8a9e", "Polski", "\u7b80\u4f53\u4e2d\u6587", "\u7e41\u9ad4\u4e2d\u6587",
        "\u0e44\u0e17\u0e22", "\ud55c\uad6d\uc5b4"}};
    if (initial_language < 1 || initial_language > names.size())
        throw odh::FormatError("language picker requires a supported language code");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    std::size_t selected = static_cast<std::size_t>(initial_language - 1);
    constexpr std::size_t row_start = 56, row_height = 25;
    const auto render = [&]() {
        odh::RasterFrame frame{};
        frame.width = window.width(); frame.height = window.height();
        frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                            std::array<std::uint8_t,4>{35,39,48,255});
        for (std::size_t index = 0; index < names.size(); ++index) {
            const auto color = index == selected ? std::array<std::uint8_t,4>{47,93,138,255} :
                std::array<std::uint8_t,4>{52,57,68,255};
            const auto y0 = row_start + index * row_height;
            for (std::size_t y = y0; y < y0 + 22; ++y)
                for (std::size_t x = 24; x + 24 < frame.width; ++x)
                    frame.pixels[y * frame.width + x] = color;
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        TextOverlayStyle style;
        style.x = 24; style.width = static_cast<std::int32_t>(frame.width) - 48;
        style.height = 24; style.font_size_px = 16; style.padding = 4;
        style.foreground = {245,245,245,255}; style.background = {22,25,32,255};
        style.y = 18;
        draw_text_overlay(frame,
            "Choose game language - Tab: next, Shift+Tab: previous, Enter or click: confirm, Esc: cancel",
            static_cast<std::uint8_t>(selected + 1), style);
        for (std::size_t index = 0; index < names.size(); ++index) {
            style.y = static_cast<std::int32_t>(row_start + index * row_height);
            draw_text_overlay(frame, std::string(index == selected ? "> " : "  ") +
                std::to_string(index + 1) + "  " + names[index],
                static_cast<std::uint8_t>(selected + 1), style);
        }
        style.y = static_cast<std::int32_t>(frame.height) - 32;
        draw_text_overlay(frame, "The game text will use this language; you can also set --language later",
            static_cast<std::uint8_t>(selected + 1), style);
#endif
        window.set_title(std::string("OpenDollhouse | Language ") +
            std::to_string(selected + 1) + " - " + names[selected] +
            " | Tab/Shift+Tab: change, Enter: confirm, Esc: cancel");
        window.present(frame);
    };
    render();
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (input.interaction_selection_delta) {
            selected = odh::advance_resident_selection(selected,
                input.interaction_selection_delta, names.size());
            render();
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if (input.primary_click) {
            const auto y = (*input.primary_click)[1];
            const auto row_end = row_start + names.size() * row_height;
            if (y >= static_cast<std::int32_t>(row_start) && y < static_cast<std::int32_t>(row_end)) {
                selected = static_cast<std::size_t>(y - static_cast<std::int32_t>(row_start)) / row_height;
                return static_cast<std::uint8_t>(selected + 1);
            }
        }
#endif
        if (input.action && !input.primary_click)
            return static_cast<std::uint8_t>(selected + 1);
        if (input.redraw) render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<std::uint16_t> show_neighborhood_picker(
        LinuxWindow& window, const PreparedNeighborhoodMap& map) {
    if (window.width() != 800 || window.height() != 600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    window.set_title("OpenDollhouse | choose a lot | Esc: cancel");
    window.present(map.frame);
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (input.primary_click) {
            const auto selected = odh::pick_neighborhood_lot(map.markers, map.marker_scale,
                (*input.primary_click)[0], (*input.primary_click)[1]);
            if (selected) return selected;
        }
        if (input.redraw) window.present(map.frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<std::uint16_t> show_neighborhood_resume_picker(
        LinuxWindow& window, const PreparedNeighborhoodMap& map, std::uint16_t active_house) {
    if (window.width() != 800 || window.height() != 600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    window.set_title("OpenDollhouse | select the current lot to resume | Esc: close");
    window.present(map.frame);
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (input.primary_click) {
            const auto selected = odh::pick_neighborhood_lot(map.markers, map.marker_scale,
                (*input.primary_click)[0], (*input.primary_click)[1]);
            if (selected && *selected == active_house) return selected;
            if (selected) window.set_title(
                "OpenDollhouse | switching lots is not available yet; select the current lot | Esc: close");
        }
        if (input.redraw) window.present(map.frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<std::size_t> show_resident_picker(
        LinuxWindow& window, const std::vector<odh::SessionEntryMember>& residents,
        std::uint8_t language) {
    if (residents.empty()) throw odh::FormatError("selected family has no residents");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    std::size_t selected = 0;
    constexpr std::size_t row_height = 36;
    const std::size_t page_size = std::max<std::size_t>(1, (window.height() - 184) / row_height);
    const auto render = [&]() {
        const std::size_t page_start = (selected / page_size) * page_size;
        odh::RasterFrame frame{};
        frame.width = window.width(); frame.height = window.height();
        frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                            std::array<std::uint8_t,4>{35, 39, 48, 255});
        for (std::size_t row = 0; row < page_size; ++row) {
            const auto index = page_start + row;
            if (index >= residents.size()) break;
            const auto color = index == selected ? std::array<std::uint8_t,4>{47,93,138,255} :
                std::array<std::uint8_t,4>{52,57,68,255};
            const auto y0 = 92 + row * row_height;
            for (std::size_t y = y0; y < y0 + 32; ++y)
                for (std::size_t x = 24; x + 24 < frame.width; ++x)
                    frame.pixels[y * frame.width + x] = color;
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        TextOverlayStyle style;
        style.x = 24; style.width = static_cast<std::int32_t>(frame.width) - 48;
        style.height = 32; style.font_size_px = 17; style.padding = 7;
        style.foreground = {245,245,245,255}; style.background = {22,25,32,255};
        style.y = 28;
        draw_text_overlay(frame, "Choose a resident - Tab: next, click a row or Enter: continue, Esc: cancel", language, style);
        for (std::size_t row = 0; row < page_size; ++row) {
            const auto index = page_start + row;
            if (index >= residents.size()) break;
            const auto& member = residents[index];
            const auto text = std::string(index == selected ? "> Resident ID " : "  Resident ID ") +
                std::to_string(member.neighbor_id) + " - " + member.character_asset;
            style.y = static_cast<std::int32_t>(92 + row * row_height);
            style.background = {22,25,32,255};
            draw_text_overlay(frame, text, language, style);
        }
        style.y = static_cast<std::int32_t>(frame.height) - 62;
        style.background = {22,25,32,255};
        const auto page_count = (residents.size() + page_size - 1) / page_size;
        draw_text_overlay(frame, "Page " + std::to_string(page_start / page_size + 1) + "/" +
            std::to_string(page_count) + " - selection is by neighborhood ID and character file", language, style);
#else
        (void)language;
#endif
        const auto& member = residents[selected];
        window.set_title("OpenDollhouse | resident " + std::to_string(member.neighbor_id) +
            " | " + member.character_asset + " | Tab: next, Enter: continue, Esc: cancel");
        window.present(frame);
    };
    render();
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (input.interaction_selection_delta) {
            selected = odh::advance_resident_selection(selected,
                input.interaction_selection_delta, residents.size());
            render();
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if (input.primary_click) {
            const auto y = (*input.primary_click)[1];
            constexpr std::size_t row_start = 92;
            const std::size_t row_end = row_start + page_size * row_height;
            if (y >= static_cast<std::int32_t>(row_start) && y < static_cast<std::int32_t>(row_end)) {
                const auto row = static_cast<std::size_t>(y - static_cast<std::int32_t>(row_start)) / row_height;
                const auto page_start = (selected / page_size) * page_size;
                const auto clicked = odh::resident_selection_row(page_start, row, page_size, residents.size());
                if (clicked) return clicked;
            }
        }
#endif
        if (input.action && !input.primary_click) return selected;
        if (input.redraw) render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

} // namespace odh_host
