#include "neighborhood_screen.hpp"

#include "odh/family.hpp"
#include "odh/neighborhood_picker.hpp"
#include "resource.hpp"

#include <algorithm>
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

} // namespace odh_host
