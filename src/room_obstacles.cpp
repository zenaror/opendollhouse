#include "odh/room_obstacles.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace odh {
namespace {
constexpr std::uint32_t maximum_dimension = 64;

bool belongs_to(std::uint32_t packed, std::uint16_t room) {
    const auto first = static_cast<std::uint16_t>(packed & 0xffffU);
    const auto second = static_cast<std::uint16_t>((packed >> 16) & 0x7fffU);
    return first == room || second == room;
}

void optimize(std::vector<RoomObstacleRect>& rectangles) {
    // Port the ordered LinkedList merge in VMRoomMap.OptimizeObstacles. Keep
    // iteration order stable because the reference mutates the first rectangle
    // and removes later matches as it walks the list.
    std::vector<bool> alive(rectangles.size(), true);
    for (std::size_t i = 0; i < rectangles.size(); ++i) {
        if (!alive[i]) continue;
        auto& first = rectangles[i];
        // The source's inner LinkedList walk begins at item.Next, so only
        // later rectangles may be merged into the current rectangle.
        for (std::size_t j = i + 1; j < rectangles.size(); ++j) {
            if (!alive[j]) continue;
            const auto& second = rectangles[j];
            if (first.x1 == second.x1 && first.x2 == second.x2 &&
                !(first.y1 > second.y2 || first.y2 < second.y1)) {
                first.y1 = std::min(first.y1, second.y1);
                first.y2 = std::max(first.y2, second.y2);
                alive[j] = false;
            } else if (first.y1 == second.y1 && first.y2 == second.y2 &&
                       !(first.x1 > second.x2 || first.x2 < second.x1)) {
                first.x1 = std::min(first.x1, second.x1);
                first.x2 = std::max(first.x2, second.x2);
                alive[j] = false;
            }
        }
    }
    std::vector<RoomObstacleRect> merged;
    merged.reserve(rectangles.size());
    for (std::size_t i = 0; i < rectangles.size(); ++i)
        if (alive[i]) merged.push_back(rectangles[i]);
    rectangles = std::move(merged);
}
} // namespace

RoomPortalInventory build_room_portal_inventory(
    const GeometricRoomMap& map,
    const std::vector<RoomPortalMemberSnapshot>& members) {
    RoomPortalInventory result;
    if (!map.width || !map.height || map.width > maximum_dimension ||
        map.height > maximum_dimension || map.rooms.empty()) {
        result.complete = false;
        return result;
    }
    const auto cells = std::size_t(map.width) * map.height;
    for (const auto& grid : map.levels) {
        if (grid.size() != cells) {
            result.complete = false;
            return result;
        }
    }

    using GroupId = std::int16_t;
    using TileKey = std::tuple<std::uint8_t, std::uint32_t, std::uint32_t>;
    using OpeningKey = std::tuple<std::uint16_t, std::uint32_t, std::uint32_t, std::uint8_t>;
    std::map<GroupId, std::vector<const RoomPortalMemberSnapshot*>> groups;
    std::set<std::int16_t> seen_ids;
    for (const auto& member : members) {
        if (member.id <= 0 || !seen_ids.insert(member.id).second) {
            result.complete = false;
            continue;
        }
        const auto group = member.group_leader > 0 ? member.group_leader : member.id;
        groups[group].push_back(&member);
    }

    const auto locate = [&map](const std::optional<EntityPosition>& position,
                               std::uint32_t& tile_x, std::uint32_t& tile_y,
                               std::uint8_t& tile_level,
                               std::optional<std::uint16_t>& room) {
        if (!position || position->x < 0 || position->y < 0 ||
            position->level < 1 || position->level > 2)
            return false;
        tile_x = static_cast<std::uint32_t>(position->x / 16);
        tile_y = static_cast<std::uint32_t>(position->y / 16);
        tile_level = static_cast<std::uint8_t>(position->level - 1);
        if (tile_x >= map.width || tile_y >= map.height) return false;
        room = geometric_room_at(map, position->x, position->y, position->level);
        return room.has_value();
    };

    std::map<TileKey, std::set<GroupId>> door_groups_by_tile;
    std::set<OpeningKey> openings;
    for (const auto& [group_id, group_members] : groups) {
        const bool has_door = std::any_of(group_members.begin(), group_members.end(),
            [](const auto* member) { return member->architectural_door && !member->contained; });
        if (!has_door) continue;

        std::set<std::uint16_t> target_rooms;
        for (const auto* member : group_members) {
            if (!member->portal) continue;
            std::uint32_t tile_x = 0, tile_y = 0;
            std::uint8_t tile_level = 0;
            std::optional<std::uint16_t> room;
            if (!locate(member->position, tile_x, tile_y, tile_level, room)) {
                result.complete = false;
                continue;
            }
            target_rooms.insert(*room);
        }

        std::set<TileKey> door_tiles;
        for (const auto* member : group_members) {
            if (!member->architectural_door || member->contained) continue;
            std::uint32_t tile_x = 0, tile_y = 0;
            std::uint8_t tile_level = 0;
            std::optional<std::uint16_t> room;
            if (!locate(member->position, tile_x, tile_y, tile_level, room)) {
                result.complete = false;
                continue;
            }
            const TileKey tile{tile_level, tile_x, tile_y};
            door_tiles.insert(tile);
            door_groups_by_tile[tile].insert(group_id);
        }

        for (const auto& [tile_level, tile_x, tile_y] : door_tiles)
            for (const auto target_room : target_rooms)
                openings.emplace(target_room, tile_x, tile_y, tile_level);
    }

    for (const auto& [tile, groups_at_tile] : door_groups_by_tile) {
        (void)tile;
        if (groups_at_tile.size() > 1) {
            ++result.overlapping_door_tiles;
            result.complete = false;
        }
    }
    result.openings.reserve(openings.size());
    for (const auto& [room, tile_x, tile_y, tile_level] : openings)
        result.openings.push_back({room, tile_x, tile_y, tile_level});
    return result;
}

