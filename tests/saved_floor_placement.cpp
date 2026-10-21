#include "odh/saved_floor_placement.hpp"
#include "odh/prepared_architecture.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
odh::PreparedArchitecture architecture(std::uint16_t floor) {
    odh::PreparedArchitecture result{};
    result.width = result.height = 1;
    for (auto& level : result.levels) {
        level.floors = {floor};
        level.walls = {odh::SavedWallTile{}};
    }
    return result;
}
odh::SavedWorldEntity entity(std::int16_t flags, std::uint16_t offset, bool avatar = false) {
    odh::SavedWorldEntity result{};
    result.saved.prefix.object_data[42] = flags;
    result.definition.level_offset = offset;
    if (avatar) result.tail.person.emplace();
    return result;
}
}

int main() {
    try {
        using odh::FloorPlacementResult;
        using odh::SavedFloorPlacementState;
        auto arch = architecture(5);
        auto object = entity(static_cast<std::int16_t>(odh::placement_flag::on_floor), 0);
        auto result = odh::check_saved_floor_placement(object, arch, 8, 8, 1);
        require(result.state == SavedFloorPlacementState::allowed && result.floor == 5 &&
                result.placement_flags == odh::placement_flag::on_floor && result.level_offset == 0 &&
                result.rule_result == FloorPlacementResult::allowed,
                "saved OBJM flag and OBJD offset reach FloorChangeValid");

        object.saved.prefix.object_data[42] = 0;
        result = odh::check_saved_floor_placement(object, arch, 8, 8, 1);
        require(result.state == SavedFloorPlacementState::rejected &&
                result.rule_result == FloorPlacementResult::not_allowed_on_floor,
                "ordinary saved object rejects a floor without OnFloor");

        auto avatar = entity(0, 0, true);
        result = odh::check_saved_floor_placement(avatar, arch, 8, 8, 1);
        require(result.state == SavedFloorPlacementState::allowed,
                "saved avatar follows non-VMGameObject floor branch");

        arch = architecture(0);
        avatar.saved.prefix.object_data[42] = static_cast<std::int16_t>(odh::placement_flag::on_terrain);
        result = odh::check_saved_floor_placement(avatar, arch, 8, 8, 1);
        require(result.state == SavedFloorPlacementState::allowed && result.floor == 0,
                "terrain floor zero remains a valid queried pattern");

        avatar.saved.prefix.object_data[42] = static_cast<std::int16_t>(0x8002);
        result = odh::check_saved_floor_placement(avatar, arch, 8, 8, 1);
        require(result.state == SavedFloorPlacementState::allowed && result.placement_flags == 0x8002,
                "signed OBJM storage preserves high flag bits");

        result = odh::check_saved_floor_placement(avatar, arch, 8, 8, 2);
        require(result.state == SavedFloorPlacementState::rejected &&
                result.rule_result == FloorPlacementResult::cant_place_in_air,
                "upper level floor zero is checked using the requested level");
        avatar.definition.level_offset = 0x8000;
        result = odh::check_saved_floor_placement(avatar, arch, 8, 8, 2);
        require(result.state == SavedFloorPlacementState::allowed && result.level_offset == 0x8000,
                "unsigned OBJD LevelOffset is preserved through the adapter");

        auto missing = avatar;
        missing.definition.level_offset.reset();
        result = odh::check_saved_floor_placement(missing, arch, 8, 8, 1);
        require(result.state == SavedFloorPlacementState::unsupported && !result.rule_result,
                "missing OBJD LevelOffset does not silently default");
        result = odh::check_saved_floor_placement(avatar, arch, 16, 8, 1);
        require(result.state == SavedFloorPlacementState::unsupported && !result.floor,
                "out-of-architecture candidate is unsupported");

        arch.diagnostics.unresolved_floor_alias_references = 1;
        result = odh::check_saved_floor_placement(object, arch, 8, 8, 1);
        require(result.state == SavedFloorPlacementState::unsupported && !result.rule_result,
                "unresolved declared floor identity cannot yield an allowed placement");

        arch = architecture(65535);
        avatar.saved.prefix.object_data[42] = static_cast<std::int16_t>(odh::placement_flag::allow_on_pool);
        result = odh::check_saved_floor_placement(avatar, arch, 8, 8, 1);
        require(result.state == SavedFloorPlacementState::allowed && result.floor == 65535,
                "prepared pool sentinel reaches pool placement rule");
        arch = architecture(65534);
        result = odh::check_saved_floor_placement(avatar, arch, 8, 8, 1);
        require(result.state == SavedFloorPlacementState::rejected &&
                result.rule_result == FloorPlacementResult::cant_place_on_water,
                "water sentinel remains distinct from pool");

        std::cout << checks << " saved FloorChangeValid checks passed (floor gate only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Saved floor placement test failed: " << error.what() << '\n';
        return 1;
    }
}
