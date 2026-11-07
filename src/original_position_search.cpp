// SPDX-License-Identifier: MPL-2.0
#include "odh/original_position_search.hpp"
#include <algorithm>

namespace odh {
namespace {
std::int32_t signed_byte(std::int64_t value){
    const auto bits=static_cast<std::uint64_t>(value)&255U;
    return bits<128?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-256;
}
std::int32_t tile_of(std::int32_t value){
    const auto wide=static_cast<std::int64_t>(value);
    return static_cast<std::int32_t>(wide>=0?wide/16:-((-wide+15)/16));
}
}
OriginalPositionSearchResult search_original_position(const OriginalPositionSearchRequest& request,
    const OriginalPositionSearchServices& services){
    OriginalPositionSearchResult result;
    const auto fail=[&](const std::string& why){result.status=OriginalPositionSearchStatus::unsupported;result.position.reset();result.diagnostic=why;return result;};
    try{
        const auto current=[&]{return services.source_still_matches&&services.source_still_matches();};
        if(!current())return fail("original position search source authority unavailable");
        if(!services.lot_size||!services.room_at||!services.tile_mask||!services.native_4790e8||!services.position_valid)
            return fail("original position search requires actual map and PositionValid services");
        if(request.start.level<1||request.start.level>2||request.start.direction>7)
            return fail("original position search level/direction outside fixed recovery profile");
        const auto size=services.lot_size();
        if(!current()||!size||*size<2||*size>128)return fail("original position search lot size outside supported signed-byte profile");
        const auto clamp=[&](std::int32_t value){
            auto tile=signed_byte(tile_of(value));
            if(tile>=*size-1)tile=signed_byte(*size-2);
            if(tile<1)tile=1;
            return tile;
        };
        // Native packed tile byte0 is Y; byte1 is X (433116/434d3d).
        const auto base_y=clamp(request.start.y),base_x=clamp(request.start.x);
        const auto room=services.room_at(static_cast<std::int16_t>(base_x),static_cast<std::int16_t>(base_y),request.start.level);
        if(!current()||!room)return fail("original position search anchor room unavailable");
        std::int32_t a=0,b=0;std::size_t outside=0;
        const auto outside_limit=static_cast<std::size_t>(*size)*8;
        while(result.attempts<4096&&outside<outside_limit){
            if(!current())return fail("original position search source authority expired");
            ++result.attempts;
            const auto y=signed_byte(base_y+signed_byte(a)),x=signed_byte(base_x+signed_byte(b));
            if(x<1||y<1||x>=*size-1||y>=*size-1)++outside;
            else{
                outside=0;
                const auto tx=static_cast<std::int16_t>(x),ty=static_cast<std::int16_t>(y);
                const auto mask=services.tile_mask(tx,ty,request.start.level);
                if(!current()||!mask)return fail("original position search tile mask unavailable");
                bool eligible=(*mask&0x30U)==0;
                if(eligible&&request.same_room){
                    const auto candidate_room=services.room_at(tx,ty,request.start.level);
                    if(!current()||!candidate_room)return fail("original position search candidate room unavailable");
                    eligible=*candidate_room==*room;
                }
                if(eligible){
                    const auto excluded=services.native_4790e8(tx,ty,request.start.level);
                    if(!current()||!excluded)return fail("original position search native tile predicate unavailable");
                    eligible=!*excluded;
                }
                if(eligible){
                    const EntityPosition candidate{x*16+8,y*16+8,request.start.level,request.start.direction};
                    ++result.position_checks;
                    const auto valid=services.position_valid(candidate);
                    if(!current())return fail("original position search authority expired during PositionValid");
                    if(valid.status==Ts1PlacementStatus::unsupported)return fail("original position search PositionValid unsupported: "+valid.diagnostic);
                    if(valid.status==Ts1PlacementStatus::success){result.status=OriginalPositionSearchStatus::found;result.position=candidate;return result;}
                    if(valid.status!=Ts1PlacementStatus::blocked)return fail("original position search PositionValid returned invalid status");
                }
            }
            // Literal -1 branch434c56..434c89, preserving its boundary ties.
            if(b<=-a&&b<=a)--a;
            else if(b>-a&&b<=a)--b;
            else if(b>=-a)++a;
            else ++b;
        }
        result.status=OriginalPositionSearchStatus::exhausted;
        result.diagnostic="original position search exhausted its native iteration/outside limit";return result;
    }catch(const std::exception& error){return fail(std::string("original position search callback failed: ")+error.what());}
    catch(...){return fail("original position search callback failed");}
}
} // namespace odh
