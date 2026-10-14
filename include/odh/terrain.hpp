#pragma once
#include "odh/saved_architecture.hpp"
#include <cstdint>

namespace odh {
// Pure altitude sampling in tile units. The TS1 reference loader expands ARRY0
// byte 1 by ten, then applies (height-base_alt)*3/160. Default base_alt=0 is
// reference-loader policy, not evidence about the original game's renderer.
// Missing/malformed grids and invalid sample coordinates throw runtime_error.
float terrain_corner_altitude(const SavedArchitecture& architecture,
    std::uint32_t x, std::uint32_t y, std::int32_t base_alt=0);
float terrain_interp_altitude(const SavedArchitecture& architecture,
    float x, float y, std::int32_t base_alt=0);
}
