#include "odh/world_avatar_place_audit.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

std::vector<odh::EntryPoint> entries(std::size_t count = 33) {
    return std::vector<odh::EntryPoint>(count, odh::EntryPoint{0, 0});
}

void add_entity(odh::World& world, std::int16_t id, odh::EntityKind kind,
                std::optional<std::int16_t> flags, bool bind_entries = true,
                std::size_t entry_count = 33) {
    require(world.create(id, kind), "entity creation failed");
    require(world.define_type(id, static_cast<std::uint32_t>(1000 + id)), "entity type setup failed");
    if (flags) require(world.declare_object_word(id, 8, *flags), "flags setup failed");
    if (bind_entries)
        require(world.define_entry_points(id, entries(entry_count)), "entry-point setup failed");
}

void set_flags(odh::World& world, std::int16_t id, std::int16_t flags) {
    const odh::VmFrame frame{odh::routine_key(4096), 0, {id, id, id, 0}, {}, {}};
    require(world.write(frame, 3, 8, flags), "flags update failed");
}

odh::RoomInfoObstacleQuery query(std::initializer_list<std::int32_t> ids) {
    odh::RoomInfoObstacleQuery out;
    out.status = odh::RoomInfoObstacleIndexStatus::complete;
    out.reason = odh::RoomInfoObstacleIndexReason::none;
    for (const auto id : ids)
        out.intersections.push_back({id, 0, {0, 0, 1, 1}, odh::RoomInfoObstacleCollection::static_set});
    return out;
}

auto different_groups = [](std::int16_t, std::int16_t) { return std::optional<bool>(false); };
}

