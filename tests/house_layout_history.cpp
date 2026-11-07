// SPDX-License-Identifier: MPL-2.0
#include "odh/house_layout_history.hpp"
#include <climits>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void word(std::string& s,std::uint32_t n){for(unsigned k=0;k<4;++k)s.push_back(char(n>>(k*8)));}
template<class F>void rejects(F f){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error("invalid original history accepted");}
std::optional<odh::HouseLayoutHistory> read(const std::string& s){std::istringstream stream(s);return odh::read_house_layout_history(odh::View(stream,s.size()));}
void actor(odh::World& w,std::int16_t id){
    need(w.create(id,odh::EntityKind::avatar,{0,88,0,16})&&w.declare_object_word(id,34,0),"create complete live avatar bank");
}
}
int main(){try{
    std::string payload;word(payload,0);word(payload,0x3e);payload+="SUOH";payload.append(2,'\0');
    for(unsigned i=0;i<6;++i){word(payload,i);word(payload,20+i);}
    auto imported=read(payload);need(imported&&imported->current.samples==25&&imported->current.routing==5&&!imported->last_sample_tick,
        "original HOUS imports twelve actual counters without inventing a native sample stamp");
    auto truncated=payload;truncated.pop_back();rejects([&]{read(truncated);});
    auto unsupported=payload;unsupported[4]=char(0x50);rejects([&]{read(unsupported);});
    auto negative=payload;for(unsigned n=14;n<18;++n)negative[n]=char(255);rejects([&]{read(negative);});
    auto legacy=payload;legacy[4]=char(0x25);need(!read(legacy),"old original payload without owned history stays unknown");
    auto history=odh::new_house_layout_history(200);need(history.past[0].samples==200&&history.past[4].routing==200&&history.current.samples==0,
        "new fill policy is explicit and differs from imported or missing history");
    odh::World world;actor(world,1);actor(world,2);actor(world,3);
    need(world.set_person_data(3,32,1),"third avatar is a visitor, not a resident sample");
    unsigned queries=0;
    const auto unknown_second=[&](odh::WorldEntityReference ref)->std::optional<bool>{++queries;return ref.id==1?std::optional<bool>{true}:std::nullopt;};
    need(!odh::sample_house_layout_history(history,world,10,unknown_second)&&queries==2&&history.current.samples==0&&!history.last_sample_tick,
        "unknown second routing state cannot publish the first avatar's partial counter update");
    bool threw=false;
    try{odh::sample_house_layout_history(history,world,10,[](auto)->std::optional<bool>{throw std::runtime_error("unavailable route authority");});}
    catch(const std::runtime_error&){threw=true;}
    need(threw&&history.current.samples==0&&!history.last_sample_tick,"throwing authority cannot publish counters or sample stamp");
    need(world.set_motive(2,11,-1),"second resident sleeps");
    const auto routing=[&](odh::WorldEntityReference ref)->std::optional<bool>{need(ref.id==1&&ref.generation==world.find(1)->generation,"query exact live resident");return true;};
    need(odh::sample_house_layout_history(history,world,10,routing)&&history.current.samples==1&&history.current.routing==1&&history.last_sample_tick==10,
        "only awake visible resident with an actual routing record contributes");
    const auto before=history;
    need(!odh::sample_house_layout_history(history,world,10,routing)&&!odh::sample_house_layout_history(history,world,0,routing)&&
        history.current.samples==before.current.samples,"duplicate and backward sample ticks preserve history");
    need(odh::sample_house_layout_history(history,world,11,{})&&history.last_sample_tick==10,"non-sample frame does not consult missing route authority");
    odh::rotate_house_layout_history(history);
    need(history.past[0].samples==1&&history.past[0].routing==1&&history.past[4].samples==200&&
        history.current.samples==0&&history.last_sample_tick==10,"daily rotation preserves exact forward-copy behavior and sample stamp");
    need(odh::sample_house_layout_history(history,world,20,[](auto)->std::optional<bool>{return false;}),"next original sample accepts known no-route state");
    odh::rotate_house_layout_history(history);
    need(history.past[0].samples==1&&history.past[0].routing==0&&history.past[4].samples==1&&history.past[4].routing==1,
        "second daily event propagates previous first pair, rather than replacing the algorithm with an idealized queue");
    history.current={INT32_MAX,0};
    need(!odh::sample_house_layout_history(history,world,30,routing)&&history.current.samples==INT32_MAX&&history.last_sample_tick==20,
        "counter overflow cannot change counters or stamp");
    history.last_sample_tick=21;need(!odh::house_layout_history_valid(history),"malformed sample cadence is invalid portable state");
    std::cout<<"PASS portable House history import, atomic samples and original daily rotation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
