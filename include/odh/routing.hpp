#pragma once
#include "odh/world.hpp"
#include "odh/source_direction.hpp"
#include "odh/room_obstacles.hpp"
#include "odh/route_placement_proof.hpp"
#include "odh/route_execution_proof.hpp"
#include "odh/route.hpp"
#include "odh/route_frame.hpp"
#include "odh/source_walk_plan.hpp"
#include "odh/walk_motion.hpp"
#include "odh/source_route_collision.hpp"
#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace odh {
struct RouteMovement {std::uint64_t tick;std::int16_t entity;EntityPosition position;};
// One ranked-selection attempt, not persistent state or an execution capability.
// Counts refer to calls already made; the observer must not rerun a provider or
// mutate simulation state. Host collectors choose their own bounded retention.
struct RouteGoalSelectionReceipt {
    std::int16_t actor=0,target=0;
    std::uint64_t actor_generation=0,target_generation=0,ticket=0,run_generation=0;
    CodeOwner owner=0;
    RoutineKey routine{};
    std::size_t pc=0,slot_index=0;
    Instruction instruction{};
    EntityPosition actor_position{},target_position{};
    std::string backend;
    bool authenticated=false,circle=false,range=false,relative=false,sparse=false,provider_completed=false;
    std::size_t provider_goals=0,accepted_goals=0,reachable_attempts=0;
    // Reachability attempts count sparse per-goal solver invocations, or one
    // ranked-grid batch solver invocation. They are not successful path counts.
    std::string reachability_backend,outcome,reason;
};
using RouteGoalSelectionObserver=std::function<void(const RouteGoalSelectionReceipt&)>;
// Diagnostic value only: first failure decision is distinct from VM completion.
// Source fields come from the actual pending ticket (including suspended waits)
// or the actual primitive frame for an immediate result, never a replayed query.
enum class RouteOutcomeStage {failure_decision,completion};
struct RouteBlockedMovementReceipt {
    std::uint64_t tick=0;
    EntityPosition candidate{};
    std::uint8_t placement_status=0;
    std::optional<std::int16_t> placement_error;
    WorldEntityReference blocker;
    std::int32_t retries_before=0,retries_after=0;
    std::optional<SourceWalkPlanState> replan;
    SourceRouteCollisionAction action=SourceRouteCollisionAction::unsupported;
};

struct RouteOutcomeReceipt {
    RouteOutcomeStage stage=RouteOutcomeStage::completion;
    std::uint64_t ticket=0,run_generation=0;
    WorldEntityReference actor,target,blocker;
    bool result=false,completion_attempted=false,completion_accepted=false,blocker_generation_is_current_lookup=false;
    bool source_available=false,source_is_pending=false;
    CodeOwner owner=0;RoutineKey routine{};std::size_t pc=0,source_depth=0;
    Instruction instruction{};
    std::optional<EntityPosition> actor_position,target_position,selected_goal;
    std::optional<std::int16_t> actor_object_room,target_object_room,failure_code;
    std::optional<std::uint16_t> sparse_room;
    std::string selected_goal_origin,reason;
    std::size_t nested_depth=0,portal_steps=0,portal_retries=0,path_points=0,next_point=0;
    RouteFrameState frame_state=RouteFrameState::initial;
    bool failure_tree_invoked=false,restored_job=false;
    std::uint64_t blocked_movement_count=0;
    std::optional<RouteBlockedMovementReceipt> first_blocked_movement,last_blocked_movement;
};
using RouteOutcomeObserver=std::function<void(const RouteOutcomeReceipt&)>;


// Transient standing SLOT built by the pinned VMGotoRelativePosition for
// any of the eight relative sides, or AnywhereNear. No object/resource IDs.
bool is_source_relative_range_slot(const SlotRecord&);
// Bounded source circle branch, not the legacy ODH cardinal-first helper.
// Nonempty directional mask, positive equal radius, standing only, FaceTowards.
// Ignored SLOT metadata is preserved by the route manifest rather than guessed.
bool is_source_slot_circle(const SlotRecord&);
struct SourceSlotCircleGoal {
    RoutePoint position{};
    std::uint8_t entry_flags=0,facing=0;
    float radian_direction=0;
};
struct SourceSlotCircleRouteSnapshot {
    std::uint32_t slot_version=0;
    std::size_t slot_index=0;
    SlotRecord slot{};
    float target_radian_direction=0;
    SourceSlotCircleGoal selected{};
    std::vector<SourceSlotCircleGoal> fallback_goals;
};
// Generic standing/range routes have a distinct manifest; circle/point saves
// retain their existing strict resource and geometry profiles.
struct SourceSlotRangeRouteSnapshot {
    std::uint32_t slot_version=0;
    std::size_t slot_index=0;
    SlotRecord slot{};
    float target_radian_direction=0;
    SourceSlotCircleGoal selected{};
    std::vector<SourceSlotCircleGoal> fallback_goals;
};
// Source CurRoute/Choices independent of AttemptWalk. A chair runs EP26
// immediately after room routing, before any motion towards its coordinates.
struct SourceSlotChoiceGoal {
    RoutePoint position{};
    std::uint8_t entry_flags=0,facing=0;
    float radian_direction=0;
    bool face_anywhere=false;
    std::optional<LiveAttemptWalkIdentity> chair;
    std::int16_t level=0;
};
struct SourceSlotChoicesSnapshot {
    std::uint32_t slot_version=0;
    std::size_t slot_index=0;
    SlotRecord slot{};
    float target_radian_direction=0;
    SourceSlotChoiceGoal selected;
    std::vector<SourceSlotChoiceGoal> remaining;
    bool chair_attempted=false;
};
// Compatible zero-offset standing ranges share authenticated placement for
// FaceTowards and FaceAnywhere; FaceAnywhere freezes neutral radians/facing.
bool is_source_slot_range(const SlotRecord&);
bool is_source_standing_range_slot(const SlotRecord&);
std::vector<SourceSlotCircleGoal> enumerate_source_slot_range_candidates(
    const SlotTable&,std::size_t,const EntityPosition& target,float target_radians);
