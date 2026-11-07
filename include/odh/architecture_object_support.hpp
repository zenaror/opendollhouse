// SPDX-License-Identifier: MPL-2.0
// FreeSO VMArchitecture.SetObjectSupported and VMGameObject position lifecycle,
// pinned revision 7e76c1960252ddd033d8937d5edb47d326f5b69c.
#pragma once
#include "odh/object_position_valid.hpp"

namespace odh {
// Exact event-owned boolean grids, not SupportStrength, occupancy counts or a
// projection of current objects. Unknown legacy history is represented by an
// absent optional in the host, never by calling the fresh constructor.
struct ArchitectureObjectSupport {
    std::uint32_t width=0,height=0;
    std::array<std::vector<std::uint8_t>,2> levels;
    bool operator==(const ArchitectureObjectSupport& other) const {
        return width==other.width&&height==other.height&&levels==other.levels;
    }
    bool operator!=(const ArchitectureObjectSupport& other) const {return !(*this==other);}
};
// Only for the actual new VMArchitecture bootstrap, before any object position
// lifecycle events. Import/replay and exact checkpoint restore are host owners.
ArchitectureObjectSupport make_architecture_object_support(std::uint32_t width,std::uint32_t height);
void validate_architecture_object_support(const ArchitectureObjectSupport&,std::uint32_t dimension=64);
// The caller must be inside the real SetObjectSupported lifecycle event, after
// its Category8 check. The capability binds that event to its live owner. A
// rejected or unavailable event leaves the complete grid unchanged.
bool set_architecture_object_supported(ArchitectureObjectSupport&,const World&,
    ObjectPositionIdentity,const EntityPosition&,bool set,
    const std::function<bool()>& source_still_matches);
}
