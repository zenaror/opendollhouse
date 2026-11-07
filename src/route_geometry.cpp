#include "odh/route_geometry.hpp"
#include "odh/prepared_architecture.hpp"

#include <cstddef>
#include <cstdint>

namespace odh {
namespace {
constexpr std::uint8_t segment_top_left = 1;
constexpr std::uint8_t segment_top_right = 2;
constexpr std::uint32_t maximum_dimension = 64;

template<class Architecture>
bool valid_architecture(const Architecture& architecture) {
    if (!architecture.width || !architecture.height ||
        architecture.width > maximum_dimension || architecture.height > maximum_dimension)
        return false;
    const auto cells = std::size_t(architecture.width) * architecture.height;
    for (const auto& level : architecture.levels) {
        if (level.floors.size() != cells || level.walls.size() != cells) return false;
    }
    return true;
}

template<class Architecture>
std::optional<std::uint16_t> precise_floor(
    const Architecture& architecture, std::int32_t x, std::int32_t y, std::int16_t level) {
    if (!valid_architecture(architecture) || level < 1 || level > 2 || x < 0 || y < 0)
        return std::nullopt;
    const auto tile_x = static_cast<std::uint32_t>(x / 16);
    const auto tile_y = static_cast<std::uint32_t>(y / 16);
    if (tile_x >= architecture.width || tile_y >= architecture.height) return std::nullopt;
    const auto offset = std::size_t(tile_y) * architecture.width + tile_x;
    const auto& wall = architecture.levels[static_cast<std::size_t>(level - 1)].walls[offset];
    const auto local_x = x % 16;
    const auto local_y = y % 16;
    if ((wall.segments & wall_vertical_diagonal) != 0)
        return local_x - local_y > 0 ? wall.top_left_pattern : wall.top_left_style;
    if ((wall.segments & wall_horizontal_diagonal) != 0)
        return local_x + local_y > 15 ? wall.top_left_pattern : wall.top_left_style;
    return architecture.levels[static_cast<std::size_t>(level - 1)].floors[offset];
}

std::optional<std::uint16_t> crossed_edge(
    const SavedArchitecture& architecture, const std::vector<ResolvedWallEdges>& resolved,
    std::int16_t level, std::int32_t x, std::int32_t y, bool top_left) {
    if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= architecture.width ||
        static_cast<std::uint32_t>(y) >= architecture.height)
        return std::uint16_t{0}; // VMArchitecture.GetWall returns the empty tile OOB.
    const auto offset = std::size_t(y) * architecture.width + static_cast<std::size_t>(x);
    const auto segment = top_left ? segment_top_left : segment_top_right;
    if ((architecture.levels[static_cast<std::size_t>(level - 1)].walls[offset].segments & segment) == 0)
        return std::uint16_t{0};
    const auto knowledge = top_left ? resolved[offset].top_left : resolved[offset].top_right;
    if (knowledge == WallEdgeKnowledge::unresolved) return std::nullopt;
    return knowledge == WallEdgeKnowledge::solid ? std::uint16_t{1} : std::uint16_t{0};
}

WallRaycastResult check_edge(
    const SavedArchitecture& architecture, const std::vector<ResolvedWallEdges>& resolved,
    std::int16_t level, std::int32_t x, std::int32_t y, bool top_left) {
    const auto solid = crossed_edge(architecture, resolved, level, x, y, top_left);
    if (!solid) return {WallRaycastState::unsupported, x, y, top_left,WallRaycastFailure::unresolved_edge};
    if (*solid) return {WallRaycastState::blocked, x, y, top_left};
    return {WallRaycastState::clear, -1, -1, false};
}
} // namespace

std::optional<std::uint16_t> saved_precise_floor(
    const SavedArchitecture& architecture, std::int32_t x, std::int32_t y, std::int16_t level) {
    return precise_floor(architecture, x, y, level);
}

std::optional<std::uint16_t> prepared_precise_floor(
    const PreparedArchitecture& architecture, std::int32_t x, std::int32_t y, std::int16_t level) {
    return precise_floor(architecture, x, y, level);
}

