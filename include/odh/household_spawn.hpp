#pragma once

#include "odh/avatar_neighbor.hpp"
#include "odh/world.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace odh {

// Shared native avatar base-constructor word profile. No family/neighbor,
// appearance, startup, placement or activation effects. Avatar must exist.
// Private-candidate caller discards state if an allocation/budget check fails.
// CurrentValue41 is a group property: supply only an authenticated catalog
// initial value. Omitted means unknown, including old restored World banks.
void initialize_avatar_constructor_words(World&,std::int16_t avatar_id,
    std::optional<std::int16_t> authenticated_initial_value={});
// Pinned TS1ObjectProvider catalog predicate + VMContext constructor price,
// evaluated from the avatar's complete, versioned OBJD VM word bank. Returns
// known zero for a proven noncatalog avatar, not for absent/truncated input.
std::optional<std::int16_t> avatar_constructor_initial_value(
    const std::vector<std::int16_t>& definition_words);

// Candidate order used by the TS1 mailbox-based family arrival search.
// Positions are in sixteenth-tile units. Callers must still apply lot
// placement and collision rules before using a candidate.
std::vector<EntityPosition> household_spawn_candidates(
    EntityPosition mailbox, std::uint16_t lot_width, std::uint16_t lot_height);

// Return the first candidate accepted by the caller's placement validator,
// matching FindLocationFor's stop-on-success behavior. The callback must
// perform the full placement check; World::set_position alone is insufficient.
std::optional<EntityPosition> first_household_spawn_candidate(
    EntityPosition mailbox, std::uint16_t lot_width, std::uint16_t lot_height,
    const std::function<bool(const EntityPosition&)>& accepts);

struct HouseholdSpawnRequest {
    std::uint32_t guid;
    const NeighborRecord* neighbor;
};
struct MaterializedHouseholdMember {
    std::uint32_t guid;
    std::int16_t runtime_id;
};
struct HouseholdSpawnResult {
    std::unique_ptr<World> candidate;
    std::map<std::uint32_t, std::int16_t> runtime_ids;
    std::vector<MaterializedHouseholdMember> new_members;
};

// Copies the supplied owned WorldSnapshot and adds missing FAMI avatars to a
// private candidate. This stage returns new avatars without NBRS inheritance,
// family PersonData[61], or positions so a caller can run exact Init/EP8 and
// build runtime controllers before applying inherited data and testing arrival.
// It does not execute BHAVs, run the scheduler, or persist
// SavedWorld/OBJM/OBJT records. The callback binds exact character resources
// and metadata; callers remain responsible for successful runtime startup.
// Callback failure throws FormatError; callback exceptions propagate. Failure
// discards the private candidate and leaves source untouched.
using HouseholdAvatarPreparer = std::function<bool(World&, std::int16_t, std::uint32_t)>;
// Applies NBRS data and the FAMI chunk id to a member returned by
// stage_household_members. The caller can run Init/EP8 in the preparer before
// invoking this explicit inheritance phase.
void inherit_staged_household_member(
    World& candidate, std::int16_t runtime_id, const NeighborRecord& neighbor,
    std::uint16_t family_chunk_id);
using HouseholdArrivalResolver = std::function<std::optional<EntityPosition>(
    const World&, std::int16_t, std::uint32_t)>;

// Mirrors the TS1 VerifyFamily lifecycle while keeping the candidate World
// private until every requested resident is prepared and activated. A
// position callback must perform one stateful SetPosition attempt, including
// its validation and entry-point side effects. `blocked` continues searching
// and must leave the avatar out of world; `placed` requires the avatar to have
// exactly the requested position; `unsupported` aborts the whole candidate
// because the host cannot reproduce the required placement semantics.
enum class HouseholdPositionAttemptStatus { blocked, placed, unsupported };
using HouseholdPositionAttempt = std::function<HouseholdPositionAttemptStatus(
    World&, std::int16_t, const EntityPosition&)>;
// Called after startup, NBRS/FAMI inheritance and optional arrival search.
// Hosts use this to apply owner permissions and register/reschedule the Sim.
using HouseholdMemberActivation = std::function<bool(
    World&, std::int16_t, std::uint32_t, bool)>;
struct VerifiedHouseholdMember {
    std::uint32_t guid;
    std::int16_t runtime_id;
    bool positioned;
    std::size_t position_attempts;
};
struct HouseholdVerificationResult {
    std::unique_ptr<World> candidate;
    std::map<std::uint32_t, std::int16_t> runtime_ids;
    std::vector<VerifiedHouseholdMember> new_members;
};

// Processes missing members in the order supplied by household. The mailbox
// position is captured once for the search; callers must not move it from an
// attempt callback during this operation.
// In-place VerifyFamily for an unpublished bootstrap AFTER original lot Load
// and class recovery. Uses actual World constructors and exact generations;
// never restores/publishes a snapshot and never dispatches Main. Host callbacks
// own Init, real placement, and scheduler admission in that order. On any
// failure, effects may already exist: discard the ENTIRE private bootstrap.
// This is not a transaction for an already published gameplay World.
struct HouseholdActivationResult {
    std::map<std::uint32_t,std::int16_t> runtime_ids;
    std::vector<VerifiedHouseholdMember> new_members;
};
HouseholdActivationResult verify_household_members_in_place(
    World&,const std::vector<HouseholdSpawnRequest>&,std::uint16_t family_chunk_id,
    const HouseholdAvatarPreparer&,std::optional<EntityPosition> mailbox,
    std::uint16_t lot_width,std::uint16_t lot_height,
    const HouseholdPositionAttempt&,const HouseholdMemberActivation&);

HouseholdVerificationResult verify_household_members(
    const WorldSnapshot& source,
    const std::vector<HouseholdSpawnRequest>& household,
    std::uint16_t family_chunk_id,
    const HouseholdAvatarPreparer& prepare_avatar,
    std::optional<EntityPosition> mailbox,
    std::uint16_t lot_width, std::uint16_t lot_height,
    const HouseholdPositionAttempt& attempt_position,
    const HouseholdMemberActivation& activate_member,
    WorldLimits limits = {});

HouseholdSpawnResult stage_household_members(
    const WorldSnapshot& source,
    const std::vector<HouseholdSpawnRequest>& household,
    std::uint16_t family_chunk_id,
    const HouseholdAvatarPreparer& prepare_avatar,
    WorldLimits limits = {});

// Convenience path for callers that already prepared each avatar completely.
// Arrival must perform full placement validation; World::set_position is only
// applied after acceptance and is not itself the validator.
HouseholdSpawnResult materialize_household_members(
    const WorldSnapshot& source,
    const std::vector<HouseholdSpawnRequest>& household,
    std::uint16_t family_chunk_id,
    const HouseholdAvatarPreparer& prepare_avatar,
    const HouseholdArrivalResolver& resolve_arrival,
    WorldLimits limits = {});

} // namespace odh
