// SPDX-License-Identifier: MPL-2.0
// Native port of FreeSO VMRoutingFrame.AttemptWalk at
// 7e76c1960252ddd033d8937d5edb47d326f5b69c; MPL-2.0.
#pragma once

#include "odh/rect_router.hpp"
#include "odh/world.hpp"
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace odh {

struct LiveAttemptWalkIdentity {
    std::int16_t id=0;
    std::uint64_t generation=0;
};
enum class LiveAttemptWalkFootprintState { present, absent, unsupported };
struct LiveAttemptWalkFootprint {
    LiveAttemptWalkFootprintState state=LiveAttemptWalkFootprintState::unsupported;
    std::optional<RectRouteObstacle> rectangle;
};
struct LiveAttemptWalkRoomBounds {
    std::int32_t x=0,y=0,width=0,height=0; // Source room bounds in tiles.
};
struct LiveAttemptWalkRequest {
    LiveAttemptWalkIdentity actor;
    std::uint16_t room=0;
    std::uint8_t level=0; // One-based World level, not an architecture array index.
    LiveAttemptWalkRoomBounds room_bounds;
    // Complete source RoughBalanced architectural prefix followed by the four
    // AttemptWalk padded-room bands in source insertion order. No reshuffling.
    std::vector<RectRouteObstacle> architectural_and_bounds;
    std::size_t architectural_count=0;
    bool architecture_complete=false;
    // Complete RoomInfo.Entities, in strictly ascending source ObjectID order.
    std::vector<LiveAttemptWalkIdentity> entities;
    bool entities_complete=false;
    std::vector<LiveAttemptWalkIdentity> avatars_to_consider;
    bool avatars_to_consider_complete=false;
};
struct LiveAttemptWalkEntryPoint5Request {
    LiveAttemptWalkIdentity tested,other;
    std::uint16_t action=0;
    std::int16_t ghost=1;
    std::array<std::int16_t,4> arguments{};
};
enum class LiveAttemptWalkEntryPoint5State { returned_true, returned_false, unsupported, budget_exhausted };
struct LiveAttemptWalkEntryPoint5Result {
    LiveAttemptWalkEntryPoint5State state=LiveAttemptWalkEntryPoint5State::unsupported;
    std::string diagnostic;
};
struct LiveAttemptWalkServices {
    // Must certify complete current RoomInfo membership/order and the exact
    // source room/actor context, not just that the supplied IDs still exist.
    // It must detect remove/reinsert membership changes, including net-zero
    // changes, as source foreach does. Must be read-only and source-authenticated.
    std::function<bool(const LiveAttemptWalkRequest&)> inventory_still_matches;
    // Side-effect-free geometry lookup of the CURRENT entity footprint when
    // its source foreach turn begins. EP5 is the effectful service below.
    std::function<LiveAttemptWalkFootprint(LiveAttemptWalkIdentity)> footprint;
    // Execute against the tested entity's actual Thread registers and live
    // memory/RNG. The host owns source-frame authorization. No diagnostic clone.
    std::function<LiveAttemptWalkEntryPoint5Result(const LiveAttemptWalkEntryPoint5Request&)> entry_point5;
};
struct LiveAttemptWalkLimits {
    std::size_t entities=4096,obstacles=16384,entry_point5_calls=4096;
};
enum class LiveAttemptWalkState { ready, unsupported, budget_exhausted };
struct LiveAttemptWalkResult {
    LiveAttemptWalkState state=LiveAttemptWalkState::unsupported;
    std::vector<RectRouteObstacle> obstacles;
    std::vector<std::int16_t> entity_obstacle_ids;
    std::size_t entry_point5_calls=0;
    std::optional<std::int16_t> unsupported_entity;
    std::string diagnostic;
};

// Source contract: pinned FreeSO 7e76c1960252ddd033d8937d5edb47d326f5b69c,
// VMRoutingFrame.AttemptWalk. Composes obstacles only; no solver, movement,
// heading adjustment, register clone or RNG work. Failed results expose no
// partial obstacle set; preceding live EP5 effects remain published.
LiveAttemptWalkResult compose_live_attempt_walk_obstacles(const World&,
    const LiveAttemptWalkRequest&,const LiveAttemptWalkServices&,
    const LiveAttemptWalkLimits& = {});

} // namespace odh
