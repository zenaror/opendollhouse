#pragma once

#include "odh/definition_index.hpp"
#include "odh/family.hpp"
#include "odh/lot_locations.hpp"
#include "odh/neighbor_context.hpp"

namespace odh {

struct SessionEntryMember {
    std::uint32_t guid;
    std::int16_t neighbor_id;
    std::string character_source;
    std::string character_asset;
    std::uint16_t object_definition_id;
};

// Resolved identity for a resident household selected from the neighborhood.
// It does not instantiate Sims or start the lot simulation.
struct SessionEntry {
    unsigned neighborhood_mode;
    std::uint16_t house_number;
    std::string house_file;
    std::uint16_t family_chunk_id;
    std::int32_t family_number;
    std::vector<SessionEntryMember> members;
};

// `character_definitions` must come from the selected UserData/Characters
// catalog, not from the general object catalog. House-file existence and
// readability are checked by the caller before passing its path.
SessionEntry resolve_session_entry(unsigned neighborhood_mode,
    std::uint16_t house_number, std::string house_file,
    const LotLocations&, const FamilyContext&, const NeighborContext&,
    const DefinitionIndex& character_definitions);

} // namespace odh
