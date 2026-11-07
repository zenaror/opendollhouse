#include "runtime_attempt_walk_probe.hpp"

#include "odh/room_map.hpp"
#include "odh/room_obstacles.hpp"
#include "odh/rect_router.hpp"
#include "odh/wall_route_state.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace {
constexpr std::uint16_t stair_portal_action = 4105;
}

RuntimeAttemptWalkArchitecture build_runtime_attempt_walk_architecture(
    const odh::SavedWorld& saved,const odh::World& world,
    const odh::DefinitionIndex& definitions,const SavedArchitectureResource& architecture,
    const odh::PreparedArchitecture& prepared,std::uint16_t room,std::uint8_t level) {
    RuntimeAttemptWalkArchitecture result;
    if (!prepared.floor_identity_unambiguous() || level > 1 || room == 0)
        return result;

    try {
        result.rooms = odh::generate_prepared_room_map(prepared);
        const auto& rooms=result.rooms;
        if (room >= rooms.rooms.size() || rooms.rooms[room].id!=room ||
            rooms.rooms[room].level!=level || !rooms.rooms[room].bounds.width ||
            !rooms.rooms[room].bounds.height) return result;

        auto wall_appearance = saved_wall_appearance(
            saved, definitions, architecture.architecture, true);
        std::set<std::int16_t> known_architectural;
        for(auto& input:wall_appearance.inputs){
            if((input.flags2&0xc000U)==0)continue;
            const auto* live=world.find(input.id);
            const odh::VmFrame context{odh::routine_key(4096),0,{input.id,input.id,input.id,0},{},{}};
            const auto flags=world.read(context,3,40),placement=world.read(context,3,13);
            if(!live||!live->definition||live->generation!=input.generation||live->kind!=input.kind||
               live->definition->guid!=input.guid||bool(live->position)!=bool(input.position)||
               (live->position&&!(*live->position==*input.position))||
               bool(live->attachment)!=input.contained||!flags||!placement||
               (static_cast<std::uint16_t>(*flags)&0xc000U)!=(input.flags2&0xc000U))return result;
            known_architectural.insert(input.id);
            input.flags2=static_cast<std::uint16_t>(*flags);
            input.placement=static_cast<std::uint16_t>(*placement);
        }
        // Same verified live placement inputs as the native SLOT wall verifier:
        // door routines may change word13 while their transform remains fixed.
        wall_appearance.appearance=odh::derive_wall_appearance_restore_order(
            architecture.architecture,wall_appearance.inputs);
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
            const auto* entity = world.find(cursor);
            if (!entity) return result;
            if (entity->kind != odh::EntityKind::object || !entity->position || entity->attachment)
                continue;
            const odh::VmFrame frame{odh::routine_key(4096), 0, {cursor, cursor, cursor, 0}, {}, {}};
            const auto flags = world.read(frame, 3, 40);
            if (!flags || ((static_cast<std::uint16_t>(*flags) & 0xc000U) &&
                !known_architectural.count(cursor))) return result;
        }

        std::map<std::int16_t, const odh::SavedWorldEntity*> records;
        for (const auto& record : saved.records)
            records.emplace(static_cast<std::int16_t>(record.saved.object_id), &record);
        std::map<std::int16_t, const odh::SavedMultitileGroup*> groups_by_member;
        std::set<std::int16_t> ambiguous_group_members;
        for (const auto& group : saved.multitile_groups)
            for (const auto member : group.members)
                if (!groups_by_member.emplace(member, &group).second)
                    ambiguous_group_members.insert(member);
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
        std::set<std::int16_t> validated_stair_lower_ids, validated_stair_upper_ids;
        // Stair groups are identified from the saved multi-tile membership and
        // the resolved asset metadata. Other multi-tile objects are left alone.
        for (const auto& group : saved.multitile_groups) {
            if (group.members.empty() ||
                std::count(group.members.begin(), group.members.end(), group.leader) != 1) return result;
            bool stair_group = false;
            for (const auto part_id : group.members) {
                const auto part_record = records.find(part_id);
                if (part_record == records.end()) return result;
                const auto* candidate = definitions.unique(part_record->second->definition.guid);
                if (!candidate) return result;
                stair_group = stair_group || candidate->asset == "stair.iff";
            }
            if (!stair_group) continue;
            if (!group.master_guid) return result;

            unsigned lower_count = 0, upper_count = 0;
            std::optional<std::int16_t> lower_id, upper_id;
            std::optional<std::uint16_t> master_id;
            std::set<std::int16_t> unique_members;
            for (const auto part_id : group.members) {
                if (!unique_members.insert(part_id).second || ambiguous_group_members.count(part_id)) return result;
                const auto part_record = records.find(part_id);
                const auto* entity = world.find(part_id);
                if (part_record == records.end() || !entity || !entity->definition || !entity->position ||
                    !entity->definition->master_guid || *entity->definition->master_guid != *group.master_guid)
                    return result;
                const auto& definition = part_record->second->definition;
                const auto* candidate = definitions.unique(definition.guid);
                if (!candidate || candidate->asset != "stair.iff" ||
                    candidate->definition.master_id == 0 ||
                    candidate->definition.master_id != definition.master_id ||
                    candidate->definition.sub_index != definition.sub_index ||
                    candidate->definition.guid != definition.guid) return result;
                if (!master_id) master_id = definition.master_id;
                else if (*master_id != definition.master_id) return result;

                if (definition.sub_index == 5) {
                    if (entity->position->level != 1 || portal_for(*part_record->second).action != stair_portal_action)
                        return result;
                    ++lower_count;
                    lower_id = part_id;
                } else if (definition.sub_index == 0) {
                    if (entity->position->level != 2 || portal_for(*part_record->second).action != stair_portal_action)
                        return result;
                    ++upper_count;
                    upper_id = part_id;
                }
            }
            if (lower_count != 1 || upper_count != 1 || !unique_members.count(group.leader) ||
                !lower_id || !upper_id ||
                !validated_stair_lower_ids.insert(*lower_id).second ||
                !validated_stair_upper_ids.insert(*upper_id).second) return result;
            door_leaders.insert(group.leader);
        }

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
                const auto& original=*input->second;
                if(entity->generation!=original.generation||entity->kind!=original.kind||
                   !entity->definition||entity->definition->guid!=original.guid||
                   entity->definition->guid!=record->second->definition.guid||
                   bool(entity->position)!=bool(original.position)||
                   (entity->position&&!(*entity->position==*original.position))||
                   bool(entity->attachment)!=original.contained)return result;
                const auto portal = portal_for(*record->second);
                portal_members.push_back({member, leader,
                    (input->second->flags2 & 0x8000U) != 0,
                    portal.action != 0, input->second->contained, entity->position});
            }
        }
        odh::DynamicRoomPortalInventory portal_index(rooms);
        if (!portal_index.replace_members(portal_members)) return result;
        const auto& portal_inventory = portal_index.current();
        result.portal_inventory_complete = portal_inventory.complete;
        result.portal_openings = portal_inventory.openings.size();
        result.portal_edges = portal_inventory.edges;

        const auto room_obstacles = odh::generate_room_obstacle_candidates(
            rooms, room, level, portal_inventory.openings, portal_inventory.complete);
        const auto edges = odh::resolve_initial_wall_edges(
            architecture.architecture, wall_appearance.appearance,
            static_cast<std::int16_t>(level + 1));
        if (edges.status != odh::WallEdgeBuildStatus::ready) return result;
        const auto wall_obstacles = odh::generate_wall_obstacle_candidates(
            architecture.architecture, level, edges.edges);
        if (!room_obstacles.complete || !wall_obstacles.complete || !result.portal_inventory_complete) return result;
        // Publish only after every stair group and the complete portal inventory
        // have passed; duplicate IDs across groups reject the snapshot above.
        result.stair_lower_portal_ids = std::move(validated_stair_lower_ids);
        result.stair_upper_portal_ids = std::move(validated_stair_upper_ids);

        result.room_obstacles=room_obstacles;
        result.wall_obstacles=wall_obstacles;
        for(const auto& r:wall_obstacles.rectangles)
            result.architectural_and_bounds.push_back({r.x1,r.y1,r.x2,r.y2});
        for(const auto& r:room_obstacles.rectangles)
            result.architectural_and_bounds.push_back({r.x1,r.y1,r.x2,r.y2});
        result.architectural_count=result.architectural_and_bounds.size();
        if(result.architectural_count>16380 ||
           !odh::rough_balance_obstacle_prefix(result.architectural_and_bounds,result.architectural_count))return {};
        const auto& b=rooms.rooms[room].bounds;
        // generate_prepared_room_map bounds are within the <=64-tile profile.
        const auto left=(static_cast<std::int32_t>(b.x)-1)*16;
        const auto top=(static_cast<std::int32_t>(b.y)-1)*16;
        const auto right=left+(static_cast<std::int32_t>(b.width)+2)*16;
        const auto bottom=top+(static_cast<std::int32_t>(b.height)+2)*16;
        result.architectural_and_bounds.push_back({left-16,top-16,right+16,top});
        result.architectural_and_bounds.push_back({left-16,bottom,right+16,bottom+16});
        result.architectural_and_bounds.push_back({left-16,top-16,left,bottom+16});
        result.architectural_and_bounds.push_back({right,top-16,right+16,bottom+16});
        result.complete=true;
        return result;
    }catch(const std::exception&){return {};}
}

