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
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
#include "linux_text_codec.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <thread>
#include <utility>

namespace odh_host {
namespace {
constexpr std::array<std::uint8_t, 4> kNeighborhoodMatte{32, 36, 44, 255};
constexpr std::array<unsigned, 6> kPickerModes{1, 2, 3, 4, 5, 7};
constexpr std::array<const char*, 6> kPickerModeNames{
    "Neighborhood", "Downtown", "Vacation Island", "Old Town", "Studio Town", "Magic Town"};
constexpr std::int32_t kModeTabTop = 508;
constexpr std::int32_t kModeTabHeight = 40;
constexpr std::int32_t kModeTabLeft = 16;
constexpr std::int32_t kModeTabWidth = 126;
constexpr std::int32_t kModeTabGap = 2;
// These hit areas follow the three empty button wells in Nbhd\PickBkg.bmp.
constexpr std::int32_t kFamilyPickerButtonTop = 530;
constexpr std::int32_t kFamilyPickerButtonBottom = 592;
constexpr std::int32_t kFamilyPickerCancelLeft = 320;
constexpr std::int32_t kFamilyPickerArchiveLeft = 414;
constexpr std::int32_t kFamilyPickerMoveInLeft = 506;
constexpr std::int32_t kFamilyPickerButtonRight = 670;

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
        const odh::FamilyOverlay* saved_overlay, std::uint8_t language) {
    const auto config = odh::neighborhood_map_config(mode);
    if (!config) throw odh::FormatError("neighborhood map is not available for this mode");
    if (language < 1 || language > 20)
        throw odh::FormatError("neighborhood map language is outside the supported range");

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
    std::map<std::uint16_t, std::string> family_names;
    with_iff_file((user_data / "Neighborhood.iff").string(), [&](const auto& view) {
        const auto identity = saved_overlay ? neighborhood_identity(view) :
            std::array<std::uint8_t, 32>{};
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type == "FAMs") {
                if (family_names.count(chunk.id))
                    throw odh::FormatError("ambiguous FAMs family name chunk");
                const auto table = odh::read_strings(chunk.payload);
                if (const auto* name = odh::string_in_language_or_english(table, language, 0)) {
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
                    family_names.emplace(chunk.id, legacy_text_to_utf8(name->value, language));
#else
                    family_names.emplace(chunk.id, name->value);
#endif
                }
                return;
            }
            if (chunk.type != "FAMI") return;
            if (entries.size() >= 4096) throw odh::FormatError("too many FAMI chunks");
            entries.push_back({chunk.id, odh::read_fami(chunk.payload)});
        });
        for (auto& entry : entries) {
            const auto name = family_names.find(entry.chunk_id);
            if (name != family_names.end()) entry.display_name = name->second;
        }
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
            std::move(markers), config->marker_scale, mode, std::move(source_families),
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
    return show_neighborhood_picker_with_actions(window, map, localized_title,
        localized_cancel, language, true);
}

unsigned neighborhood_mode_button_at(std::int32_t x, std::int32_t y) noexcept {
    if (y < kModeTabTop || y >= kModeTabTop + kModeTabHeight || x < kModeTabLeft)
        return 0;
    const auto stride = kModeTabWidth + kModeTabGap;
    const auto index = (x - kModeTabLeft) / stride;
    if (index < 0 || index >= static_cast<std::int32_t>(kPickerModes.size())) return 0;
    const auto offset = (x - kModeTabLeft) % stride;
    if (offset >= kModeTabWidth) return 0;
    return kPickerModes[static_cast<std::size_t>(index)];
}

