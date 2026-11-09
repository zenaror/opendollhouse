#pragma once
#include "odh/avatar_body.hpp"
#include "odh/avatar_neighbor.hpp"
#include "odh/avatar_startup.hpp"
#include "odh/create_object_instance.hpp"
#include "odh/neighbor_context.hpp"

namespace odh {
enum class NpcAvatarRequestStatus { ready, missing_neighbor, unsupported, duplicate };
struct NpcAvatarRequest {
    CreateObjectInstanceRequest creation;
    std::int16_t caller=0;
    std::uint64_t caller_generation=0;
    EntityPosition position{};
    std::int16_t main_param=0,main_stack_object=0;
    // Owned inheritance-relevant NBRS fields only (version, GUID, ID, mode,
    // person bank). Names/relationships/opaque metadata are not copied.
    // Literal Person GUID constructors may have no neighborhood row. Zero here
    // distinguishes that resource-only path; never manufacture a NeighborID.
    std::uint32_t direct_resource_guid=0;
    // Frozen common Create42 geometry for literal AND UseNeighbor requests. The
    // historical field name does not select the request kind. Absent destination
    // is the real OOW sentinel; absent plan is only Generic19/20 construction.
    std::optional<CreateObjectPlacementPlan> direct_placement;
    std::uint32_t guid() const {return direct_resource_guid?direct_resource_guid:requested_neighbor.guid;}
    // Explicit original fresh constructor (transport or authenticated created
    // character receipt), never inferred from NBRS format or bank width.
    bool original_semantics=false;
    std::optional<bool> original_residential;
    std::optional<std::int16_t> original_definition_version;
    std::function<bool()> original_load_current; // Frontend load only; no fake Caller.
    std::function<bool(WorldEntityReference,Vm&,std::string&)> original_main_prepared;
    std::function<bool(WorldEntityReference,Vm&,CodeOwner,const std::function<bool()>&)> original_guarded_init;
    std::function<bool(WorldEntityReference,Vm&,std::uint64_t,std::string&)> original_initial_tick;
    std::function<bool(WorldEntityReference,std::string&)> original_lookup_flags;
    std::function<bool(WorldEntityReference,std::string&)> original_curves_ready;
    NeighborRecord requested_neighbor{};
    std::optional<NeighborRecord> constructor_neighbor;
};
struct NpcAvatarRequestResult {
    NpcAvatarRequestStatus status=NpcAvatarRequestStatus::unsupported;
    std::optional<NpcAvatarRequest> request;
    std::string diagnostic;
};
// UseNeighbor shares non-container geometry/flags with the literal constructor:
// NoDuplicate, PassObjectIDs, PassTemp0, source-unused8/32. The neighbor
// supplies the effective GUID, independently from any literal operand GUID.
NpcAvatarRequestResult resolve_npc_avatar_request(const CreateObjectInstanceRequest&,
    const VmFrame&,const Ts1Temps&,const NeighborContext&,const World&);

// Literal-GUID Person constructor, with optional existing GUID inheritance.
// Uses common captured Create42 geometry for non-container positions, optional
// NoDuplicate, PassObjectIDs/PassTemp0 and source-unused bits8/32; never invents NBRS data.
NpcAvatarRequestResult resolve_npc_avatar_guid_request(const CreateObjectInstanceRequest&,
    const VmFrame&,const Ts1Temps&,const NeighborContext&,const World&);

// Read-only admission, repeated against the current World immediately BEFORE
// allocating an ID/materializing each queued request. Duplicate is source false,
// unsupported means absence could not be proved. Never call after construction
// (the newly created actor would count itself). No Init, ID, World or RNG effects.
NpcAvatarRequestStatus preflight_npc_avatar_creation(const NpcAvatarRequest&,
    const World&,std::string* diagnostic=nullptr);

// Source Generic19/20: resolve the token's NID to a GUID, then use the
// constructor's GUID-selected NBRS record. No UnderneathMe caller placement.
NpcAvatarRequestResult resolve_npc_avatar_out_of_world_request(std::int16_t neighbor,
    const VmFrame&,const NeighborContext&,const World&);

// Resource-proven OBJD type2 singleton, complete attribute count and entries.
// Creates an avatar (person101, slots3, motives16), never a household member.
// Operates on a PRIVATE candidate: failure/exception can leave partial state;
// caller discards it. Does not run Init, apply body, inherit NBRS or place it.
// Resource-only constructor for fresh restore metadata. No caller, inheritance,
// body selection, execution, placement or RNG; retains constructor person101.
bool materialize_npc_avatar_base(World&,std::int16_t id,
    const ObjectDefinition&,const EntryPointTable&,
    std::optional<std::int16_t> authenticated_initial_value={});
bool materialize_npc_avatar(World& candidate,std::int16_t id,const NpcAvatarRequest&,
    const ObjectDefinition&,const EntryPointTable&,
    std::optional<std::int16_t> authenticated_initial_value={});

struct NpcAvatarBirthTime {
    std::int16_t year=0,month=0,day=0,hour=0,minute=0;
};
struct NpcAvatarBodyInputs {
    std::optional<AvatarBodyStrings> body;
    StringTable global_walk{},global_swim{};
    std::optional<StringTable> object_walk;
    std::optional<NpcAvatarBirthTime> birth_time;
};
enum class NpcPositionAttemptStatus { placed, blocked, unsupported };
// One real SetPosition(Default) attempt. false ignore flag is the initial
// VMContext attempt; true is ONLY the primitive's UnderneathMe retry ignoring
// the Caller's multitile group, not global AllowIntersection. Retry rereads
// current Caller coordinates/level and preserves the captured direction.
// placed must publish exactly the requested position; blocked must leave the
// avatar out of world. Callback owns full placement/entry-point semantics.
using NpcPositionAttempt=std::function<NpcPositionAttemptStatus(World&,std::int16_t,
    const EntityPosition&,std::int16_t caller,bool ignore_caller_group)>;
struct NpcAvatarStartupResult : AvatarStartupResult {
    bool main_started=false,placement_blocked=false;
    std::size_t position_attempts=0;
    std::optional<AvatarBodyDescriptor> body;
    std::vector<NeighborInheritance> inheritance;
};
// Original-profile requests keep the real256-word selective initializer and
// initial Main tick, then use the same captured creation placement lifecycle.
// Pinned-profile requests below retain their existing construction order.
// Private World + private RNG + caller-bound VM/resources, external safe point.
// Init0/EP8 use disposable self/self/stack0 checks (exactly four zeros each,
// including ExecuteEntryPoint's normalization of the source null Init array);
// seed from this new avatar Thread, sharing register effects only during a tick.
// Main is prepared with exactly four words (captured MainParam,0,0,0), including
// normalization of source null when MainParam is zero, but NEVER dispatched. Then body/
// motives, default placement, GUID-selected inheritance, birth time, optional
// Underneath retry, requested-NID inheritance, in pinned7e76 source order.
// NBRS inheritance intentionally changes person101 to the reference's88-word
// runtime bank (no working-tree bridge padding). No scheduler/UI/controller/
// persistence or FAMI authority. On failure discard all candidate state.
NpcAvatarStartupResult initialize_npc_avatar(World& candidate,Vm& candidate_thread,
    std::int16_t id,CodeOwner owner,const NpcAvatarRequest&,const EntryPointTable&,
    const NpcAvatarBodyInputs&,RandomState& candidate_random,
    std::optional<std::uint16_t> current_family,const NpcPositionAttempt&,
    std::uint64_t instruction_budget=10000,const VmPrimitive& synchronous_primitive={},
    bool scheduler_running_now=false,const std::function<bool()>& self_deleted={});
} // namespace odh
