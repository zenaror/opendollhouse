#include "neighborhood_screen.hpp"

#include "odh/family.hpp"
#include "odh/family_overlay.hpp"
#include "odh/neighborhood_picker.hpp"
#include "odh/resident_selection.hpp"
#include "odh/sha256.hpp"
#include "resource.hpp"
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
#include "linux_text_renderer.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <thread>
#include <utility>

namespace odh_host {
namespace {
constexpr std::array<std::uint8_t, 4> kNeighborhoodMatte{32, 36, 44, 255};

bool neighborhood_mask_color(const std::array<std::uint8_t, 4>& pixel) {
    return (pixel[0] == 0xff && pixel[1] == 0x00 && pixel[2] == 0xff) ||
           (pixel[0] == 0xfe && pixel[1] == 0x02 && pixel[2] == 0xfe) ||
           (pixel[0] == 0xff && pixel[1] == 0x01 && pixel[2] == 0xff);
}

void apply_neighborhood_layers(odh::BitmapImage& base,
        const std::vector<odh::NeighborhoodMapLayer>& layers, const std::string& archive,
        std::vector<std::string>& missing_layers) {
    for (auto& pixel : base.pixels)
        if (neighborhood_mask_color(pixel)) pixel = kNeighborhoodMatte;

    for (const auto& layer : layers) {
        std::optional<odh::BitmapImage> image;
        try {
            with_far_entry(archive, layer.resource, [&](const auto& view) {
                image = odh::read_bmp(view);
            });
        } catch (const FarEntryNotFound&) {
            // Decorative layers are optional in base-game-only installations.
            missing_layers.push_back(layer.resource);
            continue;
        }
        if (!image || image->pixels.size() !=
                static_cast<std::size_t>(image->width) * image->height)
            throw odh::FormatError("neighborhood layer has inconsistent pixel data");
        for (std::uint32_t y = 0; y < image->height; ++y) {
            const auto target_y = static_cast<std::int64_t>(layer.y) + y;
            if (target_y < 0 || target_y >= base.height) continue;
            for (std::uint32_t x = 0; x < image->width; ++x) {
                const auto target_x = static_cast<std::int64_t>(layer.x) + x;
                if (target_x < 0 || target_x >= base.width) continue;
                const auto& pixel = image->pixels[static_cast<std::size_t>(y) * image->width + x];
                if (!neighborhood_mask_color(pixel))
                    base.pixels[static_cast<std::size_t>(target_y) * base.width +
                                static_cast<std::size_t>(target_x)] = pixel;
            }
        }
    }
}

std::array<std::uint8_t, 32> neighborhood_identity(const odh::View& view) {
    odh::Sha256 hash;
    std::array<std::uint8_t, 64 * 1024> block{};
    for (std::uint64_t offset = 0; offset < view.size();) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            block.size(), view.size() - offset));
        view.read(offset, block.data(), count);
        hash.update(block.data(), count);
        offset += count;
    }
    return hash.finish();
}

std::string family_display_label(const odh::FamilyEntry& family) {
    if (!family.display_name.empty()) return family.display_name;
    return "Family " + std::to_string(family.family.family_number);
}
}

PreparedNeighborhoodMap prepare_neighborhood_map(
        const std::filesystem::path& user_data,
        const std::filesystem::path& ui_graphics, unsigned mode,
        const odh::FamilyOverlay* saved_overlay) {
    const auto config = odh::neighborhood_map_config(mode);
    if (!config) throw odh::FormatError("neighborhood map is not available for this mode");

    std::optional<odh::BitmapImage> image;
    with_far_entry(ui_graphics.string(), config->resource,
        [&](const auto& view) { image = odh::read_bmp(view); });
    if (!image) throw odh::FormatError("neighborhood map resource was not decoded");
    std::vector<std::string> missing_layers;
    apply_neighborhood_layers(*image, config->first_frame_layers, ui_graphics.string(),
                              missing_layers);
    for (const auto& resource : missing_layers)
        std::clog << "OpenDollhouse: neighborhood map layer is missing: " << resource << '\n';

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
    std::vector<odh::FamilyEntry> source_families;
    with_iff_file((user_data / "Neighborhood.iff").string(), [&](const auto& view) {
        const auto identity = saved_overlay ? neighborhood_identity(view) :
            std::array<std::uint8_t, 32>{};
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type != "FAMI") return;
            if (entries.size() >= 4096) throw odh::FormatError("too many FAMI chunks");
            entries.push_back({chunk.id, odh::read_fami(chunk.payload)});
        });
        source_families = entries;
        if (saved_overlay)
            entries = odh::apply_family_overlay(entries, identity, *saved_overlay);
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
            std::move(markers), config->marker_scale, std::move(source_families),
            std::move(entries), std::move(missing_layers)};
}

