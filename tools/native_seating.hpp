// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/world.hpp"
#include <functional>
#include <optional>
#include <string>

namespace odh_host {
struct NativeSeatingTile {std::int32_t x=0,y=0;std::int16_t level=1;};
struct NativeSeatingContext {
    odh::World* world=nullptr;
    std::uint32_t width=0,height=0;
    std::function<bool()> source_still_matches;
    // Actual native325af owner: nullopt unknown, {0,0} known empty. The
    // argument is the native tile center in World XY units, not YX call order.
    std::function<std::optional<odh::WorldEntityReference>(const odh::EntityPosition&)> floor_head;
    // Native receiver-owned Thread, NULL TEMP arguments and StackObject0.
    // The preflight is read-only and must not publish World changes.
    std::function<bool(odh::WorldEntityReference,std::uint16_t,std::string&)> preflight_entry;
    std::function<bool(odh::WorldEntityReference,std::uint16_t,std::string&)> native_entry;
};
struct NativeSeatingResult {
    bool completed=false,effects_started=false;
    std::optional<bool> matching_neighbor;
    std::size_t visited=0,changed=0,entry_calls=0;
    std::string diagnostic;
};
// Native4336c1: cardinal-facing lookup, then current native-tree traversal
// (slot0 first, physical1+), Flags8 bit100 and conditional receiver EP9.
// Boundaries/levels rejected by native325af are known no-ops. Unknown heads,
// missing reciprocal storage and expired generations never mean empty.
bool preflight_native_seating(const NativeSeatingContext&,NativeSeatingTile,std::string&);
NativeSeatingResult update_native_seating(const NativeSeatingContext&,NativeSeatingTile);
// Native6c2948 is World DX;6c2924 is World DY. Caller supplies the original
// pose/direction captured at the source stage, not a made-up current heading.
std::optional<NativeSeatingTile> native_seating_front_tile(
    const odh::EntityPosition&,std::int16_t direction,std::string&);
NativeSeatingTile native_seating_center_tile(const odh::EntityPosition&);
}
