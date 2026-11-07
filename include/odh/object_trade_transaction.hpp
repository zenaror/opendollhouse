// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/object_trade.hpp"
#include "odh/actorless_object_trade.hpp"
#include "odh/object_runtime_resources.hpp"
#include "odh/object_position_change.hpp"
#include "odh/random.hpp"
#include "odh/relationship.hpp"
#include "odh/session_bundle.hpp"
#include "odh/runtime_globals.hpp"

namespace odh {
// Exact resolved definition and banks. No resource search/precedence happens in
// a transaction. Unknown GLOB/global closure must fail binding, not become empty.
struct ObjectTradeGroupResources;
class ObjectValueRegistry;
class DynamicObjectGroups;
struct ObjectTradeResource {
    SessionCreatedObject origin; // id/generation assigned by native construction.
    ObjectRuntimeResources runtime;
    RoutineSet global_routines, linked_routines;
    std::optional<SessionLinkedSemiglobal> linked_semiglobal;
    // Complete authored group plan. Leaf part resources must not nest plans.
    // Null retains the same core's ordinary singleton representation.
    std::shared_ptr<const ObjectTradeGroupResources> group;
    // Authenticated raw MyLeadObject for a direct singleton construction.
    std::optional<std::uint16_t> lead_object;
};
struct ObjectTradeGroupResourcePart {
    ObjectTradeResource resource;
    std::int16_t offset_x=0,offset_y=0,offset_level=0;
    std::optional<std::uint16_t> lead_object;
};
struct ObjectTradeGroupResources {
    // Requested construction resource is independent of mutable price_origin.
    // Imported heterogeneous membership has no fabricated common master.
    std::optional<SessionCreatedObject> requested;
    bool multitile=false,imported=false;
    bool live_association=false;
    std::size_t base_index=0;
    std::vector<ObjectTradeGroupResourcePart> parts; // Exact source order, <=64.
};
// Owned event-ledger snapshot from the same private candidate World. Missing
// values retain legacy absence; an existing source owner may not be dropped.
struct ObjectTradeArchitectureCapture {
    std::optional<ArchitectureObjectSupport> object_support;
    std::optional<ArchitectureObjectState> object_state;
};
// Exact immutable program collection from the selected source Thread. The
// kernel authenticates the global fingerprint and every owner against the
// captured SessionObjectThread manifest before restoring any execution state.
// Absence retains the legacy single-resource binding; it cannot hide additional
// installed owners. Fresh purchases have no selected source programs.
struct ObjectTradeSelectedPrograms {
    std::shared_ptr<const VmProgram> globals;
    SharedRoutineOwners owners;
};
struct ObjectTradeMemberAdmission {
    ObjectTradeSelectedPrograms programs;
    VmSavedRoutinePolicy saved_routine_policy=VmSavedRoutinePolicy::require_present;
};
struct ObjectTradeRuntimeBinding {
    VmMemory memory;
    // Explicit actual-constructor profile; an old physical/value archive alone
    // never authenticates the native MT constructor fields.
    NativeObjectGroupConstructionProfile native_group_construction=NativeObjectGroupConstructionProfile::unknown_legacy;
    // Built only after this binding has a stable lifetime. Adapters may retain
    // memory by reference; the VM receives a COPY of this memory, never a move.
    std::function<VmPrimitive(const VmMemory&)> primitive_factory;
    // Wiring only: binds placement/entry services to the actual restored/new
    // thread. The reference must remain private to this binding and must never
    // escape preparation or be used after the thread is destroyed. No VM writes.
    std::function<void(Vm&)> on_thread_bound;
    ObjectPositionValidServices placement;
    ObjectPositionChangeServices position_change;
    // Real buy/delete command, session version and candidate World authority.
    // Never manufacture an opcode42/45 ticket to stand in for a UI command.
    std::function<bool()> command_still_matches;
    // Optional adapter-owned final event capture, after World/relationship
    // capture and before the FIRST archive validation. Must preserve actual
    // event history and authenticate it against this exact World snapshot;
    // no load replay, reconstruction from coordinates or I/O is authorized.
    // Default absence keeps existing transaction/aggregate behavior.
    std::function<ObjectTradeArchitectureCapture(const WorldSnapshot&)> capture_architecture_state;
    // Exact selected source Thread admission, authenticated by the host against
    // its generation and immutable banks. Absence remains strict. This restores
    // retained imported frames only; synchronous command/entry checks stay strict.
    VmSavedRoutinePolicy selected_saved_routine_policy=VmSavedRoutinePolicy::require_present;
    std::optional<ObjectTradeSelectedPrograms> selected_programs;
    // Complete physical membership is published after all materializations and
    // shared-value admission, BEFORE any Init. Imported membership may have
    // definition_guid0; its individual origins are authenticated separately.
    std::function<void(const ObjectGroup&)> on_group_materialized;
    // One shared candidate binding routes services to every actual VM, not a
    // duplicate selected_thread. Called exactly once for each represented part.
    std::function<void(ObjectTradeIdentity,CodeOwner,Vm&)> on_member_thread_bound;
    // Exact saved programs/policy for each existing source part. No inference
    // from status, price origin or another piece's execution/registers.
    std::function<std::optional<ObjectTradeMemberAdmission>(ObjectTradeIdentity)> member_programs;
    // Binds the actual candidate physical/value owners, never a stale copy.
    std::function<std::shared_ptr<DynamicObjectGroups>(ObjectGroupSnapshot&,ObjectValueRegistry&)> on_group_state_bound;
    std::function<void(std::uint64_t&)> on_instruction_budget;
    std::function<bool(ObjectPositionIdentity,std::uint16_t)> execute_bound_entry;
};
// Every callback is bound to these candidate states, with lifetimes through
// preparation. It must not capture/write the live World/budget/RNG/relations,
// perform I/O, or publish rendering/audio effects. Source room/footprint state
// must be reconstructed here. admit_single_ordinary proves complete physical
// singleton topology AND no unrepresented sound/plugin/dynamic-peer cleanup.
// RuntimeGlobals is nullable only for unknown legacy state; the clock is
// rebuilt only from SIMS and is read-only throughout this external command.
// Neither missing capability permits fallback to a live bank or invented time.
// Lifecycle callbacks perform actual source operations, never dummy success.
using ObjectTradeBindingFactory = std::function<ObjectTradeRuntimeBinding(
    World&,RelationshipState&,SessionHostState&,RandomState&,RuntimeGlobals*,const Ts1Clock*,const ObjectTradeResource&)>;
struct ObjectPurchaseRequest {
    std::int16_t new_id=0; // Exact host allocator choice; availability checked.
    ObjectTradeCatalogItem catalog;
    EntityPosition position{};
    // Atomic host allocation in exact group order; empty keeps new_id for one
    // part. Nonempty must cover every part once and start with new_id.
    std::vector<std::int16_t> new_ids;
};
struct ObjectSaleRequest {
    ObjectTradeIdentity target;
    std::vector<ObjectTradeGroup> groups;
    std::vector<ObjectTradeAvatarThread> avatar_threads;
    bool avatar_threads_complete=false,move_objects=false;
    std::optional<ObjectTradeBuildableArea> buildable;
    std::optional<std::uint8_t> selected_disabled;
};
struct ObjectMoveRequest {
    ObjectSaleRequest admission;
    EntityPosition position{};
};
enum class ObjectTradeTransactionStatus {
    rejected_preflight, unsupported, prepared_success, prepared_authored_refusal
};
struct ObjectTradeTransactionResult {
    ObjectTradeTransactionStatus status=ObjectTradeTransactionStatus::unsupported;
    ObjectTradeDecision decision;
    std::string diagnostic;
    // A prepared archive is not a live commit. The host must atomically publish
    // it against its exact journal/source version and rebind runtime resources.
    // A placement refusal may retain Init/EP11/RNG effects and burned identity.
    std::optional<SessionBundle> candidate;
    std::vector<std::uint8_t> archive;
};
// Ordinary physical singleton lifecycle, including staged (unexecuted) Main.
// No children, multipart or architectural portal/window; proven LevelOffset0.
// Sale also supports a leaf in an authenticated reciprocal object container.
// Purchase and movement retain their existing placement/topology admission.
// Deletion publishes World.Dead before both source PrePositionChange passes;
// exact wall-use services suppress EP6 for that represented deleting member.
// This external buy command constructs MainParam/MainStackObject as source zero. Synchronous real
// Init/EP8/EP9/EP11/EP12 are supported through exact resource-owned execution.
// Technical/unsupported failures publish nothing. Authored placement failure
// follows delete+refund, retaining all other represented candidate effects.
// Inputs, borrowed source threads and original commercial resources are read-only.
ObjectTradeTransactionResult prepare_ts1_object_purchase(const SessionBundle&,
    const ObjectPurchaseRequest&,const ObjectTradeResource&,const ObjectTradeBindingFactory&,
    SessionBundleLimits = {},std::uint64_t instruction_budget=10000);
ObjectTradeTransactionResult prepare_ts1_object_sale(const SessionBundle&,
    const ObjectSaleRequest&,const ObjectTradeResource&,const ObjectTradeBindingFactory&,
    SessionBundleLimits = {},std::uint64_t instruction_budget=10000);
// Source UI move/rotation: permission filter, ordinary ChangePosition, then
// EP11 only on placement success. No price deduction or implicit user refund.
ObjectTradeTransactionResult prepare_ts1_object_move(const SessionBundle&,
    const ObjectMoveRequest&,const ObjectTradeResource&,const ObjectTradeBindingFactory&,
    SessionBundleLimits = {},std::uint64_t instruction_budget=10000);

struct ActorlessObjectTradeResult {
    ObjectTradeTransactionStatus status=ObjectTradeTransactionStatus::unsupported;
    ObjectTradeDecision decision;
    std::string diagnostic;
    std::optional<ActorlessObjectTradeState> candidate;
    std::vector<std::uint8_t> archive;
};
// Same source lifecycle and callbacks, with known absence of CurrentFamily.
// No fake resident/HOST or family balance is supplied to the codec.
ActorlessObjectTradeResult prepare_ts1_actorless_object_purchase(const ActorlessObjectTradeState&,
    const ObjectPurchaseRequest&,const ObjectTradeResource&,const ObjectTradeBindingFactory&,
    SessionBundleLimits = actorless_object_trade_limits(),std::uint64_t instruction_budget=10000);
ActorlessObjectTradeResult prepare_ts1_actorless_object_sale(const ActorlessObjectTradeState&,
    const ObjectSaleRequest&,const ObjectTradeResource&,const ObjectTradeBindingFactory&,
    SessionBundleLimits = actorless_object_trade_limits(),std::uint64_t instruction_budget=10000);
ActorlessObjectTradeResult prepare_ts1_actorless_object_move(const ActorlessObjectTradeState&,
    const ObjectMoveRequest&,const ObjectTradeResource&,const ObjectTradeBindingFactory&,
    SessionBundleLimits = actorless_object_trade_limits(),std::uint64_t instruction_budget=10000);
} // namespace odh