std::optional<std::uint8_t> show_language_picker(LinuxWindow& window,
        std::uint8_t initial_language) {
    static constexpr std::array<const char*,20> names{{
        "English (US)", "English (UK)", "Fran\u00e7ais", "Deutsch", "Italiano",
        "Espa\u00f1ol", "Nederlands", "Dansk", "Svenska", "Norsk", "Suomi",
        "\u05e2\u05d1\u05e8\u05d9\u05ea", "\u0420\u0443\u0441\u0441\u043a\u0438\u0439", "Portugu\u00eas",
        "\u65e5\u672c\u8a9e", "Polski", "\u7b80\u4f53\u4e2d\u6587", "\u7e41\u9ad4\u4e2d\u6587",
        "\u0e44\u0e17\u0e22", "\ud55c\uad6d\uc5b4"}};
#ifdef ODH_HAVE_LINUX_BITMAP_TEXT_FALLBACK
    static constexpr std::array<const char*,20> ascii_names{{
        "English (US)", "English (UK)", "Francais", "Deutsch", "Italiano",
        "Espanol", "Nederlands", "Dansk", "Svenska", "Norsk", "Suomi",
        "Hebrew", "Russian", "Portugues", "Japanese", "Polski",
        "Chinese Simplified", "Chinese Traditional", "Thai", "Korean"}};
    const auto& visible_names = ascii_names;
#else
    const auto& visible_names = names;
#endif
    if (initial_language < 1 || initial_language > visible_names.size())
        throw odh::FormatError("language picker requires a supported language code");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    std::size_t selected = static_cast<std::size_t>(initial_language - 1);
    constexpr std::size_t row_start = 56, row_height = 25;
    const auto render = [&]() {
        odh::RasterFrame frame{};
        frame.width = window.width(); frame.height = window.height();
        frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                            std::array<std::uint8_t,4>{35,39,48,255});
        frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
        for (std::size_t index = 0; index < visible_names.size(); ++index) {
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
        for (std::size_t index = 0; index < visible_names.size(); ++index) {
            style.y = static_cast<std::int32_t>(row_start + index * row_height);
            draw_text_overlay(frame, std::string(index == selected ? "> " : "  ") +
                std::to_string(index + 1) + "  " + visible_names[index],
                static_cast<std::uint8_t>(selected + 1), style);
        }
        style.y = static_cast<std::int32_t>(frame.height) - 32;
        draw_text_overlay(frame, "The game text will use this language; you can also set --language later",
            static_cast<std::uint8_t>(selected + 1), style);
#endif
        window.set_title(std::string("OpenDollhouse | Language ") +
            std::to_string(selected + 1) + " - " + visible_names[selected] +
            " | Tab/Shift+Tab: change, Enter: confirm, Esc: cancel");
        window.present(frame);
    };
    render();
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (input.interaction_selection_delta) {
            selected = odh::advance_resident_selection(selected,
                input.interaction_selection_delta, visible_names.size());
            render();
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if (input.primary_click) {
            const auto y = (*input.primary_click)[1];
            const auto row_end = row_start + visible_names.size() * row_height;
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

odh::RasterFrame compose_neighborhood_picker_frame(
        const PreparedNeighborhoodMap& map,
        const std::string& localized_title,
        const std::string& localized_cancel,
        std::uint8_t language) {
    if (map.frame.width != 800 || map.frame.height != 600 || language < 1 || language > 20)
        throw odh::FormatError("neighborhood frame requires 800x600 pixels and a supported language");
    auto frame = map.frame;
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle title_style;
    title_style.x = 12; title_style.y = 8; title_style.width = 300;
    title_style.height = 28; title_style.font_size_px = 17; title_style.padding = 5;
    title_style.foreground = {255,255,255,255}; title_style.background = {16,20,25,220};
    draw_text_overlay(frame, localized_title, language, title_style);
    TextOverlayStyle cancel_style = title_style;
    cancel_style.x = 800 - 12 - 180; cancel_style.y = 600 - 8 - 28;
    cancel_style.width = 180; cancel_style.font_size_px = 14;
    draw_text_overlay(frame, "Esc: " + localized_cancel, language, cancel_style);
#else
    (void)localized_title;
    (void)localized_cancel;
    (void)language;
#endif
    return frame;
}

std::optional<std::uint16_t> show_neighborhood_picker(
        LinuxWindow& window, const PreparedNeighborhoodMap& map,
        const std::string& localized_title,
        const std::string& localized_cancel,
        std::uint8_t language) {
    if (window.width() != 800 || window.height() != 600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    window.set_title("OpenDollhouse | " + localized_title + " | Esc: " + localized_cancel);
    const auto frame = compose_neighborhood_picker_frame(map, localized_title,
        localized_cancel, language);
    window.present(frame);
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (input.primary_click) {
            const auto selected = odh::pick_neighborhood_lot(map.markers, map.marker_scale,
                (*input.primary_click)[0], (*input.primary_click)[1]);
            if (selected) return selected;
        }
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<NeighborhoodPickerResult> show_neighborhood_picker_with_create(
        LinuxWindow& window,const PreparedNeighborhoodMap& map,
        const std::string& localized_title,const std::string& localized_cancel,
        std::uint8_t language) {
    if(window.width()!=800||window.height()!=600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    window.set_title("OpenDollhouse | "+localized_title+" | C: Create family | Esc: "+localized_cancel);
    auto frame=compose_neighborhood_picker_frame(map,localized_title,localized_cancel,language);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle style;style.x=20;style.y=558;style.width=760;style.height=28;
    style.font_size_px=15;style.padding=5;style.foreground={245,245,245,255};
    style.background={22,25,32,230};
    draw_text_overlay(frame,"C: Create a family",language,style);
#endif
    window.present(frame);
    for(;;) {
        const auto input=window.poll();
        if(input.quit)return std::nullopt;
        if(input.create_family)return NeighborhoodPickerResult{NeighborhoodPickerAction::create_family,0};
        if(input.primary_click) {
            const auto selected=odh::pick_neighborhood_lot(map.markers,map.marker_scale,
                (*input.primary_click)[0],(*input.primary_click)[1]);
            if(selected)return NeighborhoodPickerResult{NeighborhoodPickerAction::lot,*selected};
        }
        if(input.redraw)window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

OccupiedLotChoice show_occupied_lot_choice(LinuxWindow& window,
        const odh::FamilyEntry& family, std::uint16_t house_number,
        bool move_out_supported, std::uint8_t language) {
    const auto current_house = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(family.family.house_number) & 0xffffU);
    if (house_number == 0 || house_number > 98 || current_house != house_number ||
        language < 1 || language > 20)
        throw odh::FormatError("occupied-lot choice received an invalid family");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    odh::RasterFrame frame{};
    frame.width = window.width(); frame.height = window.height();
    frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                        std::array<std::uint8_t,4>{35, 39, 48, 255});
    frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle style;
    style.x = 24; style.width = static_cast<std::int32_t>(frame.width) - 48;
    style.height = 36; style.font_size_px = 18; style.padding = 7;
    style.foreground = {245,245,245,255}; style.background = {22,25,32,255};
    style.y = 156;
    draw_text_overlay(frame, "House " + std::to_string(house_number) + " | " +
        family_display_label(family), language, style);
    style.y = 220;
    draw_text_overlay(frame, "Enter or click: enter this house", language, style);
    style.y = 276;
    draw_text_overlay(frame, move_out_supported ?
        "E: move this family out" : "Move-out is unavailable for this family", language, style);
    style.y = 350;
    draw_text_overlay(frame, "Esc: cancel", language, style);
#else
    (void)language;
#endif
    window.set_title("OpenDollhouse | House " + std::to_string(house_number) +
        " | Enter: enter | " + (move_out_supported ? "E: move out" : "move-out unavailable") +
        " | Esc: cancel");
    window.present(frame);
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return OccupiedLotChoice::cancel;
        if (input.move_out && move_out_supported) return OccupiedLotChoice::move_out;
        if (input.action || input.primary_click) return OccupiedLotChoice::enter;
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<MoveInFamilySelection> show_move_in_family_picker(
        LinuxWindow& window, const std::vector<odh::FamilyEntry>& families,
        std::uint16_t house_number, std::int32_t purchase_price, std::uint8_t language,
        const std::string& localized_action, const std::string& localized_title,
        const std::string& localized_no_family_title,
        const std::string& localized_no_family_message,
        const std::string& localized_cancel) {
    if (!house_number || house_number > 98 || purchase_price < 0 || language < 1 || language > 20)
        throw odh::FormatError("family picker received an invalid move-in request");
    std::vector<const odh::FamilyEntry*> choices;
    const bool has_unhoused_family = std::any_of(families.begin(), families.end(), [](const auto& entry) {
        const auto current_house = static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(entry.family.house_number) & 0xffffU);
        return current_house == 0 && !entry.family.member_guids.empty();
    });
    for (const auto& entry : families) {
        const auto current_house = static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(entry.family.house_number) & 0xffffU);
        if (current_house == 0 && !entry.family.member_guids.empty() &&
            entry.family.budget >= purchase_price)
            choices.push_back(&entry);
    }
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    std::size_t selected = 0;
    constexpr std::size_t row_height = 36, row_start = 160;
    const std::size_t page_size = std::max<std::size_t>(1, (window.height() - 224) / row_height);
    const auto render = [&]() {
        odh::RasterFrame frame{};
        frame.width = window.width(); frame.height = window.height();
        frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                            std::array<std::uint8_t,4>{35, 39, 48, 255});
        frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
        for (std::size_t row = 0; row < page_size; ++row) {
            const auto index = (selected / page_size) * page_size + row;
            if (index >= choices.size()) break;
            const auto color = index == selected ? std::array<std::uint8_t,4>{47,93,138,255} :
                std::array<std::uint8_t,4>{52,57,68,255};
            const auto y0 = row_start + row * row_height;
            for (std::size_t y = y0; y < y0 + 32; ++y)
                for (std::size_t x = 24; x + 24 < frame.width; ++x)
                    frame.pixels[y * frame.width + x] = color;
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        TextOverlayStyle style;
        style.x = 24; style.width = static_cast<std::int32_t>(frame.width) - 48;
        style.height = 32; style.font_size_px = 17; style.padding = 7;
        style.foreground = {245,245,245,255}; style.background = {22,25,32,255};
        style.y = 24;
        draw_text_overlay(frame, choices.empty() && has_unhoused_family ?
                          localized_no_family_title : localized_title, language, style);
        style.y = 56;
        draw_text_overlay(frame, choices.empty() ?
            (has_unhoused_family ? localized_no_family_message :
                "No unhoused family with enough money is available") : localized_action,
            language, style);
        style.y = 88;
        draw_text_overlay(frame, choices.empty() ? "Esc: " + localized_cancel :
            "Enter or click to continue, Esc to cancel", language, style);
        style.y = 120;
        draw_text_overlay(frame, "House " + std::to_string(house_number) + " costs " +
            std::to_string(purchase_price) + " simoleons", language, style);
        for (std::size_t row = 0; row < page_size; ++row) {
            const auto index = (selected / page_size) * page_size + row;
            if (index >= choices.size()) break;
            const auto& family_entry = *choices[index];
            const auto& family = family_entry.family;
            const auto label = std::string(index == selected ? "> " : "  ") +
                family_display_label(family_entry) + " | " +
                std::to_string(family.member_guids.size()) + " residents | budget " +
                std::to_string(family.budget);
            style.y = static_cast<std::int32_t>(row_start + row * row_height);
            draw_text_overlay(frame, label, language, style);
        }
#else
        (void)language;
        (void)localized_action;
        (void)localized_title;
        (void)localized_no_family_title;
        (void)localized_no_family_message;
        (void)localized_cancel;
#endif
        const std::string title = choices.empty() ?
            "OpenDollhouse | no family can afford House " + std::to_string(house_number) + " | Esc: cancel" :
            "OpenDollhouse | choose family for House " + std::to_string(house_number) +
                " | budget after: " + std::to_string(choices[selected]->family.budget - purchase_price) +
                " | Tab: next, Enter: continue, Esc: cancel";
        window.set_title(title);
        window.present(frame);
    };
    render();
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (choices.empty()) {
            if (input.action || input.primary_click) return std::nullopt;
        } else {
            if (input.interaction_selection_delta) {
                selected = odh::advance_resident_selection(selected,
                    input.interaction_selection_delta, choices.size());
                render();
            }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
            if (input.primary_click) {
                const auto y = (*input.primary_click)[1];
                const std::size_t row_end = row_start + page_size * row_height;
                if (y >= static_cast<std::int32_t>(row_start) && y < static_cast<std::int32_t>(row_end)) {
                    const auto row = static_cast<std::size_t>(y - static_cast<std::int32_t>(row_start)) / row_height;
                    const auto index = (selected / page_size) * page_size + row;
                    if (index < choices.size()) {
                        const auto& entry = *choices[index];
                        return MoveInFamilySelection{entry.chunk_id,
                            odh::family_overlay_identity(entry)};
                    }
                }
            }
#endif
            if (input.action && !input.primary_click) {
                const auto& entry = *choices[selected];
                return MoveInFamilySelection{entry.chunk_id, odh::family_overlay_identity(entry)};
            }
        }
        if (input.redraw) render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

bool show_move_in_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
        std::uint16_t house_number, std::int32_t purchase_price, std::uint8_t language,
        const std::string& localized_title, const std::string& localized_question,
        const std::string& localized_yes, const std::string& localized_no) {
    if (house_number == 0 || house_number > 98 || purchase_price < 0 || language < 1 || language > 20 ||
        family.family.budget < purchase_price)
        throw odh::FormatError("move-in confirmation received an invalid purchase");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    odh::RasterFrame frame{};
    frame.width = window.width(); frame.height = window.height();
    frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                        std::array<std::uint8_t,4>{35, 39, 48, 255});
    frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle style;
    style.x = 24; style.width = static_cast<std::int32_t>(frame.width) - 48;
    style.height = 34; style.font_size_px = 18; style.padding = 7;
    style.foreground = {245,245,245,255}; style.background = {22,25,32,255};
    style.y = 130;
    draw_text_overlay(frame, localized_title, language, style);
    style.y = 188;
    draw_text_overlay(frame, localized_question, language, style);
    style.y = 236;
    draw_text_overlay(frame, family_display_label(family) +
        " | House " + std::to_string(house_number), language, style);
    style.y = 284;
    draw_text_overlay(frame, "Price: " + std::to_string(purchase_price) + " | Current budget: " +
        std::to_string(family.family.budget), language, style);
    style.y = 332;
    draw_text_overlay(frame, "Budget after purchase: " +
        std::to_string(family.family.budget - purchase_price), language, style);
    style.y = 390;
    draw_text_overlay(frame, localized_yes + " (Enter/click); " + localized_no + " (Esc)",
        language, style);
#else
    (void)language;
    (void)localized_title;
    (void)localized_question;
    (void)localized_yes;
    (void)localized_no;
#endif
    window.set_title("OpenDollhouse | confirm family move-in | House " +
        std::to_string(house_number) + " | price " + std::to_string(purchase_price) +
        " | budget after " + std::to_string(family.family.budget - purchase_price));
    window.present(frame);
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return false;
        if (input.action || input.primary_click) return true;
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

bool show_move_out_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
        std::uint16_t house_number, std::uint8_t language,
        const std::string& localized_title, const std::string& localized_yes,
        const std::string& localized_no) {
    if (house_number == 0 || house_number > 98 || language < 1 || language > 20 ||
        family.family.value_in_arch < 0 || family.family.budget < 0)
        throw odh::FormatError("move-out confirmation received invalid family data");
    const auto next_budget = static_cast<std::int64_t>(family.family.budget) +
        family.family.value_in_arch;
    if (next_budget > std::numeric_limits<std::int32_t>::max())
        throw odh::FormatError("move-out refund overflows the family budget");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    odh::RasterFrame frame{};
    frame.width = window.width(); frame.height = window.height();
    frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                        std::array<std::uint8_t,4>{35, 39, 48, 255});
    frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle style;
    style.x = 24; style.width = static_cast<std::int32_t>(frame.width) - 48;
    style.height = 36; style.font_size_px = 18; style.padding = 7;
    style.foreground = {245,245,245,255}; style.background = {22,25,32,255};
    style.y = 142;
    draw_text_overlay(frame, localized_title, language, style);
    style.y = 202;
    draw_text_overlay(frame, family_display_label(family) +
        " | House " + std::to_string(house_number), language, style);
    style.y = 246;
    draw_text_overlay(frame, "Refund: " + std::to_string(family.family.value_in_arch), language, style);
    style.y = 290;
    draw_text_overlay(frame, "Budget after move-out: " + std::to_string(next_budget), language, style);
    style.y = 340;
    draw_text_overlay(frame, localized_yes + " (Enter/click); " + localized_no + " (Esc)",
        language, style);
#else
    (void)language;
    (void)localized_title;
    (void)localized_yes;
    (void)localized_no;
#endif
    window.set_title("OpenDollhouse | confirm move-out | House " +
        std::to_string(house_number) + " | refund " +
        std::to_string(family.family.value_in_arch) + " | budget after " +
        std::to_string(next_budget));
    window.present(frame);
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return false;
        if (input.action || input.primary_click) return true;
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<std::uint16_t> show_neighborhood_resume_picker(
        LinuxWindow& window, const PreparedNeighborhoodMap& map, std::uint16_t active_house) {
    if (window.width() != 800 || window.height() != 600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    window.set_title("OpenDollhouse | select a lot; current lot " +
        std::to_string(active_house) + " resumes | Esc: close");
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
        frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
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