const char* wall_raycast_failure_name(WallRaycastFailure failure) noexcept {
    switch(failure){
    case WallRaycastFailure::none:return "none";
    case WallRaycastFailure::architecture:return "architecture";
    case WallRaycastFailure::level:return "level";
    case WallRaycastFailure::resolver_extent:return "resolver_extent";
    case WallRaycastFailure::coordinate_domain:return "coordinate_domain";
    case WallRaycastFailure::unresolved_edge:return "unresolved_edge";
    }
    return "unknown";
}
WallRaycastResult saved_wall_raycast(
    const SavedArchitecture& architecture, std::int16_t level,
    std::int32_t start_x, std::int32_t start_y, std::int32_t end_x, std::int32_t end_y,
    const std::vector<ResolvedWallEdges>& resolved) {
    if(!valid_architecture(architecture))return {WallRaycastState::unsupported,-1,-1,false,WallRaycastFailure::architecture};
    if(level<1||level>2)return {WallRaycastState::unsupported,-1,-1,false,WallRaycastFailure::level};
    if(resolved.size()!=std::size_t(architecture.width)*architecture.height)
        return {WallRaycastState::unsupported,-1,-1,false,WallRaycastFailure::resolver_extent};
    constexpr std::int32_t minimum_tile=-2048,maximum_tile=2047;
    const auto represented=[&](std::int32_t value){return value>=minimum_tile&&value<=maximum_tile;};
    if(!represented(start_x)||!represented(start_y)||!represented(end_x)||!represented(end_y))
        return {WallRaycastState::unsupported,-1,-1,false,WallRaycastFailure::coordinate_domain};

    // Bounded exact translation of the reference's integer Bresenham walk.
    auto x = start_x;
    auto y = start_y;
    const auto dx_signed = end_x - x;
    const auto dy_signed = end_y - y;
    const auto xstep = dx_signed < 0 ? -1 : 1;
    const auto ystep = dy_signed < 0 ? -1 : 1;
    const auto dx = dx_signed < 0 ? -dx_signed : dx_signed;
    const auto dy = dy_signed < 0 ? -dy_signed : dy_signed;
    const auto ddy = dy * 2;
    const auto ddx = dx * 2;
    const auto x_offset = xstep > 0 ? 1 : 0;
    const auto y_offset = ystep > 0 ? 1 : 0;
    if (ddx >= ddy) {
        auto error_prev = dx;
        auto error = dx;
        for (std::int32_t i = 0; i < dx; ++i) {
            const auto old_x = x;
            const auto old_y = y;
            x += xstep;
            error += ddy;
            if (error > ddx) {
                y += ystep;
                error -= ddx;
                if (error + error_prev < ddx) {
                    auto result = check_edge(architecture, resolved, level, old_x + x_offset, old_y, true);
                    if (result.state != WallRaycastState::clear) return result;
                    result = check_edge(architecture, resolved, level, x, old_y + y_offset, false);
                    if (result.state != WallRaycastState::clear) return result;
                } else {
                    auto result = check_edge(architecture, resolved, level, old_x, old_y + y_offset, false);
                    if (result.state != WallRaycastState::clear) return result;
                    result = check_edge(architecture, resolved, level, old_x + x_offset, y, true);
                    if (result.state != WallRaycastState::clear) return result;
                }
            } else {
                const auto result = check_edge(architecture, resolved, level, old_x + x_offset, old_y, true);
                if (result.state != WallRaycastState::clear) return result;
            }
            error_prev = error;
        }
    } else {
        auto error_prev = dy;
        auto error = dy;
        for (std::int32_t i = 0; i < dy; ++i) {
            const auto old_x = x;
            const auto old_y = y;
            y += ystep;
            error += ddx;
            if (error > ddy) {
                x += xstep;
                error -= ddy;
                if (error + error_prev < ddy) {
                    auto result = check_edge(architecture, resolved, level, old_x, old_y + y_offset, false);
                    if (result.state != WallRaycastState::clear) return result;
                    result = check_edge(architecture, resolved, level, old_x + x_offset, y, true);
                    if (result.state != WallRaycastState::clear) return result;
                } else {
                    auto result = check_edge(architecture, resolved, level, old_x + x_offset, old_y, true);
                    if (result.state != WallRaycastState::clear) return result;
                    result = check_edge(architecture, resolved, level, x, old_y + y_offset, false);
                    if (result.state != WallRaycastState::clear) return result;
                }
            } else {
                const auto result = check_edge(architecture, resolved, level, old_x, old_y + y_offset, false);
                if (result.state != WallRaycastState::clear) return result;
            }
            error_prev = error;
        }
    }
    return {WallRaycastState::clear, -1, -1, false};
}
} // namespace odh
