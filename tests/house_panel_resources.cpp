// SPDX-License-Identifier: MPL-2.0
#include "house_panel_resources.hpp"
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace odh_host;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void put(std::string& s,std::uint32_t value,unsigned width=4){for(unsigned i=0;i<width;++i)s.push_back(static_cast<char>(value>>(8*i)));}
void at(std::string& s,unsigned offset,std::uint32_t value,unsigned width,bool big=false){for(unsigned i=0;i<width;++i)s.at(offset+i)=static_cast<char>(value>>(8*(big?width-i-1:i)));}
std::string named(const std::vector<std::pair<std::string,float>>& rows){
    std::string s;put(s,0);put(s,1);s+="SNCF";put(s,static_cast<unsigned>(rows.size()));
    for(const auto& row:rows){s+=row.first;s.push_back(0);if(row.first.size()%2==0)s.push_back(0);
        std::uint32_t bits;std::memcpy(&bits,&row.second,4);put(s,bits);s.push_back(0);s.push_back(0);}return s;
}
std::string chunk(const char* type,unsigned id,const std::string& payload){
    std::string s(76,'\0');s.replace(0,4,type);at(s,4,static_cast<unsigned>(76+payload.size()),4,true);at(s,8,id,2,true);return s+payload;
}
std::string iff(const std::vector<std::string>& chunks){std::string s="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";s.resize(64,'\0');for(const auto& c:chunks)s+=c;return s;}
std::string curves(){
    std::string s;put(s,0xffff,2);put(s,2,2);s+="(0;0) (100;100)";s.push_back(0);s+="(0;100) (10;0)";s.push_back(0);return s;
}
HousePanelResources read(const std::string& bytes,HousePanelResourceLimits limits={}){
    std::istringstream stream(bytes,std::ios::binary);return read_house_panel_resources(odh::View(stream,bytes.size()),limits);
}
template<class F>void reject(F function){bool failed=false;try{function();}catch(const odh::FormatError&){failed=true;}need(failed,"bad selected House resources admitted");}
void cases(){
    const auto house=named({{"yard score multiplier",.33f},{"upkeep penalty per dirty object",3},{"upkeep penalty per broken object",6},{"fill value for layout history",200}});
    const auto room=named({{"outdoor room impact factor",2},{"outdoor good object count factor",1}});
    const auto bytes=iff({chunk("FCNS",4,house),chunk("FCNS",3,room),chunk("STR#",505,curves())});
    const auto resources=read(bytes);
    need(resources.tuning.yard_multiplier==.33f&&resources.tuning.dirty_penalty==3&&resources.tuning.broken_penalty==6&&resources.new_layout_history_fill==200&&
        resources.tuning.outdoor_negative_impact_factor==2&&resources.tuning.outdoor_good_count_factor==1,"selected values supersede compiled6/15 penalties");
    for(auto flag:resources.defaulted)need(!flag,"present selected keys never labelled defaulted");
    need(resources.size_curve&&resources.furnishings_curve&&resources.size_curve->point(50)==50&&resources.furnishings_curve->point(5)==50,"owned real Global505 curves remain usable after View destruction");
    const auto absent=read(iff({}));
    need(absent.tuning.dirty_penalty==6&&absent.tuning.broken_penalty==15&&absent.new_layout_history_fill==200&&!absent.size_curve&&!absent.furnishings_curve,
        "validated absent table authorizes exact compiled defaults, not invented curves");
    for(auto flag:absent.defaulted)need(flag,"known absent selected resources report per-key fallback authority");
    const auto partial=read(iff({chunk("FCNS",4,named({{"upkeep penalty per dirty object",9},{"yard score multiplier ",.7f}}))}));
    need(partial.tuning.dirty_penalty==9&&partial.tuning.yard_multiplier==.33f&&
        !partial.defaulted[static_cast<unsigned>(HousePanelTuningKey::dirty_penalty)]&&partial.defaulted[static_cast<unsigned>(HousePanelTuningKey::yard_multiplier)],
        "exact key spaces are preserved and only individually absent keys default");
    const auto empty=read(iff({chunk("FCNS",4,named({}))}));need(empty.tuning.dirty_penalty==6,"validated empty table permits missing-key default");
    for(const auto value:{-1.f,3.5f,2147483648.f})reject([&]{read(iff({chunk("FCNS",4,named({{"upkeep penalty per dirty object",value}}))}));});
    reject([&]{read(iff({chunk("FCNS",4,named({{"yard score multiplier",-1}}))}));});
    reject([&]{read(iff({chunk("FCNS",4,house),chunk("FCNS",4,house)}));});
    reject([&]{read(iff({chunk("FCNS",3,room),chunk("FCNS",3,room)}));});
    reject([&]{read(iff({chunk("FCNS",4,"broken")}));});
    reject([&]{read(iff({chunk("FCNS",4,named({{"upkeep penalty per dirty object",3},{"upkeep penalty per dirty object",9}}))}));});
    reject([&]{read(iff({chunk("STR#",505,curves()),chunk("STR#",505,curves())}));});
    reject([&]{read(iff({chunk("STR#",505,"bad")}));});
    HousePanelResourceLimits limit;limit.iff_bytes=bytes.size()-1;reject([&]{read(bytes,limit);});
    limit={};limit.chunks=2;reject([&]{read(bytes,limit);});
    limit={};limit.named.records=1;reject([&]{read(bytes,limit);});
    // One full cell, one diagonal cell; a second story reuses the same XY
    // pools grid but owns different room identities. Only pool byte<16 counts.
    odh::SavedArchitecture architecture;architecture.width=architecture.height=2;architecture.pools=std::vector<std::uint8_t>{0,15,16,255};
    odh::GeometricRoomMap map;map.width=map.height=2;map.rooms.resize(5);
    for(unsigned i=0;i<5;++i){map.rooms[i].id=static_cast<std::uint16_t>(i);map.rooms[i].level=static_cast<std::uint8_t>(i>=3?1:0);}
    const auto packed=[](unsigned a,unsigned b){return a|(b<<16);};
    map.levels[0]={packed(1,1),packed(1,2)|0x80000000u,packed(2,2),packed(0,0)};
    map.levels[1]={packed(3,3),packed(3,4),packed(4,4),packed(0,0)};
    const auto counts=house_pool_good_cells(architecture,map);
    need(counts&&*counts==std::vector<std::uint32_t>{0,2,1,2,1},"pool threshold/dedup/diagonal mask/story-independent XY lookup");
    architecture.pools->at(0)=16;
    need(*house_pool_good_cells(architecture,map)==std::vector<std::uint32_t>{0,1,1,1,1},"current pool changes are observed without loading saved baseline");
    auto no_pool=architecture;no_pool.pools.reset();need(!house_pool_good_cells(no_pool,map),"missing current ARRY9 stays unavailable");
    for(unsigned bad=0;bad<5;++bad){auto a=architecture;auto m=map;
        if(bad==0)a.pools->pop_back();
        if(bad==1)m.width=3;
        if(bad==2)m.levels[1].pop_back();
        if(bad==3)m.rooms[1].id=2;
        if(bad==4)m.levels[1][0]=packed(1,1);
        reject([&]{house_pool_good_cells(a,m);});
    }
    map.levels[0][3]=packed(65535,65535);reject([&]{house_pool_good_cells(architecture,map);}); // Even a nongood cell must validate IDs.
}
}
int main(){try{cases();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
