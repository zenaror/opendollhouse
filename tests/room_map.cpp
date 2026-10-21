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
    require(rejected, "invalid room map accepted");
}
odh::SavedArchitecture grid(std::uint32_t width, std::uint32_t height, std::uint16_t floor = 1) {
    odh::SavedArchitecture result;
    result.width = width; result.height = height;
    for (auto& level : result.levels) {
        level.floors.assign(std::size_t(width) * height, floor);
        level.walls.resize(level.floors.size());
    }
    return result;
}
std::uint16_t first(std::uint32_t packed) { return static_cast<std::uint16_t>(packed); }
std::uint16_t second(std::uint32_t packed) { return static_cast<std::uint16_t>((packed >> 16) & 0x7fffU); }
bool adjacent(const odh::GeometricRoomMap& map, unsigned a, unsigned b) {
    const auto& values = map.rooms[a].adjacent;
    return std::find(values.begin(), values.end(), b) != values.end();
}
void complete(const odh::GeometricRoomMap& result) {
    for (std::size_t level = 0; level < result.levels.size(); ++level)
        for (auto packed : result.levels[level]) {
            const auto a = first(packed), b = second(packed);
            require(a > 0 && b > 0 && a < result.rooms.size() && b < result.rooms.size(), "unassigned/invalid room ID");
            require(result.rooms[a].level == level && result.rooms[b].level == level, "global level ownership");
        }
    for (const auto& room : result.rooms)
        for (const auto other : room.adjacent)
            require(other != room.id && adjacent(result, other, room.id), "adjacency reciprocity");
}
}
int main() { try {
    auto architecture = grid(3, 2);
    architecture.levels[0].floors = {0, 1, 2, 3, 4, 5};
    auto result = odh::generate_saved_room_map(architecture);
    require(result.rooms.size() == 3 && result.rooms[0].id == 0, "dummy + two global rooms");
    require(result.rooms[1].area == 6 && result.rooms[2].area == 6, "connected normal tile area");
    require(result.rooms[1].outside && result.rooms[2].outside, "first room of each level is outside");
    require(result.rooms[1].bounds.x == 0 && result.rooms[1].bounds.y == 0 &&
        result.rooms[1].bounds.width == 3 && result.rooms[1].bounds.height == 2, "normal room bounds");
    require(result.levels[0] == std::vector<std::uint32_t>(6, 0x00010001U) &&
        result.levels[1] == std::vector<std::uint32_t>(6, 0x00020002U), "packed repeated IDs");
    complete(result);
    {
        odh::GeometricRoomMap locations;
        locations.width=2;locations.height=1;locations.rooms.resize(6);
        for(std::uint16_t id=1;id<locations.rooms.size();++id)
            locations.rooms[id]={id,static_cast<std::uint8_t>(id==5?1:0),1,{},false,false,{}};
        const auto packed=[](std::uint16_t a,std::uint16_t b){return std::uint32_t(a)|(std::uint32_t(b)<<16);};
        locations.levels[0]={packed(1,2),0x80000000U|packed(3,4)};
        locations.levels[1]={packed(5,5),packed(0,0)};
        require(odh::geometric_room_at(locations,4,5,1)==2&&
                odh::geometric_room_at(locations,5,5,1)==2&&
                odh::geometric_room_at(locations,6,5,1)==1,
                "vertical diagonal chooses second half at equality and first only when x exceeds y");
        require(odh::geometric_room_at(locations,16+8,6,1)==3&&
                odh::geometric_room_at(locations,16+8,7,1)==3&&
                odh::geometric_room_at(locations,16+8,8,1)==4,
                "horizontal diagonal chooses second half only when coordinate sum exceeds fifteen");
        require(odh::geometric_room_at(locations,0,0,2)==5&&
                odh::geometric_room_at(locations,16,0,2)==0,
                "same-half room and explicit room zero are values on the selected level");
        require(!odh::geometric_room_at(locations,-1,0,1)&&
                !odh::geometric_room_at(locations,32,0,1)&&
                !odh::geometric_room_at(locations,0,0,0)&&
                !odh::geometric_room_at(locations,0,0,3),
                "out-of-bounds coordinates and levels are unsupported");
        require(odh::geometric_room_at_runtime(locations,-1,0,1)==0&&
                    odh::geometric_room_at_runtime(locations,32,0,1)==0&&
                    odh::geometric_room_at_runtime(locations,0,-1,1)==0&&
                    odh::geometric_room_at_runtime(locations,0,0,0)==0&&
                    odh::geometric_room_at_runtime(locations,0,0,2,1)==0,
                "runtime GetRoomAt fallback resolves outside coordinates and levels to room zero");
        require(!odh::geometric_room_at_runtime(locations,0,0,1,0)&&
                    !odh::geometric_room_at_runtime(locations,0,0,1,3),
                "unsupported runtime story counts remain absent");
        auto malformed=locations;malformed.levels[1].pop_back();
        require(!odh::geometric_room_at(malformed,0,0,1),"malformed room-grid extent is unsupported");
        require(!odh::geometric_room_at_runtime(malformed,32,0,1),
                "malformed room-grid extent is not hidden by outside fallback");
        malformed=locations;malformed.levels[1][0]=packed(6,6);
        require(!odh::geometric_room_at(malformed,0,0,2),"unknown packed room identity is unsupported");
        malformed=locations;malformed.levels[1][0]=packed(1,1);
        require(!odh::geometric_room_at(malformed,0,0,2),"room owned by another level is unsupported");
        malformed=locations;malformed.levels[0][0]=packed(1,6);
        require(!odh::geometric_room_at(malformed,6,5,1),
                "a valid selected half does not hide an unknown opposite half");
        malformed=locations;malformed.levels[0][0]=packed(1,5);
        require(!odh::geometric_room_at(malformed,6,5,1),
                "a valid selected half does not hide opposite-half ownership by another level");
    }

    // Wrap joins the outer columns; cutting only the middle boundary cannot
    // divide a toroidal map. Both seams are needed to make two rooms.
    architecture = grid(4, 2);
    for (unsigned y = 0; y < 2; ++y) {
        auto& wall = architecture.levels[0].walls[y * 4 + 2];
        wall.segments = odh::wall_top_left; wall.top_left_style = 1;
    }
    result = odh::generate_saved_room_map(architecture);
    require(result.rooms.size() == 3, "toroidal edge wrap connects solid-wall sides");
    for (unsigned y = 0; y < 2; ++y) {
        auto& wall = architecture.levels[0].walls[y * 4];
        wall.segments = odh::wall_top_left; wall.top_left_style = 1;
    }
    result = odh::generate_saved_room_map(architecture);
    require(result.rooms.size() == 4 && result.rooms[1].area == 4 && result.rooms[2].area == 4, "solid wall splits");
    require(result.rooms[1].outside && !result.rooms[2].outside && result.rooms[2].adjacent.empty(), "solid walls do not create adjacency");
    require(result.rooms[2].bounds.x == 2 && result.rooms[2].bounds.width == 2, "split bounds");
    complete(result);

    // The style != 1 path permits SpreadOnto, but forceAdj prevents merging.
    // DecodeWalls retains only these four non-wall styles on normal tiles.
    for (auto style : {2, 12, 13, 14}) for (bool horizontal : {false, true}) {
        architecture = horizontal ? grid(1, 4) : grid(4, 1);
        for (auto& wall : architecture.levels[0].walls) {
            wall.segments = horizontal ? odh::wall_top_right : odh::wall_top_left;
            wall.top_left_style = wall.top_right_style = static_cast<std::uint16_t>(style);
        }
        result = odh::generate_saved_room_map(architecture);
        require(result.rooms.size() == 6, "non-wall segments separate four rooms + upper room");
        require(result.rooms[2].adjacent == std::vector<std::uint16_t>({1, 3}), "ordered reciprocal adjacency");
        require(adjacent(result, 1, 4) && adjacent(result, 3, 4), "wrapped fence adjacency");
        for (unsigned id = 1; id <= 4; ++id)
            require(result.rooms[id].area == 1 && result.rooms[id].outside, "outside propagates across non-wall adjacency");
        complete(result);
    }

    // Saved styles are normalized before segmentation on a local copy. Raw
    // zero and non-whitelisted values on present normal edges become solid.
    for (auto style : {0, 1, 3, 15, 22, 255, 65535}) for (bool horizontal : {false, true}) {
        architecture = horizontal ? grid(1, 4) : grid(4, 1);
        for (auto& wall : architecture.levels[0].walls) {
            wall.segments = horizontal ? odh::wall_top_right : odh::wall_top_left;
            wall.top_left_style = wall.top_right_style = static_cast<std::uint16_t>(style);
        }
        result = odh::generate_saved_room_map(architecture);
        require(result.rooms.size() == 6, "normalized walls separate four rooms + upper room");
        for (unsigned id = 1; id <= 4; ++id)
            require(result.rooms[id].adjacent.empty() && result.rooms[id].outside == (id == 1), "normalized walls block adjacency and exterior propagation");
        for (const auto& wall : architecture.levels[0].walls)
            require(wall.top_left_style == style && wall.top_right_style == style, "normalization does not mutate saved styles");
        complete(result);
    }
    architecture = grid(2, 2);
    for (auto& wall : architecture.levels[0].walls) wall.top_left_style = wall.top_right_style = 255;
    result = odh::generate_saved_room_map(architecture);
    require(result.rooms.size() == 3 && result.rooms[1].area == 4, "absent segments stay open despite raw style");
    require(architecture.levels[0].walls[0].top_left_style == 255, "absent saved style remains unchanged");

    // Diagonal styles carry floor data and bypass DecodeWalls normalization,
    // including values that would become style 1 on an ordinary wall tile.
    for (const auto diagonal : {odh::wall_horizontal_diagonal, odh::wall_vertical_diagonal})
        for (auto style : {0, 255}) {
            architecture = grid(1, 1);
            for (auto& level : architecture.levels) {
                auto& wall = level.walls[0];
                wall.segments = static_cast<std::uint8_t>(diagonal | odh::wall_top_left | odh::wall_top_right);
                wall.top_left_style = static_cast<std::uint16_t>(style);
                wall.top_right_style = 1;
                wall.top_left_pattern = 1;
            }
            result = odh::generate_saved_room_map(architecture);
            require(result.rooms.size() == 5 && first(result.levels[0][0]) == 1 && second(result.levels[0][0]) == 2,
                "diagonal raw-style halves remain distinct");
            require(adjacent(result, 1, 2) && result.rooms[2].outside, "diagonal non-wall style is preserved for wrapped edge adjacency");
            require(architecture.levels[0].walls[0].top_left_style == style, "diagonal saved style remains unchanged");
            complete(result);
        }

    architecture = grid(3, 1);
    architecture.levels[0].floors = {0, 4, 0};
    architecture.levels[1].floors = {0, 4, 0};
    result = odh::generate_saved_room_map(architecture);
    require(result.rooms.size() == 4 && result.rooms[1].area == 3, "ground zero/nonzero floors merge");
    require(result.levels[1] == std::vector<std::uint32_t>({0x00020002U, 0x00030003U, 0x00020002U}), "upper floor noFloorBad partitions zero and floor");
    require(result.rooms[2].area == 2 && result.rooms[3].area == 1 && adjacent(result, 2, 3), "upper zero-floor adjacency and area");
    complete(result);

    architecture = grid(5, 1, 7);
    architecture.pools = std::vector<std::uint8_t>({0, 255, 1, 1, 0});
    architecture.water = std::vector<std::uint8_t>({255, 0, 0, 1, 2});
    result = odh::generate_saved_room_map(architecture);
    require(!result.floor_resources_resolved, "raw-floor candidate records unresolved resource mapping");
    require(result.levels[0] == std::vector<std::uint32_t>({0x00010001U, 0x00010001U, 0x00020002U, 0x00030003U, 0x00030003U}),
        "pool/water overlays ignore zero/255 and water takes precedence");
    require(result.rooms[1].area == 2 && !result.rooms[1].pool && result.rooms[2].area == 1 &&
        result.rooms[2].pool && result.rooms[3].area == 2 && result.rooms[3].pool, "overlay pool/water partitioning");
    require(result.levels[1] == std::vector<std::uint32_t>(5, 0x00040004U), "overlays affect first story only");
    require(architecture.levels[0].floors == std::vector<std::uint16_t>(5, 7) &&
        *architecture.pools == std::vector<std::uint8_t>({0, 255, 1, 1, 0}) &&
        *architecture.water == std::vector<std::uint8_t>({255, 0, 0, 1, 2}), "overlay application does not modify input");
    complete(result);
    architecture = grid(2, 1);
    architecture.water = std::vector<std::uint8_t>({1, 0});
    result = odh::generate_saved_room_map(architecture);
    require(result.rooms[1].pool && !result.rooms[2].pool, "water overlay works when pool grid absent");

    architecture = grid(4, 1);
    architecture.levels[0].floors = {1, 65534, 65535, 2};
    result = odh::generate_saved_room_map(architecture);
    require(result.rooms.size() == 5 && result.rooms[1].area == 2 &&
        result.rooms[2].pool && result.rooms[3].pool && !result.rooms[1].pool, "distinct pool sentinels partition ordinary floor");
    require(result.levels[0] == std::vector<std::uint32_t>({0x00010001U, 0x00020002U, 0x00030003U, 0x00010001U}), "pool seed ordering");
    complete(result);

    for (const auto diagonal : {odh::wall_horizontal_diagonal, odh::wall_vertical_diagonal}) {
        architecture = grid(3, 3);
        for (auto& level : architecture.levels) {
            auto& wall = level.walls[4];
            wall.segments = static_cast<std::uint8_t>(diagonal | odh::wall_top_left | odh::wall_top_right);
            wall.top_left_style = wall.top_right_style = 1;
            wall.top_left_pattern = 65534;
            level.walls[5].segments = odh::wall_top_left; level.walls[5].top_left_style = 1;
            level.walls[7].segments = odh::wall_top_right; level.walls[7].top_right_style = 1;
        }
        result = odh::generate_saved_room_map(architecture);
        require(result.rooms.size() == 7 && result.rooms[1].area == 8, "diagonal isolated halves + exterior");
        const auto flag = diagonal == odh::wall_horizontal_diagonal ? 0x80000000U : 0U;
        require(result.levels[0][4] == (flag | 0x00030002U) &&
            result.levels[1][4] == (flag | 0x00060005U), "diagonal orientation bit and global subtile IDs");
        require(result.rooms[2].area == 1 && result.rooms[3].area == 1 &&
            result.rooms[2].bounds.x == 1 && result.rooms[2].bounds.y == 1 &&
            result.rooms[2].bounds.width == 1 && result.rooms[2].bounds.height == 1, "diagonal half area/bounds");
        require(result.rooms[diagonal == odh::wall_horizontal_diagonal ? 3 : 2].pool, "diagonal floor fields use reference orientation");
        require(!result.rooms[2].outside && !result.rooms[3].outside &&
            result.rooms[2].adjacent.empty() && result.rooms[3].adjacent.empty(), "diagonal wall has no adjacency across halves");
        complete(result);
    }

    for (const auto diagonal : {odh::wall_horizontal_diagonal, odh::wall_vertical_diagonal}) {
        architecture = grid(1, 1, 65534);
        for (auto& level : architecture.levels) {
            auto& wall = level.walls[0];
            wall.segments = static_cast<std::uint8_t>(diagonal | odh::wall_top_left | odh::wall_top_right);
            wall.top_right_style = 1;
        }
        result = odh::generate_saved_room_map(architecture);
        require(result.rooms.size() == 5 && result.rooms[1].pool && result.rooms[2].pool,
            "both-zero diagonal floor fields inherit raw floor sentinel");
        require(architecture.levels[0].walls[0].top_left_style == 0 &&
            architecture.levels[0].walls[0].top_left_pattern == 0, "diagonal fallback does not modify input");
        complete(result);
        architecture.levels[0].floors[0] = 7;
        architecture.pools = std::vector<std::uint8_t>({1});
        architecture.water = std::vector<std::uint8_t>({1});
        result = odh::generate_saved_room_map(architecture);
        require(!result.rooms[1].pool && !result.rooms[2].pool,
            "diagonal shared-floor fallback precedes pool/water overlays");
        for (auto& level : architecture.levels) level.walls[0].top_left_pattern = 65535;
        result = odh::generate_saved_room_map(architecture);
        const bool horizontal = diagonal == odh::wall_horizontal_diagonal;
        require(result.rooms[horizontal ? 2 : 1].pool && !result.rooms[horizontal ? 1 : 2].pool,
            "nonzero diagonal field prevents fallback of the zero field");
        complete(result);
    }

    // Both diagonal halves can join the same room around open neighbors.
    for (const auto diagonal : {odh::wall_horizontal_diagonal, odh::wall_vertical_diagonal}) {
        architecture = grid(2, 2);
        architecture.levels[0].walls[1].segments = static_cast<std::uint8_t>(diagonal);
        architecture.levels[0].walls[1].top_left_style = 1;
        architecture.levels[0].walls[1].top_left_pattern = 1;
        result = odh::generate_saved_room_map(architecture);
        require(first(result.levels[0][1]) == 1 && second(result.levels[0][1]) == 1 &&
            result.rooms[1].area == 5, "spread reaches both diagonal halves through open neighbors");
        complete(result);
    }

    // Isolate every tile, then open exactly one cardinal edge of the center
    // diagonal. This identifies which packed half each inDir reaches, whether
    // the neighboring tile or the diagonal half is discovered first.
    for (const auto diagonal : {odh::wall_horizontal_diagonal, odh::wall_vertical_diagonal})
        for (unsigned direction = 0; direction < 4; ++direction) {
            architecture = grid(3, 3);
            for (auto& wall : architecture.levels[0].walls) {
                wall.segments = odh::wall_top_left | odh::wall_top_right;
                wall.top_left_style = wall.top_right_style = 1;
                wall.top_left_pattern = 1;
            }
            auto& walls = architecture.levels[0].walls;
            walls[4].segments |= diagonal;
            const unsigned neighbor[] = {3, 1, 5, 7};
            if (direction == 0) walls[4].segments &= ~odh::wall_top_left;
            if (direction == 1) walls[4].segments &= ~odh::wall_top_right;
            if (direction == 2) walls[5].segments &= ~odh::wall_top_left;
            if (direction == 3) walls[7].segments &= ~odh::wall_top_right;
            result = odh::generate_saved_room_map(architecture);
            const auto neighbor_room = first(result.levels[0][neighbor[direction]]);
            const bool first_half = diagonal == odh::wall_horizontal_diagonal ? direction < 2 : direction == 1 || direction == 2;
            const auto packed = result.levels[0][4];
            require((first_half ? first(packed) : second(packed)) == neighbor_room &&
                (first_half ? second(packed) : first(packed)) != neighbor_room,
                "cardinal entry selects source-faithful diagonal half");
            require(result.rooms[neighbor_room].area == 2, "tile plus diagonal half has two fill items");
            complete(result);
        }

    architecture = grid(64, 64);
    result = odh::generate_saved_room_map(architecture);
    require(result.rooms[1].area == 4096 && result.rooms[2].id == 2, "maximum dimensions");
    complete(result);
    bad([&] { auto a = grid(0, 2); odh::generate_saved_room_map(a); });
    bad([&] { auto a = grid(65, 2); odh::generate_saved_room_map(a); });
    bad([&] { auto a = grid(2, 2); a.levels[0].floors.pop_back(); odh::generate_saved_room_map(a); });
    bad([&] { auto a = grid(2, 2); a.levels[1].walls.push_back({}); odh::generate_saved_room_map(a); });
    bad([&] { auto a = grid(2, 2); a.pools = std::vector<std::uint8_t>(3); odh::generate_saved_room_map(a); });
    bad([&] { auto a = grid(2, 2); a.water = std::vector<std::uint8_t>(5); odh::generate_saved_room_map(a); });
    for (auto segments : {48, 64, 128}) bad([&] {
        auto a = grid(2, 2); a.levels[0].walls[0].segments = static_cast<std::uint8_t>(segments);
        odh::generate_saved_room_map(a);
    });
    bad([&] { odh::RoomMapLimits limits; limits.dimension = 1; odh::generate_saved_room_map(grid(2, 2), limits); });
    bad([&] { odh::RoomMapLimits limits; limits.rooms = 1; odh::generate_saved_room_map(grid(2, 2), limits); });
    bad([&] { odh::RoomMapLimits limits; limits.rooms = 0; odh::generate_saved_room_map(grid(2, 2), limits); });
    std::cout << checks << " room map checks passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
