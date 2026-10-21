#pragma once

#include <cstdint>
#include <optional>

namespace odh {

// Lifecycle states observed in Simitone's VMRoutingFrame. This context is a
// portable state value; it does not itself execute BHAVs or advance a route.
enum class RouteFrameState : std::uint8_t {
    initial,
    begin_turn,
    walking,
    turn_only,
    end_turn,
    shooed,
    room_portal,
    stand_func,
    failed
};

struct RouteFrameContext {
    RouteFrameState state = RouteFrameState::initial;
    std::uint32_t portal_turns = 0;
    std::optional<std::uint32_t> wait_time;
    std::optional<std::uint8_t> retries_remaining;
};

// Mirrors VMRoutingFrame's effective parent selection: nested frames consult
// their parent; a root frame consults itself. Non-finite avatar directions do
// not produce an incoming direction.
std::optional<float> route_incoming_direction(
    const RouteFrameContext& current, const RouteFrameContext* parent,
    float caller_radian_direction) noexcept;

// Mirrors CanPortalTurn's one-turn allowance while an effective route frame is
// executing a room portal. Other states allow a turn without consuming it.
bool consume_route_portal_turn(RouteFrameContext& current,
                              RouteFrameContext* parent) noexcept;

} // namespace odh
