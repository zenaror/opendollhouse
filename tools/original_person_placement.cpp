// SPDX-License-Identifier: MPL-2.0
#include "original_person_placement.hpp"
#include <exception>

namespace odh_host {
std::optional<bool> original_person_is_in_world(
        const std::optional<odh::EntityPosition>& position,std::int32_t lot_size){
    if(lot_size<2||lot_size>128)return {};
    if(!position)return false;
    const auto tile_byte=[](std::int32_t coordinate){
        // Define arithmetic shift and signed-byte wrapping without relying on
        // implementation-defined signed shifts or narrowing conversions.
        const auto value=static_cast<std::int64_t>(coordinate);
        const auto tile=value>=0?value/16:-((-value+15)/16);
        const auto low=static_cast<std::uint8_t>(static_cast<std::uint64_t>(tile)&255U);
        return low<128?static_cast<int>(low):static_cast<int>(low)-256;
    };
    const auto x=tile_byte(position->x),y=tile_byte(position->y);
    return x>=1&&y>=1&&x<lot_size-1&&y<lot_size-1;
}
OriginalPersonPlacementResult place_original_person(const OriginalPersonPlacementServices& s){
    OriginalPersonPlacementResult result;
    const auto fail=[&](const std::string& why){result.diagnostic=why;return result;};
    try{
        const auto current=[&]{return s.source_still_matches&&s.source_still_matches();};
        if(!current())return fail("original person placement authority unavailable");
        if(!s.contained_snapshot||!s.remove_from_world||!s.current_position||!s.query54||!s.search||
           !s.skip_native_recovery_position||!s.is_in_world||!s.lot_size||!s.force88||!s.footprint74)
            return fail("original person placement services incomplete");
        std::string error;
        // Search outputs only update this local candidate; native caller ignores
        // the Boolean after the second search and continues with the old point.
        const auto probe=[&](odh::EntityPosition& candidate){
            if(!current()){error="original person placement authority expired before query";return false;}
            result.effects_started=true;++result.queries;
            const auto query=s.query54(candidate);
            if(!current()){error="original person placement authority expired during query";return false;}
            if(query.status==odh::Ts1PlacementStatus::success)return true;
            if(query.status!=odh::Ts1PlacementStatus::blocked){error="original person query unsupported: "+query.diagnostic;return false;}
            const auto anchor=candidate;
            for(const bool same_room:{true,false}){
                if(!current()){error="original person placement authority expired before search";return false;}
                ++result.searches;const auto found=s.search(anchor,same_room);
                if(!current()){error="original person placement authority expired during search";return false;}
                if(found.status==odh::OriginalPositionSearchStatus::found){
                    if(!found.position||found.position->level!=anchor.level||found.position->direction!=anchor.direction){
                        error="original person search returned an invalid preserved pose";return false;
                    }
                    candidate=*found.position;return true;
                }
                if(found.status!=odh::OriginalPositionSearchStatus::exhausted||found.position){
                    error="original person search unsupported: "+found.diagnostic;return false;
                }
            }
            return true;
        };
        const auto contained=s.contained_snapshot();
        if(!current()||!contained)return fail("original person containment snapshot unavailable");
        if(*contained){
            result.effects_started=true;
            if(!s.remove_from_world(error)||!current())return fail("original person contained removal failed: "+error);
            auto point=s.current_position();
            if(!current()||!point)return fail("original person pose after contained removal unavailable");
            if(!probe(*point))return fail(error);
        }
        const auto skip=s.skip_native_recovery_position();
        if(!current()||!skip)return fail("original person native bit6 unavailable");
        if(*skip){result.completed=true;return result;}
        const auto in_world=s.is_in_world();
        if(!current()||!in_world)return fail("original person IsInWorld unavailable");
        if(*in_world){result.completed=true;return result;}
        result.effects_started=true;
        if(!s.remove_from_world(error)||!current())return fail("original person fallback removal failed: "+error);
        auto point=s.current_position();
        if(!current()||!point)return fail("original person pose after fallback removal unavailable");
        const auto size=s.lot_size();
        if(!current()||!size||*size<2||*size>128)return fail("original person fallback lot size unsupported");
        //45eb13..45eb45: both become center+8. X's old low nibble is
        // temporarily ORed in, then AND AL,f8 / OR AL,8 discards it.
        const auto center=(*size/2)*16;
        point->x=center+8;point->y=center+8;
        if(!probe(*point))return fail(error);
        if(!current())return fail("original person placement authority expired before force");
        result.fallback_executed=true;result.forced_position=*point;
        if(!s.force88(*point,error)||!current())return fail("original person force88 incomplete: "+error);
        if(!s.footprint74(error)||!current())return fail("original person footprint74 incomplete: "+error);
        result.completed=true;return result;
    }catch(const std::exception& e){return fail(std::string("original person placement callback failed: ")+e.what());}
    catch(...){return fail("original person placement callback failed");}
}
} // namespace odh_host
