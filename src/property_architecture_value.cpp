#include "odh/property_architecture_value.hpp"
#include <limits>

namespace odh {
PropertyArchitectureValue inspect_property_architecture_value(const SavedArchitecture& architecture,
    const PropertyArchitecturePrices& prices,const PropertyArchitectureValueLimits& limits) {
    using R=PropertyArchitectureValueReason;
    const auto unresolved=[](R reason){PropertyArchitectureValue result;result.reason=reason;return result;};
    if(!architecture.width || architecture.width>64 || architecture.height!=architecture.width)
        return unresolved(R::invalid_extent);
    const auto cells=std::size_t(architecture.width)*architecture.height;
    for(const auto* map:{&prices.structural,&prices.floors,&prices.wallpaper})
        if(map->size()>65536 || map->size()>limits.price_entries)return unresolved(R::price_budget);
    for(const auto& level:architecture.levels) {
        if(level.walls.size()!=cells || level.floors.size()!=cells)return unresolved(R::invalid_extent);
        if(!level.flags || level.flags->size()!=cells)return unresolved(R::missing_flags);
        for(const auto& wall:level.walls)if(wall.segments&0xc0U)return unresolved(R::unknown_segments);
    }
    if(!architecture.pools || architecture.pools->size()!=cells)return unresolved(R::missing_pools);
    PropertyArchitectureValue result;
    const auto add=[](std::int32_t& sum,std::int32_t value){
        const auto wide=std::int64_t(sum)+value;
        if(wide<std::numeric_limits<std::int32_t>::min() || wide>std::numeric_limits<std::int32_t>::max())return false;
        sum=static_cast<std::int32_t>(wide);return true;
    };
    const auto lookup=[](const auto& map,std::uint16_t id,bool complete)->std::optional<std::int32_t>{
        const auto entry=map.find(id);
        if(entry!=map.end())return entry->second;
        return complete?std::optional<std::int32_t>{0}:std::nullopt;
    };
    for(std::size_t story=0;story<2;++story) {
        const auto& level=architecture.levels[story];
        for(std::size_t y=0;y<architecture.height;++y)for(std::size_t x=0;x<architecture.width;++x) {
            const auto index=y*architecture.width+x;
            //0047b1d9: this story's raw flag, including pool exclusion on ground.
            if((*level.flags)[index]&0x20U)continue;
            const auto& wall=level.walls[index];
            const auto mask=static_cast<unsigned>(wall.segments);
            result.has_architecture=result.has_architecture || mask!=0;
            //0053bb44 copies the raw mask.00479737 supplies positive-neighbor
            // styles; it does NOT OR those neighbor edges into the mask.
            unsigned bit=0;
            if(mask){bit=1;while(!(mask&bit))bit<<=1;}
            while(bit) {
                std::uint16_t style=wall.top_right_style,pattern=0;
                switch(bit) {
                case 1:style=wall.top_left_style;pattern=wall.top_left_pattern;break;
                case 2:pattern=wall.top_right_pattern;break;
                case 4:
                    if(x+1>=architecture.width || !(level.walls[index+1].segments&1U))
                        return unresolved(R::missing_neighbor_style);
                    style=level.walls[index+1].top_left_style;pattern=wall.bottom_right_pattern;break;
                case 8:
                    if(y+1>=architecture.height || !(level.walls[index+architecture.width].segments&2U))
                        return unresolved(R::missing_neighbor_style);
                    style=level.walls[index+architecture.width].top_right_style;pattern=wall.bottom_left_pattern;break;
                case 16:pattern=wall.bottom_right_pattern;break;
                case 32:pattern=wall.bottom_left_pattern;break;
                default:return unresolved(R::unknown_segments);
                }
                const auto structural=prices.structural.find(style);
                if(structural==prices.structural.end())return unresolved(R::missing_structural_price);
                if(!add(result.structural,structural->second))return unresolved(R::arithmetic_overflow);
                if(style==1 || style==7 || style==8 || style==9) {
                    const auto price=lookup(prices.wallpaper,pattern,prices.wallpaper_lookup_complete);
                    if(!price)return unresolved(R::incomplete_wallpaper_lookup);
                    if(!add(result.surfaces_and_pools,*price))return unresolved(R::arithmetic_overflow);
                }
                //0047b286..299: lowest set bit is always processed, even32;
                // after that no bit>=32 is processed (mixed masks differ).
                bit<<=1;
                while(bit<32 && !(mask&bit))bit<<=1;
                if(bit>=32)break;
            }
            const auto floor=level.floors[index];
            result.has_architecture=result.has_architecture || floor!=0;
            if(floor && floor!=255) {
                const auto price=lookup(prices.floors,floor,prices.floor_lookup_complete);
                if(!price)return unresolved(R::incomplete_floor_lookup);
                if(!add(result.surfaces_and_pools,*price))return unresolved(R::arithmetic_overflow);
            } else if(floor==255 && (mask&0x30U)) {
                //0047b2d3..302: two distinct signed /2 operations, not sum/2.
                for(const auto id:{wall.top_left_style,wall.top_left_pattern})if(id) {
                    const auto price=lookup(prices.floors,id,prices.floor_lookup_complete);
                    if(!price)return unresolved(R::incomplete_floor_lookup);
                    if(!add(result.surfaces_and_pools,*price/2))return unresolved(R::arithmetic_overflow);
                }
            }
            if(story==0) {
                const auto pool=(*architecture.pools)[index];
                if(pool<16) {
                    if(!prices.pool_tile_price)return unresolved(R::missing_pool_price);
                    if(!add(result.surfaces_and_pools,*prices.pool_tile_price))return unresolved(R::arithmetic_overflow);
                }
                if(pool<=16)result.has_architecture=true;
            }
        }
    }
    result.total=result.structural/2;
    if(!add(result.total,result.surfaces_and_pools))return unresolved(R::arithmetic_overflow);
    result.status=PropertyArchitectureValueStatus::resolved;result.reason=R::none;return result;
}
const char* property_architecture_value_status_name(PropertyArchitectureValueStatus value) noexcept {
    switch(value){case PropertyArchitectureValueStatus::resolved:return "resolved";case PropertyArchitectureValueStatus::unresolved:return "unresolved";}return "invalid";
}
const char* property_architecture_value_reason_name(PropertyArchitectureValueReason value) noexcept {
    switch(value){
    case PropertyArchitectureValueReason::none:return "none";
    case PropertyArchitectureValueReason::invalid_extent:return "invalid_extent";
    case PropertyArchitectureValueReason::missing_flags:return "missing_flags";
    case PropertyArchitectureValueReason::missing_pools:return "missing_pools";
    case PropertyArchitectureValueReason::price_budget:return "price_budget";
    case PropertyArchitectureValueReason::unknown_segments:return "unknown_segments";
    case PropertyArchitectureValueReason::missing_neighbor_style:return "missing_neighbor_style";
    case PropertyArchitectureValueReason::missing_structural_price:return "missing_structural_price";
    case PropertyArchitectureValueReason::incomplete_floor_lookup:return "incomplete_floor_lookup";
    case PropertyArchitectureValueReason::incomplete_wallpaper_lookup:return "incomplete_wallpaper_lookup";
    case PropertyArchitectureValueReason::missing_pool_price:return "missing_pool_price";
    case PropertyArchitectureValueReason::arithmetic_overflow:return "arithmetic_overflow";
    }return "invalid";
}
} // namespace odh
