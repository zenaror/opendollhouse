#pragma once
#include "odh/lot_locations.hpp"
#include "odh/raster.hpp"
namespace odh {
struct NeighborhoodMapConfig { std::string resource; unsigned marker_scale = 1; };
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
