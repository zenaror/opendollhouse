#pragma once
#include "odh/architecture_arrays.hpp"
#include <array>
#include <optional>

namespace odh {
struct SavedArchitectureArray { std::uint16_t id = 0; ArchitectureArray array; };
enum SavedWallSegment : std::uint8_t {
    wall_top_left=1, wall_top_right=2, wall_bottom_right=4, wall_bottom_left=8,
    wall_horizontal_diagonal=16, wall_vertical_diagonal=32
};
struct SavedWallTile {
    std::uint8_t segments=0, unknown=0;
    std::uint16_t top_left_style=0, top_right_style=0;
    std::uint16_t top_left_pattern=0, top_right_pattern=0;
    std::uint16_t bottom_left_pattern=0, bottom_right_pattern=0;
};
struct SavedArchitectureLevel {
    std::vector<std::uint16_t> floors;
    std::vector<SavedWallTile> walls;
    std::optional<std::vector<std::uint16_t>> object_ids;
    std::optional<std::vector<std::uint8_t>> flags;
};
struct SavedArchitectureLimits {
    std::size_t arrays=32;
    std::uint32_t dimension=64;
    std::size_t retained_bytes=2*1024*1024;
};
struct SavedArchitecture {
    std::uint32_t width=0, height=0;
    bool advanced=false;
    // All typed grids have row-major index y*width+x. No road flips, resizing,
    // style clamping, resource remapping, grass conversion or water substitution.
    std::array<SavedArchitectureLevel,2> levels;
    std::optional<std::vector<std::array<std::uint8_t,4>>> altitude;
    std::optional<std::vector<std::uint8_t>> grass, target_grass, pools, water;
    // Original arrays own encoded, decoded, trailing bytes and opaque metadata,
    // including unknown IDs. Duplicate IDs are rejected before interpretation.
    std::vector<SavedArchitectureArray> records;
};
SavedArchitecture interpret_saved_architecture(
    const std::vector<SavedArchitectureArray>& arrays,
    const SavedArchitectureLimits& limits={});
}
