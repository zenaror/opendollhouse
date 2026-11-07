// SPDX-License-Identifier: MPL-2.0
#include "odh/house_layout_history.hpp"
#include <algorithm>
#include <climits>
#include <utility>
namespace odh {
namespace {
std::int32_t signed32(std::uint32_t v){return v<=INT32_MAX?std::int32_t(v):std::int32_t(std::int64_t(v)-4294967296LL);}
bool valid(const HouseLayoutPair& p){return p.samples>=0&&p.routing>=0&&p.routing<=p.samples;}
bool history_valid(const HouseLayoutHistory& h){
    return (!h.last_sample_tick||*h.last_sample_tick%10==0)&&valid(h.current)&&std::all_of(h.past.begin(),h.past.end(),[](const auto& p){return valid(p);});
}
std::optional<std::int16_t> word(const Entity& e,std::int16_t field){
    const auto at=e.object_words.find(field);return at==e.object_words.end()?std::optional<std::int16_t>{}:at->second;
}
}
bool house_layout_history_valid(const HouseLayoutHistory& history) noexcept{return history_valid(history);}
std::optional<HouseLayoutHistory> read_house_layout_history(const odh::View& payload){
    const auto version=payload.le32(4);
    std::array<char,4> magic{};payload.read(8,magic.data(),4);
    if(magic!=std::array<char,4>{'S','U','O','H'})throw odh::FormatError("unsupported HOUS history signature");
    if(version<0x26)return {};
    switch(version){case 0x26:case 0x33:case 0x38:case 0x3e:case 0x3f:case 0x45:case 0x48:case 0x49:break;
        default:throw odh::FormatError("unsupported HOUS history version");}
    std::uint64_t at=12;std::size_t bytes=0;
    while(payload.u8(at++)!=0)if(++bytes>256)throw odh::FormatError("HOUS history name exceeds bound");
    if(at&1)++at;
    // Original41655e serializes B before A for each pair, including current.
    HouseLayoutHistory result;
    for(auto* pair:{&result.past[0],&result.past[1],&result.past[2],&result.past[3],&result.past[4],&result.current}){
        pair->routing=signed32(payload.le32(at));pair->samples=signed32(payload.le32(at+4));at+=8;
    }
    if(!history_valid(result))throw odh::FormatError("invalid HOUS history counters");
    return result;
}
HouseLayoutHistory new_house_layout_history(std::int32_t fill){
    if(fill<0)throw odh::FormatError("negative new House layout fill");
    HouseLayoutHistory result;for(auto& pair:result.past)pair={fill,fill};return result;
}
bool sample_house_layout_history(HouseLayoutHistory& history,const odh::World& world,std::uint64_t tick,
    const std::function<std::optional<bool>(odh::WorldEntityReference)>& routing_active){
    if(!history_valid(history))return false;
    if(tick%10)return true;
    if(history.last_sample_tick&&tick<=*history.last_sample_tick)return false;
    auto candidate=history;
    auto cursor=std::int16_t{-1};std::size_t count=0;
    while(const auto id=world.next_entity(cursor,odh::EntityKind::avatar)){
        cursor=*id;if(++count>4096)return false;
        const auto* entity=world.find(*id);if(!entity||entity->person_data.size()<=32)return false;
        if(entity->person_data[32]!=0)continue;
        const auto hidden=word(*entity,34);if(!hidden)return false;if(*hidden)continue;
        if(entity->motives.size()<=11)return false;if(entity->motives[11]<0)continue;
        if(!routing_active)return false;
        const auto routing=routing_active({entity->id,entity->generation});if(!routing)return false;
        if(candidate.current.samples==INT32_MAX||(*routing&&candidate.current.routing==INT32_MAX))return false;
        ++candidate.current.samples;if(*routing)++candidate.current.routing;
    }
    candidate.last_sample_tick=tick;history=std::move(candidate);return true;
}
void rotate_house_layout_history(HouseLayoutHistory& history){
    if(!history_valid(history))throw odh::FormatError("invalid House layout history");
    // Original416449 copies FORWARD through overlapping pairs. Preserve its
    // observed propagation of the old first pair, rather than "fixing" it.
    for(std::size_t i=1;i<5;++i)history.past[i]=history.past[i-1];
    history.past[0]=history.current;history.current={};
}
} // namespace odh
