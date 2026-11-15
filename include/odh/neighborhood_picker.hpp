#pragma once
#include "odh/lot_locations.hpp"
#include "odh/raster.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace odh {
struct NeighborhoodMapLayer {
    std::string resource;
    std::int32_t x = 0, y = 0;
};
struct NeighborhoodMapConfig {
    std::string resource;
    unsigned marker_scale = 1;
    std::vector<NeighborhoodMapLayer> first_frame_layers;
    std::vector<std::string> animation_frames;
    bool animation_pulsates = true;
    unsigned animation_frame_ms = 250; // fixed Simitone profile: 15 ticks at 60 Hz
};
// Exact FAR spelling. Mode 6 deliberately has no configured map.
std::optional<NeighborhoodMapConfig> neighborhood_map_config(unsigned mode);
struct NeighborhoodMarker {
    LotLocation location;
    bool file_exists = false;
    bool occupied = false; // FAMI association, not runtime occupancy.
};
// Fixed 800x600 coordinates; ODH squares, not original UI sprites.
// Later markers draw on top and win overlapping hits. Missing files never select.
std::optional<std::uint16_t> pick_neighborhood_lot(
    const std::vector<NeighborhoodMarker>&, unsigned marker_scale, std::int32_t x, std::int32_t y);
RasterFrame draw_neighborhood_map(const BitmapImage&, const std::vector<NeighborhoodMarker>&, unsigned marker_scale);
}
