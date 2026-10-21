#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace odh {

struct RectRoutePoint {
    std::int32_t x = 0, y = 0;
};

struct RectRouteObstacle {
    std::int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

enum class RectRouteStatus : std::uint8_t {
    found, no_path, blocked_start, invalid_input, unsupported_coordinates, budget_exhausted
};

struct RectRouteLimits {
    std::size_t obstacles = 16384;
    std::size_t rectangles = 4096;
    std::size_t expansions = 4096;
    std::size_t free_regions = 65536;
    std::size_t work_units = 4194304;
};

struct RectRouteCorridorEntry {
    RectRouteObstacle rectangle;
    RectRoutePoint entry;
};

struct RectRouteResult {
    RectRouteStatus status = RectRouteStatus::invalid_input;
    std::vector<RectRouteCorridorEntry> corridor;
    std::size_t generated_rectangles = 0;
    std::size_t expanded_rectangles = 0;
    std::size_t work_units = 0;
};

// Reproduces Simitone VMObstacleSet.RoughBalanced's seeded shuffle for only
// the supplied architectural prefix, leaving AttemptWalk's appended bounds
// and entity obstacles in their original order. Returns false for an invalid
// prefix length and leaves the input untouched in that case.
bool rough_balance_obstacle_prefix(std::vector<RectRouteObstacle>& obstacles,
                                   std::size_t architectural_count);

// Pinned-Simitone corridor model, not original TS1 equivalence. Coordinates are
// integer world units and heading is 0=top, 1=right, 2=bottom, 3=left. Input order
// is the insertion order of the initial VMObstacleSet; callers reproducing a
// RoughBalanced room tree must supply its already-shuffled order. This API does
// not shuffle, optimize lines, traverse portals, or move an entity. Unsupported
// source arithmetic and resource limits fail closed without a partial corridor.
RectRouteResult route_rectangles(const std::vector<RectRouteObstacle>& insertion_order,
                                 RectRoutePoint from, RectRoutePoint to,
                                 std::uint8_t initial_heading,
                                 const RectRouteLimits& limits = {});

const char* rect_route_status_name(RectRouteStatus) noexcept;

} // namespace odh
