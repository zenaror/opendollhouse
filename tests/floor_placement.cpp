#include "odh/floor_placement.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void expect(odh::FloorPlacementResult actual, odh::FloorPlacementResult expected,
            const char* message) {
    ++checks;
    if (actual != expected) throw std::runtime_error(message);
}
}

int main() {
    using odh::FloorPlacementResult;
    using namespace odh::placement_flag;
    try {
        expect(odh::floor_change_valid(65535, 1, 0, true, 0), FloorPlacementResult::cant_place_on_water, "pool rejected without pool flags");
        expect(odh::floor_change_valid(65535, 1, allow_on_pool, true, 0), FloorPlacementResult::allowed, "allow-on-pool accepted");
        expect(odh::floor_change_valid(65535, 1, require_pool, true, 0), FloorPlacementResult::allowed, "require-pool also allows pool");
        expect(odh::floor_change_valid(3, 1, require_pool, true, 0), FloorPlacementResult::must_place_on_pool, "required pool rejects ordinary floor");
        expect(odh::floor_change_valid(65534, 1, 0, true, 0), FloorPlacementResult::cant_place_on_water, "water rejected without water flags");
        expect(odh::floor_change_valid(65534, 1, on_water, true, 0), FloorPlacementResult::allowed, "on-water accepted");
        expect(odh::floor_change_valid(3, 1, require_water, true, 0), FloorPlacementResult::must_place_on_water, "required water rejects ordinary floor");
        expect(odh::floor_change_valid(0, 1, 0, true, 0), FloorPlacementResult::not_allowed_on_terrain, "terrain requires on-terrain");
        expect(odh::floor_change_valid(0, 1, on_terrain, true, 0), FloorPlacementResult::allowed, "on-terrain accepted");
        expect(odh::floor_change_valid(0, 2, 0, true, 0), FloorPlacementResult::cant_place_in_air, "zero floor above level one requires air or offset");
        expect(odh::floor_change_valid(0, 2, in_air, true, 0), FloorPlacementResult::allowed, "in-air accepted above first level");
        expect(odh::floor_change_valid(0, 2, 0, true, 1), FloorPlacementResult::allowed, "nonzero level offset accepted");
        expect(odh::floor_change_valid(0, 2, 0, true, 0x8000), FloorPlacementResult::allowed, "unsigned OBJD level offset remains nonzero");
        expect(odh::floor_change_valid(3, 2, 0, true, 0), FloorPlacementResult::not_allowed_on_floor, "game object needs on-floor");
        expect(odh::floor_change_valid(3, 2, on_floor, true, 0), FloorPlacementResult::allowed, "on-floor accepted");
        expect(odh::floor_change_valid(3, 2, in_air, true, 1), FloorPlacementResult::allowed, "in-air plus nonzero offset accepted");
        expect(odh::floor_change_valid(3, 2, in_air, true, 0), FloorPlacementResult::not_allowed_on_floor, "in-air alone does not allow ordinary floor");
        expect(odh::floor_change_valid(3, 2, 0, false, 0), FloorPlacementResult::allowed, "avatar-like non-game object bypasses ordinary-floor check");
        expect(odh::floor_change_valid(3, 2, 1u << 7, true, 0), FloorPlacementResult::not_allowed_on_floor, "RequireFirstLevel is not consulted by this routine");
        std::cout << checks << " FloorChangeValid checks passed (pure rule only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Floor placement test failed: " << error.what() << '\n';
        return 1;
    }
}
