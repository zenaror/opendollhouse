#pragma once

#include "odh/household_spawn.hpp"
#include "odh/floor_placement.hpp"
#include "odh/room_map.hpp"
#include "odh/world_room_info.hpp"
#include "odh/world_avatar_place_audit.hpp"
#include "odh/prepared_architecture.hpp"
#include "odh/route_geometry.hpp"
#include <algorithm>

// Deliberately bounded SetPosition adapter for a newly constructed, singleton,
// empty-handed avatar. Room and footprint membership are derived from World by
// query_world_room_info, rather than a second mutable registration cache.
// No EP5 BHAV is executed and placement EP9 must be absent. Every attempt uses
// a disposable World; unknown facts and any registration failure fail closed.
inline odh::HouseholdPositionAttemptStatus attempt_household_runtime_arrival(
    odh::World& world, std::int16_t id, const odh::EntityPosition& position,
    const odh::DefinitionIndex& definitions, const odh::PreparedArchitecture& architecture) {
    using Status = odh::HouseholdPositionAttemptStatus;
    const auto* avatar = world.find(id);
    if (!avatar || avatar->kind != odh::EntityKind::avatar || !avatar->definition ||
        avatar->position || avatar->attachment || avatar->ui_ghost ||
        avatar->kill_timeout != std::optional<std::int32_t>{-1} || !avatar->static_footprint ||
        std::any_of(avatar->slots.begin(), avatar->slots.end(), [](auto value) { return value != 0; }) ||
        !avatar->entry_points || avatar->entry_points->size() <= 15 ||
        (*avatar->entry_points)[9].action != 0 || (*avatar->entry_points)[15].action != 0 ||
        !architecture.floor_identity_unambiguous())
        return Status::unsupported;
    const odh::VmFrame frame{odh::routine_key(4096), 0, {id,id,id,static_cast<odh::CodeOwner>(id)}, {}, {}};
    const auto impact = world.read(frame,3,7);
    const auto flags2 = world.read(frame,3,40);
    const auto lighting = world.read(frame,3,51);
    // VMAvatar.MovesOften is always true, but registration can still mutate
    // room scores, deferred lighting or window/portal inventories. Those
    // services are not part of this private World transaction.
    if (!impact || *impact != 0 || !lighting || *lighting != 0 || !flags2 ||
        (static_cast<std::uint16_t>(*flags2) & 0x4000U)) return Status::unsupported;
    const auto* definition = definitions.unique(avatar->definition->guid);
    if (!definition || definition->definition.object_type != 2 || definition->definition.is_multitile ||
        !definition->definition.level_offset) return Status::unsupported;
    if (position.level < 1 || position.level > 2 || position.direction > 7 ||
        position.x < 0 || position.y < 0 ||
        static_cast<std::uint32_t>(position.x / 16) >= architecture.width ||
        static_cast<std::uint32_t>(position.y / 16) >= architecture.height)
        return Status::blocked;
    const auto floor = odh::prepared_precise_floor(architecture, position.x, position.y, position.level);
    const auto flags = world.read(frame, 3, 42);
    if (!floor || !flags) return Status::unsupported;
    if (odh::floor_change_valid(*floor, static_cast<std::int8_t>(position.level),
        static_cast<std::uint16_t>(*flags), false, *definition->definition.level_offset) !=
        odh::FloorPlacementResult::allowed) return Status::blocked;
    // Avatar PositionValid skips wall rules and SlopeValid always succeeds.
    // Room geometry still has to be complete for overlap/registration queries.
    const auto rooms = odh::generate_prepared_room_map(architecture);
    const odh::RuntimeRoomResolver room_for = [&rooms](odh::EntityPosition p) {
        return odh::geometric_room_at_runtime(rooms,p.x,p.y,p.level);
    };
    const auto room = room_for(position);
    const auto shape = odh::avatar_candidate_collision_shape(position);
    if (!room || !*room || shape.state != odh::CollisionShapeState::present) return Status::unsupported;
    odh::WorldLimits limits; limits.words = 131072;
    odh::World attempt(limits, world.snapshot().attribute_policy);
    if (!attempt.restore_snapshot(world.snapshot())) return Status::unsupported;
    const odh::AvatarKillTimeoutResolver kill_for = [&attempt](std::int16_t actor, std::uint64_t generation)
        -> std::optional<std::int32_t> {
        const auto* entity = attempt.find(actor);
        return entity && entity->generation == generation ? entity->kill_timeout : std::nullopt;
    };
    const auto query = odh::query_world_room_info(attempt,definitions,room_for,kill_for,*room,shape.box,id);
    const auto overlaps = odh::audit_world_avatar_place_overlaps(attempt,id,query,false,
        [](std::int16_t, std::int16_t) { return std::optional<bool>{false}; });
    if (overlaps.rules.status == odh::SavedAvatarPlaceAuditStatus::unsupported) return Status::unsupported;
    if (overlaps.rules.status == odh::SavedAvatarPlaceAuditStatus::blocked) return Status::blocked;
    if (!attempt.declare_object_word(id,29,static_cast<std::int16_t>(*room - 1)) ||
        !attempt.set_position(id,position)) return Status::unsupported;
    const auto registered = odh::snapshot_world_room_info(attempt,definitions,room_for,kill_for,*room);
    if (registered.status != odh::RoomInfoObstacleIndexStatus::complete ||
        std::count_if(registered.obstacles.begin(),registered.obstacles.end(),[&](const auto& obstacle) {
            return obstacle.owner_id == id && obstacle.box.x1 == shape.box.x1 &&
                obstacle.box.y1 == shape.box.y1 && obstacle.box.x2 == shape.box.x2 && obstacle.box.y2 == shape.box.y2;
        }) != 1) return Status::unsupported;
    if (!world.publish_snapshot(attempt.snapshot())) return Status::unsupported;
    return Status::placed;
}