// A source LookTowards routing frame has no moving destination. The result of
// its original StackObject/group lookup is frozen before the first turn tick.
struct SourceLookTowardsRouteSnapshot {
    std::uint8_t mode=2;
    float start_radian_direction=0,target_radian_direction=0;
    bool in_pool=false;
    std::int16_t walk_style=0;
    std::uint8_t animation_index=0;
    float animation_speed=1;
    std::uint32_t animation_num_frames=0;
    float turn_tweak=0;
};
// Value-only continuation for the sparse source AttemptWalk path. Host
// callbacks, RoomInfo pointers and collision leases are deliberately excluded.
enum class SourceWalkToPurpose : std::uint8_t { ranked_alternative=0,shoo=1,architecture_invalidation=2 };
enum class RouteExternalFramePurpose : std::uint8_t { portal=0,sitting=1,failure_tree=2,shoo=3,standing=4 };
// Native ordinary GoHere geometry: source OnTopOf/FaceAnywhere. Ignored
// flags/counters/tail never confer target authority. Callers must separately
// prove the actual target is ordinary (not a historical UI-only ghost marker).
bool is_source_ordinary_go_here_instruction(const Instruction&) noexcept;
// Defined source opcode27 locations/directions: -2, -1, 0..7. Other enum
// values remain explicitly unsupported. Execute ignores both counters, the
// eighth operand byte and all flag bits except NoFailureTrees (2).
struct SourceGotoRelativePositionRequest {
    std::int16_t location=0,direction=0;
    bool no_failure_trees=false;
};
std::optional<SourceGotoRelativePositionRequest> decode_source_goto_relative_position(const Instruction&) noexcept;
bool is_source_exact_goto_relative_instruction(const Instruction&) noexcept;
// Transient ranged SLOT: eight sectors or AnywhereNear, same shared decoder.
bool is_source_ranged_goto_relative_instruction(const Instruction&) noexcept;
// Exact transient SLOT defaults from VMGotoRelativePosition.Execute; index0.
// Includes admitted OnTopOf/ordinary GoHere forms and ranged sector/near forms.
bool is_source_goto_relative_slot_instruction(const Instruction&) noexcept;
std::optional<SlotTable> make_source_goto_relative_slot(const Instruction&);
// VMSlotParser.VerifyAndAddLocation facing, including authored fixed0..7.
float source_slot_facing_radians(const SlotRecord&,const EntityPosition& target,RoutePoint candidate);
struct SourceSparseRouteSnapshot {
    std::uint16_t room=0;
    std::vector<LiveAttemptWalkIdentity> avatars_to_consider;
    WalkMotionSnapshot motion{};
    WalkMotionProfile profile{};
    float target_radian_direction=0;
    bool face_anywhere=false,hard_start=true,locomotion_started=false;
    std::uint8_t lifecycle=0; // 0 walking, 1 begin turn, 2 end turn
    std::optional<SourceLookTowardsRouteSnapshot> turn;
    // Frozen ranked standing SLOT row and ordered choices for profiles that
    // do not have a more specific point/range/circle manifest. This value
    // shape is reused for generic ranking; it does not assert range semantics.
    std::optional<SourceSlotRangeRouteSnapshot> choices;
    std::optional<SourceRouteCollisionSnapshot> collision;
    // Source WalkTo is null after AttemptRoute; CurrentPath/motion remains
    // historical until a later WALKING AttemptWalk installs its replacement.
    bool walk_to_pending=false;
    std::vector<RoomPortalEdge> fallback_room_route;
    SourceWalkToPurpose walk_to_purpose=SourceWalkToPurpose::ranked_alternative;
};
struct SourceLookTowardsProfile {std::int16_t walk_style=0;bool in_pool=false;};
struct SourceLookTowardsGroupMember {
    std::int16_t id=0;std::uint64_t generation=0;EntityPosition position{};
};
// The group resolver must return the complete authoritative group, including
// the StackObject. A missing resolver differs from a proven single-part group.
using SourceLookTowardsGroupResolver=std::function<std::optional<std::vector<SourceLookTowardsGroupMember>>(
    const VmFrame&)>;
struct SourceLookTowardsServices {
    std::function<std::optional<SourceLookTowardsProfile>(const VmFrame&)> profile;
    std::function<std::optional<std::uint32_t>(const VmFrame&,std::uint64_t ticket,
        std::uint8_t animation_index,bool in_pool,float speed)> start;
    // Reads the shared current primary state, including source-allowed
    // replacement by another frame; it must not require the old clip to survive.
    std::function<std::optional<bool>(const VmFrame&,std::uint64_t ticket)> ended;
    std::function<bool(const VmFrame&,std::uint64_t ticket,bool in_pool)> reset;
};
std::vector<SourceSlotCircleGoal> enumerate_source_slot_circle_candidates(
    const SlotTable&,std::size_t,const EntityPosition& target,float target_radians);
// Value-only continuation for one verified local Route45/27 walk. Sparse
// source motion is independent of the resource-specific SLOT manifest and
// deliberately omits callbacks, portal state, sitting fallbacks and pointers.
// Legacy grid paths still revalidate against the prepared grid; sparse motion
// restores its bounded source geometry without consulting that grid.
struct RouteCarriedTargetLink {
    LiveAttemptWalkIdentity child{},parent{};
    std::size_t slot=0;
    EntityPosition original{};
    float radian_direction=0;
    bool positioned=false;
};
// Frozen request ancestry, target first and actor last as the final parent.
// Geometry remains anchored at target_position; this proves live continuation.
bool route_carried_target_matches(const World&,LiveAttemptWalkIdentity actor,LiveAttemptWalkIdentity target,
    const std::vector<RouteCarriedTargetLink>&,const EntityPosition& anchor,bool allow_original=true);
