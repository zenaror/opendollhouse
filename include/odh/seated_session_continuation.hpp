#pragma once

#include "odh/definition_index.hpp"
#include "odh/session_bundle.hpp"

namespace odh {

// A bounded, deterministic digest of executable private BHAV fields, including
// keys and 8002 metadata. The host still verifies source/asset/root separately.
std::array<std::uint8_t,32> session_action_routine_fingerprint(const RoutineSet&);

// Value-only checks. Requires the continuation and exactly one idle resident,
// a successful completed Sit, reciprocal positioned containment and matching
// Stand EP27. Dynamic owner declarations must equal the referenced owner set.
// A blocking idle animation may remain only in the resident's own Main frame;
// its Playback wait must match the current VM pending opcode 44 completely.
void validate_seated_session_continuation(const SessionBundle&);

struct SessionActionRoutineSource {
    RoutineSet routines;
    std::optional<SessionLinkedSemiglobal> linked_semiglobal;
};
using SessionActionRoutineLoader = std::function<SessionActionRoutineSource(
    const DefinitionCandidate&,std::uint16_t)>;

// Load only from the caller's unique immutable DefinitionCandidate. Verifies
// exact provenance, live EntryPoint table and both Sit/Stand closure digests
// before returning any owner. No VM, World or live session is modified.
RoutineOwners resolve_seated_session_owners(const SessionBundle&,
    const DefinitionCandidate&, const std::vector<EntryPoint>&,
    const SessionActionRoutineLoader&);

} // namespace odh
