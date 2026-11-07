// SPDX-License-Identifier: MPL-2.0
// Native port of FreeSO VMRoutingFrame collision/WalkInterrupt/wait policy,
// revision 7e76c1960252ddd033d8937d5edb47d326f5b69c (MPL-2.0).
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace odh {
struct SourceRouteCollisionIdentity {
    std::int16_t id=0;
    std::uint64_t generation=0;
};
enum class SourceRouteCollisionBlocker : std::uint8_t { none, object, avatar };
enum class SourceRouteCollisionPhase : std::uint8_t { ready, reroute_pending, shoo_pending, waiting, soft_failure };
struct SourceRouteCollisionSnapshot {
    std::int32_t retries=5,wait_time=0,timeout=300;
    std::vector<SourceRouteCollisionIdentity> considered;
    SourceRouteCollisionPhase phase=SourceRouteCollisionPhase::ready;
};
struct SourceRouteCollisionInput {
    SourceRouteCollisionIdentity caller,blocker;
    SourceRouteCollisionBlocker kind=SourceRouteCollisionBlocker::none;
    // Exact LAST Thread.Stack frame, not any buried routing frame. Unknown
    // facts must remain nullopt. wait is required only when top_route is true.
    std::optional<bool> avatar_top_route;
    std::optional<std::int32_t> avatar_route_wait;
};
enum class SourceRouteCollisionAction : std::uint8_t {
    unsupported, attempt_walk, continue_replanned, wait_next_tick,
    shoo_direct_route, shoo_interaction, soft_fail, resume_walking
};
struct SourceRouteCollisionDecision {
    SourceRouteCollisionAction action=SourceRouteCollisionAction::unsupported;
    std::int16_t failure_code=0;
    SourceRouteCollisionIdentity failure_blocker;
    std::int32_t wait_time=0,velocity=0;
    std::array<float,3> animation_weights{};
};
enum class SourceRouteCollisionReplan : std::uint8_t { ready, no_path, unsupported };
// The source reverse scan does not break: its final topRoute is the OLDEST
// routing frame in the tested avatar's stack, then checks that route's parent.
struct SourceRouteShooFacts {
    std::optional<bool> has_route;
    std::optional<bool> oldest_route_shooed,parent_route_shooed;
    // Exact queue predicate Callee.GUID==0x7c4 && InteractionNumber==3.
    std::optional<bool> queued_destination_shoo;
};
std::optional<bool> source_can_shoo_avatar(const SourceRouteShooFacts&) noexcept;
class SourceRouteCollision {
public:
    static constexpr std::int32_t max_retries=5,wait_timeout=300;
    // TS1 only: the TSO job-lot AllowPersonIntersection shortcut is absent.
    // Called after WalkMotion.reject_step, before the effectful AttemptWalk.
    // Retry decrements and consideration insertion publish in source order.
    SourceRouteCollisionDecision begin_collision(const SourceRouteCollisionInput&);
    // Caller retains old rejected motion until replan succeeds. An avatar
    // no_path restores its old remaining WalkTo before consulting CanShooAvatar.
    // can_shoo must certify source oldest-route/parent SHOOED and queue checks.
    SourceRouteCollisionDecision finish_reroute(SourceRouteCollisionReplan,
        std::optional<bool> can_shoo=std::nullopt);
    // Host performs exact source CreateObject/ExecuteSubRoutine or interaction
    // push first. A failed/unknown service leaves shoo_pending unsupported.
    SourceRouteCollisionDecision acknowledge_shoo(bool completed) noexcept;
    // Source WaitTime tick: decelerate, weights, wait--, timeout--. Even the
    // tick taking wait1 to0 returns NEXT_TICK. Timeout yields SoftFail3(null),
    // whose real ordered fallback processing belongs to Routing.
    SourceRouteCollisionDecision tick_wait(std::int32_t velocity) noexcept;
    bool continue_after_soft_failure() noexcept;
    // No snapshot of an in-flight AttemptWalk/shoo callback can be restored.
    std::optional<SourceRouteCollisionSnapshot> snapshot() const;
    bool restore(const SourceRouteCollisionSnapshot&);
    SourceRouteCollisionPhase phase() const noexcept{return state_.phase;}
    // Inspect live consideration/counters while AttemptWalk is pending. This
    // view is NOT a persistable checkpoint; snapshot() enforces that boundary.
    const SourceRouteCollisionSnapshot& state() const noexcept{return state_;}
private:
    SourceRouteCollisionSnapshot state_;
    std::optional<SourceRouteCollisionInput> pending_;
};
} // namespace odh