struct LocalRouteSnapshot {
    std::uint64_t ticket=0,actor_generation=0,target_generation=0,run_generation=0;
    std::int16_t actor=0,target=0;
    EntityPosition expected{},target_position{};
    float expected_radian_direction=0;
    RouteFrameContext frame{};
    std::vector<RoutePoint> path;
    std::size_t next=0;
    std::int16_t entry_flags=0;
    std::uint8_t facing=0;
    bool face_anywhere=false,walking_started=false,call_failure_trees=false;
    bool same_room_verified=false;
    // Source exact-point SLOTs retain their continuous arrival facing. Presence
    // also requires the host's exact SLOT resource to be re-evaluated on load.
    std::optional<float> arrival_radian_direction=std::nullopt;
    // Captured source-point target transform (also required for self targets).
    // target_position is the authored anchor, not the live target progress.
    std::optional<float> target_anchor_radian_direction=std::nullopt;
    std::optional<SourceSlotCircleRouteSnapshot> source_circle=std::nullopt;
    std::optional<SourceLookTowardsRouteSnapshot> look_towards=std::nullopt;
    std::optional<SourceSlotRangeRouteSnapshot> source_range=std::nullopt;
    std::optional<SourceSparseRouteSnapshot> source_sparse=std::nullopt;
    std::optional<SourceSlotChoicesSnapshot> source_choices=std::nullopt;
    bool room_route_invalid=false; // Source VMRoutingFrame.RoomRouteInvalid, absent on old wire.
    bool room_route_replan_active=false; // Full-stack phase; local capture is forbidden.
    std::vector<RouteCarriedTargetLink> carried_target;
};
// Structural phase only; source/World/VM authority is validated by consumers.
bool is_source_standing_before_walk(const LocalRouteSnapshot&) noexcept;
// Captured source choices remain authored coordinates when their exact target moves.
// This classifies existing source profiles; it does not authorize participant identities.
bool route_has_captured_source_goals(const LocalRouteSnapshot&) noexcept;

// Enumerates the bounded offset lattice used by the pinned FreeSO
// VMSlotParser for ranged type-3 SLOTs. This is only candidate generation:
// it does not filter by distance, room, direction, floor, walls, occupancy,
// scoring, or establish original The Sims 1 semantics.
std::vector<RoutePoint> enumerate_reference_slot_range_lattice(
    const SlotTable&,std::size_t type3_index,std::size_t candidate_limit=4096);
// Applies only the pinned FreeSO proximity band (including its +/-0.5
// tolerance and FSOSquare distance mode) to that lattice. Room, direction,
// geometry, occupancy, score/RNG and original-game semantics remain unknown.
std::vector<RoutePoint> filter_reference_slot_range_proximity(
    const SlotTable&,std::size_t type3_index,const std::vector<RoutePoint>& offsets);
// Reproduces the pinned VMSlotParser.GetSearchDirection floor-tile angle
// sectors, then returns the SLOT direction bits accepted for this candidate.
// Positions use the ODH 1/16-tile lattice; object_direction is 0..7.
std::uint8_t reference_slot_entry_flags(
    RoutePoint object_position,std::uint8_t object_direction,RoutePoint candidate_position,
    std::uint8_t slot_direction_flags,std::optional<float> object_radians=std::nullopt);
struct ReferenceSlotCircleCandidate { RoutePoint position; std::uint8_t entry_flags; };
struct ReferenceSlotPointCandidate { RoutePoint position; std::uint8_t facing; };
// Exact-position support is restricted to the four verified House07
// Medicine Cabinet SLOT records; offsets are rotated in fixed point.
ReferenceSlotPointCandidate enumerate_reference_slot_point_candidate(
    const SlotTable&,std::size_t type3_index,RoutePoint object_position,std::uint8_t object_direction);
struct SourceSlotPointCandidate {
    EntityPosition position{};
    std::uint8_t facing=0;
    float radian_direction=0;
    bool face_anywhere=false;
};
// Pinned VMSlotParser exact-position branch: v10, flags0, no sitting/snap,
// zero Z offset. Finite XY offsets rotate by continuous object direction;
// source float center arithmetic precedes nearest-even coordinate rounding.
// Weights/proximity do not exclude the one candidate. This computes geometry;
// the host still validates bounds, wall raycast and PositionValid semantics.
SourceSlotPointCandidate enumerate_source_slot_point_candidate(
    const SlotTable&,std::size_t type3_index,const EntityPosition& object_position,
    float object_radian_direction);
// ODH's bounded exact-radius profiles for verified House07 and Door SLOTs.
// Eight positions are returned, including those rejected by the SLOT mask.
std::vector<ReferenceSlotCircleCandidate> enumerate_reference_slot_circle_candidates(
    const SlotTable&,std::size_t type3_index,RoutePoint object_position,std::uint8_t object_direction);
// Input order is already ranked by the caller, including stable score ties.
// This routing consumer does not compute score/RNG. The separate explicit
// score_reference_slot_candidates adapter can supply this ranked output.
struct PreRankedSlotGoal {
    RoutePoint position;
    std::uint8_t entry_flags;
    std::uint8_t facing;
    // Set only when this goal is occupied by a validated sitting target whose
    // EntryPoint26 should run after the actor reaches the destination.
    std::optional<std::int16_t> sitting_object{};
    // A clear grid point the actor can reach before EntryPoint26 takes over.
    // The SLOT position remains in `position` for scoring and seat identity.
    std::optional<RoutePoint> sitting_approach{};
    std::optional<float> radian_direction{}; // Source circle's continuous arrival.
};
// Value-only scheduler continuation, ordered from the oldest suspended parent
// to the active child. VM frames remain owned by VmSnapshot; the frozen copies
// certify exact pending-frame linkage on restore, without executing a BHAV.
struct RouteJobSnapshot : LocalRouteSnapshot {
    VmPending pending{};
    VmFrame route_frame{};
    bool external_frame_active=false;
    CodeOwner external_frame_owner=0;
    std::optional<VmCall> external_frame_call;
    std::size_t portal_retries=0;
    bool portal_replan_required=false,deferred_room_route=false;
    std::optional<VmFrame> replay_frame;
    std::optional<Instruction> replay_instruction;
    std::size_t portal_steps=0;
    bool room_context_open=false,failure_tree_invoked=false,failure_pending=false,interrupt_pending=false;
    std::int16_t failure_code=3,failure_blocker=0;
    std::string failure_reason;
    std::optional<std::int16_t> selected_sitting_object;
    std::vector<PreRankedSlotGoal> fallback_goals;
    bool external_frame_sitting=false;
    std::optional<std::vector<LiveAttemptWalkIdentity>> sparse_consideration;
    // Absent preserves the original portal/sitting/failure-tree wire profile.
    std::optional<RouteExternalFramePurpose> external_frame_purpose;
    std::uint64_t external_frame_callee_generation=0;
};
// Host-owned room graph state. Core capture leaves this list empty; the host
// fills and authenticates it before writing a session. Core restore validates
// its ticket/call linkage and never performs graph discovery or EP5 calls.
// An exhausted route may retain its rejected CurrentPath while its exact
// source failure tree398 runs. This proof does not authorize resumed walking.
bool is_source_failure_tree_continuation(const RouteJobSnapshot&) noexcept;
struct RoutePortalContextSnapshot {
    std::uint64_t ticket=0;
    std::vector<IgnoredRoomPortal> ignored;
    std::optional<RoomPortalEdge> pending_edge;
    std::optional<VmCall> pending_call;
    std::uint64_t portal_generation=0;
};
struct RoutePortalOwnerSnapshot {
    std::string source,asset;
    std::uint16_t action=0;
    CodeOwner owner=0;
};
struct RouteStackSnapshot {
    std::vector<RouteJobSnapshot> jobs;
    std::optional<std::uint64_t> last_tick;
    std::vector<RoutePortalContextSnapshot> portal_contexts;
    std::vector<RoutePortalOwnerSnapshot> portal_owners;
    std::uint64_t portal_planned_calls=0,portal_returns=0,portal_transitions=0;
};
// Value-only source WallsChanged event; preserves all jobs and their history.
void invalidate_session_room_routes(RouteStackSnapshot&) noexcept;
// Explicit old-wire upgrade against the already normalized owning VM.
// Modern contexts/flags are validated later and never overwritten here.
bool bind_legacy_route_references(RouteStackSnapshot&,const World&,const VmSnapshot&);

