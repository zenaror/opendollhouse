// SPDX-License-Identifier: MPL-2.0
#include "house_panel_metrics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace odh_host {
namespace {
std::optional<std::int16_t> rating(float value){
    if(!std::isfinite(value))return {};
    return static_cast<std::int16_t>(std::clamp(value,0.0f,100.0f));
}
bool tuning_valid(const HousePanelTuning& t){
    return std::isfinite(t.yard_multiplier)&&t.yard_multiplier>=0&&
        std::isfinite(t.outdoor_negative_impact_factor)&&t.outdoor_negative_impact_factor>=0&&
        std::isfinite(t.outdoor_good_count_factor)&&t.outdoor_good_count_factor>=0&&
        t.dirty_penalty>=0&&t.broken_penalty>=0;
}
}
bool house_panel_is_home(std::uint16_t loaded_house,std::int32_t family_house,
    std::optional<std::int16_t> zoning,bool visiting,std::int16_t pending_switch) noexcept {
    return loaded_house>0&&loaded_house<=99&&family_house==loaded_house&&
        (!zoning||*zoning==0)&&!visiting&&pending_switch==-1;
}
HousePanelTuning original_house_panel_default_tuning(){return {0.33f,2.0f,1.0f,6,15};}
bool HousePanelSnapshot::complete()const{
    return area_square_feet&&bedrooms&&bathrooms&&std::all_of(ratings.begin(),ratings.end(),[](const auto& v){return v.has_value();});
}
std::optional<unsigned> house_lot_class(std::optional<std::int16_t> loaded_side){
    if(!loaded_side)return {};
    const auto side=*loaded_side==0?64:*loaded_side;
    if(side<4||side>64)return {};
    return side<40?0u:side<50?1u:2u;
}
HousePanelSnapshot prepare_house_panel_metrics(const HousePanelMetrics& input){
    HousePanelSnapshot out;
    const auto& map=input.rooms;
    const bool tuning=input.tuning&&tuning_valid(*input.tuning);
    if(input.furnishings_ratio&&std::isfinite(*input.furnishings_ratio)&&input.furnishings_curve)
        out.ratings[1]=rating(input.furnishings_curve->point(*input.furnishings_ratio));
    const auto history_valid=[](const HousePanelLayoutMetrics& history){
        const auto valid=[](const auto& pair){return pair.samples>=0&&pair.routing>=0&&pair.routing<=pair.samples;};
        return (!history.last_sample_tick||*history.last_sample_tick%10==0)&&valid(history.current)&&
            std::all_of(history.past.begin(),history.past.end(),valid);
    };
    if(input.layout_history&&history_valid(*input.layout_history)){
        std::int64_t a=input.layout_history->current.samples,b=input.layout_history->current.routing;
        for(const auto& pair:input.layout_history->past){a+=pair.samples;b+=pair.routing;}
        const float denominator=float(std::max<std::int64_t>(a,1));
        out.ratings[4]=rating(100.0f*std::clamp((denominator-2.0f*float(b))/denominator,0.0f,1.0f));
    }
    bool geometry=map.floor_resources_resolved&&map.width>0&&map.width==map.height&&map.width<=64&&
        !map.rooms.empty()&&map.rooms.size()<=32768;
    if(geometry)for(std::size_t id=0;id<map.rooms.size();++id)
        if(map.rooms[id].id!=id||map.rooms[id].level>=map.levels.size()){geometry=false;break;}
    std::vector<std::uint32_t> halves(geometry?map.rooms.size():0);
    if(geometry)for(std::size_t level=0;level<map.levels.size()&&geometry;++level){
        if(map.levels[level].size()!=std::size_t(map.width)*map.height){geometry=false;break;}
        for(const auto packed:map.levels[level])for(const auto id:{packed&0xffffU,(packed>>16)&0x7fffU}){
            if(id>=map.rooms.size()||map.rooms[id].id!=id||(id&&map.rooms[id].level!=level)){geometry=false;break;}
            if(id)++halves[id];
        }
    }
    if(geometry){
        std::uint32_t area=0;for(std::size_t id=1;id<map.rooms.size();++id)
            if(!map.rooms[id].outside)area+=(halves[id]/2)*9;
        out.area_square_feet=area;
        if(input.family_members&&input.size_curve)
            out.ratings[0]=*input.family_members?rating(input.size_curve->point(float(area)/float(*input.family_members))):std::optional<std::int16_t>{0};
    }
    std::vector<bool> bed(halves.size()),bath(halves.size());
    std::vector<std::int64_t> outdoor_negative(halves.size()),outdoor_good(halves.size());
    bool room_objects=geometry&&input.object_words_current&&(input.objects_complete&&input.objects.size()<=4096);
    bool yard=room_objects&&tuning&&bool(input.outdoor_good_terrain_cells),upkeep=tuning&&(input.objects_complete&&input.objects.size()<=4096);
    std::int64_t penalties=0,eligible=0;
    for(const auto& entity:input.objects){
        if(upkeep){
            const auto repair=entity.repair,dirty=entity.dirty;
            const auto a=entity.upkeep_definition_words[0],b=entity.upkeep_definition_words[1],c=entity.upkeep_definition_words[2];
            if(!repair||!dirty||!a||!b||!c)upkeep=false;
            else{penalties+=*repair?input.tuning->broken_penalty:*dirty>0?input.tuning->dirty_penalty:0;if(*a||*b||*c)++eligible;}
        }
        if(!room_objects&&!yard)continue;
        const auto hidden=entity.hidden;
        if(!hidden){room_objects=yard=false;continue;}if(*hidden)continue;
        // World.GetValue(Room) returns OUT_OF_WORLD for unpositioned bodies.
        if(!entity.positioned)continue;
        const auto room=entity.room;
        if(!room){room_objects=yard=false;continue;}if(*room<0)continue;
        if(std::size_t(*room)>=map.rooms.size()){room_objects=yard=false;continue;}
        if(*room==0)continue;
        const auto category=entity.category;
        if(!category)room_objects=false;
        else{if(*category==4)bed[*room]=true;else if(*category>=5&&*category<8)bath[*room]=true;}
        if(map.rooms[*room].outside&&yard){
            const auto impact=entity.impact;
            if(!impact)yard=false;else if(*impact<0)outdoor_negative[*room]+=*impact;else ++outdoor_good[*room];
        }
    }
    if(room_objects){
        std::uint32_t bedrooms=0,bathrooms=0;
        for(std::size_t id=1;id<map.rooms.size();++id)if(!map.rooms[id].outside){bedrooms+=bed[id];bathrooms+=bath[id];}
        out.bedrooms=bedrooms;out.bathrooms=bathrooms;
    }
    if(yard){
        float sum=0;
        for(std::size_t id=1;id<map.rooms.size();++id)if(map.rooms[id].outside&&halves[id]){
            const auto terrain=input.outdoor_good_terrain_cells(static_cast<std::uint16_t>(id));
            if(!terrain||*terrain>std::size_t(map.width)*map.height){yard=false;break;}
            const float value=float(outdoor_negative[id])*input.tuning->outdoor_negative_impact_factor+
                float(outdoor_good[id]+*terrain)*input.tuning->outdoor_good_count_factor;
            sum+=value;
        }
        if(yard)out.ratings[2]=rating(sum*input.tuning->yard_multiplier);
    }
    if(upkeep)out.ratings[3]=eligible?rating(100.0f*std::clamp(float(eligible-penalties)/float(eligible),0.0f,1.0f)):
        std::optional<std::int16_t>{0};
    return out;
}
} // namespace odh_host