RuntimeAttemptWalkProbe build_runtime_attempt_walk_probe(
    const odh::SavedWorld& saved, const odh::World& world,
    const odh::DefinitionIndex& definitions,
    const SavedArchitectureResource& architecture,
    const odh::PreparedArchitecture& prepared,
    std::uint16_t room, std::uint8_t level, std::int16_t actor_id,
    bool avatar_consideration_complete,
    const std::vector<std::int16_t>& avatars_to_consider,
    const odh::AttemptWalkEntryPoint5Resolver& resolve_entry_point5,
    std::optional<odh::EntityPosition> actor_position_override) {
    RuntimeAttemptWalkProbe result;
    const auto architecture_input=build_runtime_attempt_walk_architecture(
        saved,world,definitions,architecture,prepared,room,level);
    result.architecture_complete=architecture_input.complete;
    result.portal_inventory_complete=architecture_input.portal_inventory_complete;
    result.portal_openings=architecture_input.portal_openings;
    result.portal_edges=architecture_input.portal_edges;
    result.room_obstacle_rectangles=architecture_input.room_obstacles.rectangles.size();
    result.wall_obstacle_rectangles=architecture_input.wall_obstacles.rectangles.size();
    result.stair_lower_portal_ids=architecture_input.stair_lower_portal_ids;
    result.stair_upper_portal_ids=architecture_input.stair_upper_portal_ids;
    if(!architecture_input.complete)return result;
    const auto& rooms=architecture_input.rooms;
    const auto& room_obstacles=architecture_input.room_obstacles;
    const auto& wall_obstacles=architecture_input.wall_obstacles;
    try {
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
            result.avatar_consideration_complete, avatars_to_consider, resolve_entry_point5, {},
            actor_position_override);
        if (result.snapshot.state == odh::AttemptWalkObstacleState::ready) {
            const auto& ids = result.snapshot.entity_obstacle_ids;
            result.entity_obstacle_order_matches_reference =
                std::adjacent_find(ids.begin(), ids.end(), std::greater_equal<>()) == ids.end();
        }
        if (result.snapshot.state == odh::AttemptWalkObstacleState::ready) {
            if(result.snapshot.obstacles.size()<architecture_input.architectural_and_bounds.size()){
                result.snapshot={};result.snapshot.reason=odh::AttemptWalkObstacleReason::obstacle_set_mismatch;
            }else{
                // Reuse the architecture-only source prefix. No EP5 runs in
                // its construction, and entity obstacles remain in live order.
                for(std::size_t i=0;i<architecture_input.architectural_and_bounds.size();++i){
                    const auto& r=architecture_input.architectural_and_bounds[i];
                    result.snapshot.obstacles[i]={r.x1,r.y1,r.x2,r.y2};
                }
            }
        }
        return result;
    } catch (const std::exception&) {
        return result;
    }
}