struct AuthenticatedRangeSlotGoals {
    std::vector<PreRankedSlotGoal> goals;
    std::int16_t failure_code=7,blocker=0;
    std::uint64_t blocker_generation=0;
    // Present for the full source parser: preserves chair epochs and exact
    // source FaceAnywhere independently of the legacy grid-goal DTO.
    std::vector<SourceSlotChoiceGoal> semantic_goals;
};
using AuthenticatedRangeSlotGoalProvider=std::function<AuthenticatedRangeSlotGoals(
    const SlotTable&,std::size_t,const EntityPosition& caller,const EntityPosition& target,
    float target_radians,const RoutePlacementContext&)>;
struct PreRankedSlotRoute {
    RoutePath path;
    std::optional<std::size_t> selected;
};
// Try one goal at a time; only definitive no_path advances. Expanded work is
// cumulative; frontier is a per-attempt live-memory ceiling. Invalid metadata
// or a goal outside the supplied grid stops with invalid_input.
PreRankedSlotRoute find_pre_ranked_slot_route(const RouteGrid&,RoutePoint start,
    const std::vector<PreRankedSlotGoal>&,RouteLimits limits={});
// Supplies already eligible destinations for the supported SLOT profile.
// Called synchronously; reentry or changes to actor/target are rejected.
using PreRankedSlotGoalProvider=std::function<std::vector<PreRankedSlotGoal>(
    const SlotTable&,std::size_t,const EntityPosition&,const EntityPosition&)>;
using ContinuousPreRankedSlotGoalProvider=std::function<std::vector<PreRankedSlotGoal>(
    const SlotTable&,std::size_t,const EntityPosition&,const EntityPosition&,std::optional<float>)>;
// A sitting-weighted SLOT can use its standing alternative only after this
// host proves that no nearby eligible sitting object exists. A diagnostic
// means the route must remain unsupported; it must not become a false result.
using SittingSlotCandidatePreflight=std::function<std::optional<std::string>(
    const SlotTable&,std::size_t,const EntityPosition&,const EntityPosition&)>;
// Resolves a StackObject's authored SLOT table for Route45 Literal scope.
// Returned tables must outlive the Routing controller and remain immutable.
using StackObjectSlotTableProvider=std::function<const SlotTable*(const VmFrame&)>;
// Read-only host filter for candidates constrained to the active route room.
using RouteGoalValidator=std::function<bool(const VmFrame&,RoutePoint)>;
// Narrow host opt-in for the saved House03 stair BHAV 4112 failure-tree profile.
using StairGotoRelativeValidator=std::function<bool(const VmFrame&,const Instruction&)>;
// Rebuilds the active room grid after an external portal frame moved the avatar.
using RouteGridRefresh=std::function<bool(const EntityPosition&)>;
// Recomputes the next Portal15 call after an external portal frame returns
// false. The provider owns room-graph policy and must exclude the failed edge;
// Routing bounds retries and still validates every VM ticket and CodeOwner.
using RoutePortalRetryProvider=std::function<std::optional<VmCall>(
    const VmCall& failed_call,const EntityPosition& actor,const EntityPosition& target,
    std::size_t retry_index)>;
// unavailable means a complete room graph has no route. A missing service,
// invalid participant or over-budget graph cannot decide that gameplay result.
enum class RoutePortalStepStatus : std::uint8_t { call_ready,destination_reached,unavailable,unsupported };
struct RoutePortalStepPlan {
    RoutePortalStepStatus status=RoutePortalStepStatus::unsupported;
    std::optional<VmCall> call;
    std::string diagnostic;
};
using RouteRoomPairProvider=std::function<std::optional<std::pair<std::uint16_t,std::uint16_t>>(
    const EntityPosition& actor,const EntityPosition& target)>;
// Called at a Route45 scheduler safe point. `previous_call` and
// `previous_result` describe the last completed Portal15 frame; both are empty
// for the initial step. The provider owns graph/ignored-edge policy.
using RoutePortalStepProvider=std::function<RoutePortalStepPlan(
    std::uint64_t root_ticket,const EntityPosition& actor,const EntityPosition& target,
    const std::optional<VmCall>& previous_call,const std::optional<bool>& previous_result,
    std::size_t step_index)>;
using RoutePortalContextEndProvider=std::function<void(std::uint64_t root_ticket)>;
using RouteSittingActionProvider=std::function<std::optional<VmCall>(
    std::int16_t actor_id,std::int16_t chair_id)>;
// Narrow authenticated source execution seam. The callback runs only while
// Routing owns an exact AttemptWalk or set-position execution lease.
// Exceptions remain unsupported and never become authored route failures.
using RouteExecutionCallback=std::function<void(const RouteExecutionContext&)>;
struct SourceSparsePlanInput {
    SourceWalkPlanRequest request{};
    SourceWalkPlanServices services{};
    WalkMotionProfile profile{};
    std::vector<LiveAttemptWalkIdentity> avatars_to_consider;
};
using SourceSparsePlanInputProvider=std::function<std::optional<SourceSparsePlanInput>(
    const RouteExecutionContext&,RoutePoint,std::optional<float>,
    std::optional<std::vector<LiveAttemptWalkIdentity>>) >;