int main() {
    using namespace odh;

    World world;
    add_entity(world, 1, EntityKind::avatar, 0);
    require(world.create(2, EntityKind::object), "entity creation failed");
    require(world.define_type(2, 1002), "entity type setup failed");
    require(world.declare_object_word(2, 8, 0), "flags setup failed");
    auto blocker_entries = entries();
    blocker_entries[26].action = 273;
    require(world.define_entry_points(2, blocker_entries), "EP26 setup failed");
    add_entity(world, 3, EntityKind::object, 0);

    auto hit = query({1, 3});
    auto result = audit_world_avatar_place_overlaps(world, 1, hit, false, different_groups);
    require(result.reason == WorldAvatarPlaceAuditReason::none &&
            result.rules.status == SavedAvatarPlaceAuditStatus::blocked &&
            result.rules.final_blocker_id == 3 && result.rules.early_stop_known == false,
            "static self must be ignored and a solid object must block");

    set_flags(world, 2, 16);
    result = audit_world_avatar_place_overlaps(world, 1, query({2}), false, different_groups);
    require(result.rules.status == SavedAvatarPlaceAuditStatus::allowed,
            "object AllowPersonIntersection must permit overlap");

    set_flags(world, 2, 18);
    result = audit_world_avatar_place_overlaps(world, 1, query({2}), false, different_groups);
    require(result.rules.status == SavedAvatarPlaceAuditStatus::blocked &&
            result.rules.early_stop_known == true,
            "Disallow must dominate Allow and nonzero EP26 must stop the scan");

    set_flags(world, 2, 18);
    int group_lookups = 0;
    auto first_blocks = [&group_lookups](std::int16_t, std::int16_t blocker_id) {
        ++group_lookups;
        if (blocker_id == 2) return std::optional<bool>(false);
        return std::optional<bool>{};
    };
    result = audit_world_avatar_place_overlaps(world, 1, query({2, 3}), false,
                                               first_blocks);
    require(result.rules.status == SavedAvatarPlaceAuditStatus::blocked &&
            result.rules.early_stop_known == true && group_lookups == 1,
            "known EP26 stop must not resolve later blocker inputs");

    World group_world;
    add_entity(group_world, 1, EntityKind::avatar, std::nullopt, false);
    add_entity(group_world, 2, EntityKind::object, std::nullopt, false);
    auto same_group = [](std::int16_t, std::int16_t) { return std::optional<bool>(true); };
    result = audit_world_avatar_place_overlaps(group_world, 1, query({2}), std::nullopt, same_group);
    require(result.rules.status == SavedAvatarPlaceAuditStatus::allowed,
            "same multitile group must be skipped before unrelated runtime inputs");

    World bhav_world;
    require(bhav_world.create(1, EntityKind::avatar), "actor creation failed");
    require(bhav_world.define_type(1, 1001), "actor type setup failed");
    require(bhav_world.declare_object_word(1, 8, 0), "actor flags setup failed");
    auto bhav_entries = entries();
    bhav_entries[5].action = 4000;
    require(bhav_world.define_entry_points(1, bhav_entries), "actor EP5 setup failed");
    add_entity(bhav_world, 2, EntityKind::object, 0);
    result = audit_world_avatar_place_overlaps(bhav_world, 1, query({2}), false, different_groups);
    require(result.reason == WorldAvatarPlaceAuditReason::entry_point5_bhav_unimplemented &&
            result.rules.status == SavedAvatarPlaceAuditStatus::unsupported,
            "BHAV-backed EP5 must fail closed");
    auto actor_ep5_unknown = [](std::int16_t, std::int16_t) { return std::optional<bool>{}; };
    result = audit_world_avatar_place_overlaps(bhav_world, 1, query({2}), false,
                                               different_groups, actor_ep5_unknown);
    require(result.reason == WorldAvatarPlaceAuditReason::entry_point5_result_unavailable,
            "an EP5 resolver that cannot classify the result must remain unsupported");
    auto actor_ep5_true = [](std::int16_t caller, std::int16_t other) {
        return caller == 1 && other == 2 ? std::optional<bool>(true) : std::optional<bool>{};
    };
    result = audit_world_avatar_place_overlaps(bhav_world, 1, query({2}), false,
                                               different_groups, actor_ep5_true);
    require(result.rules.status == SavedAvatarPlaceAuditStatus::allowed &&
            result.rules.pair_classifications == 1,
            "caller-supplied true actor EP5 must exempt the pair before blocker EP5");

    World avatar_world;
    add_entity(avatar_world, 1, EntityKind::avatar, 0);
    add_entity(avatar_world, 2, EntityKind::avatar, 0);
    result = audit_world_avatar_place_overlaps(avatar_world, 1, query({2}), true, different_groups);
    require(result.rules.status == SavedAvatarPlaceAuditStatus::blocked,
            "AllAvatarsSolid must block avatar overlap");
    result = audit_world_avatar_place_overlaps(avatar_world, 1, query({2}), std::nullopt, different_groups);
    require(result.reason == WorldAvatarPlaceAuditReason::place_flags_unavailable &&
            result.rules.status == SavedAvatarPlaceAuditStatus::unsupported,
            "missing placement flags must remain unsupported for avatar blockers");

    auto incomplete = query({2});
    incomplete.status = RoomInfoObstacleIndexStatus::unsupported;
    result = audit_world_avatar_place_overlaps(world, 1, incomplete, false, different_groups);
    require(result.reason == WorldAvatarPlaceAuditReason::room_info_incomplete,
            "incomplete RoomInfo inventory must fail closed");

    World missing_ep26;
    add_entity(missing_ep26, 1, EntityKind::avatar, 0);
    add_entity(missing_ep26, 2, EntityKind::object, 0, true, 6);
    add_entity(missing_ep26, 3, EntityKind::object, 0, true, 6);
    result = audit_world_avatar_place_overlaps(missing_ep26, 1, query({2, 3}), false,
                                               different_groups);
    require(result.rules.status == SavedAvatarPlaceAuditStatus::unsupported,
            "unknown EP26 iteration behavior before later blockers must fail closed");

    std::cout << "World-backed avatar placement audit checks passed\n";
}
