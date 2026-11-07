// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/route_frame.hpp"
#include "odh/walk_path.hpp"
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace odh {
struct WalkMotionProfile {
    bool is_pet=false;
    std::int16_t person_type=0;
    bool in_pool=false;
    std::int16_t walk_style=0;
};
struct WalkMotionPose {RectRoutePoint position{};std::int16_t level=1;float radians=0;};
enum class WalkMotionPhase : std::uint8_t {
    uninitialized, walking, awaiting_placement, waypoint_required, collision_required, finished
};
enum class WalkMotionStatus : std::uint8_t {
    success, waypoint_required, collision_required, finished,
    invalid_input, unsupported_arithmetic, budget_exhausted
};
// A caller may lower, but cannot raise, the supported 1..4096 segment ceiling.
struct WalkMotionLimits {std::size_t segments=4096;};
// Geometry and source movement counters only. CurrentPath's arc lookup and Last
// velocity are rebuilt/reset from move_total_frames just like the source marshal.
// Collision handling, waits, shoo, beginning/ending turn animations, identities,
// World placement, VM tickets and route resource provenance belong to Routing.
struct WalkMotionSnapshot {
    std::optional<WalkSegmentGeometry> current;
    std::vector<WalkSegmentGeometry> remaining;
    WalkMotionPose previous_position{};
    std::int32_t move_frames=0,move_total_frames=0,velocity=0,turn_frames=0;
    double walk_direction=0,target_direction=0;
    WalkMotionPhase phase=WalkMotionPhase::uninitialized;
};
struct WalkMotionStepData {
    WalkPathSample sample{};
    std::int16_t level=1;
    std::int32_t velocity=0,move_frames=0;
    std::array<float,3> animation_weights{};
    bool portal_turn_queried=false;
};
class WalkMotion;
class PreparedWalkMotionStep {
public:
    PreparedWalkMotionStep()=default;
    const WalkMotionStepData& data() const noexcept{return data_;}
private:
    struct Lease;
    std::weak_ptr<Lease> lease_;
    WalkMotionStepData data_{};
    explicit PreparedWalkMotionStep(const std::shared_ptr<Lease>&);
    friend class WalkMotion;
};
struct WalkMotionPrepared {
    WalkMotionStatus status=WalkMotionStatus::invalid_input;
    std::optional<PreparedWalkMotionStep> step;
};
struct WalkMotionEffects {
    WalkVector visual{},velocity{};
    float visual_z=0,radian_direction=0;
    double turn_velocity=0;
};
struct WalkMotionCommitted {
    WalkMotionStatus status=WalkMotionStatus::invalid_input;
    std::optional<WalkMotionEffects> effects;
};
struct WalkMotionCaptured {
    WalkMotionStatus status=WalkMotionStatus::invalid_input;
    std::optional<WalkMotionSnapshot> snapshot;
};
class WalkMotion {
public:
    explicit WalkMotion(WalkMotionLimits limits={}):limits_(limits){}
    WalkMotion(const WalkMotion&)=delete;
    WalkMotion& operator=(const WalkMotion&)=delete;
    WalkMotionPhase phase() const noexcept{return state_.phase;}
    // initial_velocity comes from BeginWalk (hard start0 or portal continuation8)
    // or a source replan. No animation/turn operation is fabricated here.
    WalkMotionStatus initialize(const std::vector<WalkSegmentGeometry>&,
        const WalkMotionPose&,const WalkMotionProfile&,bool already_walking=false,
        std::int32_t initial_velocity=0);
    // Caller must perform this in the SAME routing tick after commit reports
    // waypoint_required. Empty remaining queue yields finished and sets source
    // MoveTotalFrames=-1; EndWalk and its turns are then the caller's work.
    WalkMotionStatus advance_waypoint(const WalkMotionPose&,const WalkMotionProfile&,
        bool already_walking=true) noexcept;
    // Executes the pre-SetPosition portion of WALKING. Only the source's tail
    // condition calls CanPortalTurn; unrelated ticks never consume its counter.
    // current.state must be WALKING; an optional parent supplies portal context.
    // Successful prepare publishes counters/cache and an opaque one-use lease.
    // Host now performs real SetPosition(NORTH), restoring its stored heading.
    WalkMotionPrepared prepare_step(RouteFrameContext& current,
        RouteFrameContext* parent=nullptr) noexcept;
    bool can_commit(const PreparedWalkMotionStep&) const noexcept;
    // Source Success and CantBeThroughWall both take the accepted branch. Host
    // classifies placement; this method neither assumes nor changes World pose.
    // Returned direction is float(OLD TargetDirection); feedback stores the new
    // target for the NEXT tick. Velocity uses the PRE-feedback movement speed.
    WalkMotionCommitted commit_step(const PreparedWalkMotionStep&) noexcept;
    // Source failure retains velocity/frame progress and ResetToFrame caches.
    // Requires an external collision/replan/wait/shoo decision; no automatic
    // step, authored failure or fake successful World move is produced.
    WalkMotionStatus reject_step(const PreparedWalkMotionStep&) noexcept;
    // WaitTime deceleration changes only source Velocity, retaining the failed
    // sample's frame/counters and ResetToFrame cache. Resume only after Routing
    // has completed its exact collision decision/wait; never commits that sample.
    WalkMotionStatus set_collision_velocity(std::int32_t velocity) noexcept;
    WalkMotionStatus resume_after_collision() noexcept;
    // Source WaitTime/timeout changes Velocity before its route-state switch,
    // including while a selected alternative has WalkTo=null but retains the
    // old CurrentPath. Change only Velocity on a stable walking, rejected or
    // finished integrator; never resume, resample or reset its source caches.
    WalkMotionStatus set_velocity(std::int32_t velocity) noexcept;
    // No checkpoint can capture a placement callback or same-tick waypoint gap.
    WalkMotionCaptured snapshot() const;
    WalkMotionStatus restore(const WalkMotionSnapshot&);
private:
    WalkMotionLimits limits_;
    WalkMotionSnapshot state_{};
    std::optional<WalkPathSegment> current_;
    std::shared_ptr<PreparedWalkMotionStep::Lease> pending_;
};
const char* walk_motion_status_name(WalkMotionStatus) noexcept;
} // namespace odh
