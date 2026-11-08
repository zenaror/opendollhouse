#pragma once
#include "linux_family_session_journal.hpp"
#include "odh/runtime_globals.hpp"
namespace odh_host {
// Original4f1b96 clears Simless (Global32) before and after a fresh
// authenticated destination load. Callers own that source phase; native cold
// restore is explicitly refused. Other globals, including single-family0, stay.
void reset_original_frontend_load_globals(odh::RuntimeGlobals&,const odh::Ts1TravelState&,bool native_restore);
struct TravelSessionCheckpoint {
    // Exact immutable home bytes survive the full trip, independently of outer
    // journal metadata (budget/inventory) promoted from the active destination.
    std::vector<std::uint8_t> suspended_home;
    odh::SessionBundle active; // Visited lot, or sealed HOME TRIP pending departure.
};
// Build a residential journal envelope without changing occupancy or running
// home simulation. Caller prepares and validates destination BEFORE publishing.
odh::SessionBundle make_travel_session_envelope(
    const std::vector<std::uint8_t>& suspended_home,const odh::SessionBundle& active,
    odh::SessionBundleLimits limits = {});
TravelSessionCheckpoint reopen_travel_session_envelope(
    const odh::SessionBundle&, odh::SessionBundleLimits limits = {});
// Full journal expected-head/lease validation before reopening an envelope
// or a sealed pending HOME TRIP after Generic17.
TravelSessionCheckpoint reopen_travel_session_checkpoint(
    const FamilySessionJournal&,const std::vector<std::uint8_t>&,
    odh::SessionBundleLimits limits = {});
std::filesystem::path publish_travel_session_checkpoint(FamilySessionJournal&,
    const std::vector<std::uint8_t>& suspended_home,const odh::SessionBundle& active,
    odh::SessionBundleLimits limits = {});
using TravelCheckpointDigest=std::array<std::uint8_t,32>;
// Authenticate a visited runtime payload against the current residential
// journal envelope, not against its own (necessarily different) file digest.
// No standalone/modified inner archive or sealed pending HOME is accepted.
TravelSessionCheckpoint authenticate_managed_visited_restore(
    const FamilySessionJournal&,const TravelCheckpointDigest& expected_envelope_head,
    const std::vector<std::uint8_t>& exact_visited_archive,
    odh::SessionBundleLimits limits = {});
// Separate guarded private-save authority for installed, unmanaged families.
// Caller supplies immutable identity freshly authenticated from installed data.
// Adopt an already authenticated legacy head without modifying it. A competing
// publisher may only have installed the exact same bytes at the new path.
void adopt_private_checkpoint_namespace(const std::filesystem::path& legacy,
    const std::filesystem::path& scoped,const TravelCheckpointDigest& expected_legacy,
    odh::SessionBundleLimits limits = {}, LinuxNeighborhoodStorage storage = {});
// A null expected head requires the canonical private file to be absent.
TravelCheckpointDigest publish_private_travel_session_checkpoint(
    const std::filesystem::path&,std::optional<TravelCheckpointDigest> expected_head,
    const std::vector<std::uint8_t>& suspended_home,const odh::SessionBundle& active,
    const odh::Ts1TravelState& expected_origin,odh::SessionBundleLimits limits = {});
// Uses the same head guard for the sealed departure capture and final home return.
TravelCheckpointDigest publish_private_home_session_checkpoint(
    const std::filesystem::path&,std::optional<TravelCheckpointDigest> expected_head,
    const odh::SessionBundle& home,const odh::Ts1TravelState& expected_origin,
    odh::SessionBundleLimits limits = {});
TravelSessionCheckpoint reopen_private_travel_session_checkpoint(
    const std::filesystem::path&,const TravelCheckpointDigest& expected_head,
    const odh::Ts1TravelState& expected_origin,odh::SessionBundleLimits limits = {});

}
