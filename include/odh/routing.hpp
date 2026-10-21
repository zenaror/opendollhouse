#pragma once
#include "odh/world.hpp"
#include "odh/route.hpp"
#include "odh/route_frame.hpp"
#include <cstddef>
#include <functional>
#include <optional>
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
// ODH's bounded exact-radius circle profile for the observed House07 SLOT.
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
// Initial controlled route adapter. One stable VM per controller; all referenced
// objects must outlive calls. This is not the complete TS1 routing policy.
class Routing {
public:
    Routing(World& world,RouteGrid& grid,const SlotTable& globals,
            PreRankedSlotGoalProvider goals={},RouteLimits limits={})
        :world_(world),grid_(grid),globals_(globals),ranked_goals_(std::move(goals)),limits_(limits) {}
    Routing(const Routing&)=delete;
    Routing& operator=(const Routing&)=delete;
    bool bind(Vm&);
    VmPrimitive primitive();
    // Schedule a host-resolved portal/route BHAV while Route45 is waiting.
    // The host must supply the correct routine, code owner and VM context;
    // this controller does not discover portals or resolve EntryPoint tables.
    bool call_external_frame(const VmCall&);
    bool tick(std::uint64_t tick,const std::function<void(const RouteMovement&)>& moved={});
    bool active() const {return job_.has_value();}
    std::optional<RouteFrameContext> active_frame_context() const;
    std::optional<float> incoming_direction(float caller_radian_direction) const noexcept;
    bool consume_portal_turn() noexcept;
    const std::string& diagnostic() const {return diagnostic_;}
private:
    static constexpr std::size_t max_nested_jobs_=16;
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
        bool walking_started=false;
        std::uint64_t run_generation=0;
        bool external_frame_active=false;
        CodeOwner external_frame_owner=0;
    };
    PrimitiveOutcome request(const VmFrame&,const Instruction&,const VmDispatch&);
    bool finish(bool result,const std::string& reason);
    void cancel_chain(const std::string& reason);
    World& world_;
    RouteGrid& grid_;
    const SlotTable& globals_;
    PreRankedSlotGoalProvider ranked_goals_;
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
