// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/architecture_object_support.hpp"
#include "odh/wall_appearance.hpp"
#include "odh/object_position_registry.hpp"
#include "odh/room_lighting.hpp"

namespace odh {
// One owner for the object-produced runtime architecture. Authored wall grids
// remain in ArchitectureSnapshot; these event fields must not be re-derived
// from post-event flags or replayed EP scripts when loading a native checkpoint.
struct ArchitectureObjectState {
    ArchitectureObjectSupport object_support;
    WallAppearance wall_appearance;
    std::array<std::vector<std::uint8_t>,2> occupied_walls;
    // Missing registry remains unknown; a successful complete bootstrap stores
    // its actual snapshot here and restores it without replaying object scripts.
    std::optional<ObjectPositionRegistrySnapshot> position_registry;
    // Missing on legacy AOBJ1; only an actual lighting bootstrap may populate.
    std::optional<RoomLightingSnapshot> lighting;
    // Blueprint's last visual publication can lag simulation lighting (for
    // example when the final deferred request is room0). Keep both domains.
    std::optional<RoomLightingSnapshot> lighting_committed;
};
struct ArchitectureObjectStateLimits {
    std::size_t bytes=2*1024*1024,faces=16384,applications=4096,registry_entries=65536;
    std::uint32_t dimension=64;
};
void validate_architecture_object_state(const ArchitectureObjectState&,const ArchitectureObjectStateLimits& = {});
std::vector<std::uint8_t> encode_architecture_object_state(const ArchitectureObjectState&,const ArchitectureObjectStateLimits& = {});
ArchitectureObjectState decode_architecture_object_state(const std::vector<std::uint8_t>&,const ArchitectureObjectStateLimits& = {});
}
