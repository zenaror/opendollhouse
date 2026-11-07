// SPDX-License-Identifier: MPL-2.0
// FreeSO 7e76c196 VMRefresh.Execute and VMContext.RefreshRoomScore.
#pragma once
#include "odh/room_score.hpp"
#include "odh/object_position_valid.hpp"
namespace odh_host {
// SavedLotRenderer resolves the live Graphic word/DGRP for every capture; the
// native simulation owns no per-instance DGRP cache. Room scores likewise use
// the shared live calculation, so candidate publication cannot retain stale
// renderer or RoomInfo cache values. Lighting requires the actual event owner
// and consumer; a caller without that service still fails explicitly.
struct SessionRefreshServices {
    std::function<odh::PrimitiveOutcome(odh::ObjectPositionIdentity)> lighting,area;
};
inline odh::VmPrimitive session_refresh_primitive(odh::World& world,const odh::GeometricRoomMap* rooms,
        SessionRefreshServices services={}) {
    return [&world,rooms,services=std::move(services)](const odh::VmFrame& frame,const odh::Instruction& instruction,
            const odh::Ts1Temps&,const odh::VmDispatch&)->odh::PrimitiveOutcome {
        if(instruction.opcode!=7)return {};
        const auto selector=static_cast<std::uint16_t>(instruction.operands[0]|(instruction.operands[1]<<8));
        const auto type=static_cast<std::uint16_t>(instruction.operands[2]|(instruction.operands[3]<<8));
        // Source ignores RefreshGraphic's return value, including a missing
        // target or DGRP. Native rendering derives the result from live data.
        if(type==0)return {odh::PrimitiveDisposition::goto_true,{}};
        // VMRefresh's source switch has no default action for other values.
        if(type!=1&&type!=2)return {odh::PrimitiveDisposition::goto_true,{}};
        const auto* target=selector==0?world.frame_entity(frame,odh::VmEntityRole::caller,true):
            selector==1?world.frame_entity(frame,odh::VmEntityRole::stack_object,true):nullptr;
        if(!target)return {odh::PrimitiveDisposition::unsupported,"Refresh target is absent"};
        const odh::ObjectPositionIdentity identity{target->id,target->generation};
        if(type==1)return services.lighting?services.lighting(identity):
            odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"Refresh lighting requires real deferred lighting state"};
        if(services.area)return services.area(identity);
        // GetObjectRoom(OUT_OF_WORLD)==0; RefreshRoomScore(room0) returns before
        // accessing RoomInfo. This does not invent a positioned room.
        if(!target->position)return {odh::PrimitiveDisposition::goto_true,{}};
        if(!rooms)return {odh::PrimitiveDisposition::unsupported,"Refresh area requires the actual runtime room map"};
        const auto pos=*target->position;
        if(!odh::calculate_room_score(*rooms,odh::room_score_contributions(world),pos.x,pos.y,pos.level))
            return {odh::PrimitiveDisposition::unsupported,"Refresh area room score inputs are unresolved"};
        return {odh::PrimitiveDisposition::goto_true,{}};
    };
}
}
