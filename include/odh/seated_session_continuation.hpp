#pragma once

#include "odh/definition_index.hpp"
#include "odh/session_bundle.hpp"

namespace odh {

// A bounded, deterministic digest of executable private BHAV fields, including
// keys and 8002 metadata. The host still verifies source/asset/root separately.
std::array<std::uint8_t,32> session_action_routine_fingerprint(const RoutineSet&);

// GEXP resource identity of the EXACT loaded private/semiglobal bank, not a
// claim that every potential call is resolvable. Missing targets remain normal
// native VM runtime faults if reached. Checks native owner key consistency;
// hashes raw versions/headers, optional metadata and 64-bit lengths without
// imposing seated closure limits or validating unused executable paths. Never
// loads more code. Domain is distinct from legacy closures.
std::array<std::uint8_t,32> session_loaded_bank_fingerprint(const RoutineSet&);

// Value-only checks. The singular wrapper retains the legacy one-resident profile.
// The record overload validates one idle household actor using its GEXP banks.
// Requires the continuation,
// a successful completed Sit, reciprocal positioned containment and matching
// Stand EP27. Dynamic owner declarations must equal the referenced owner set.
// A blocking idle animation may remain only in the resident's own Main frame;
// its Playback wait must match the current VM pending opcode 44 completely.
void validate_seated_session_continuation(const SessionBundle&);
void validate_seated_session_continuation(const SessionBundle&, const SessionSeatedContinuation&);
// Generic execution already preserves the actual VM/queues/route ancestry and
// installed banks. Validate current physical posture without asserting a
// completed player Sit or requiring its legacy sidecar. Value-only, no calls.
void validate_generic_session_posture(const SessionBundle&);
// Validates profile selection, ordering and complete seated actor coverage.
void validate_seated_session_continuations(const SessionBundle&);
const SessionSeatedContinuation* find_seated_session_continuation(const SessionBundle&, std::int16_t);

struct SessionActionRoutineSource {
    RoutineSet routines;
    std::optional<SessionLinkedSemiglobal> linked_semiglobal;
};
using SessionActionRoutineLoader = std::function<SessionActionRoutineSource(
    const DefinitionCandidate&,std::uint16_t)>;

// Load only from the caller's unique immutable DefinitionCandidate. Verifies
// exact provenance, live EntryPoint table and both Sit/Stand closure digests
// before returning any owner. No VM, World or live session is modified.
RoutineOwners resolve_seated_session_owners(const SessionBundle&, const SessionSeatedContinuation&,
    const DefinitionCandidate&, const std::vector<EntryPoint>&,
    const SessionActionRoutineLoader&);
RoutineOwners resolve_seated_session_owners(const SessionBundle&,
    const DefinitionCandidate&, const std::vector<EntryPoint>&,
    const SessionActionRoutineLoader&);

} // namespace odh
