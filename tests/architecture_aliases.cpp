#include "odh/architecture_aliases.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Action>
void bad(const Action& action) {
    bool rejected = false;
    try { action(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "invalid architecture aliases accepted");
}
odh::ArchitectureMapEntry entry(std::string name, std::uint8_t id) {
    odh::ArchitectureMapEntry result;
    result.name = std::move(name);
    result.id = id;
    result.unknown = -7;
    result.name_padding = {0xa5};
    result.unknown2 = {0x12, 0x34};
    return result;
}
}

int main() {
    try {
        odh::ArchitectureMap floors, walls;
        floors.chunk_id = 1;
        floors.encoded = {0xde, 0xad};
        floors.entries = {
            entry("WALLPAPER.flr", 3),
            entry("floor.part.flr", 4),
            entry("first.flr", 5),
            entry("missing.flr", 6),
            entry("FLOOR.flr", 3), // Successful later record overwrites ID 3.
            entry("unknown.flr", 7), // Missing later record does not erase ID 3.
            entry(".flr", 8) // Empty prefix is a valid C# alias key.
        };
        walls.chunk_id = 0;
        walls.entries = {entry("door.wll", 3), entry("trim.wll", 9)};
        const auto floor_snapshot = floors.entries;
        const auto wall_snapshot = walls.entries;

        // Catalog order is explicit. Names are provider keys (stem without
        // final extension); duplicate case-folded keys use the later ID.
        const std::vector<odh::ArchitectureCatalogResource> floor_catalog = {
            {"wallpaper", 256}, {"FLOOR.PART", 257}, {"floor", 258},
            {"first", 260}, {"WALLPAPER", 261}, {"", 262}
        };
        const std::vector<odh::ArchitectureCatalogResource> wall_catalog = {
            {"door", 300}, {"trim", 301}
        };
        const auto resolved = odh::resolve_architecture_aliases(
            floors, walls, floor_catalog, wall_catalog);

        require(resolved.aliases.floors[3] == 258 &&
                resolved.aliases.floors[4] == 258 &&
                resolved.aliases.floors[5] == 260 &&
                resolved.aliases.floors[8] == 262,
                "map aliases use first-dot lowercase and ordered successful writes");
        require(!resolved.aliases.floors[6] && !resolved.aliases.floors[7] &&
                resolved.diagnostics.floor_map_misses == 2 &&
                resolved.diagnostics.unresolved_floor_aliases[6] &&
                resolved.diagnostics.unresolved_floor_aliases[7],
                "missing names are reported without manufacturing aliases");
        require(resolved.aliases.walls[3] == 300 && resolved.aliases.walls[9] == 301 &&
                resolved.wall_map_complete() && !resolved.floor_map_complete(),
                "wall and floor maps resolve independently");
        require(resolved.diagnostics.floor_catalog_duplicates == 1 &&
                resolved.diagnostics.wall_catalog_duplicates == 0 &&
                resolved.diagnostics.floor_map_overwrites == 1,
                "catalog and alias duplicate precedence is observable");
        require(floors.entries.size() == floor_snapshot.size() &&
                floors.entries[0].name == floor_snapshot[0].name &&
                floors.entries[0].unknown == floor_snapshot[0].unknown &&
                walls.entries[0].name == wall_snapshot[0].name &&
                floors.encoded == std::vector<std::uint8_t>({0xde, 0xad}),
                "resolution preserves source map records and payload");

        auto duplicate_aliases = floors;
        duplicate_aliases.entries = {entry("first.flr", 3), entry("wallpaper.flr", 3)};
        const auto duplicate_result = odh::resolve_architecture_aliases(
            duplicate_aliases, {}, floor_catalog, {});
        require(duplicate_result.aliases.floors[3] == 261 &&
                duplicate_result.diagnostics.floor_map_overwrites == 1,
                "later matching duplicate map alias overrides earlier value");
        duplicate_aliases.entries = {entry("first.flr", 3), entry("not-in-catalog.flr", 3)};
        const auto miss_after_match = odh::resolve_architecture_aliases(
            duplicate_aliases, {}, floor_catalog, {});
        require(miss_after_match.aliases.floors[3] == 260 &&
                miss_after_match.diagnostics.unresolved_floor_aliases[3],
                "later missing duplicate does not erase earlier successful alias");

        auto non_ascii = floors;
        non_ascii.entries = {entry(std::string("\xc0") + ".flr", 12)};
        const auto unsupported = odh::resolve_architecture_aliases(
            non_ascii, {}, {}, {});
        require(unsupported.diagnostics.unsupported_floor_map_names == 1 &&
                unsupported.diagnostics.floor_map_misses == 1 &&
                !unsupported.aliases.floors[12],
                "unsupported non-ASCII names are visible and remain unresolved");
        const std::vector<odh::ArchitectureCatalogResource> unsupported_floor_catalog = {
            {std::string("\xc0"), 256}
        };
        const auto unsupported_catalog = odh::resolve_architecture_aliases(
            {}, {}, unsupported_floor_catalog, wall_catalog);
        require(unsupported_catalog.diagnostics.unsupported_floor_catalog_names == 1 &&
                unsupported_catalog.diagnostics.unsupported_wall_catalog_names == 0,
                "unsupported catalog names are counted separately by resource family");

        bad([&] {
            auto catalog = floor_catalog;
            catalog.push_back({"bad", 65534});
            (void)odh::resolve_architecture_aliases(floors, {}, catalog, {});
        });
        bad([&] {
            auto catalog = floor_catalog;
            catalog.push_back({"other", 258});
            (void)odh::resolve_architecture_aliases(floors, {}, catalog, {});
        });
        bad([&] {
            odh::ArchitectureAliasResolverLimits limits;
            limits.catalog_entries = 1;
            (void)odh::resolve_architecture_aliases(floors, {}, floor_catalog, {}, limits);
        });
        bad([&] {
            odh::ArchitectureAliasResolverLimits limits;
            limits.map_entries = 1;
            (void)odh::resolve_architecture_aliases(floors, {}, {}, {}, limits);
        });
        bad([&] {
            odh::ArchitectureAliasResolverLimits limits;
            limits.name_length = 2;
            (void)odh::resolve_architecture_aliases(floors, {}, floor_catalog, {}, limits);
        });
        bad([&] {
            odh::ArchitectureAliasResolverLimits limits;
            limits.name_bytes = 4;
            (void)odh::resolve_architecture_aliases(floors, {}, floor_catalog, {}, limits);
        });
        std::cout << checks << " architecture alias checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
