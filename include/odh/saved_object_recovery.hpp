#pragma once
#include "odh/world.hpp"
#include <functional>
#include <string>
#include <vector>

namespace odh {
// Original OBJM import only. An ODHS continuation must never call this service.
// This coordinator mutates the caller's PRIVATE World, never a second World.
enum class SavedRecoveryClass { object, person, portal, special_cursor };
enum class SavedRecoveryPhase {
    load, notify_person, capture_placement, portal_detach,
    remove_entry, detach, before_clear, restart_main, init,
    restore_group, restore_placement, placement_entry, portal_restore, portal_reconcile,
    cleanup, cleanup_flags, person_unwind, person_visuals,
    person_placement, person_cleanup, person_actions, refresh, special_cursor,
    // Base431edd always publishes World lookup masks2/8 after Load/reset,
    // including a compatible original whose body was retained after removal.
    post_load_lookup_flags
};
struct SavedRecoveryEntity {
    WorldEntityReference reference{};
    SavedRecoveryClass kind=SavedRecoveryClass::object;
    bool code_changed=false;
    bool has_cleanup=false; // actual EP3 action nonzero, not routine availability
    bool native_group=false; // a surviving single piece can still have this class
    std::int16_t definition_version=0;
    Vm* thread=nullptr; // existing restored VM; identity/register ownership retained
    // Initial authenticated membership, used for preflight only. Execution uses
    // live native links supplied by the host, never this frozen vector.
    std::vector<WorldEntityReference> pieces;
};
struct SavedRecoveryCall {
    SavedRecoveryPhase phase;
    WorldEntityReference entity; // actual piece/person receiving the operation
    WorldEntityReference target; // original reset target; notification target for person
    bool full_person_reset=false; // self path; notify adapter selects by live uses/icon
    // Present only during execution. Person use unwind calls this to reset a
    // referenced object in this SAME recovery, without Load or post-load Init.
    // Reentrant reset of an already resetting identity is the native guarded no-op.
    std::function<bool(WorldEntityReference,std::string&)> reset_referenced;
    // Set from the exact original body at dispatch. The host must reject an
    // unproved deferred-dead callback path, never redirect to a reused ID.
    bool deferred_dead=false;
};
enum class SavedRecoveryLinkStatus { member, end, unsupported };
struct SavedRecoveryLink {
    SavedRecoveryLinkStatus status=SavedRecoveryLinkStatus::unsupported;
    WorldEntityReference reference{};
};
struct SavedRecoveryPersonTraversal {
    // Captures the native PERSON vector's begin/storage identity once. Every
    // lookup rereads its current end. END means ordinal == current length;
    // ordinal past end or invalidated storage is unsupported, never END.
    // Erase shifts slots; append may extend iteration within stable storage.
    std::function<SavedRecoveryLink(std::size_t,std::string&)> at;
};
struct SavedRecoveryHost {
    // MUST be read-only. Validate actual class adapters, code banks, scheduler
    // safe point, original ordered person-use/action records and recursive use
    // closure, placement/containment and group/index capabilities for the WHOLE
    // cohort. Missing capability is unsupported, never an accepted no-op.
    std::function<bool(const std::vector<SavedRecoveryEntity>&,std::string&)> preflight;
    // MUST be read-only; called for every operation before the first Load.
    std::function<bool(const SavedRecoveryCall&,std::string&)> supports;
    // Required original post-load traversal: acquire world.head30 ONCE after
    // whole-cohort preflight; read CURRENT.nexte0 AFTER its complete Load/reset/
    // final Init. Native creations prepend and are not visited retroactively.
    // Removed-current links require actual authority; unknown is not END.
    std::function<SavedRecoveryLink(std::string&)> world_head;
    std::function<SavedRecoveryLink(WorldEntityReference,std::string&)> world_next;
    // PERSON registration order is independent of the World linked list and
    // descriptor/cohort order. Each notifier call acquires its own cursor;
    // recursive resets must not replace a caller's cached storage identity.
    std::function<std::optional<SavedRecoveryPersonTraversal>(std::string&)> begin_person_notifications;
    // Required for native groups. These are read-only live link queries. A
    // missing/unknown cursor is UNSUPPORTED, never END. Both can return exact
    // deferred-dead identities; they must never substitute a reused numeric ID.
    // head resolves target.parent190 ?: target for remove_entry (cached across
    // EP10/detach), before_clear (cached across reset/offset/Init), and
    // restore_placement (cached across placement/EP9). next reads current.next18c
    // AFTER all callbacks for that iteration, even if current was deleted.
    // Host preflight authenticates link ownership for the whole cohort and
    // guarantees removed-current link semantics or explicitly declines them.
    std::function<SavedRecoveryLink(WorldEntityReference,SavedRecoveryPhase,std::string&)> group_head;
    std::function<SavedRecoveryLink(WorldEntityReference,SavedRecoveryPhase,std::string&)> group_next;
    // Owns native services/callbacks, not ordering or generic bank cleanup.
    // load should reuse run_saved_entity_load; EPs are synchronous except Main,
    // which is only started. notify_person/person_* use live ordered metadata,
    // unwind UseCount/actions (EP4) and may recursively reset referenced objects.
    // Person Main/EP3 retain TEMP; person_unwind must detach asynchronous owners.
    // before_clear unregisters Category/flags/footprints BEFORE raw bank clear.
    // capture_placement/restore_* retain the same identity, group offsets/prices,
    // slots and descendants; portals own passage/occupation/obstacle indexes.
    // portal_reconcile runs in finally after success, deletion or failure, even
    // if the original target was deleted. Rebuild surviving topology only; it
    // does not imply rollback or candidate acceptance after a failed callback.
    // Deleted originals may be skipped; a new generation is NEVER substituted.
    std::function<bool(const SavedRecoveryCall&,std::string&)> execute;
};
// Exact native selection for a non-null notification target. Queue targets,
// last action, arbitrary StackObject and TTA validity are intentionally absent.
// The host supplies CURRENT ordered use ownership and active icon identities.
bool saved_person_notification_requires_reset(WorldEntityReference target,
    const std::vector<WorldEntityReference>& active_uses,WorldEntityReference active_icon);

struct SavedRecoveryLimits {
    std::size_t entities=4096,planned_operations=262144,executed_operations=1048576,nested_resets=256;
};
struct SavedRecoveryResult {
    bool prepared=false,completed=false;
    bool discard_candidate=false; // effects began; this is not a rollback promise
    std::size_t operations=0;
    std::string diagnostic;
};
// Ordered caller-per-entity traversal: groups are deliberately NOT deduplicated.
// Preflights the whole cohort (including compatible notified people), then Load,
// notifications, class reset and changed-code final Init, before normal dispatch.
SavedRecoveryResult recover_original_saved_objects(World&,
    const std::vector<SavedRecoveryEntity>&,const SavedRecoveryHost&,
    bool recovery_enabled=true,SavedRecoveryLimits limits={});
} // namespace odh
