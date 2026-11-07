// SPDX-License-Identifier: MPL-2.0
#include "original_cursor_policy.hpp"

namespace odh_host {
OriginalCursorSelection select_original_cursor(const OriginalCursorPolicyInput& input)noexcept{
    using Asset=OriginalCursorAsset;using Reason=OriginalCursorReason;
    if(!input.focused||!input.pointer_in_window)return {Asset::Arrow,Reason::inactive};
    const bool world=input.domain==OriginalCursorDomain::world_canvas||input.domain==OriginalCursorDomain::world_ground||input.domain==OriginalCursorDomain::world_object||
        input.domain==OriginalCursorDomain::world_person;
    if(!world)return {Asset::Arrow,Reason::ui};
    // The selected reference uses directional arrows/anchor during right drag,
    // not Pan.cur. Defer that adapter rather than guessing a direction here.
    if(input.right_drag)return {Asset::Arrow,Reason::unbound_right_drag};
    if(input.mode!=OriginalCursorMode::live&&input.object_holder_active){
        // ObjectHolder sets its cursor after camera processing in the source.
        switch(input.object_cursor){
        case OriginalObjectCursor::move:return {Asset::SimsMove,Reason::object_move};
        case OriginalObjectCursor::place:return {Asset::SimsPlace,Reason::object_place};
        case OriginalObjectCursor::rotate:return {Asset::SimsRotate,Reason::object_rotate};
        case OriginalObjectCursor::none:return {Asset::Arrow,Reason::unbound_tool};
        }
    }
    if(input.edge_scrolling_enabled){
        const auto x=input.edge[0],y=input.edge[1];
        if(x<-1||x>1||y<-1||y>1)return {Asset::Arrow,Reason::invalid_edge};
        if(x||y){
            const Asset directions[3][3]={{Asset::UPLEFT,Asset::UP,Asset::UPRIGHT},
                {Asset::LEFT,Asset::Arrow,Asset::RIGHT},{Asset::DOWNLEFT,Asset::DOWN,Asset::DOWNRIGHT}};
            return {directions[y+1][x+1],Reason::edge};
        }
    }
    if(input.mode!=OriginalCursorMode::live)return {Asset::Arrow,Reason::unbound_tool};
    if(input.domain==OriginalCursorDomain::world_ground)return {Asset::LiveNothing,Reason::ground};
    if(!input.interactions_available)return {Asset::Arrow,Reason::unknown_availability};
    if(!*input.interactions_available)return {Asset::LiveObjectUnavail,Reason::unavailable};
    if(input.domain==OriginalCursorDomain::world_person)return {Asset::LivePerson,Reason::available_person};
    return {Asset::LiveObjectAvail,Reason::available_object};
}
}