DynamicRoomPortalInventory::DynamicRoomPortalInventory(
    GeometricRoomMap map, std::size_t member_limit)
    : map_(std::move(map)), member_limit_(member_limit) {
    inventory_.complete = false;
}

bool DynamicRoomPortalInventory::publish(
    std::map<std::int16_t, RoomPortalMemberSnapshot>&& candidate) {
    std::vector<RoomPortalMemberSnapshot> snapshots;
    snapshots.reserve(candidate.size());
    for (const auto& [id, member] : candidate) {
        (void)id;
        snapshots.push_back(member);
    }
    auto candidate_inventory = build_room_portal_inventory(map_, snapshots);
    members_.swap(candidate);
    inventory_ = std::move(candidate_inventory);
    ++generation_;
    return true;
}

bool DynamicRoomPortalInventory::register_member(
    const RoomPortalMemberSnapshot& member) {
    if (member.id <= 0 || members_.find(member.id) != members_.end() ||
        members_.size() >= member_limit_)
        return false;
    auto candidate = members_;
    candidate.emplace(member.id, member);
    return publish(std::move(candidate));
}

bool DynamicRoomPortalInventory::update_member(
    const RoomPortalMemberSnapshot& member) {
    if (member.id <= 0 || members_.find(member.id) == members_.end()) return false;
    auto candidate = members_;
    candidate[member.id] = member;
    return publish(std::move(candidate));
}

bool DynamicRoomPortalInventory::unregister_member(std::int16_t id) {
    if (id <= 0 || members_.find(id) == members_.end()) return false;
    auto candidate = members_;
    candidate.erase(id);
    return publish(std::move(candidate));
}

bool DynamicRoomPortalInventory::replace_members(
    const std::vector<RoomPortalMemberSnapshot>& members) {
    if (members.size() > member_limit_) return false;
    std::map<std::int16_t, RoomPortalMemberSnapshot> candidate;
    for (const auto& member : members) {
        if (member.id <= 0 || !candidate.emplace(member.id, member).second)
            return false;
    }
    return publish(std::move(candidate));
}

