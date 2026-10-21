#include "odh/route_frame.hpp"

#include <cmath>
#include <limits>

namespace odh {
namespace {
const RouteFrameContext& effective(const RouteFrameContext& current,
                                  const RouteFrameContext* parent) noexcept {
    return parent ? *parent : current;
}
RouteFrameContext& effective(RouteFrameContext& current,
                             RouteFrameContext* parent) noexcept {
    return parent ? *parent : current;
}
}

std::optional<float> route_incoming_direction(
    const RouteFrameContext& current, const RouteFrameContext* parent,
    float caller_radian_direction) noexcept {
    if (!std::isfinite(caller_radian_direction)) return {};
    const auto& context = effective(current, parent);
    if (context.state == RouteFrameState::room_portal || context.portal_turns > 0)
        return caller_radian_direction;
    return {};
}

bool consume_route_portal_turn(RouteFrameContext& current,
                               RouteFrameContext* parent) noexcept {
    auto& context = effective(current, parent);
    if (context.state != RouteFrameState::room_portal) return true;
    if (context.portal_turns == std::numeric_limits<std::uint32_t>::max()) return false;
    return context.portal_turns++ == 0;
}

} // namespace odh
