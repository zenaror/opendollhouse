// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "community_lot_object_scene.hpp"
#include "community_lot_document.hpp"
#include "odh/family_overlay.hpp"
namespace odh_host {
// Sealed provenance derived from an authenticated occupied checkpoint and the
// exact vacant document. Never an externally supplied list of deletable IDs.
class ResidentialPropertyRetirementProof {
public:
    bool matches(const odh::ActorlessObjectTradeState&)const;
    bool retired_original_avatar(const odh::Entity&)const;
    bool retired_npc(const odh::SessionNpcAvatarOrigin&)const;
    bool retired_household(const odh::SessionRetiredHouseholdAvatar&)const;
private:
    ResidentialPropertyRetirementProof()=default;
    struct Actor {std::int16_t id;std::uint64_t generation;std::uint32_t guid;};
    std::array<std::uint8_t,32> state_sha256_{};
    std::vector<Actor> actors_;
    std::vector<odh::SessionNpcAvatarOrigin> npcs_;
    std::vector<odh::SessionRetiredHouseholdAvatar> households_;
    friend ResidentialPropertyRetirementProof prepare_residential_property_retirement(
        const odh::NeighborhoodFamilySource&,const odh::FamilyOverlay&,std::uint16_t,
        const std::vector<std::uint8_t>&,const CommunityLotDocument&,
        const odh::DefinitionIndex&,const odh::SessionNpcAvatarResolver&,const odh::SessionRetiredHouseholdAvatarResolver&);
};
// Producer validation before publication, against the exact still-occupied head.
ResidentialPropertyRetirementProof prepare_residential_property_retirement(
    const odh::NeighborhoodFamilySource&,const odh::FamilyOverlay& occupied,std::uint16_t family,
    const std::vector<std::uint8_t>& source_checkpoint,const CommunityLotDocument&,
    const odh::DefinitionIndex&,const odh::SessionNpcAvatarResolver& verified_npc_resources={},
    const odh::SessionRetiredHouseholdAvatarResolver& verified_historical_households={});
// Consumer validation: verifies committed release/document digest, then derives
// the SAME proof against the historical occupied prefix, including deleted families.
ResidentialPropertyRetirementProof restore_residential_property_retirement(
    const odh::NeighborhoodFamilySource&,const odh::FamilyOverlay& committed,
    const odh::FamilyPropertyRelease&,const std::vector<std::uint8_t>& source_checkpoint,
    const CommunityLotDocument&,const odh::DefinitionIndex&,
    const odh::SessionNpcAvatarResolver& verified_npc_resources={},
    const odh::SessionRetiredHouseholdAvatarResolver& verified_historical_households={});
// Defined by the shared scene implementation. Legacy four-argument consumer
// keeps rejecting unsupported actor history; only this exact proof opens the path.
CommunityObjectScene prepare_community_object_scene(const odh::SavedWorld&,
    const odh::ActorlessObjectTradeState&,const odh::DefinitionIndex&,
    ObjectTradeResourceLibrary&,const ResidentialPropertyRetirementProof&);
}