enum class SourceSparsePlacementStatus : std::uint8_t {
    success,cant_be_through_wall,blocked_object,blocked_avatar,unsupported
};
struct SourceSparsePlacementOutcome {
    SourceSparsePlacementStatus status=SourceSparsePlacementStatus::unsupported;
    std::optional<std::int16_t> blocker;
    std::uint64_t blocker_generation=0;
    std::string diagnostic;
    std::optional<std::int16_t> placement_error;
};
struct RoutePositionPublicationContext {
    RouteExecutionContext execution;
    EntityPosition old_position{},new_position{};
    // True when Routing already owns a source PositionValid callback. The
    // provider must call it exactly once AFTER PrePositionChange, rather than
    // repeat its checks/entry-point effects outside this lifecycle.
    bool has_placement_check=false;
    std::function<std::optional<SourceSparsePlacementOutcome>()> check_position;
    // Fixed destination, root-only SetIndivPosition. The provider owns ordered
    // child refresh and PositionChange after this exact publication.
    std::function<bool()> publish_position;
    // Callback-scoped real pending route/job/ticket and actor/target identity.
    // Supports PositionValid's temporary NORTH pose and the published phase.
    // A target already contained below the actor may follow only the captured
    // reciprocal chain to the fixed published XYZ, keeping its own heading and
    // continuous angle. Other targets remain at their captured exact pose.
    std::function<bool()> source_still_matches;
};
using RoutePositionPublicationProvider=std::function<std::optional<SourceSparsePlacementOutcome>(
    const RoutePositionPublicationContext&)>;
using SourceSparsePlacementProvider=std::function<std::optional<SourceSparsePlacementOutcome>(
    const RouteExecutionContext&,const EntityPosition&)>;
struct SourceSparseMotionContext {
    const Vm* vm=nullptr;
    VmFrame frame{};
    VmDispatch dispatch{};
    std::int16_t actor=0;
    std::uint64_t actor_generation=0,run_generation=0;
    EntityPosition position{};
    bool root_route=true;
    bool room_context_open=false;
};
using SourceSparseAnimationProvider=std::function<bool(const SourceSparseMotionContext&,
    const WalkMotionProfile&,std::string*)>;
using SourceSparseMotionPublisher=std::function<bool(const SourceSparseMotionContext&,
    const WalkMotionEffects&,const std::array<float,3>&,std::string*)>;
