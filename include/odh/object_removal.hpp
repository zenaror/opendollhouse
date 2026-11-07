#pragma once
#include "odh/world.hpp"

namespace odh {
struct ObjectRemovalRequest {
    std::int16_t target;
    std::uint64_t generation;
    std::uint8_t flags;
    bool caller;
};
// nullopt accepts; a diagnostic refuses before the VM branch. The host callback
// must atomically preflight/admit its queue, including all unsupported profiles,
// immediate containment detach and end-of-tick policy. It must not retain the
// VmDispatch VM pointer. Throwing callbacks must leave host state unchanged.
using ObjectRemovalSchedule=std::function<std::optional<std::string>(
    const ObjectRemovalRequest&,const VmDispatch&)>;
// Native451baa→442db0: only AFTER actual host deletion admission, publish
// operands[2] bit1 into the exact body's internal recovery policy. This does
// not queue/remove anything, run notifications or imply full native Delete.
// Host must supply the authenticated canonical target for non-dynamic MT.
bool publish_native_object_removal_policy(World&,const ObjectRemovalRequest&);
// Native440f2b marks every admitted physical member before deleting the first.
// Host supplies the complete authenticated cohort after capacity/profile checks
// and before detach or script callbacks. Exact live identities are preflighted
// together; this never derives native state from World::mark_dead.
bool mark_native_object_removal_positions(World&,const std::vector<WorldEntityReference>&);
// Pinned FreeSO7e76 VMRemoveObjectInstance: Target LE16 zero selects Caller,
// every other value selects StackObject. Flags byte2 is passed through but
// ignored by reference execution. Caller removal branches TRUE_NEXT_TICK,
// including repeated queue admission; other targets branch TRUE immediately.
// Bound roles resolve exact live/retained references; unbound legacy roles use
// their live numeric lookup. Null or already-Dead targets do not schedule work.
// A nonzero bound generation without its exact body is unsupported, not nil.
// TRUE_NEXT_TICK compares resolved pointers (including two null roles), never
// numeric IDs. Only an actual VM primitive/query callback may invoke this service.
// No entity is retired here.
// World and callback dependencies must outlive the returned serial handler.
VmPrimitive object_removal_primitive(World&,ObjectRemovalSchedule);
} // namespace odh
