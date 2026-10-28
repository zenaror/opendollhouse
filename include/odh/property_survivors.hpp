#pragma once
#include "odh/saved_world.hpp"

namespace odh {
struct PropertyDepartingActor {
    std::int16_t id = 0;
    std::uint64_t generation = 0;
    std::uint32_t guid = 0;
};
struct PropertySurvivors {
    std::unique_ptr<World> world;
    std::unique_ptr<RelationshipState> relationships;
    std::vector<SavedMultitileGroup> groups;
    std::vector<std::int16_t> removed_ids;
};
// STRUCTURAL preparation only, never an executable property or release receipt.
// Caller pins a stable CURRENT checkpoint/resource index and certifies the full
// current partition (including singletons). Seeds are explicit selections from
// the audited disposition policy; this routine does not choose them or grant
// financial/household authority. Actor approvals must be derived from the exact
// departing lease, not all type-2 objects in the lot.
// Complete groups and occupied children are recursively removed, irrespective
// of child retention flags. Every removed actor (EntityKind avatar OR own OBJD
// type2) needs exact id/generation/GUID approval; retained actors are unsupported
// until a fresh-activation policy exists. Ghosts and noncanonical/stale OBJECT
// relationship references reject. NBRS matrices are preserved without ID casts.
// Returns owned private durable state, reserved retired IDs and unsplit groups;
// never copies source records, VM queues, stacks, schedulers or host callbacks.
// Arbitrary object/attribute words are not interpreted as references. Their
// typed-reference audit, provenance, fresh execution, architecture and artifact
// publication remain REQUIRED before activation or a financial commit.
// FormatError on unsupported/invalid input; source state is never mutated.
PropertySurvivors prepare_property_survivors(const World&, const RelationshipState&,
    const DefinitionIndex&, const std::vector<SavedMultitileGroup>& complete_partition,
    bool complete_inventory, const std::vector<std::int16_t>& deletion_seeds,
    const std::vector<PropertyDepartingActor>& departing_actors,
    const SavedWorldLimits& = {});
} // namespace odh
