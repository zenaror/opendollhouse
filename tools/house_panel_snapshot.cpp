// SPDX-License-Identifier: MPL-2.0
#include "house_panel_snapshot.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace odh_host {
namespace {
std::optional<std::int16_t> word(const odh::Entity& e,std::int16_t field){
    const auto at=e.object_words.find(field);return at==e.object_words.end()?std::optional<std::int16_t>{}:at->second;
}
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
HousePanelSnapshot prepare_house_panel_snapshot(const HousePanelInput& input){
    HousePanelSnapshot out;
    const auto& map=input.rooms;
    const bool tuning=input.tuning&&tuning_valid(*input.tuning);
    if(input.valuation&&input.valuation->status==odh::PropertyValuationStatus::resolved&&
        input.valuation->objects.primary&&input.valuation->combined_architecture&&input.furnishings_curve){
        const float denominator=float(*input.valuation->objects.primary);
        const float value=denominator==0?100.0f:float(*input.valuation->combined_architecture)/denominator;
        out.ratings[1]=rating(input.furnishings_curve->point(value));
    }
    if(input.object_assets&&input.architecture_value&&input.furnishings_curve&&
        input.object_assets->status==odh::PropertyObjectAssetsStatus::resolved&&
        input.architecture_value->status==odh::PropertyArchitectureValueStatus::resolved&&
        input.object_assets->primary&&input.object_assets->architecture_objects){
        const auto combined=std::int64_t(input.architecture_value->total)+*input.object_assets->architecture_objects;
        if(combined>=INT32_MIN&&combined<=INT32_MAX){
            const float denominator=float(*input.object_assets->primary);
            const float value=denominator==0?100.0f:float(combined)/denominator;
            out.ratings[1]=rating(input.furnishings_curve->point(value));
        }
    }
    if(input.layout_history&&odh::house_layout_history_valid(*input.layout_history)){
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
    const auto object_word=[&](const odh::Entity& entity,std::int16_t field)->std::optional<std::int16_t>{
        if(input.bindings.object_word)return input.bindings.object_word(entity,field);
        return field==29?std::optional<std::int16_t>{}:word(entity,field);
    };
    std::vector<bool> bed(halves.size()),bath(halves.size());
    std::vector<std::int64_t> outdoor_negative(halves.size()),outdoor_good(halves.size());
    bool room_objects=geometry&&bool(input.bindings.object_word);
    bool yard=room_objects&&tuning&&bool(input.bindings.outdoor_good_terrain_cells),upkeep=tuning;
    std::int64_t penalties=0,eligible=0;std::size_t entity_count=0;
    auto cursor=std::int16_t{-1};
    while(const auto id=input.world.next_entity(cursor)){
        cursor=*id;if(++entity_count>4096){room_objects=yard=upkeep=false;break;}
        const auto* entity=input.world.find(*id);if(!entity){room_objects=yard=upkeep=false;break;}
        if(upkeep){
            const auto repair=object_word(*entity,15),dirty=object_word(*entity,39);
            const auto& resolver=input.bindings.definition_word;
            const auto a=resolver?resolver(*entity,39):std::optional<std::int16_t>{};
            const auto b=resolver?resolver(*entity,40):std::optional<std::int16_t>{};
            const auto c=resolver?resolver(*entity,69):std::optional<std::int16_t>{};
            if(!repair||!dirty||!a||!b||!c)upkeep=false;
            else{penalties+=*repair?input.tuning->broken_penalty:*dirty>0?input.tuning->dirty_penalty:0;if(*a||*b||*c)++eligible;}
        }
        if(!room_objects&&!yard)continue;
        const auto hidden=object_word(*entity,34);
        if(!hidden){room_objects=yard=false;continue;}if(*hidden)continue;
        // World.GetValue(Room) returns OUT_OF_WORLD for unpositioned bodies.
        if(!entity->position)continue;
        const auto room=object_word(*entity,29);
        if(!room){room_objects=yard=false;continue;}if(*room<0)continue;
        if(std::size_t(*room)>=map.rooms.size()){room_objects=yard=false;continue;}
        if(*room==0)continue;
        const auto category=object_word(*entity,59);
        if(!category)room_objects=false;
        else{if(*category==4)bed[*room]=true;else if(*category>=5&&*category<8)bath[*room]=true;}
        if(map.rooms[*room].outside&&yard){
            const auto impact=object_word(*entity,7);
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
            const auto terrain=input.bindings.outdoor_good_terrain_cells(static_cast<std::uint16_t>(id));
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
