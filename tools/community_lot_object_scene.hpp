// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "community_lot_objects.hpp"
#include "saved_lot_renderer.hpp"

namespace odh_host {
// Start a new frozen native lifetime from the actual imported source. The
// caller supplies the resource manifest, actual source global bank (already
// activated as simless: Global32=1), actual clock when known, and the new native
// VM's selected RNG seed. No original object/avatar Thread is manufactured.
// History covers this fresh native lifetime; all live IDs are unavailable by
// World membership, not duplicated into permanent retirement reservations.
odh::ActorlessObjectTradeState make_community_object_baseline(const odh::SavedWorld&,
    odh::SessionResourceIdentity,odh::RuntimeGlobalsSnapshot,
    std::optional<odh::Ts1ClockSnapshot>,std::uint64_t random_seed);

struct CommunityObjectScene {
    // Keep alive for the entire lifetime of the renderer, which borrows it.
    std::unique_ptr<odh::SavedWorld> saved_world;
    std::vector<SavedLotRuntimeObject> runtime_objects;
    std::vector<odh::ObjectGroup> runtime_groups;
    // Complete current physical membership, including imported heterogeneous
    // groups and singletons. This says nothing about avatar Thread coverage.
    std::vector<odh::ObjectTradeGroup> physical_groups;
};
// Pure owned reconstruction, no publication or VM ticks. Match surviving and
// removed original identities against the exact immutable import. Match new
// ordinary singleton origins and all frozen object programs against actual
// installed resources; restore VM stacks to validate their closure without
// running Main. Preserve actual Init/placement effects from state.world.
// Unsupported provenance/topology/closures throw FormatError, never reset the
// document to a fresh lot. original/definitions/resources remain unmodified.
CommunityObjectScene prepare_community_object_scene(const odh::SavedWorld& original,
    const odh::ActorlessObjectTradeState&,const odh::DefinitionIndex&,
    ObjectTradeResourceLibrary&);
}
