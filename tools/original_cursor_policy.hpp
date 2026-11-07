// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "original_cursors.hpp"

namespace odh_host {
enum class OriginalCursorDomain {ui,modal,menu,world_canvas,world_ground,world_object,world_person};
enum class OriginalCursorMode {live,buy,build};
enum class OriginalObjectCursor {none,move,place,rotate};
struct OriginalCursorPolicyInput {
    OriginalCursorDomain domain=OriginalCursorDomain::ui;
    OriginalCursorMode mode=OriginalCursorMode::live;
    std::optional<bool> interactions_available;
    bool focused=true,pointer_in_window=true;
    // Supply actual consumer-enabled lot_camera_edge output, not raw mouse
    // position. Disabled edge scrolling cannot advertise a movement cursor.
    bool edge_scrolling_enabled=false,right_drag=false;
    std::array<int,2> edge{0,0};
    // This is the real object-holder adapter, not merely entering Buy/Build.
    bool object_holder_active=false;
    OriginalObjectCursor object_cursor=OriginalObjectCursor::none;
};
enum class OriginalCursorReason {
    ui,inactive,unknown_availability,ground,available_object,available_person,
    unavailable,edge,object_move,object_place,object_rotate,
    unbound_right_drag,unbound_tool,invalid_edge
};
struct OriginalCursorSelection {
    OriginalCursorAsset asset=OriginalCursorAsset::Arrow;
    OriginalCursorReason reason=OriginalCursorReason::ui;
};
// Pure presentation policy. Source anchors: pinned Simitone UILotControl
// LiveModeUpdate626–654 and UIObjectHolder.Update335–357/446–448; file mapping
// pinned FreeSO CursorManager.GenMap. No VM query or file read occurs here.
// Right-drag direction/anchor and custom Build tools need separate proven
// adapters; the asset name Pan is deliberately not interpreted as a rule.
OriginalCursorSelection select_original_cursor(const OriginalCursorPolicyInput&) noexcept;
}
