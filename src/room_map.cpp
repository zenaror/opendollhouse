#include "odh/room_map.hpp"
#include "odh/prepared_architecture.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace odh {
namespace {
constexpr std::uint8_t any_adj = 15, any_diag = 48;
struct SpreadItem { std::uint32_t x, y; std::uint8_t directions; };

void add_unique(std::vector<std::uint16_t>& values, std::uint16_t value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

template<class Level>
void spread_onto(const Level& level, std::uint32_t width,
                 std::uint32_t x, std::uint32_t y, unsigned direction,
                 std::vector<std::uint32_t>& map, std::vector<SpreadItem>& spread,
                 std::uint16_t room, std::uint16_t expected, bool no_air,
                 std::vector<std::uint16_t>& adjacent, bool force_adjacent) {
    const auto index = std::size_t(y) * width + x;
    const auto& wall = level.walls[index];
    const bool expectation = (expected == 0 && no_air) || expected > 65533;
    std::uint16_t target_room, target_floor;
    std::uint32_t apply;
    std::uint8_t directions;
    if (wall.segments & wall_horizontal_diagonal) {
        if (direction < 2) {
            directions = wall_top_left | wall_top_right;
            target_floor = wall.top_left_style;
            target_room = static_cast<std::uint16_t>(map[index]);
            apply = room | 0x80000000U;
        } else {
            directions = wall_bottom_left | wall_bottom_right;
            target_floor = wall.top_left_pattern;
            target_room = static_cast<std::uint16_t>((map[index] >> 16) & 0x7fffU);
            apply = (std::uint32_t(room) << 16) | 0x80000000U;
        }
    } else if (wall.segments & wall_vertical_diagonal) {
        if (direction > 0 && direction < 3) {
            directions = wall_top_right | wall_bottom_right;
            target_floor = wall.top_left_pattern;
            target_room = static_cast<std::uint16_t>(map[index]);
            apply = room;
        } else {
            directions = wall_top_left | wall_bottom_left;
            target_floor = wall.top_left_style;
            target_room = static_cast<std::uint16_t>((map[index] >> 16) & 0x7fffU);
            apply = std::uint32_t(room) << 16;
        }
    } else {
        directions = any_adj;
        target_room = static_cast<std::uint16_t>(map[index]);
        target_floor = level.floors[index];
        apply = room | (std::uint32_t(room) << 16);
    }
    const bool cannot_spread = (expectation && expected != target_floor) ||
        (!expectation && ((target_floor == 0 && no_air) || target_floor > 65533));
    if (force_adjacent || target_room || cannot_spread) {
        if (target_room && target_room != room) add_unique(adjacent, target_room);
        return;
    }
    map[index] |= apply;
    spread.push_back({x, y, directions});
}

void make_outside(std::vector<GeometricRoom>& rooms, std::uint16_t id) {
    rooms[id].outside = true;
    auto visit = rooms[id].adjacent;
    for (std::size_t i = 0; i < visit.size(); ++i) {
        const auto next = visit[i];
        if (next >= rooms.size() || rooms[next].outside) continue;
        rooms[next].outside = true;
        visit.insert(visit.end(), rooms[next].adjacent.begin(), rooms[next].adjacent.end());
    }
}

SavedArchitectureLevel candidate_level(const SavedArchitecture& architecture,
                                        std::uint8_t level_index) {
    // VMTS1Activator.DecodeWalls/DecodeAdvWalls normalize saved styles before
    // GenerateMap. Diagonal styles also hold floor patterns and bypass this
    // style normalization; the shared-floor fallback below is independent.
    const auto& saved_level = architecture.levels[level_index];
    SavedArchitectureLevel level;
    level.floors = saved_level.floors;
    level.walls = saved_level.walls;
    for (std::size_t index = 0; index < level.walls.size(); ++index) {
        auto& wall = level.walls[index];
        if (wall.segments & any_diag) {
            // RemapWalls' shared-floor fallback precedes pool/water overlays.
            // Named floor resources remain unresolved in this candidate map.
            if (wall.top_left_pattern == 0 && wall.top_left_style == 0)
                wall.top_left_pattern = wall.top_left_style = level.floors[index];
            continue;
        }
        const auto normalize = [](std::uint16_t style, bool present) -> std::uint16_t {
            if (!present) return 0;
            return style == 2 || style == 12 || style == 13 || style == 14 ? style : 1;
        };
        wall.top_left_style = normalize(wall.top_left_style, wall.segments & wall_top_left);
        wall.top_right_style = normalize(wall.top_right_style, wall.segments & wall_top_right);
    }
    if (level_index == 0) {
        for (std::size_t index = 0; index < level.floors.size(); ++index) {
            const auto active = [index](const std::optional<std::vector<std::uint8_t>>& grid) {
                return grid && (*grid)[index] != 0 && (*grid)[index] != 255;
            };
            if (active(architecture.pools)) level.floors[index] = 65535;
            if (active(architecture.water)) level.floors[index] = 65534;
        }
    }
    return level;
}

template<class Level>
void generate_level(const Level& level, std::uint8_t level_index,
                    GeometricRoomMap& result, const RoomMapLimits& limits) {
    auto& map = result.levels[level_index];
    map.resize(std::size_t(result.width) * result.height);
    const bool no_floor_bad = result.rooms.size() > 1;
    bool outside = true;
    std::size_t i = 0;
    while (i < map.size()) {
        std::vector<SpreadItem> spread;
        std::uint16_t expected = 0;
        const auto room = static_cast<std::uint16_t>(result.rooms.size());
        while (i < map.size()) {
            const auto& wall = level.walls[i];
            std::uint8_t directions;
            if (map[i] == 0 && !(wall.segments & any_diag)) {
                expected = level.floors[i];
                map[i] = room | (std::uint32_t(room) << 16);
                directions = any_adj;
            } else if ((map[i] & 0xffffU) == 0) {
                if (wall.segments & wall_horizontal_diagonal) {
                    directions = wall_top_left | wall_top_right;
                    expected = wall.top_left_style;
                    map[i] |= 0x80000000U;
                } else {
                    directions = wall_top_right | wall_bottom_right;
                    expected = wall.top_left_pattern;
                }
                map[i] |= room;
            } else if ((map[i] & 0x7fff0000U) == 0) {
                if (wall.segments & wall_horizontal_diagonal) {
                    directions = wall_bottom_left | wall_bottom_right;
                    expected = wall.top_left_pattern;
                    map[i] |= 0x80000000U;
                } else {
                    directions = wall_top_left | wall_bottom_left;
                    expected = wall.top_left_style;
                }
                map[i] |= std::uint32_t(room) << 16;
                spread.push_back({static_cast<std::uint32_t>(i % result.width),
                                  static_cast<std::uint32_t>(i / result.width), directions});
                ++i; // Only the second diagonal seed advances before filling.
                break;
            } else {
                ++i;
                continue;
            }
            spread.push_back({static_cast<std::uint32_t>(i % result.width),
                              static_cast<std::uint32_t>(i / result.width), directions});
            break;
        }
        if (spread.empty()) break;
        if (result.rooms.size() > limits.rooms || result.rooms.size() > 32767)
            throw std::runtime_error("room map room limit exceeded");
        auto min_x = spread.back().x, max_x = min_x;
        auto min_y = spread.back().y, max_y = min_y;
        std::uint16_t area = 0;
        std::vector<std::uint16_t> adjacent;
        while (!spread.empty()) {
            const auto item = spread.back();
            spread.pop_back();
            ++area;
            min_x = std::min(min_x, item.x); max_x = std::max(max_x, item.x);
            min_y = std::min(min_y, item.y); max_y = std::max(max_y, item.y);
            const auto plus_x = (item.x + 1) % result.width;
            const auto minus_x = (item.x + result.width - 1) % result.width;
            const auto plus_y = (item.y + 1) % result.height;
            const auto minus_y = (item.y + result.height - 1) % result.height;
            const auto& wall = level.walls[std::size_t(item.y) * result.width + item.x];
            const auto& px_wall = level.walls[std::size_t(item.y) * result.width + plus_x];
            const auto& py_wall = level.walls[std::size_t(plus_y) * result.width + item.x];
            const auto step = [&](const SavedWallTile& edge, std::uint8_t segment,
                                  std::uint16_t style, std::uint8_t allowed,
                                  std::uint32_t x, std::uint32_t y, unsigned direction) {
                const bool segment_allow = !(edge.segments & segment);
                if ((segment_allow || style != 1) && (item.directions & allowed))
                    spread_onto(level, result.width, x, y, direction, map, spread,
                                room, expected, no_floor_bad, adjacent, !segment_allow);
            };
            // Push order matters: the reference uses a LIFO stack.
            step(px_wall, wall_top_left, px_wall.top_left_style, wall_bottom_right, plus_x, item.y, 0);
            step(wall, wall_top_left, wall.top_left_style, wall_top_left, minus_x, item.y, 2);
            step(py_wall, wall_top_right, py_wall.top_right_style, wall_bottom_left, item.x, plus_y, 1);
            step(wall, wall_top_right, wall.top_right_style, wall_top_right, item.x, minus_y, 3);
        }
        for (const auto neighbor : adjacent) {
            add_unique(result.rooms[neighbor].adjacent, room);
            if (outside) make_outside(result.rooms, neighbor);
            else if (result.rooms[neighbor].outside) outside = true;
        }
        result.rooms.push_back({room, level_index, area,
            {min_x, min_y, max_x - min_x + 1, max_y - min_y + 1},
            outside, expected > 65533, std::move(adjacent)});
        outside = false;
    }
}
} // namespace

GeometricRoomMap generate_saved_room_map(const SavedArchitecture& architecture,
                                        const RoomMapLimits& limits) {
    if (!architecture.width || !architecture.height || architecture.width > 64 ||
        architecture.height > 64 || architecture.width > limits.dimension ||
        architecture.height > limits.dimension)
        throw std::runtime_error("room map dimensions unsupported");
    const auto cells = std::size_t(architecture.width) * architecture.height;
    for (const auto* overlay : {&architecture.pools, &architecture.water})
        if (*overlay && (*overlay)->size() != cells)
            throw std::runtime_error("room map overlay extent mismatch");
    for (const auto& level : architecture.levels) {
        if (level.floors.size() != cells || level.walls.size() != cells)
            throw std::runtime_error("room map grid extent mismatch");
        for (const auto& wall : level.walls)
            if ((wall.segments & 0xc0U) || (wall.segments & any_diag) == any_diag)
                throw std::runtime_error("room map unsupported wall segments");
    }
    GeometricRoomMap result;
    result.width = architecture.width;
    result.height = architecture.height;
    result.rooms.emplace_back(); // VMArchitecture.RegenRoomMap dummy.
    for (std::uint8_t level = 0; level < result.levels.size(); ++level)
        generate_level(candidate_level(architecture, level), level, result, limits);
    return result;
}

GeometricRoomMap generate_prepared_room_map(const PreparedArchitecture& architecture,
                                           const RoomMapLimits& limits) {
    if (!architecture.width || architecture.width != architecture.height || architecture.width > 64 ||
        architecture.height > 64 || architecture.width > limits.dimension ||
        architecture.height > limits.dimension)
        throw std::runtime_error("prepared room map dimensions unsupported");
    const auto cells = std::size_t(architecture.width) * architecture.height;
    if (architecture.buildable.size() != cells)
        throw std::runtime_error("prepared room map buildability extent mismatch");
    for (const auto& level : architecture.levels) {
        if (level.floors.size() != cells || level.walls.size() != cells)
            throw std::runtime_error("prepared room map grid extent mismatch");
        for (const auto& wall : level.walls)
            if ((wall.segments & 0xc0U) || (wall.segments & any_diag) == any_diag)
                throw std::runtime_error("prepared room map unsupported wall segments");
    }
    GeometricRoomMap result;
    result.width = architecture.width;
    result.height = architecture.height;
    result.floor_resources_resolved = architecture.floor_identity_unambiguous();
    result.rooms.emplace_back();
    for (std::uint8_t level = 0; level < result.levels.size(); ++level)
        generate_level(architecture.levels[level], level, result, limits);
    return result;
}

std::optional<std::uint16_t> geometric_room_at(
    const GeometricRoomMap& map, std::int32_t x, std::int32_t y, std::int16_t level) {
    if (!map.width || !map.height || map.width > 64 || map.height > 64 ||
        level < 1 || level > 2 || x < 0 || y < 0 || map.rooms.empty() || map.rooms.size() > 32768)
        return std::nullopt;
    const auto cells = std::size_t(map.width) * map.height;
    for (const auto& room_level : map.levels)
        if (room_level.size() != cells) return std::nullopt;
    const auto tile_x = static_cast<std::uint32_t>(x / 16);
    const auto tile_y = static_cast<std::uint32_t>(y / 16);
    if (tile_x >= map.width || tile_y >= map.height) return std::nullopt;

    const auto packed = map.levels[static_cast<std::size_t>(level - 1)]
        [std::size_t(tile_y) * map.width + tile_x];
    const auto first = static_cast<std::uint16_t>(packed & 0xffffU);
    const auto second = static_cast<std::uint16_t>((packed >> 16) & 0x7fffU);
    const auto valid_room = [&](std::uint16_t id) {
        return id == 0 || (id < map.rooms.size() && map.rooms[id].id == id &&
            map.rooms[id].level == static_cast<std::uint8_t>(level - 1));
    };
    if (!valid_room(first) || !valid_room(second)) return std::nullopt;
    auto selected = first;
    if (first != second) {
        const auto local_x = x % 16;
        const auto local_y = y % 16;
        if ((packed & 0x80000000U) != 0) {
            if (local_x + local_y > 15) selected = second;
        } else if (local_x - local_y <= 0) {
            selected = second;
        }
    }
    return selected;
}

std::optional<std::uint16_t> geometric_room_at_runtime(
    const GeometricRoomMap& map, std::int32_t x, std::int32_t y,
    std::int16_t level, std::uint8_t stories) {
    if (!map.width || !map.height || map.width > 64 || map.height > 64 ||
        stories == 0 || stories > map.levels.size() || map.rooms.empty() ||
        map.rooms.size() > 32768)
        return std::nullopt;
    const auto cells = std::size_t(map.width) * map.height;
    for (const auto& room_level : map.levels)
        if (room_level.size() != cells) return std::nullopt;

    // GetRoomAt returns room zero before indexing Architecture.Rooms for
    // coordinates or levels outside the lot. Keep malformed in-lot cells
    // distinct from this valid reference fallback.
    if (level < 1 || level > stories || x < 0 || y < 0 ||
        static_cast<std::uint32_t>(x) / 16U >= map.width ||
        static_cast<std::uint32_t>(y) / 16U >= map.height)
        return std::uint16_t{0};
    return geometric_room_at(map, x, y, level);
}
} // namespace odh
