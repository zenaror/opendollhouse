#pragma once
#include "odh/animation.hpp"
namespace odh {
struct LotLocation { std::uint16_t house; std::int32_t x, y; };
struct LotLocationLimits {
    std::uint32_t rows = 4096, row_bytes = 128;
    std::uint64_t payload_bytes = 1024 * 1024;
    std::uint16_t max_house = 99;
    std::int32_t coordinate_magnitude = 32767;
};
// Exact language, no fallback. House 99 is a validated, omitted sentinel.
// Coordinates are source pixels, not world tiles or transformed screen positions.
struct LotLocations {
    std::vector<LotLocation> lots;
    std::uint64_t remaining_bytes; // Unknown STR# tail, never interpreted.
};
LotLocations read_lot_locations(const View&, std::uint8_t language = 1,
                                           const LotLocationLimits& = {});
bool neighborhood_mode_supported(unsigned mode); // Throws outside 1..7; 6 unsupported.
}