RoomObstacleCandidateSet generate_room_obstacle_candidates(
    const GeometricRoomMap& map, std::uint16_t room, std::uint8_t level) {
    return generate_room_obstacle_candidates(map, room, level, {}, false);
}

RoomObstacleCandidateSet generate_room_obstacle_candidates(
    const GeometricRoomMap& map, std::uint16_t room, std::uint8_t level,
    const std::vector<RoomPortalOpening>& portal_openings,
    bool portal_inventory_complete) {
    if (!map.width || !map.height || map.width > maximum_dimension ||
        map.height > maximum_dimension || level >= map.levels.size())
        throw std::runtime_error("room obstacle map dimensions or level unsupported");
    const auto cells = std::size_t(map.width) * map.height;
    for (const auto& grid : map.levels)
        if (grid.size() != cells)
            throw std::runtime_error("room obstacle packed grid extent mismatch");

    GeometricRoomBounds bounds;
    const bool outside_room = room == 0;
    if (outside_room) {
        // GenerateRoomObs uses the interior of the architecture for room zero,
        // then expands that rectangle by one tile, covering the whole grid.
    } else {
        if (room >= map.rooms.size() || map.rooms[room].id != room ||
            map.rooms[room].level != level)
            throw std::runtime_error("room obstacle room ID or level unsupported");
        bounds = map.rooms[room].bounds;
        if (!bounds.width || !bounds.height || bounds.x >= map.width ||
            bounds.y >= map.height || bounds.width > map.width - bounds.x ||
            bounds.height > map.height - bounds.y)
            throw std::runtime_error("room obstacle bounds unsupported");
    }

    // Match Rectangle.Right/Bottom (exclusive loop end after adding one).
    const auto x1 = outside_room || bounds.x == 0 ? 0 : bounds.x - 1;
    const auto y1 = outside_room || bounds.y == 0 ? 0 : bounds.y - 1;
    const auto right = bounds.x + bounds.width;
    const auto bottom = bounds.y + bounds.height;
    const auto x2 = outside_room ? map.width :
        std::min(map.width, right + (right < map.width ? 1U : 0U));
    const auto y2 = outside_room ? map.height :
        std::min(map.height, bottom + (bottom < map.height ? 1U : 0U));

    RoomObstacleCandidateSet result;
    result.room = room;
    result.level = level;
    const auto& packed = map.levels[level];
    std::vector<std::uint8_t> open_tiles(cells, 0);
    for (const auto& opening : portal_openings) {
        if (opening.tile_x >= map.width || opening.tile_y >= map.height ||
            opening.tile_level >= map.levels.size() || opening.target_room >= map.rooms.size())
            throw std::runtime_error("room obstacle portal opening outside map or level");
        if (opening.target_room == room && opening.tile_level == level)
            open_tiles[std::size_t(opening.tile_y) * map.width + opening.tile_x] = 1;
    }
    result.complete = map.floor_resources_resolved && portal_inventory_complete;
    for (auto y = y1; y < y2; ++y) {
        bool run = false;
        std::size_t run_index = 0;
        for (auto x = x1; x < x2; ++x) {
            const auto index = std::size_t(y) * map.width + x;
            const bool blocked = !belongs_to(packed[index], room) && !open_tiles[index];
            if (blocked) {
                if (run) {
                    result.rectangles[run_index].x2 += 16;
                } else {
                    run = true;
                    run_index = result.rectangles.size();
                    const auto px = static_cast<std::int32_t>(x * 16U);
                    const auto py = static_cast<std::int32_t>(y * 16U);
                    result.rectangles.push_back({px - 3, py - 3, px + 19, py + 19});
                }
            } else {
                run = false;
            }
        }
    }
    optimize(result.rectangles);
    return result;
}

