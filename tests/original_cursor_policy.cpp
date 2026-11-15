// SPDX-License-Identifier: MPL-2.0
#include "original_cursor_policy.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace odh_host;
using Asset=OriginalCursorAsset;using Domain=OriginalCursorDomain;using Mode=OriginalCursorMode;using Tool=OriginalObjectCursor;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void cases(){
    {
        OriginalCursorPolicyInput camera;camera.domain=Domain::world_canvas;camera.right_drag=true;
        need(select_original_cursor(camera).reason==OriginalCursorReason::unbound_right_drag,"RMB without actual direction remains unbound");
        camera.right_drag_direction=std::array<int,2>{1,-1};
        need(select_original_cursor(camera).asset==Asset::UPRIGHT,"captured RMB direction reaches existing original cursor asset mapping");
        camera.domain=Domain::modal;
        need(select_original_cursor(camera).asset==Asset::Arrow,"modal suppresses captured RMB direction");
    }

    {
        OriginalCursorPolicyInput canvas;canvas.domain=OriginalCursorDomain::world_canvas;
        need(select_original_cursor(canvas).asset==OriginalCursorAsset::Arrow,"unpicked world canvas does not invent a ground target");
        canvas.edge_scrolling_enabled=true;canvas.edge={1,0};
        need(select_original_cursor(canvas).asset==OriginalCursorAsset::RIGHT,"real camera edge works even where no floor/object was picked");
    }

    OriginalCursorPolicyInput input;
    for(const auto domain:{Domain::ui,Domain::modal,Domain::menu})for(const auto mode:{Mode::live,Mode::buy,Mode::build}){
        input={};input.domain=domain;input.mode=mode;input.interactions_available=true;
        input.edge_scrolling_enabled=true;input.edge={1,-1};input.object_holder_active=true;input.object_cursor=Tool::place;
        need(select_original_cursor(input).asset==Asset::Arrow,"UI/modal/menu never inherits a world or held-object cursor");
    }
    for(const auto domain:{Domain::world_object,Domain::world_person}){
        input={};input.domain=domain;
        need(select_original_cursor(input).asset==Asset::Arrow&&select_original_cursor(input).reason==OriginalCursorReason::unknown_availability,
            "unknown interaction capability never pretends unavailable");
        input.interactions_available=false;
        need(select_original_cursor(input).asset==Asset::LiveObjectUnavail,"known evaluated false is source unavailable cursor for either entity kind");
        input.interactions_available=true;
        need(select_original_cursor(input).asset==(domain==Domain::world_person?Asset::LivePerson:Asset::LiveObjectAvail),
            "only proven available interactions select original person/object cursor");
    }
    input={};input.domain=Domain::world_ground;
    need(select_original_cursor(input).asset==Asset::LiveNothing,"live empty ground follows pinned source");
    const Asset directions[3][3]={{Asset::UPLEFT,Asset::UP,Asset::UPRIGHT},{Asset::LEFT,Asset::LiveNothing,Asset::RIGHT},
        {Asset::DOWNLEFT,Asset::DOWN,Asset::DOWNRIGHT}};
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        input={};input.domain=Domain::world_ground;input.edge={x,y};
        need(select_original_cursor(input).asset==Asset::LiveNothing,"disabled scrolling ignores even a valid edge");
        input.edge_scrolling_enabled=true;
        need(select_original_cursor(input).asset==directions[y+1][x+1],"all eight actual camera edges map without inferring from asset names");
        input.domain=Domain::world_person;input.interactions_available=true;
        if(x||y)need(select_original_cursor(input).asset==directions[y+1][x+1],"live scrolling owns cursor before person hover");
    }
    input.edge={2,0};need(select_original_cursor(input).asset==Asset::Arrow&&select_original_cursor(input).reason==OriginalCursorReason::invalid_edge,
        "unexpected edge data does not index the direction map");
    for(const auto mode:{Mode::buy,Mode::build})for(const auto tool:{Tool::none,Tool::move,Tool::place,Tool::rotate}){
        input={};input.domain=Domain::world_object;input.mode=mode;input.object_cursor=tool;input.interactions_available=true;
        need(select_original_cursor(input).asset==Asset::Arrow,"mode or tool intent alone does not fabricate an active object holder");
        input.object_holder_active=true;
        const auto expected=tool==Tool::move?Asset::SimsMove:tool==Tool::place?Asset::SimsPlace:tool==Tool::rotate?Asset::SimsRotate:Asset::Arrow;
        need(select_original_cursor(input).asset==expected,"real holder state follows pinned move/place/rotate branches");
        input.edge_scrolling_enabled=true;input.edge={1,1};
        need(select_original_cursor(input).asset==expected,"source object holder runs after edge cursor selection");
        input.right_drag=true;
        need(select_original_cursor(input).asset==Asset::Arrow&&select_original_cursor(input).reason==OriginalCursorReason::unbound_right_drag,
            "unbound right-drag adapter never invents Pan or leaks held object cursor");
    }
    input={};input.domain=Domain::world_ground;input.object_holder_active=true;input.object_cursor=Tool::place;
    need(select_original_cursor(input).asset==Asset::LiveNothing,"returning to Live ignores stale Buy holder intent");
    input.domain=Domain::world_person;input.interactions_available=true;
    input.focused=false;need(select_original_cursor(input).asset==Asset::Arrow,"focus loss resets to base arrow");
    input.focused=true;input.pointer_in_window=false;need(select_original_cursor(input).asset==Asset::Arrow,"leaving window resets to base arrow");
}
}
int main(){try{cases();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