struct SourceSparseCollisionAvatarState {
    bool top_route=false;
    std::int32_t wait_time=0;
    std::optional<bool> can_shoo;
};
struct SourceRoomRoutePlan {
    bool found=false;
    // Front is the next source Rooms.Pop; this is pure graph state.
    std::vector<RoomPortalEdge> portals;
};
struct SourceSparseServices {
    std::function<std::optional<std::vector<LiveAttemptWalkIdentity>>(
        const RouteExecutionContext&,std::optional<std::vector<LiveAttemptWalkIdentity>>)> capture_avatars_to_consider;
    SourceSparsePlanInputProvider plan_input;
    SourceSparsePlacementProvider place;
    SourceSparseAnimationProvider begin_locomotion;
    SourceSparseAnimationProvider end_locomotion;
    SourceSparseMotionPublisher publish_motion;
    std::function<bool(const SourceSparseMotionContext&)> source_tick;
    std::function<bool(const SourceSparseMotionContext&,const std::array<float,3>&,
        std::string*)> publish_velocity;
    std::function<std::optional<SourceSparseCollisionAvatarState>(const SourceSparseMotionContext&,
        LiveAttemptWalkIdentity)> avatar_collision_state;
    std::function<bool(const SourceSparseMotionContext&,LiveAttemptWalkIdentity,bool direct_route,
        std::string*)> shoo;
    std::function<std::optional<SourceRoomRoutePlan>(const SourceSparseMotionContext&,
        const EntityPosition& target)> room_route;
    // Read the current room's pool/family and current WalkStyle without planning.
    std::function<std::optional<WalkMotionProfile>(const SourceSparseMotionContext&)> locomotion_profile;
};
// Runtime-only one-use authority. A token cannot be serialized or transferred
// to a different Routing instance, VM run, route revision or participant epoch.
class PreparedSourceRouteShoo {
public:
    PreparedSourceRouteShoo()=default;
private:
    struct State;
    std::shared_ptr<State> state_;
    friend class Routing;
};
// Controlled async route adapter: profiled Route45 SLOTs and source opcode27
// point geometry use the legacy grid path unless sparse source services are
// installed. Sparse services provide complete-room AttemptWalk planning and
// source motion for exact point and ranked range goals; they do not imply that
// unsupported collision outcomes are authored route failures.
class Routing {
public:
    Routing(World& world,RouteGrid& grid,const SlotTable& globals,
            PreRankedSlotGoalProvider goals={},RouteLimits limits={})
        :world_(world),grid_(grid),globals_(globals),ranked_goals_(std::move(goals)),limits_(limits) {}
    Routing(const Routing&)=delete;
    Routing& operator=(const Routing&)=delete;
    bool bind(Vm&);
    void set_outcome_observer(RouteOutcomeObserver observer){
        if(executing_||job_)throw std::logic_error("cannot replace route outcome observer while active");
        outcome_observer_=std::move(observer);
    }
    void set_goal_selection_observer(RouteGoalSelectionObserver observer) {
        if(executing_||job_)throw std::logic_error("cannot replace route goal observer while active");
        goal_selection_observer_=std::move(observer);
    }
    void set_look_towards_services(SourceLookTowardsServices services) {
        if(executing_||job_)throw std::logic_error("cannot replace LookTowards services while active");
        look_towards_services_=std::move(services);
    }
    void set_look_towards_group_resolver(SourceLookTowardsGroupResolver resolver) {
        if(executing_||job_)throw std::logic_error("cannot replace LookTowards group resolver while active");
        look_towards_group_=std::move(resolver);
    }
    // Stop an active, local Go Here walk through its saved failure tree.
    // If a route-owned child frame is active, apply the request at its next return.
    bool interrupt();
    // Install before VM execution. Missing/ambiguous tables remain unsupported.
    void set_stack_object_slot_table_provider(StackObjectSlotTableProvider provider) {
        if(executing_||job_)throw std::logic_error("cannot replace Route45 SLOT provider while active");
        stack_object_slots_=std::move(provider);
    }
    void set_route_goal_validator(RouteGoalValidator validator) {
        if(executing_||job_)throw std::logic_error("cannot replace Route45 goal validator while active");
        goal_validator_=std::move(validator);
    }
    void set_stair_goto_relative_validator(StairGotoRelativeValidator validator) {
        if(executing_||job_)throw std::logic_error("cannot replace stair Route27 validator while active");
        stair_goto_relative_=std::move(validator);
    }
    void set_route_grid_refresh(RouteGridRefresh refresh) {
        if(executing_||job_)throw std::logic_error("cannot replace Route45 grid refresher while active");
        route_grid_refresh_=std::move(refresh);
    }
    void set_portal_retry_provider(RoutePortalRetryProvider provider) {
        if(executing_||job_)throw std::logic_error("cannot replace Route45 portal retry provider while active");
        portal_retry_=std::move(provider);
    }
    void set_room_pair_provider(RouteRoomPairProvider provider) {
        if(executing_||job_)throw std::logic_error("cannot replace Route45 room resolver while active");
        room_pair_=std::move(provider);
    }
    void set_room_portal_step_provider(RoutePortalStepProvider provider) {
        if(executing_||job_)throw std::logic_error("cannot replace Route45 room portal planner while active");
        room_portal_step_=std::move(provider);
    }
    void set_room_portal_context_end_provider(RoutePortalContextEndProvider provider) {
        if(executing_||job_)throw std::logic_error("cannot replace Route45 room context cleanup while active");
        room_portal_context_end_=std::move(provider);
    }
    // Source STAND_FUNC runs actual container EP27 before AttemptWalk.
    void set_standing_action_provider(RouteSittingActionProvider provider) {
        if(executing_||job_)throw std::logic_error("cannot replace stand provider while routing");
        standing_action_=std::move(provider);
    }
    void set_sitting_action_provider(RouteSittingActionProvider provider) {
        if(executing_||job_)throw std::logic_error("cannot replace Route45 sitting action provider while active");
        sitting_action_=std::move(provider);
    }
    void set_route_execution_callback(RouteExecutionPhase phase,RouteExecutionCallback callback) {
        if(executing_||job_)throw std::logic_error("cannot replace route execution callback while active");
        if(phase==RouteExecutionPhase::attempt_walk)attempt_walk_execution_=std::move(callback);
        else if(phase==RouteExecutionPhase::set_position)set_position_execution_=std::move(callback);
        else throw std::invalid_argument("unknown source route execution phase");
    }
    void set_source_sparse_services(SourceSparseServices services) {
        if(executing_||job_)throw std::logic_error("cannot replace sparse route services while active");
        source_sparse_services_=std::move(services);
    }
    // Provider for bounded, source-verified ranged SLOT profiles.
    void set_ranged_slot_goal_provider(PreRankedSlotGoalProvider provider) {
        if(executing_||job_)throw std::logic_error("cannot replace ranged SLOT provider while active");
        ranged_slot_goals_=std::move(provider);
        continuous_ranged_slot_goals_={};
    }
    // Relative sectors use the target's actual angle, including between notches.
    // Other existing SLOT profiles receive nullopt and retain their old policy.
    void set_continuous_ranged_slot_goal_provider(ContinuousPreRankedSlotGoalProvider provider) {
        if(executing_||job_)throw std::logic_error("cannot replace ranged SLOT provider while active");
        continuous_ranged_slot_goals_=std::move(provider);
        ranged_slot_goals_={};
    }
    void set_authenticated_range_slot_goal_provider(AuthenticatedRangeSlotGoalProvider provider) {
        if(executing_||job_)throw std::logic_error("cannot replace authenticated range provider while active");
        authenticated_range_goals_=std::move(provider);
    }
    void set_authenticated_slot_goal_provider(AuthenticatedRangeSlotGoalProvider provider) {
        if(executing_)throw std::logic_error("route goal provider cannot change during execution");
        authenticated_range_goals_=std::move(provider);general_slot_provider_=bool(authenticated_range_goals_);
    }
    void set_sitting_slot_candidate_preflight(SittingSlotCandidatePreflight preflight) {
        if(executing_||job_)throw std::logic_error("cannot replace sitting SLOT preflight while active");
        sitting_slot_candidate_preflight_=std::move(preflight);
    }
    VmPrimitive primitive();
    // Schedule a host-resolved portal/route BHAV while Route45 is waiting.
    // The host must supply the correct routine, code owner and VM context;
    // this controller does not discover portals or resolve EntryPoint tables.
    bool call_external_frame(const VmCall&);
    // Host admission must first prove source CanShooAvatar's exact queue
    // predicate. Preparation also checks the oldest owned route's SHOOED state.
    std::optional<PreparedSourceRouteShoo> prepare_source_shoo(LiveAttemptWalkIdentity shooer,
        std::string* rejection_reason=nullptr) const;
    bool can_commit_source_shoo(const PreparedSourceRouteShoo&,const VmCall&,
        std::string* rejection_reason=nullptr) const;
    bool commit_source_shoo(PreparedSourceRouteShoo&,const VmCall&,
        std::string* rejection_reason=nullptr);

    bool tick(std::uint64_t tick,const std::function<void(const RouteMovement&)>& moved={});
    bool invalidate_room_routes();
    void set_room_route_invalidation_provider(std::function<bool(std::uint64_t)> provider) {
        if(executing_||job_)throw std::logic_error("cannot replace room invalidation provider while active");
        room_route_invalidation_=std::move(provider);
    }
    void set_position_publication_provider(RoutePositionPublicationProvider provider){
        if(executing_||job_)throw std::logic_error("cannot replace route position publication provider while active");
        position_publication_=std::move(provider);
    }
    // Releases actual room contexts/footprints without completing VM tickets.
    // False can follow partial host effects: recovery must reject that candidate,
    // never resume/save it or retry as if the operation were rolled back.
    bool discard_for_source_reset();
    std::vector<WorldEntityReference> strong_entity_references() const;
    bool active() const {return job_.has_value();}
    // Original House layout counts any retained route record, including covered
    // and immediate continuations, irrespective of its current movement phase.
    bool has_route_record() const noexcept {return job_.has_value()||!suspended_jobs_.empty()||!parked_immediate_routes_.empty();}
    // The host may discard only an idle derived cache, never a controller
    // currently executing a callback or retaining a covered/placement job.
    // VM, portal and UI ownership must also be checked by the host.
    bool can_release_idle_cache() const noexcept {
        return !executing_&&!job_&&suspended_jobs_.empty()&&parked_immediate_routes_.empty()&&
            active_range_lease_.expired()&&!replay_placement_authorization_;
    }
    // Source immediate actions cover a routing frame without executing it.
    static bool validates_immediate_parent(const World&,const Vm&,const VmPending&,const VmFrame&);
    bool validates_immediate_parent(const VmPending&,const VmFrame&) const;
    bool can_park_for_immediate(const VmPending&,const VmFrame&) const;
    bool park_for_immediate(std::uint64_t parent_ticket);
    bool retry_immediate_parent(std::uint64_t parent_ticket);
    bool parked_for_immediate() const;

