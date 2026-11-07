#pragma once

#include "odh/definition_index.hpp"
#include "odh/family.hpp"
#include "odh/family_overlay.hpp"
#include "odh/lot_locations.hpp"
#include "odh/neighbor_context.hpp"
#include "odh/world.hpp"

#include <filesystem>

namespace odh {

// Read the immutable family baseline and its identity from the same IFF view.
NeighborhoodFamilySource load_neighborhood_family_source(
    const std::filesystem::path& neighborhood_iff);

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

enum class HouseholdMemberPresence {
    absent,
    present_without_position,
    present_with_position
};

struct ReconciledHouseholdMember {
    std::uint32_t guid;
    HouseholdMemberPresence presence;
    std::optional<std::int16_t> object_id;
    std::optional<EntityPosition> position;
};

// Read-only reconciliation against a restored World. Every avatar must have
// an exact definition GUID so an unknown avatar cannot be mistaken as absent.
// It does not create entities, move avatars, or start simulation.
std::vector<ReconciledHouseholdMember> reconcile_session_household(
    const SessionEntry&, const World&);
std::vector<ReconciledHouseholdMember> reconcile_session_household(
    const std::vector<std::uint32_t>& member_guids, const World&);

// `character_definitions` must come from the selected UserData/Characters
// catalog, not from the general object catalog. House-file existence and
// readability are checked by the caller before passing its path.
SessionEntry resolve_session_entry(unsigned neighborhood_mode,
    std::uint16_t house_number, std::string house_file,
    const LotLocations&, const FamilyContext&, const NeighborContext&,
    const DefinitionIndex& character_definitions);

// Prepare a visited lot using the origin household's resolved resources.
// Destination occupancy does not choose the traveling family. This creates no
// World entities and does not perform residential arrival or a move-in.
SessionEntry resolve_visit_session_entry(const SessionEntry& permanent_home,
    unsigned destination_mode, std::uint16_t destination_house,
    std::string destination_file, const LotLocations& destination_locations,
    const std::vector<std::uint32_t>& traveler_guids);

// Load and resolve a selected household from installed UserData. An optional,
// explicit player-owned character directory may supply additional OBJD138
// person definitions; it is never inferred from a save or resource file.
SessionEntry load_session_entry(const std::filesystem::path& user_data,
    unsigned neighborhood_mode, std::uint16_t house_number, std::uint8_t language = 1,
    const FamilyOverlay* saved_overlay = nullptr,
    const std::filesystem::path& player_character_directory = {});

SessionEntry load_visit_session_entry(const std::filesystem::path& user_data,
    unsigned home_mode, std::uint16_t home_house,
    unsigned destination_mode, std::uint16_t destination_house,
    const std::vector<std::uint32_t>& traveler_guids, std::uint8_t language = 1,
    const FamilyOverlay* saved_overlay = nullptr,
    const std::filesystem::path& player_character_directory = {});

} // namespace odh