std::optional<NeighborhoodPickerResult> show_neighborhood_picker_with_actions(
        LinuxWindow& window,const PreparedNeighborhoodMap& map,
        const std::string& localized_title,const std::string& localized_cancel,
        std::uint8_t language, bool allow_create_family) {
    if(window.width()!=800||window.height()!=600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    if (!odh::neighborhood_mode_supported(map.mode))
        throw odh::FormatError("neighborhood picker received an unsupported mode");
    const auto mode_name = [&](unsigned mode) {
        const auto found = std::find(kPickerModes.begin(), kPickerModes.end(), mode);
        return found == kPickerModes.end() ? std::string("Unknown") :
            std::string(kPickerModeNames[static_cast<std::size_t>(found - kPickerModes.begin())]);
    };
    window.set_title("OpenDollhouse | "+localized_title+" | "+mode_name(map.mode)+
        (allow_create_family ? " | C: Create family" : "")+" | Esc: "+localized_cancel);
    auto frame=compose_neighborhood_picker_frame(map,localized_title,localized_cancel,language);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    for (std::size_t i = 0; i < kPickerModes.size(); ++i) {
        const auto x = kModeTabLeft + static_cast<std::int32_t>(i) *
            (kModeTabWidth + kModeTabGap);
        const auto mode = kPickerModes[i];
        const std::array<std::uint8_t, 4> color = mode == map.mode ?
            std::array<std::uint8_t, 4>{47, 93, 138, 255} :
            std::array<std::uint8_t, 4>{22, 25, 32, 255};
        for (std::int32_t y = kModeTabTop; y < kModeTabTop + kModeTabHeight; ++y)
            for (std::int32_t dx = 0; dx < kModeTabWidth; ++dx)
                frame.pixels[static_cast<std::size_t>(y) * frame.width +
                    static_cast<std::size_t>(x + dx)] = color;
        TextOverlayStyle mode_style;
        mode_style.x = x + 2; mode_style.y = kModeTabTop + 5;
        mode_style.width = kModeTabWidth - 4; mode_style.height = 30;
        mode_style.font_size_px = 13; mode_style.padding = 3;
        mode_style.foreground = {245, 245, 245, 255};
        mode_style.background = {0, 0, 0, 0};
        draw_text_overlay(frame, std::to_string(mode) + " " + kPickerModeNames[i],
            language, mode_style);
    }
    TextOverlayStyle style;style.x=20;style.y=558;style.width=760;style.height=28;
    style.font_size_px=15;style.padding=5;style.foreground={245,245,245,255};
    style.background={22,25,32,230};
    draw_text_overlay(frame, allow_create_family ?
        "Click a map tab to switch areas | C: Create a family" :
        "Click a map tab to switch areas", language, style);
#endif
    window.present(frame);
    for(;;) {
        const auto input=window.poll(allow_create_family);
        if(input.quit)return std::nullopt;
        if(allow_create_family && input.create_family)
            return NeighborhoodPickerResult{NeighborhoodPickerAction::create_family,0,0};
        if(input.primary_click) {
            const auto selected_mode = neighborhood_mode_button_at(
                (*input.primary_click)[0], (*input.primary_click)[1]);
            if (selected_mode) {
                if (selected_mode != map.mode)
                    return NeighborhoodPickerResult{NeighborhoodPickerAction::switch_mode,0,selected_mode};
                continue;
            }
            const auto selected=odh::pick_neighborhood_lot(map.markers,map.marker_scale,
                (*input.primary_click)[0],(*input.primary_click)[1]);
            if(selected)return NeighborhoodPickerResult{NeighborhoodPickerAction::lot,*selected,0};
        }
        if(input.redraw)window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

NeighborhoodLotPanelChoice show_neighborhood_lot_panel(LinuxWindow& window,
        const PreparedNeighborhoodMap& map, std::uint16_t house_number,
        const std::string& title, const std::string& description,
        const std::string& summary, const std::string& localized_accept,
        const std::string& localized_back, std::uint8_t language) {
    if (map.frame.width != 800 || map.frame.height != 600 ||
        !odh::neighborhood_mode_supported(map.mode) || house_number == 0 || house_number > 98 ||
        title.empty() || language < 1 || language > 20)
        throw odh::FormatError("lot details panel received invalid neighborhood data");
    const auto marker = std::find_if(map.markers.begin(), map.markers.end(),
        [&](const auto& value) { return value.location.house == house_number; });
    if (marker == map.markers.end())
        throw odh::FormatError("lot details panel house is absent from the selected map");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);

    auto frame = map.frame;
    constexpr std::int32_t panel_x = 20, panel_y = 92, panel_w = 420, panel_h = 438;
    for (std::int32_t y = panel_y; y < panel_y + panel_h; ++y)
        for (std::int32_t x = panel_x; x < panel_x + panel_w; ++x)
            frame.pixels[static_cast<std::size_t>(y) * frame.width +
                         static_cast<std::size_t>(x)] = {28, 33, 42, 255};
    for (std::int32_t x = panel_x; x < panel_x + panel_w; ++x)
        frame.pixels[static_cast<std::size_t>(panel_y) * frame.width +
                     static_cast<std::size_t>(x)] = {81, 143, 162, 255};
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle style;
    style.x = panel_x + 18; style.width = panel_w - 36; style.height = 34;
    style.font_size_px = 21; style.padding = 3;
    style.foreground = {248, 248, 248, 255}; style.background = {28, 33, 42, 0};
    style.y = panel_y + 20;
    draw_text_overlay(frame, title, language, style);
    style.font_size_px = 14; style.height = 29;
    style.foreground = {220, 225, 229, 255};
    std::size_t line = 0;
    std::size_t begin = 0;
    while (begin < description.size() && line < 4) {
        const auto end = description.find_first_of("\r\n", begin);
        const auto part = description.substr(begin, end == std::string::npos ? end : end - begin);
        if (!part.empty()) {
            style.y = panel_y + 66 + static_cast<std::int32_t>(line++) * 28;
            draw_text_overlay(frame, part, language, style);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
        if (description[end] == '\r' && begin < description.size() && description[begin] == '\n')
            ++begin;
    }
    line = 0;
    begin = 0;
    style.font_size_px = 15; style.height = 32;
    style.foreground = {245, 245, 245, 255};
    while (begin < summary.size() && line < 5) {
        const auto end = summary.find_first_of("\r\n", begin);
        const auto part = summary.substr(begin, end == std::string::npos ? end : end - begin);
        if (!part.empty()) {
            style.y = panel_y + 190 + static_cast<std::int32_t>(line++) * 34;
            draw_text_overlay(frame, part, language, style);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
        if (summary[end] == '\r' && begin < summary.size() && summary[begin] == '\n') ++begin;
    }
    const auto paint_button_y = panel_y + panel_h - 58;
    for (std::int32_t y = paint_button_y; y < paint_button_y + 38; ++y) {
        for (std::int32_t x = panel_x + 18; x < panel_x + 202; ++x)
            frame.pixels[static_cast<std::size_t>(y) * frame.width +
                         static_cast<std::size_t>(x)] = {47, 93, 138, 255};
        for (std::int32_t x = panel_x + 210; x < panel_x + panel_w - 18; ++x)
            frame.pixels[static_cast<std::size_t>(y) * frame.width +
                         static_cast<std::size_t>(x)] = {52, 57, 68, 255};
    }
    style.x = panel_x + 26; style.y = paint_button_y + 5; style.width = 166;
    style.height = 28; style.font_size_px = 15; style.padding = 2;
    style.background = {0, 0, 0, 0}; style.foreground = {255, 255, 255, 255};
    draw_text_overlay(frame, localized_accept, language, style);
    style.x = panel_x + 218; style.width = panel_w - 244;
    draw_text_overlay(frame, localized_back, language, style);
#endif
    window.set_title("OpenDollhouse | " + title + " | Enter: " + localized_accept +
        " | Esc: " + localized_back);
    window.present(frame);
    constexpr std::int32_t button_y = panel_y + panel_h - 58;
    for (;;) {
        const auto input = window.poll();
        if (input.quit) {
            if (input.quit_reason == WindowQuitReason::escape_key)
                window.clear_escape_quit_reason();
            return NeighborhoodLotPanelChoice::back_to_map;
        }
        if (input.confirm) return NeighborhoodLotPanelChoice::continue_to_lot;
        if (input.primary_click) {
            const auto x = (*input.primary_click)[0], y = (*input.primary_click)[1];
            if (y >= button_y && y < button_y + 38) {
                if (x >= panel_x + 18 && x < panel_x + 202)
                    return NeighborhoodLotPanelChoice::continue_to_lot;
                if (x >= panel_x + 210 && x < panel_x + panel_w - 18)
                    return NeighborhoodLotPanelChoice::back_to_map;
            }
        }
        if (input.redraw) window.present(frame);
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
        const std::string& localized_cancel,
        const std::filesystem::path& ui_graphics,
        const std::string& localized_net_worth,
        const std::string& localized_family_friends,
        const std::string& localized_archive,
        const std::vector<std::uint16_t>& deletable_family_ids) {
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
        if (current_house == 0 && !entry.family.member_guids.empty())
            choices.push_back(&entry);
    }
    const auto can_move_in = [&](const odh::FamilyEntry& entry) {
        return entry.family.budget >= purchase_price;
    };
    const auto can_archive = [&](const odh::FamilyEntry& entry) {
        return std::find(deletable_family_ids.begin(), deletable_family_ids.end(),
            entry.chunk_id) != deletable_family_ids.end();
    };
    std::optional<odh::BitmapImage> picker_background;
    std::optional<odh::BitmapImage> family_row_background;
    if (!ui_graphics.empty()) {
        try {
            with_far_entry(ui_graphics.string(), R"(Nbhd\PickBkg.bmp)",
                [&](const auto& view) { picker_background = odh::read_bmp(view); });
            if (!picker_background || picker_background->width != 800 ||
                picker_background->height != 600 ||
                picker_background->pixels.size() != 800U * 600U)
                throw odh::FormatError("original family-picker background has an unsupported size");
        } catch (const FarEntryNotFound&) {
            std::clog << "OpenDollhouse: original family-picker background is missing; using plain fallback\n";
        }
        try {
            with_far_entry(ui_graphics.string(), R"(Nbhd\FamilyPickItemBkg.bmp)",
                [&](const auto& view) { family_row_background = odh::read_bmp(view); });
            if (!family_row_background || family_row_background->width != 690 ||
                family_row_background->height != 204 ||
                family_row_background->pixels.size() != 690U * 204U)
                throw odh::FormatError("original family-picker row background has an unsupported size");
        } catch (const FarEntryNotFound&) {
            std::clog << "OpenDollhouse: original family-picker row background is missing; using plain rows\n";
        }
    }
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    std::size_t selected = 0;
    constexpr std::size_t row_height = 68, row_start = 100;
    const std::size_t page_size = std::max<std::size_t>(1, (window.height() - 232) / row_height);
    const auto render = [&]() {
        odh::RasterFrame frame{};
        frame.width = window.width(); frame.height = window.height();
        if (picker_background) frame.pixels = picker_background->pixels;
        else frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                                 std::array<std::uint8_t,4>{35, 39, 48, 255});
        frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
        for (std::size_t row = 0; row < page_size; ++row) {
            const auto index = (selected / page_size) * page_size + row;
            if (index >= choices.size()) break;
            const auto y0 = row_start + row * row_height;
            if (family_row_background) {
                const std::size_t state_y = index == selected ? 136 : 0;
                for (std::size_t y = 0; y < row_height; ++y)
                    for (std::size_t x = 0; x < family_row_background->width; ++x)
                        frame.pixels[(y0 + y) * frame.width + 30 + x] =
                            family_row_background->pixels[(state_y + y) *
                                family_row_background->width + x];
            } else {
                const auto color = index == selected ? std::array<std::uint8_t,4>{26,47,91,255} :
                    std::array<std::uint8_t,4>{7,14,49,255};
                for (std::size_t y = y0 + 8; y < y0 + row_height - 8; ++y)
                    for (std::size_t x = 32; x < frame.width - 32; ++x)
                        frame.pixels[y * frame.width + x] = color;
            }
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        TextOverlayStyle style;
        style.x = 24; style.width = static_cast<std::int32_t>(frame.width) - 48;
        style.height = 30; style.font_size_px = 16; style.padding = 2;
        style.foreground = {245,245,245,255}; style.background = {0,0,0,0};
        style.x = 30; style.width = 690; style.y = 20;
        draw_text_overlay(frame, choices.empty() && has_unhoused_family ?
                          localized_no_family_title : localized_title, language, style);
        const bool any_affordable = std::any_of(choices.begin(), choices.end(), [&](const auto* entry) {
            return can_move_in(*entry);
        });
        if (choices.empty() || !any_affordable) {
            style.y = 56;
            draw_text_overlay(frame, localized_no_family_message, language, style);
        } else {
            style.font_size_px = 12; style.height = 22; style.y = 66;
            style.x = 554; style.width = 86;
            draw_text_overlay(frame, localized_net_worth, language, style);
            // The translated family-friends caption is longer than the icon
            // column on the original background, so use a smaller font to
            // keep the whole label visible in every supported language.
            style.font_size_px = 9; style.x = 620; style.width = 102;
            draw_text_overlay(frame, localized_family_friends, language, style);
        }
        style.font_size_px = 16; style.height = 30;
        for (std::size_t row = 0; row < page_size; ++row) {
            const auto index = (selected / page_size) * page_size + row;
            if (index >= choices.size()) break;
            const auto& family_entry = *choices[index];
            const auto& family = family_entry.family;
            const auto y = static_cast<std::int32_t>(row_start + row * row_height + 18);
            style.y = y; style.x = 48; style.width = 490;
            draw_text_overlay(frame, family_display_label(family_entry), language, style);
            const auto net_worth = static_cast<std::int64_t>(family.value_in_arch) + family.budget;
            style.x = 554; style.width = 86;
            draw_text_overlay(frame, "§" + std::to_string(net_worth), language, style);
            style.x = 644; style.width = 78;
            draw_text_overlay(frame, std::to_string(family.family_friends), language, style);
        }
        // Keep the localized captions inside the original background's three
        // bottom controls instead of drifting over the decorative frame.
        style.font_size_px = 12; style.x = kFamilyPickerCancelLeft;
        style.y = 544; style.width = kFamilyPickerArchiveLeft - kFamilyPickerCancelLeft;
        style.height = 30;
        draw_text_overlay(frame, localized_cancel, language, style);
        if (!choices.empty() && can_archive(*choices[selected])) {
            style.font_size_px = 10; style.x = kFamilyPickerArchiveLeft;
            style.width = kFamilyPickerMoveInLeft - kFamilyPickerArchiveLeft;
            draw_text_overlay(frame, localized_archive, language, style);
        }
        if (!choices.empty() && can_move_in(*choices[selected])) {
            style.font_size_px = 11; style.x = kFamilyPickerMoveInLeft;
            style.width = kFamilyPickerButtonRight - kFamilyPickerMoveInLeft;
            draw_text_overlay(frame, localized_action, language, style);
        }
#else
        (void)language;
        (void)localized_action;
        (void)localized_title;
        (void)localized_no_family_title;
        (void)localized_no_family_message;
        (void)localized_cancel;
        (void)localized_archive;
#endif
        const std::string title = choices.empty() ?
            "OpenDollhouse | no family can afford House " + std::to_string(house_number) + " | Esc: cancel" :
            "OpenDollhouse | choose family for House " + std::to_string(house_number) +
            " | Tab: next, Enter: continue, Esc: cancel";
        window.set_title(title);
        window.present(frame);
    };
    render();
    for (;;) {
        const auto input = window.poll();
        if (input.quit) {
            if (input.quit_reason == WindowQuitReason::escape_key)
                window.clear_escape_quit_reason();
            return std::nullopt;
        }
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
                const auto x = (*input.primary_click)[0];
                const auto y = (*input.primary_click)[1];
                if (y >= kFamilyPickerButtonTop && y < kFamilyPickerButtonBottom &&
                    x >= kFamilyPickerCancelLeft && x < kFamilyPickerArchiveLeft)
                    return std::nullopt;
                if (y >= kFamilyPickerButtonTop && y < kFamilyPickerButtonBottom &&
                    x >= kFamilyPickerArchiveLeft && x < kFamilyPickerMoveInLeft &&
                    !choices.empty() && can_archive(*choices[selected])) {
                    const auto& entry = *choices[selected];
                    return MoveInFamilySelection{entry.chunk_id,
                        odh::family_overlay_identity(entry), MoveInFamilySelection::Action::archive};
                }
                if (y >= kFamilyPickerButtonTop && y < kFamilyPickerButtonBottom &&
                    x >= kFamilyPickerMoveInLeft && x < kFamilyPickerButtonRight &&
                    !choices.empty() && can_move_in(*choices[selected])) {
                    const auto& entry = *choices[selected];
                    return MoveInFamilySelection{entry.chunk_id,
                        odh::family_overlay_identity(entry), MoveInFamilySelection::Action::move_in};
                }
                const std::size_t row_end = row_start + page_size * row_height;
                if (y >= static_cast<std::int32_t>(row_start) && y < static_cast<std::int32_t>(row_end)) {
                    const auto row = static_cast<std::size_t>(y - static_cast<std::int32_t>(row_start)) / row_height;
                    const auto index = (selected / page_size) * page_size + row;
                    if (index < choices.size()) {
                        selected = index;
                        render();
                    }
                }
            }
#endif
            if (input.action && !input.primary_click) {
                const auto& entry = *choices[selected];
                if (can_move_in(entry))
                    return MoveInFamilySelection{entry.chunk_id,
                        odh::family_overlay_identity(entry), MoveInFamilySelection::Action::move_in};
            }
        }
        if (input.redraw) render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

bool show_family_archive_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
        std::uint8_t language, const std::string& localized_title,
        const std::string& localized_question, const std::string& localized_yes,
        const std::string& localized_no, const std::filesystem::path& ui_graphics) {
    if (language < 1 || language > 20 ||
        (static_cast<std::uint32_t>(family.family.house_number) & 0xffffU) != 0 ||
        family.family.member_guids.empty())
        throw odh::FormatError("family deletion confirmation received invalid family data");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    odh::RasterFrame frame{};
    frame.width = window.width(); frame.height = window.height();
    if (!ui_graphics.empty()) {
        try {
            with_far_entry(ui_graphics.string(), R"(Nbhd\PickBkg.bmp)",
                [&](const auto& view) {
                    const auto background = odh::read_bmp(view);
                    if (background.width != frame.width || background.height != frame.height)
                        throw odh::FormatError("original family-picker background has an unsupported size");
                    frame.pixels = background.pixels;
                });
        } catch (const FarEntryNotFound&) {
            std::clog << "OpenDollhouse: original family-picker background is missing; using plain fallback\n";
        }
    }
    if (frame.pixels.size() != static_cast<std::size_t>(frame.width) * frame.height)
        frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                            std::array<std::uint8_t,4>{35, 39, 48, 255});
    frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle style;
    style.x = 48; style.width = 704; style.height = 38;
    style.font_size_px = 18; style.padding = 6;
    style.foreground = {245,245,245,255}; style.background = {22,25,32,220};
    style.y = 168;
    draw_text_overlay(frame, localized_title, language, style);
    style.y = 226; style.height = 64;
    draw_text_overlay(frame, localized_question, language, style);
    style.y = 310; style.height = 34;
    draw_text_overlay(frame, family_display_label(family), language, style);
    // The supplied PickBkg background has its controls along the bottom edge.
    // Use the middle well for No and the right well for Yes.
    style.font_size_px = 12; style.height = 30; style.y = 544;
    style.x = kFamilyPickerArchiveLeft;
    style.width = kFamilyPickerMoveInLeft - kFamilyPickerArchiveLeft;
    draw_text_overlay(frame, localized_no, language, style);
    style.x = kFamilyPickerMoveInLeft;
    style.width = kFamilyPickerButtonRight - kFamilyPickerMoveInLeft;
    draw_text_overlay(frame, localized_yes, language, style);
#else
    (void)language;
    (void)localized_title;
    (void)localized_question;
    (void)localized_yes;
    (void)localized_no;
#endif
    window.set_title("OpenDollhouse | confirm family deletion");
    window.present(frame);
    for (;;) {
        const auto input = window.poll();
        if (input.quit) {
            if (input.quit_reason == WindowQuitReason::escape_key)
                window.clear_escape_quit_reason();
            return false;
        }
        if (input.primary_click) {
            const auto x = (*input.primary_click)[0];
            const auto y = (*input.primary_click)[1];
            if (y >= kFamilyPickerButtonTop && y < kFamilyPickerButtonBottom &&
                x >= kFamilyPickerArchiveLeft && x < kFamilyPickerMoveInLeft) return false;
            if (y >= kFamilyPickerButtonTop && y < kFamilyPickerButtonBottom &&
                x >= kFamilyPickerMoveInLeft && x < kFamilyPickerButtonRight) return true;
        } else if (input.confirm || input.action) {
            return true;
        }
        if (input.redraw) window.present(frame);
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
