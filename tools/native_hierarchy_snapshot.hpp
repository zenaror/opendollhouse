// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/world.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace odh_host {
// Native22665/328f3 use signed cached tile bytes, not optional host position.
// Level is deliberately not part of this predicate.
inline bool native_in_world_xy(const odh::EntityPosition& p,std::uint32_t width,std::uint32_t height){
    const auto tile=[](std::int32_t coordinate){const auto shifted=coordinate>=0?std::int64_t(coordinate)/16:-((15-std::int64_t(coordinate))/16);
        const auto byte=static_cast<unsigned>(shifted)&255U;return byte>=128?int(byte)-256:int(byte);};
    const auto x=tile(p.x),y=tile(p.y);
    return x>=1&&y>=1&&x<std::int64_t(width)-1&&y<std::int64_t(height)-1;
}
struct NativeHierarchyNode {
    odh::WorldEntityReference reference{};
    std::int16_t raw_slot14=0,raw_parent26=0;
};
// These are native hierarchy words, not World::attachment or dynamic MT links.
// 432664 snapshots the ROOT pose before walking zero-slot aliases, then returns
// the FINAL owner's resolved parent and (raw slot-1), except -1 stays -1.
// 328f3 does NOT walk and retains the raw slot verbatim.
struct NativeHierarchySnapshot {
    odh::WorldEntityReference root{},hierarchy_owner{};
    std::optional<odh::EntityPosition> root_position;
    // Exact native scalar state, including explicitly known OOW +110. This is
    // separate from represented position and is never inferred from nullopt.
    std::optional<odh::EntityPosition> native_root_position;
    std::vector<NativeHierarchyNode> chain;
    std::optional<odh::WorldEntityReference> parent;
    std::int32_t slot=-1;
    // Native 432664 writes raw14=-1 only on its FINAL zero-slot owner.
    // Capture is read-only: the actual execution owner must perform this write.
    std::optional<odh::WorldEntityReference> normalize_zero_owner;
};
// Unknown words/expired authority/identity changes/cycles are failures. A known
// nonpositive or absent Parent26 ID resolves NULL as native407498 does; its raw
// value is preserved in chain.back(), distinct from an unknown native word.
std::optional<NativeHierarchySnapshot> capture_native_hierarchy_snapshot(
    const odh::World&,odh::WorldEntityReference,const std::function<bool()>&,
    std::string& error);
std::optional<NativeHierarchySnapshot> capture_native_direct_hierarchy_snapshot(
    const odh::World&,odh::WorldEntityReference,const std::function<bool()>&,
    std::string& error);
// Recheck exact generations AND raw words after a callback, before a consumer
// mutates hierarchy. Also verifies the root pose and resolved parent identity.
bool native_hierarchy_snapshot_still_matches(const odh::World&,
    const NativeHierarchySnapshot&,const std::function<bool()>&,std::string& error);
struct NativeHierarchyUnlinkServices {
    // Actual native cached-tile bounds. Required for an unparented represented
    // pose; canonical negative sentinel XY can prove outside without geometry.
    std::function<std::optional<bool>(odh::WorldEntityReference)> native_in_world;
    // Actual existing placement/slot phase owner. Only physical slots use it.
    std::function<bool(odh::WorldEntityReference,std::optional<odh::WorldEntityReference>,
        std::size_t,const std::function<bool()>&)> physical_mutation;
    std::function<bool(odh::WorldEntityReference,std::int16_t,std::string&)> publish_level;
    // Native327d9 calls32888 using the OLD receiver floor tile. The real registry
    // owner must perform/validate this publication; no guessed registration.
    std::function<bool(odh::WorldEntityReference,const odh::EntityPosition&,std::string&)> register_promoted_floor;
    // Optional while legacy consumers retain their narrower contract. The
    // full native floor owner uses32984 to clear this exact receiver's old
    // tile-head index before its own raw hierarchy words are cleared.
    std::function<bool(odh::WorldEntityReference,const odh::EntityPosition&,std::string&)> unregister_native_floor;
};
// Native432a70 forced-OOW hierarchy branch only: direct328f3 ->32984(self),
// native slot0 child ->32984(child) ->327d9(child,old direct snapshot).
// Caller owns root tile unregistration, scalar OOW publication and lifecycle.
// A failure after the first write is partial; caller must discard the candidate.
bool unlink_native_hierarchy_for_out_of_world(odh::World&,odh::WorldEntityReference,
    std::optional<odh::EntityPosition> old_native_pose,const std::function<bool()>&,
    std::string& error,const NativeHierarchyUnlinkServices& services={});

// Already-admitted logical432664 destination, translated by32771 during the
// forced in-world432a70 branch. The caller owns the source InWorld/bounds proof.
struct NativeHierarchyRestoreDestination {
    odh::EntityPosition position{};
    std::optional<odh::WorldEntityReference> parent;
    std::int32_t logical_slot=-1;
};
struct NativeHierarchyRestoreServices {
    // Actual native325af tile-head index, NOT first/sorted registry occupant.
    // nullopt is unavailable; {0,0} is authenticated empty. Publication follows
    // native32888 and must compare the exact expected old head before writing.
    std::function<std::optional<odh::WorldEntityReference>(const odh::EntityPosition&)> floor_head;
    std::function<bool(const odh::EntityPosition&,odh::WorldEntityReference expected,
        odh::WorldEntityReference next,std::string&)> publish_floor_head;
    // Existing synchronous placement/slot owner. A retained operation expires
    // as soon as its invocation returns and cannot mutate a later candidate.
    std::function<bool(odh::WorldEntityReference,std::optional<odh::WorldEntityReference>,
        std::size_t,const std::function<bool()>&)> physical_mutation;
    // Only represented in-world poses need this actual guarded scalar owner.
    // OOW levels use World's explicit native sentinel-position state instead.
    std::function<bool(odh::WorldEntityReference,std::int16_t,std::string&)> publish_level;
};
// Native432a70 forced in-world insertion: detach ONLY the displaced receiver
// (32984, no child promotion), publish the inserted receiver327d9, then put the
// displaced body under its dedicated raw slot0 while retaining that body's own
// chain. Changes hierarchy and native level only; no XYZ scalar publication.
// Except for native's exact already-occupying no-op, the inserted receiver must
// already be unlinked with known-empty slot0. Failure can leave partial effects;
// the caller must discard/invalidate the candidate rather than publish it.
bool restore_native_hierarchy_in_world(odh::World&,odh::WorldEntityReference,
    const NativeHierarchyRestoreDestination&,const std::function<bool()>&,
    std::string& error,const NativeHierarchyRestoreServices&);
} // namespace odh_host
