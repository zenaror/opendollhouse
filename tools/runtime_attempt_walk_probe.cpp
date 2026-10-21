#include "runtime_attempt_walk_probe.hpp"

#include "odh/room_map.hpp"
#include "odh/room_obstacles.hpp"
#include "odh/rect_router.hpp"
#include "odh/wall_route_state.hpp"

#include <algorithm>
#include <map>
#include <set>

RuntimeAttemptWalkProbe build_runtime_attempt_walk_probe(
    const odh::SavedWorld& saved, const odh::World& world,
    const odh::DefinitionIndex& definitions,
    const SavedArchitectureResource& architecture,
    const odh::PreparedArchitecture& prepared,
    std::uint16_t room, std::uint8_t level, std::int16_t actor_id,
    bool avatar_consideration_complete,
    const std::vector<std::int16_t>& avatars_to_consider,
    const odh::AttemptWalkEntryPoint5Resolver& resolve_entry_point5) {
    RuntimeAttemptWalkProbe result;
    if (!prepared.floor_identity_unambiguous() || level > 1 || room == 0)
        return result;

    try {
        const auto rooms = odh::generate_prepared_room_map(prepared);
        if (room >= rooms.rooms.size()) return result;

        const auto wall_appearance = saved_wall_appearance(
            saved, definitions, architecture.architecture, true);
        std::map<std::int16_t, const odh::WallAppearanceInput*> inputs;
        std::set<std::int16_t> door_leaders;
        for (const auto& input : wall_appearance.inputs) {
            inputs.emplace(input.id, &input);
            if (input.kind != odh::EntityKind::object || input.contained ||
                !(input.flags2 & 0x8000U)) continue;
            const auto group = std::find_if(saved.multitile_groups.begin(), saved.multitile_groups.end(),
                [&](const auto& candidate) {
                    return std::find(candidate.members.begin(), candidate.members.end(), input.id) !=
                           candidate.members.end();
                });
            door_leaders.insert(group == saved.multitile_groups.end() ? input.id : group->leader);
        }

        // A newly created placed architectural object has no saved group or
        // wall-style provenance in this adapter; do not silently omit it.
        auto cursor = std::int16_t{0};
        while (const auto next = world.next_entity(cursor)) {
            cursor = *next;
            if (inputs.find(cursor) != inputs.end()) continue;
            const auto* entity = world.find(cursor);
            if (!entity) return result;
            if (entity->kind != odh::EntityKind::object || !entity->position || entity->attachment)
                continue;
            const odh::VmFrame frame{odh::routine_key(4096), 0, {cursor, cursor, cursor, 0}, {}, {}};
            const auto flags = world.read(frame, 3, 40);
            if (!flags || (static_cast<std::uint16_t>(*flags) & 0xc000U)) return result;
        }

        std::map<std::int16_t, const odh::SavedWorldEntity*> records;
        for (const auto& record : saved.records)
            records.emplace(static_cast<std::int16_t>(record.saved.object_id), &record);
        std::map<std::int16_t, const odh::SavedMultitileGroup*> groups_by_member;
        for (const auto& group : saved.multitile_groups)
            for (const auto member : group.members) groups_by_member.emplace(member, &group);
        std::map<std::uint32_t, SavedPortalFunction> portal_cache;
        const auto portal_for = [&](const odh::SavedWorldEntity& record) -> SavedPortalFunction {
            const auto cached = portal_cache.find(record.definition.guid);
            if (cached != portal_cache.end()) return cached->second;
            const auto* candidate = definitions.unique(record.definition.guid);
            if (!candidate) throw odh::FormatError("runtime portal definition ambiguous");
            const auto function = saved_portal_function(*candidate);
            portal_cache.emplace(record.definition.guid, function);
            return function;
        };

        std::vector<odh::RoomPortalMemberSnapshot> portal_members;
        for (const auto leader : door_leaders) {
            const auto group = groups_by_member.find(leader);
            const auto* group_record = group == groups_by_member.end() ? nullptr : group->second;
            const auto members = group_record
                ? group_record->members : std::vector<std::int16_t>{leader};
            for (const auto member : members) {
                const auto input = inputs.find(member);
                const auto record = records.find(member);
                const auto* entity = world.find(member);
                if (input == inputs.end() || record == records.end() || !entity)
                    return result;
                const auto portal = portal_for(*record->second);
                portal_members.push_back({member, leader,
                    (input->second->flags2 & 0x8000U) != 0,
                    portal.action != 0, input->second->contained, entity->position});
            }
        }
        const auto portal_inventory = odh::build_room_portal_inventory(rooms, portal_members);
        result.portal_inventory_complete = portal_inventory.complete;
        result.portal_openings = portal_inventory.openings.size();

        const auto room_obstacles = odh::generate_room_obstacle_candidates(
            rooms, room, level, portal_inventory.openings, portal_inventory.complete);
        const auto edges = odh::resolve_initial_wall_edges(
            architecture.architecture, wall_appearance.appearance,
            static_cast<std::int16_t>(level + 1));
        if (edges.status != odh::WallEdgeBuildStatus::ready) return result;
        const auto wall_obstacles = odh::generate_wall_obstacle_candidates(
            architecture.architecture, level, edges.edges);
        result.room_obstacle_rectangles = room_obstacles.rectangles.size();
        result.wall_obstacle_rectangles = wall_obstacles.rectangles.size();
        result.architecture_complete = room_obstacles.complete && wall_obstacles.complete;
        if (!result.architecture_complete || !result.portal_inventory_complete) return result;

        const odh::RuntimeRoomResolver room_for = [&rooms](const odh::EntityPosition& position) {
            return odh::geometric_room_at_runtime(rooms, position.x, position.y, position.level);
        };
        const odh::AvatarKillTimeoutResolver kill_timeout_for = [&world](
            std::int16_t id, std::uint64_t generation) -> std::optional<std::int32_t> {
            const auto* entity = world.find(id);
            if (!entity || entity->generation != generation || entity->kind != odh::EntityKind::avatar)
                return std::nullopt;
            return entity->kill_timeout;
        };
        // odh-person runs one new root diagnostic and leaves all restored
        // per-avatar threads dormant; it has no concurrent routing frames.
        // Therefore this host profile's AvatarsToConsider set is exactly
        // empty even when other avatar entities are present in the room.
        result.avatar_consideration_complete = avatar_consideration_complete;
        auto avatar_cursor = std::int16_t{0};
        while (const auto next = world.next_entity(avatar_cursor)) {
            avatar_cursor = *next;
            if (avatar_cursor == actor_id) continue;
            const auto* entity = world.find(avatar_cursor);
            if (!entity) return result;
            if (entity->kind != odh::EntityKind::avatar || !entity->position) continue;
            const auto avatar_room = room_for(*entity->position);
            if (!avatar_room || *avatar_room == room) ++result.other_avatars_in_room;
        }
        result.roughbalanced_architecture_rectangles =
            room_obstacles.rectangles.size() + wall_obstacles.rectangles.size();
        result.snapshot = odh::build_world_attempt_walk_obstacles(
            world, definitions, room_for, kill_timeout_for, rooms, room, level,
            actor_id, room_obstacles, wall_obstacles,
            result.avatar_consideration_complete, avatars_to_consider, resolve_entry_point5);
        if (result.snapshot.state == odh::AttemptWalkObstacleState::ready) {
            const auto& ids = result.snapshot.entity_obstacle_ids;
            result.entity_obstacle_order_matches_reference =
                std::adjacent_find(ids.begin(), ids.end(), std::greater_equal<>()) == ids.end();
        }
        if (result.snapshot.state == odh::AttemptWalkObstacleState::ready) {
            std::vector<odh::RectRouteObstacle> architecture_order;
            architecture_order.reserve(result.roughbalanced_architecture_rectangles);
            for (std::size_t i = 0; i < result.roughbalanced_architecture_rectangles; ++i) {
                const auto& obstacle = result.snapshot.obstacles[i];
                architecture_order.push_back({obstacle.x1, obstacle.y1, obstacle.x2, obstacle.y2});
            }
            if (!odh::rough_balance_obstacle_prefix(architecture_order,
                    result.roughbalanced_architecture_rectangles)) {
                result.snapshot = {};
                result.snapshot.reason = odh::AttemptWalkObstacleReason::obstacle_set_mismatch;
            } else {
                for (std::size_t i = 0; i < result.roughbalanced_architecture_rectangles; ++i) {
                    result.snapshot.obstacles[i] = {
                        architecture_order[i].x1, architecture_order[i].y1,
                        architecture_order[i].x2, architecture_order[i].y2};
                }
            }
        }
        return result;
    } catch (const std::exception&) {
        return result;
    }
}
