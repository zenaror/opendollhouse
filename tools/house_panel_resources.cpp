// SPDX-License-Identifier: MPL-2.0
#include "house_panel_resources.hpp"
#include <cmath>
#include <limits>

namespace odh_host {
HousePanelResources read_house_panel_resources(const odh::View& global,HousePanelResourceLimits limits){
    if(global.size()>limits.iff_bytes)throw odh::FormatError("House selected Global exceeds byte budget");
    std::optional<odh::NamedFloatConstants> room,house;std::optional<odh::StringTable> curves;
    odh::Limits iff_limits;iff_limits.iff_chunks=limits.chunks;
    odh::iff_chunks(global,[&](const odh::Chunk& chunk){
        if(chunk.type=="FCNS"&&(chunk.id==3||chunk.id==4)){
            auto& selected=chunk.id==3?room:house;
            if(selected)throw odh::FormatError("House selected Global contains duplicate FCNS");
            selected=odh::read_named_float_constants(chunk.payload,limits.named);
        }else if(chunk.type=="STR#"&&chunk.id==505){
            if(curves)throw odh::FormatError("House selected Global contains duplicate STR505");
            if(chunk.payload.size()>limits.curve.text_bytes)throw odh::FormatError("House STR505 exceeds text budget");
            curves=odh::read_strings(chunk.payload);
        }
    },iff_limits);
    HousePanelResources out;
    const auto value=[&](const std::optional<odh::NamedFloatConstants>& table,const char* name,float fallback,HousePanelTuningKey key){
        if(table)for(const auto& record:table->records)if(record.name==name)return record.value;
        out.defaulted[static_cast<std::size_t>(key)]=true;return fallback;
    };
    const auto integer=[](float input){
        if(input<0||double(input)>std::numeric_limits<std::int32_t>::max()||std::trunc(input)!=input)
            throw odh::FormatError("House integer tuning is not an exact nonnegative Int32");
        return static_cast<std::int32_t>(input);
    };
    using Key=HousePanelTuningKey;
    out.tuning.yard_multiplier=value(house,"yard score multiplier",.33f,Key::yard_multiplier);
    out.tuning.dirty_penalty=integer(value(house,"upkeep penalty per dirty object",6,Key::dirty_penalty));
    out.tuning.broken_penalty=integer(value(house,"upkeep penalty per broken object",15,Key::broken_penalty));
    out.new_layout_history_fill=integer(value(house,"fill value for layout history",200,Key::new_history_fill));
    out.tuning.outdoor_negative_impact_factor=value(room,"outdoor room impact factor",2,Key::outdoor_impact);
    out.tuning.outdoor_good_count_factor=value(room,"outdoor good object count factor",1,Key::outdoor_good_count);
    if(out.tuning.yard_multiplier<0||out.tuning.outdoor_negative_impact_factor<0||out.tuning.outdoor_good_count_factor<0)
        throw odh::FormatError("House score multipliers are negative");
    if(curves){
        if(curves->state!=odh::StringTableState::decoded)throw odh::FormatError("House STR505 is not decoded");
        if(const auto* entry=odh::string_in_language(*curves,1,0))out.size_curve=odh::parse_ts1_curve(entry->value,limits.curve);
        if(const auto* entry=odh::string_in_language(*curves,1,1))out.furnishings_curve=odh::parse_ts1_curve(entry->value,limits.curve);
    }
    return out;
}
std::optional<std::vector<std::uint32_t>> house_pool_good_cells(const odh::SavedArchitecture& current,const odh::GeometricRoomMap& map){
    if(!current.pools)return {};
    const auto cells=std::uint64_t(current.width)*current.height;
    if(!current.width||!current.height||current.width>64||current.height>64||map.width!=current.width||map.height!=current.height||
       current.pools->size()!=cells||map.rooms.empty()||map.rooms.size()>32768)
        throw odh::FormatError("House pool/current room map extents invalid");
    for(std::size_t id=0;id<map.rooms.size();++id)
        if(map.rooms[id].id!=id||map.rooms[id].level>=map.levels.size())throw odh::FormatError("House pool room identity invalid");
    std::vector<std::uint32_t> out(map.rooms.size(),0);
    for(std::size_t level=0;level<map.levels.size();++level){
        if(map.levels[level].size()!=cells)throw odh::FormatError("House pool room grid extent invalid");
        for(std::size_t cell=0;cell<cells;++cell){
            const auto packed=map.levels[level][cell];const std::uint32_t a=packed&0xffffu,b=(packed>>16)&0x7fffu;
            for(const auto id:{a,b})if(id>=map.rooms.size()||(id&&map.rooms[id].level!=level))
                throw odh::FormatError("House pool grid room membership invalid");
            if((*current.pools)[cell]<16){++out[a];if(b!=a)++out[b];}
        }
    }
    return out;
}
}