    std::size_t active_route_depth() const noexcept {return job_?suspended_jobs_.size()+1:0;}
    // Pinned IntersectsOurDestination: inclusive +/-6 around every current or
    // suspended semantic goal, deliberately without a floor filter. A pending
    // SLOT room route has no CurRoute until its first destination is selected.
    // Candidate chair identity matches before the positional test, as source.
    // Unknown/unproven state remains nullopt with an optional diagnostic.
    std::optional<bool> intersects_destination(RoutePoint,
        std::optional<std::int16_t> candidate_chair={},std::string* diagnostic=nullptr) const;
    std::optional<LocalRouteSnapshot> capture_local_session_route(
        std::optional<CodeOwner> verified_dynamic_owner={},
        std::string* rejection_reason=nullptr) const;
    bool restore_local_session_route(const LocalRouteSnapshot&,
        std::optional<CodeOwner> verified_dynamic_owner={});
    std::optional<RouteStackSnapshot> capture_session_route_stack(
        std::optional<CodeOwner> verified_dynamic_owner={},std::string* rejection_reason=nullptr) const;
    // The host must restore/authenticate portal_contexts before calling this.
    // Neither capture nor restore invokes any host callback or mutates the VM.
    bool restore_session_route_stack(const RouteStackSnapshot&,
        std::optional<CodeOwner> verified_dynamic_owner={},std::string* rejection_reason=nullptr);
    bool external_frame_active() const {return job_&&job_->external_frame_active;}
    // The selected chair remains attached to this Route45 ticket until it ends.
    std::optional<std::int16_t> active_sitting_object() const noexcept {
        return job_?job_->selected_sitting_object:std::nullopt;
    }
    bool waiting_for_room_portal_step() const noexcept {
        return job_&&job_->deferred_room_route&&!job_->external_frame_active;
    }
    std::optional<RouteFrameContext> active_frame_context() const;
    std::optional<RouteFrameContext> oldest_frame_context() const {
        if(!job_)return {};
        return suspended_jobs_.empty()?job_->frame:suspended_jobs_.front().frame;
    }
    std::optional<float> incoming_direction(float caller_radian_direction) const noexcept;
    bool consume_portal_turn() noexcept;
    const std::string& diagnostic() const {return diagnostic_;}
private:
    static constexpr std::size_t max_nested_jobs_=16;
    static constexpr std::size_t max_portal_retries_=32;
    struct Job {
        std::uint64_t ticket,actor_generation,target_generation;
        std::int16_t actor,target;
        EntityPosition expected,target_position;
        float expected_radian_direction;
        RouteFrameContext frame;
        std::vector<RoutePoint> path;
        std::size_t next=1;
        std::int16_t entry_flags;
        std::uint8_t facing;
        bool face_anywhere=false;
        bool walking_started=false;
        std::uint64_t run_generation=0;
        bool external_frame_active=false;
        CodeOwner external_frame_owner=0;
        std::optional<VmCall> external_frame_call{};
        std::size_t portal_retries=0;
        bool portal_replan_required=false;
        bool deferred_room_route=false;
        bool room_route_invalid=false,room_route_replan_active=false;
        std::optional<VmFrame> replay_frame;
        std::optional<Instruction> replay_instruction;
        std::size_t portal_steps=0;
        bool room_context_open=false;
        bool call_failure_trees=false;
        bool failure_tree_invoked=false;
        bool failure_pending=false;
        bool interrupt_pending=false;
        std::int16_t failure_code=3;
        std::int16_t failure_blocker=0;
        std::string failure_reason{};
        std::optional<std::int16_t> selected_sitting_object{};
        std::vector<PreRankedSlotGoal> fallback_goals{};
        bool external_frame_sitting=false;
        bool same_room_verified=false;
        std::optional<float> arrival_radian_direction=std::nullopt;
        std::optional<float> target_anchor_radian_direction=std::nullopt;
        std::vector<RouteCarriedTargetLink> carried_target;
        std::optional<SourceSlotCircleRouteSnapshot> source_circle=std::nullopt;
        std::optional<SourceLookTowardsRouteSnapshot> look_towards=std::nullopt;
        std::optional<SourceSlotRangeRouteSnapshot> source_range=std::nullopt;
        std::optional<SourceSparseRouteSnapshot> source_sparse=std::nullopt;
        std::optional<SourceSlotChoicesSnapshot> source_choices;
        std::optional<std::vector<LiveAttemptWalkIdentity>> sparse_consideration=std::nullopt;
        std::unique_ptr<WalkMotion> source_motion{};
        std::optional<RouteExternalFramePurpose> external_frame_purpose;
        std::uint64_t external_frame_callee_generation=0;
        // Runtime diagnostic history only; absent after restore is not a claim
        // that no earlier collision occurred in the saved route invocation.
        bool diagnostic_restored=false;
        std::uint64_t blocked_movement_count=0;
        std::optional<RouteBlockedMovementReceipt> first_blocked_movement,last_blocked_movement;
    };
    PrimitiveOutcome request(const VmFrame&,const Instruction&,const VmDispatch&,RouteOutcomeReceipt* immediate=nullptr);
    PrimitiveOutcome request_look_towards(const VmFrame&,const Instruction&,const VmDispatch&,bool nested);
    std::optional<std::string> invoke_route_execution_callback(RouteExecutionPhase,const VmFrame&,
        const Instruction&,const VmDispatch&,std::int16_t,std::uint64_t,std::int16_t,std::uint64_t,
        const EntityPosition&,const EntityPosition&,RoutePoint,std::size_t);
    bool route_execution_source_matches(const VmFrame&,const Instruction&,const VmDispatch&,
        RouteExecutionPhase) const noexcept;
    std::optional<SourceSparsePlacementOutcome> publish_route_position(const EntityPosition&,
        RoutePoint,std::size_t,bool existing_check,std::string*);
    bool tick_look_towards();
    // One RNG prefix per actual source routing-frame Tick entry. Parked
    // external BHAVs are not routing-frame entries; CONTINUE reenters it.
    bool source_route_tick_prefix();
    bool source_shoo_token_matches(const PreparedSourceRouteShoo&,std::string*) const;
    void route_mutated() noexcept {if(++route_revision_==0)++route_revision_;}
    RoutePositionPublicationProvider position_publication_;

