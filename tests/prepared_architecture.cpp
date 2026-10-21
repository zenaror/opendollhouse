#include "odh/prepared_architecture.hpp"
#include "odh/room_map.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void bad(const std::function<void()>& action) {
    bool rejected = false;
    try { action(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "invalid preparation accepted");
}
odh::SavedArchitecture source(std::uint16_t floor = 0) {
    odh::SavedArchitecture result;
    result.width = result.height = 64;
    for (auto& level : result.levels) {
        level.floors.assign(4096, floor);
        level.walls.resize(4096);
    }
    result.levels[0].flags.emplace(4096, 32);
    result.levels[1].flags.emplace(4096, 0); // Deliberately different and unused.
    result.pools.emplace(4096, 0);
    result.water.emplace(4096, 0);
    return result;
}
}

int main() { try {
    odh::ArchitectureResourceAliases aliases;
    for (unsigned key = 0; key < 256; ++key)
        aliases.floors[key] = static_cast<std::uint16_t>(256 + key);
    auto saved = source();
    std::size_t index = 0;
    for (const auto pattern : {0, 1, 30, 31}) {
        for (const auto flags : {0, 32, 1, 255}) {
            (*saved.levels[0].flags)[index] = static_cast<std::uint8_t>(flags);
            for (auto& level : saved.levels) level.floors[index] = static_cast<std::uint16_t>(pattern);
            ++index;
        }
    }
    auto prepared = odh::prepare_saved_architecture(saved, aliases);
    index = 0;
    for (const auto pattern : {0, 1, 30, 31}) {
        for (const auto flags : {0, 32, 1, 255}) {
            const bool mapped = (pattern != 0 && (flags & 32) == 0) || pattern > 30;
            const auto expected = static_cast<std::uint16_t>(mapped ? 256 + pattern : pattern);
            for (const auto& level : prepared.levels)
                require(level.floors[index] == expected, "original-value floor predicate / ground flags on both stories");
            require(prepared.buildable[index] == ((flags & 32) == 0 ? 1 : 0), "buildability uses actual source expression");
            ++index;
        }
    }
    require(prepared.floor_aliases_resolved() && prepared.wall_aliases_resolved(), "all attempted aliases resolved");

    saved = source();
    saved.levels[0].floors[0] = saved.levels[1].floors[0] = 31;
    saved.levels[0].floors[100] = 31; // A miss outside the returned crop still counts.
    prepared = odh::prepare_saved_architecture(saved, {}, {false, 1});
    require(prepared.levels[0].floors[0] == 31 && prepared.levels[1].floors[0] == 31,
            "missing alias preserves source pattern");
    require(prepared.diagnostics.floor_lookups == 3 && prepared.diagnostics.floor_alias_misses == 3 &&
            prepared.diagnostics.missing_floor_aliases[31] && !prepared.floor_aliases_resolved() &&
            prepared.floor_identity_unambiguous() &&
            odh::generate_prepared_room_map(prepared).floor_resources_resolved &&
            odh::generate_prepared_room_map(prepared).light_base_rooms_resolved,
            "diagnostics count full-source lookup misses without inventing names");
    aliases = {};
    aliases.declared_unresolved_floors[31] = true;
    prepared = odh::prepare_saved_architecture(saved, aliases, {false, 1});
    require(prepared.diagnostics.floor_alias_misses == 3 &&
            prepared.diagnostics.unresolved_floor_alias_references == 3 &&
            !prepared.floor_identity_unambiguous() &&
            !odh::generate_prepared_room_map(prepared).floor_resources_resolved &&
            !odh::generate_prepared_room_map(prepared).light_base_rooms_resolved,
            "used declared-unresolved alias keeps prepared room identity incomplete");
    saved = source();
    prepared = odh::prepare_saved_architecture(saved, aliases);
    require(prepared.diagnostics.unresolved_floor_alias_references == 0 &&
            prepared.floor_identity_unambiguous() &&
            odh::generate_prepared_room_map(prepared).floor_resources_resolved,
            "unused declared-unresolved alias does not block room identity");
    saved = source();
    saved.levels[0].walls[0].segments = odh::wall_horizontal_diagonal;
    saved.levels[0].walls[0].top_left_style = 31;
    prepared = odh::prepare_saved_architecture(saved, aliases);
    require(prepared.diagnostics.floor_alias_misses == 1 &&
            prepared.diagnostics.unresolved_floor_alias_references == 1 &&
            !prepared.floor_identity_unambiguous(),
            "declared-unresolved diagonal floor field contributes an ambiguity reference");
    saved = source();
    saved.levels[0].floors[0] = 255;
    saved.levels[0].walls[0].segments = odh::wall_horizontal_diagonal;
    saved.levels[0].walls[0].top_left_style = 8;
    aliases.declared_unresolved_floors[255] = true;
    prepared = odh::prepare_saved_architecture(saved, aliases);
    require(prepared.diagnostics.floor_alias_misses == 2 &&
            prepared.diagnostics.unresolved_floor_alias_references == 0 &&
            prepared.floor_identity_unambiguous() &&
            odh::generate_prepared_room_map(prepared).floor_resources_resolved,
            "unresolved base floor 255 is unused when diagonal fields are explicit");
    saved = source(); saved.levels[0].floors[0] = 31; (*saved.pools)[0] = 1;
    aliases.declared_unresolved_floors[31] = true;
    prepared = odh::prepare_saved_architecture(saved, aliases);
    require(prepared.diagnostics.floor_alias_misses == 1 &&
            prepared.diagnostics.unresolved_floor_alias_references == 0 &&
            prepared.floor_identity_unambiguous(),
            "pool overlay supersedes an unresolved non-diagonal ground material identity");
    saved = source(); saved.levels[0].walls[0].bottom_left_pattern = 0x0103;
    prepared = odh::prepare_saved_architecture(saved, {});
    require(prepared.levels[0].walls[0].bottom_left_pattern == 0x0103 &&
            prepared.diagnostics.wall_lookups == 1 && prepared.diagnostics.wall_alias_misses == 1 &&
            prepared.diagnostics.missing_wall_aliases[3] && !prepared.wall_aliases_resolved() &&
            prepared.floor_aliases_resolved() && prepared.floor_identity_unambiguous(),
            "wall misses retain full word and track low-byte alias independently");

    saved = source(); aliases = {};
    aliases.floors[10] = 500; aliases.floors[11] = 501;
    saved.levels[0].floors[0] = saved.levels[0].floors[2] = 10;
    saved.levels[0].floors[1] = saved.levels[0].floors[3] = 11;
    (*saved.levels[0].flags)[2] = (*saved.levels[0].flags)[3] = 0;
    prepared = odh::prepare_saved_architecture(saved, aliases, {true, 64});
    require(prepared.levels[0].floors[0] == 11 && prepared.levels[0].floors[1] == 10 &&
            prepared.levels[0].floors[2] == 500 && prepared.levels[0].floors[3] == 501,
            "road swap precedes mapping by original key");
    aliases.floors[10].reset();
    prepared = odh::prepare_saved_architecture(saved, aliases, {true, 64});
    require(prepared.levels[0].floors[2] == 11 && prepared.diagnostics.floor_alias_misses == 1,
            "failed original-key lookup preserves road swap");

    saved = source(); aliases = {};
    aliases.floors[1] = 501; aliases.floors[254] = 600; aliases.floors[255] = 601;
    saved.levels[0].floors[0] = 0x0101;
    saved.levels[0].floors[1] = 65534; saved.levels[0].floors[2] = 65535;
    prepared = odh::prepare_saved_architecture(saved, aliases);
    require(prepared.levels[0].floors[0] == 501 && prepared.levels[0].floors[1] == 600 &&
            prepared.levels[0].floors[2] == 601 && prepared.diagnostics.floor_lookups == 3,
            "advanced patterns use low-byte key even with flag32 set");

    saved = source(); aliases = {};
    for (unsigned key = 1; key <= 5; ++key) {
        aliases.floors[key] = static_cast<std::uint16_t>(500 + key);
        aliases.walls[key] = static_cast<std::uint16_t>(400 + key);
    }
    aliases.floors[31] = 531;
    auto& normal = saved.levels[0].walls[0];
    normal.segments = odh::wall_top_left | odh::wall_top_right;
    normal.top_left_style = 3; normal.top_right_style = 2;
    normal.top_left_pattern = 0x0101; normal.top_right_pattern = 2;
    normal.bottom_left_pattern = 3; normal.bottom_right_pattern = 4;
    auto& diagonal = saved.levels[0].walls[1];
    diagonal.segments = odh::wall_horizontal_diagonal;
    diagonal.top_left_pattern = 0x0101; diagonal.top_left_style = 2;
    diagonal.top_right_pattern = 3; diagonal.top_right_style = 22;
    diagonal.bottom_left_pattern = 4; diagonal.bottom_right_pattern = 5;
    for (auto& level : saved.levels) {
        level.floors[2] = 31;
        level.walls[2].segments = odh::wall_vertical_diagonal;
    }
    saved.levels[0].walls[3].segments = odh::wall_horizontal_diagonal;
    saved.levels[0].walls[3].top_left_style = 2;
    (*saved.pools)[2] = (*saved.water)[2] = 1;
    prepared = odh::prepare_saved_architecture(saved, aliases);
    const auto& n = prepared.levels[0].walls[0];
    require(n.top_left_style == 1 && n.top_right_style == 2 && n.top_left_pattern == 401 &&
            n.top_right_pattern == 402 && n.bottom_left_pattern == 403 && n.bottom_right_pattern == 404,
            "normal decode normalization and four WALm fields");
    const auto& d = prepared.levels[0].walls[1];
    require(d.top_left_pattern == 501 && d.top_left_style == 502 && d.top_right_pattern == 3 &&
            d.top_right_style == 22 && d.bottom_left_pattern == 404 && d.bottom_right_pattern == 405,
            "diagonal FLRm fields are independent and bypass flags / normalization");
    require(prepared.levels[0].walls[2].top_left_style == 531 &&
            prepared.levels[0].walls[2].top_left_pattern == 531 && prepared.levels[0].floors[2] == 65534 &&
            prepared.levels[1].walls[2].top_left_style == 531 && prepared.levels[1].floors[2] == 531,
            "diagonal fallback uses remapped floor before first-story overlays");
    require(prepared.levels[0].walls[3].top_left_pattern == 0 &&
            prepared.levels[0].walls[3].top_left_style == 502, "one-zero diagonal field never triggers shared-floor fallback");
    require(normal.top_left_style == 3 && diagonal.top_left_pattern == 0x0101 &&
            saved.levels[0].floors[2] == 31, "preparation preserves original grids");

    saved = source(5); aliases = {};
    (*saved.pools)[1] = 255; (*saved.pools)[2] = 1; (*saved.pools)[3] = 2;
    (*saved.water)[3] = 1;
    prepared = odh::prepare_saved_architecture(saved, aliases);
    require(prepared.levels[0].floors[0] == 5 && prepared.levels[0].floors[1] == 5 &&
            prepared.levels[0].floors[2] == 65535 && prepared.levels[0].floors[3] == 65534,
            "overlay 0/255 inactive; water overrides pool");
    require(prepared.levels[1].floors[2] == 5 && prepared.levels[1].floors[3] == 5,
            "overlays affect ground only");

    saved = source();
    for (std::size_t i = 0; i < 4096; ++i) {
        saved.levels[0].floors[i] = static_cast<std::uint16_t>(i);
        saved.levels[0].walls[i].bottom_left_pattern = static_cast<std::uint16_t>(i);
    }
    (*saved.levels[0].flags)[1] = (*saved.levels[0].flags)[65] = 0;
    saved.levels[0].walls[65].segments = odh::wall_horizontal_diagonal;
    (*saved.water)[65] = 1;
    prepared = odh::prepare_saved_architecture(saved, {}, {false, 2});
    require(prepared.width == 2 && prepared.height == 2 &&
            prepared.levels[0].floors == std::vector<std::uint16_t>({0, 1, 64, 65534}) &&
            prepared.levels[0].walls[2].bottom_left_pattern == 64 &&
            prepared.levels[0].walls[3].top_left_style == 65 &&
            prepared.buildable == std::vector<std::uint8_t>({0, 1, 0, 1}), "top-left crop after remap and overlays");
    require(saved.levels[0].floors.size() == 4096 && saved.levels[0].floors[65] == 65,
            "crop leaves source grids intact");
    odh::ArchitecturePreparationLimits limit;
    limit.retained_bytes = sizeof(odh::PreparedArchitecture) + 4 *
        (2 * (sizeof(std::uint16_t) + sizeof(odh::SavedWallTile)) + sizeof(std::uint8_t));
    require(odh::prepare_saved_architecture(saved, {}, {false, 2}, limit).width == 2,
            "exact retained byte allowance accepted");
    --limit.retained_bytes;
    bad([&] { odh::prepare_saved_architecture(saved, {}, {false, 2}, limit); });

    saved = source(1); aliases = {};
    saved.levels[1].floors[0] = 65535; aliases.floors[255] = 500;
    const auto raw_map = odh::generate_saved_room_map(saved);
    prepared = odh::prepare_saved_architecture(saved, aliases);
    const auto restored_map = odh::generate_prepared_room_map(prepared);
    require(raw_map.rooms.size() == 4 && restored_map.rooms.size() == 3 &&
            restored_map.floor_resources_resolved && !raw_map.floor_resources_resolved,
            "resolved advanced sentinel changes upper-floor segmentation");
    aliases.floors[255] = 900;
    const auto other_map = odh::generate_prepared_room_map(odh::prepare_saved_architecture(saved, aliases));
    require(other_map.levels == restored_map.levels && other_map.rooms.size() == restored_map.rooms.size(),
            "ordinary dynamic IDs preserve segmentation for equal resources");

    saved = source(1); saved.levels[0].floors[0] = 0;
    saved.levels[0].walls[0].segments = odh::wall_horizontal_diagonal;
    (*saved.pools)[0] = 1;
    prepared = odh::prepare_saved_architecture(saved, {});
    const auto direct_map = odh::generate_prepared_room_map(prepared);
    require(std::none_of(direct_map.rooms.begin(), direct_map.rooms.end(),
        [](const auto& room) { return room.level == 0 && room.pool; }),
        "prepared map never reapplies diagonal fallback after overlay");

    bad([&] { auto s = source(); s.width = 32; odh::prepare_saved_architecture(s, {}); });
    for (const auto size : {0U, 65U}) bad([&] { odh::prepare_saved_architecture(source(), {}, {false, size}); });
    bad([&] { auto s = source(); s.levels[0].flags.reset(); odh::prepare_saved_architecture(s, {}); });
    bad([&] { auto s = source(); s.levels[0].flags->pop_back(); odh::prepare_saved_architecture(s, {}); });
    bad([&] { auto s = source(); s.pools.reset(); odh::prepare_saved_architecture(s, {}); });
    bad([&] { auto s = source(); s.water->pop_back(); odh::prepare_saved_architecture(s, {}); });
    bad([&] { auto s = source(); s.levels[1].floors.pop_back(); odh::prepare_saved_architecture(s, {}); });
    bad([&] { auto s = source(); s.levels[1].walls[0].segments = 64; odh::prepare_saved_architecture(s, {}); });
    bad([&] { auto s = source(); s.levels[1].walls[0].segments = 48; odh::prepare_saved_architecture(s, {}); });
    for (const auto id : {0, 255, 65534, 65535}) bad([&] {
        odh::ArchitectureResourceAliases invalid; invalid.floors[0] = static_cast<std::uint16_t>(id);
        odh::prepare_saved_architecture(source(), invalid);
    });
    bad([&] { auto p = prepared; p.buildable.pop_back(); odh::generate_prepared_room_map(p); });
    bad([&] { auto p = prepared; p.levels[1].walls.pop_back(); odh::generate_prepared_room_map(p); });
    bad([&] { auto p = prepared; p.levels[0].walls[0].segments = 48; odh::generate_prepared_room_map(p); });
    bad([&] { auto p = prepared; p.width = 0; odh::generate_prepared_room_map(p); });
    bad([&] { odh::RoomMapLimits small; small.rooms = 1; odh::generate_prepared_room_map(prepared, small); });
    std::cout << checks << " prepared architecture checks passed\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