WallObstacleCandidateSet generate_wall_obstacle_candidates(
    const SavedArchitecture& architecture, std::uint8_t level,
    const std::vector<ResolvedWallEdges>& resolved_edges,
    std::size_t rectangle_limit) {
    WallObstacleCandidateSet result;
    result.level = level;
    if (!architecture.width || !architecture.height ||
        architecture.width > maximum_dimension || architecture.height > maximum_dimension ||
        level >= architecture.levels.size()) return result;
    const auto cells = std::size_t(architecture.width) * architecture.height;
    for (const auto& story : architecture.levels)
        if (story.floors.size() != cells || story.walls.size() != cells) return result;
    if (resolved_edges.size() != cells) return result;
    const auto& walls = architecture.levels[level].walls;
    const auto push = [&](std::int64_t x1, std::int64_t y1,
                          std::int64_t x2, std::int64_t y2) {
        if (result.rectangles.size() >= rectangle_limit || x1 < std::numeric_limits<std::int32_t>::min() ||
            y1 < std::numeric_limits<std::int32_t>::min() || x2 > std::numeric_limits<std::int32_t>::max() ||
            y2 > std::numeric_limits<std::int32_t>::max()) return false;
        result.rectangles.push_back({static_cast<std::int32_t>(x1), static_cast<std::int32_t>(y1),
                                     static_cast<std::int32_t>(x2), static_cast<std::int32_t>(y2)});
        return true;
    };
    const auto door = [&](std::size_t index, bool top_left) {
        const auto& tile = walls[index];
        const auto segment = top_left ? std::uint8_t{1} : std::uint8_t{2};
        if ((tile.segments & segment) == 0) return false;
        const auto edge = top_left ? resolved_edges[index].top_left : resolved_edges[index].top_right;
        // Only affirmative door-face evidence opens a gap. Missing/unresolved
        // faces are conservative solid walls.
        return edge == WallEdgeKnowledge::non_solid;
    };
    for (std::uint32_t y = 0; y < architecture.height; ++y) {
        for (std::uint32_t x = 0; x < architecture.width; ++x) {
            const auto index = std::size_t(y) * architecture.width + x;
            const auto& tile = walls[index];
            const auto bits = tile.segments;
            if ((bits & 0xc0U) || (bits & 0x30U) == 0x30U) {
                result.rectangles.clear(); return result;
            }
            const std::int64_t ox = std::int64_t(x) * 16;
            const std::int64_t oy = std::int64_t(y) * 16;
            if (bits & 0x10U) {
                if (!push(ox + 11, oy - 1, ox + 17, oy + 5) ||
                    !push(ox + 7, oy + 3, ox + 13, oy + 9) ||
                    !push(ox + 3, oy + 7, ox + 9, oy + 13) ||
                    !push(ox - 1, oy + 11, ox + 5, oy + 17)) {
                    result.rectangles.clear(); return result;
                }
            }
            if (bits & 0x20U) {
                if (!push(ox - 1, oy - 1, ox + 5, oy + 5) ||
                    !push(ox + 3, oy + 3, ox + 9, oy + 9) ||
                    !push(ox + 7, oy + 7, ox + 13, oy + 13) ||
                    !push(ox + 11, oy + 11, ox + 17, oy + 17)) {
                    result.rectangles.clear(); return result;
                }
            }
            if ((bits & 1U) && !door(index, true) &&
                !push(ox - 3, oy - 3, ox + 6, oy + 19)) {
                result.rectangles.clear(); return result;
            }
            if ((bits & 2U) && !door(index, false) &&
                !push(ox - 3, oy - 3, ox + 19, oy + 6)) {
                result.rectangles.clear(); return result;
            }
            const auto next_y = (y + 1U) % architecture.height;
            const auto next_x = (x + 1U) % architecture.width;
            const auto below = std::size_t(next_y) * architecture.width + x;
            const auto right = std::size_t(y) * architecture.width + next_x;
            const auto below_door =
                resolved_edges[below].top_right == WallEdgeKnowledge::non_solid;
            const auto right_door =
                resolved_edges[right].top_left == WallEdgeKnowledge::non_solid;
            if ((bits & 8U) && !below_door &&
                !push(ox - 3, oy + 13, ox + 19, oy + 19)) {
                result.rectangles.clear(); return result;
            }
            if ((bits & 4U) && !right_door &&
                !push(ox + 13, oy - 3, ox + 19, oy + 19)) {
                result.rectangles.clear(); return result;
            }
        }
    }
    result.complete = true;
    return result;
}
} // namespace odh
