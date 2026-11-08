// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ts1_travel.hpp"
#include "odh/neighbor_context.hpp"
namespace odh {
// Resource resolver is the host's exact HHRD/NPCA ownership adapter; it must
// reject stale generations, ambiguous catalog entries and resource substitutions.
using OriginalTravelIdentityResolver=std::function<Ts1TravelMember(WorldEntityReference)>;
std::optional<OriginalCompanionMarker> select_original_companion(const NeighborInventory&,
    const NeighborContext&,std::int16_t owner,std::int32_t type);
std::optional<WorldEntityReference> lookup_original_companion(const World&,const OriginalCompanionMarker&,
    const OriginalTravelIdentityResolver&);
PreparedNeighborInventory prepare_original_companion_consumption(const NeighborInventory&,const OriginalCompanionMarker&);
struct PreparedOriginalTravelCapture {
    OriginalTravelCapture receipt;
    PreparedNeighborInventory inventory;
};
// Explicit FAMI source order for family mode; caller alone for single mode.
// Companion is separate from departure membership and player control.
PreparedOriginalTravelCapture prepare_original_travel_capture(const World&,const NeighborInventory&,
    const NeighborContext&,WorldEntityReference caller,const std::vector<Ts1TravelDepartureMember>& family,
    const Ts1Temps&,std::int16_t hour,std::int16_t minute,bool residential,
    const OriginalTravelIdentityResolver&);
std::pair<std::int16_t,std::int16_t> original_travel_time(std::int16_t destination,
    std::int16_t hour,std::int16_t minute,std::int16_t add_hour,std::int16_t add_minute,bool residential);
std::array<std::int16_t,11> original_travel_values(const Entity&);
// Resolve appearance without mutating World or RNG. Missing evidence rejects
// the entire prepared effect operation, leaving all tokens and banks untouched.
using OriginalTravelOutfitResolver=std::function<std::optional<AvatarOutfitReference>(const World&,std::int16_t,std::uint16_t)>;
void apply_original_travel_effects(World&,NeighborInventory&,WorldEntityReference,
    const OriginalTravelOutfitResolver&);
PrimitiveOutcome consume_original_travel_time(NeighborInventory&,std::int16_t caller);
// Full source-body/resource/effects verification at the sealed Generic17 boundary.
void validate_original_travel_capture(const SessionBundle&);
} // namespace odh
