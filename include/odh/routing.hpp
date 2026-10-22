#pragma once
#include "odh/world.hpp"
#include "odh/route.hpp"
#include "odh/route_frame.hpp"
#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace odh {
struct RouteMovement {std::uint64_t tick;std::int16_t entity;EntityPosition position;};
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
    std::uint8_t slot_direction_flags);
struct ReferenceSlotCircleCandidate { RoutePoint position; std::uint8_t entry_flags; };
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
};
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
enum class RoutePortalStepStatus : std::uint8_t { call_ready,destination_reached,unavailable };
struct RoutePortalStepPlan {
    RoutePortalStepStatus status=RoutePortalStepStatus::unavailable;
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
// Controlled async route adapter: Route45's profiled SLOTs plus opcode27's
// exact-point OnTopOf/SameDirection-or-Opposing/NoFailureTrees profile.
// Opcode27 requires a host goal validator; this is not the complete TS1 routing policy.
class Routing {
public:
    Routing(World& world,RouteGrid& grid,const SlotTable& globals,
            PreRankedSlotGoalProvider goals={},RouteLimits limits={})
        :world_(world),grid_(grid),globals_(globals),ranked_goals_(std::move(goals)),limits_(limits) {}
    Routing(const Routing&)=delete;
    Routing& operator=(const Routing&)=delete;
    bool bind(Vm&);
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
    // Provider for bounded, source-verified ranged SLOT profiles.
    void set_ranged_slot_goal_provider(PreRankedSlotGoalProvider provider) {
        if(executing_||job_)throw std::logic_error("cannot replace ranged SLOT provider while active");
        ranged_slot_goals_=std::move(provider);
    }
    VmPrimitive primitive();
    // Schedule a host-resolved portal/route BHAV while Route45 is waiting.
    // The host must supply the correct routine, code owner and VM context;
    // this controller does not discover portals or resolve EntryPoint tables.
    bool call_external_frame(const VmCall&);
    bool tick(std::uint64_t tick,const std::function<void(const RouteMovement&)>& moved={});
    bool active() const {return job_.has_value();}
    bool external_frame_active() const {return job_&&job_->external_frame_active;}
    bool waiting_for_room_portal_step() const noexcept {
        return job_&&job_->deferred_room_route&&!job_->external_frame_active;
    }
    std::optional<RouteFrameContext> active_frame_context() const;
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
    };
    PrimitiveOutcome request(const VmFrame&,const Instruction&,const VmDispatch&);
    bool advance_room_route(const std::optional<VmCall>&,const std::optional<bool>&);
    void end_room_context(Job&);
    bool finish(bool result,std::string reason);
    void cancel_chain(const std::string& reason);
    World& world_;
    RouteGrid& grid_;
    const SlotTable& globals_;
    PreRankedSlotGoalProvider ranked_goals_;
    PreRankedSlotGoalProvider ranged_slot_goals_;
    StackObjectSlotTableProvider stack_object_slots_;
    RouteGoalValidator goal_validator_;
    StairGotoRelativeValidator stair_goto_relative_;
    RouteGridRefresh route_grid_refresh_;
    RoutePortalRetryProvider portal_retry_;
    RouteRoomPairProvider room_pair_;
    RoutePortalStepProvider room_portal_step_;
    RoutePortalContextEndProvider room_portal_context_end_;
    RouteLimits limits_;
    Vm* vm_=nullptr;
    std::optional<Job> job_;
    // Parent Route45 jobs remain suspended while a portal BHAV executes and
    // may itself issue Route45. The stack is bounded by max_nested_jobs_ below.
    std::vector<Job> suspended_jobs_;
    std::optional<std::uint64_t> last_tick_;
    std::string diagnostic_;
    bool executing_=false;
};
} // namespace odh