    std::optional<bool> source_sparse_attempt_alternative();
    bool cross_floor_slot_matches(const VmFrame&,const Instruction&,bool inspect_private)const;
    bool tick_source_slot_chair();
    bool tick_source_standing();
    std::optional<bool> start_source_selected_walk();
    bool tick_source_sparse(std::uint64_t now,const std::function<void(const RouteMovement&)>& moved);
    bool tick_source_sparse_step(std::uint64_t now,const std::function<void(const RouteMovement&)>& moved);
    std::optional<SourceWalkPlanResult> source_sparse_plan(const VmFrame&,
        const Instruction&,const VmDispatch&,std::int16_t,std::uint64_t,std::int16_t,
        std::uint64_t,const EntityPosition&,const EntityPosition&,RoutePoint,
        std::size_t,std::optional<float>,std::optional<std::vector<LiveAttemptWalkIdentity>>,
        WalkMotionProfile&,
        std::vector<LiveAttemptWalkIdentity>&);
    std::optional<std::vector<LiveAttemptWalkIdentity>> capture_source_sparse_consideration(
        const VmFrame&,const Instruction&,const VmDispatch&,std::int16_t,std::uint64_t,
        std::int16_t,std::uint64_t,const EntityPosition&,const EntityPosition&,
        std::optional<std::vector<LiveAttemptWalkIdentity>>);
    bool valid_look_towards_job(const Job&,std::optional<CodeOwner>,std::string*) const;
    bool advance_room_route(const std::optional<VmCall>&,const std::optional<bool>&);
    std::optional<bool> process_room_route_invalidation();
    bool advance_invalidated_room_route(const std::optional<VmCall>& previous_call={},const std::optional<bool>& previous_result={});
    std::optional<bool> resume_invalidated_walk();
    bool schedule_external_frame(const VmCall&,bool sitting);
    bool try_next_sitting_goal();
    bool valid_local_session_job(const Job&,std::optional<CodeOwner> verified_dynamic_owner={},
        std::string* rejection_reason=nullptr) const;
    bool valid_session_route_stack(const RouteStackSnapshot&,bool require_host_contexts,
        std::optional<CodeOwner>,std::string*) const;
    static LocalRouteSnapshot local_snapshot(const Job&);
    static std::optional<Job> job_from_snapshot(const RouteJobSnapshot&);
    bool valid_source_slot_point_job(const Job&,std::string* rejection_reason=nullptr) const;
    bool valid_source_slot_circle_job(const Job&,std::string* rejection_reason=nullptr) const;
    bool valid_source_slot_range_job(const Job&,std::string* rejection_reason=nullptr) const;
    bool valid_source_slot_choices_job(const Job&,std::string* rejection_reason=nullptr) const;
    bool placement_source_matches(const VmFrame&,const Instruction&,const VmDispatch&,bool replay) const noexcept;
    std::optional<bool> try_next_circle_goal();
    void end_room_context(Job&);
    std::optional<RouteOutcomeReceipt> outcome_receipt(const Job&,bool,const std::string&,RouteOutcomeStage) const noexcept;
    void emit_outcome(const RouteOutcomeReceipt&) const noexcept;
    bool finish(bool result,std::string reason);
    void cancel_chain(const std::string& reason);
    World& world_;
    RouteGrid& grid_;
    const SlotTable& globals_;
    PreRankedSlotGoalProvider ranked_goals_;
    RouteGoalSelectionObserver goal_selection_observer_;
    RouteOutcomeObserver outcome_observer_;
    PreRankedSlotGoalProvider ranged_slot_goals_;
    ContinuousPreRankedSlotGoalProvider continuous_ranged_slot_goals_;
    AuthenticatedRangeSlotGoalProvider authenticated_range_goals_;
    std::weak_ptr<RoutePlacementProof::State> active_range_lease_;
    std::optional<std::pair<std::uint64_t,std::uint64_t>> replay_placement_authorization_;
    bool replay_placement_range_=false;
    SittingSlotCandidatePreflight sitting_slot_candidate_preflight_;
    StackObjectSlotTableProvider stack_object_slots_;
    RouteGoalValidator goal_validator_;
    StairGotoRelativeValidator stair_goto_relative_;
    RouteGridRefresh route_grid_refresh_;
    RoutePortalRetryProvider portal_retry_;
    RouteRoomPairProvider room_pair_;
    RoutePortalStepProvider room_portal_step_;
    std::function<bool(std::uint64_t)> room_route_invalidation_;
    RoutePortalContextEndProvider room_portal_context_end_;
    RouteSittingActionProvider sitting_action_,standing_action_;
    SourceLookTowardsServices look_towards_services_;
    SourceLookTowardsGroupResolver look_towards_group_;
    RouteExecutionCallback attempt_walk_execution_;
    RouteExecutionCallback set_position_execution_;
    SourceSparseServices source_sparse_services_;
    std::optional<SourceSparsePlacementOutcome> last_sparse_placement_;
    std::optional<std::vector<LiveAttemptWalkIdentity>> replay_sparse_consideration_;
    RouteLimits limits_;
    Vm* vm_=nullptr;
    std::optional<Job> job_;
    // Parent Route45 jobs remain suspended while a portal BHAV executes and
    // may itself issue Route45. The stack is bounded by max_nested_jobs_ below.
    std::vector<Job> suspended_jobs_;
    // Derived from VM immediate continuations on restore; no independent wire state.
    std::vector<std::uint64_t> parked_immediate_routes_;
    std::optional<std::uint64_t> last_tick_;
    bool source_sparse_continue_=false;
    bool general_slot_provider_=false;
    std::optional<bool> advance_source_slot_choice();
    bool validate_source_slot_choices(const Job&,std::string*) const;
    std::string diagnostic_;
    mutable bool executing_=false;
    bool source_reset_detach_failed_=false;
    std::uint64_t route_revision_=1;
    std::shared_ptr<int> shoo_lifetime_=std::make_shared<int>(0);
};
} // namespace odh
