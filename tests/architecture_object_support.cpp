// SPDX-License-Identifier: MPL-2.0
#include "odh/architecture_object_support.hpp"
#include "odh/architecture_edit.hpp"
#include "odh/formats.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
template<class F> void malformed(F f,const char* why){bool denied=false;try{f();}catch(const odh::FormatError&){denied=true;}require(denied,why);}
}
int main(){
    using namespace odh;
    auto support=make_architecture_object_support(8,8);
    require(support.levels[0].size()==64&&support.levels[1].size()==64,"new Architecture owns two explicit false grids");
    World world;
    for(std::int16_t id:{1,2}){
        require(world.create(id,EntityKind::object,{0,0,1}),"create actual category8 object");
        require(world.declare_object_word(id,59,8)&&world.set_position(id,{40,56,1,0}),"position actual source object");
    }
    const auto event=[&](std::int16_t id,bool set){const auto* e=world.find(id);return set_architecture_object_supported(support,world,{id,e->generation},*e->position,set,[]{return true;});};
    require(event(1,true)&&support.levels[0][26]==1,"source event sets floor-divided tile");
    require(event(2,true)&&event(1,false)&&support.levels[0][26]==0,"overlapping supports use boolean assignment, not occupancy counts");
    require(world.set_position(2,{40,56,2,0})&&event(2,true)&&support.levels[1][26]==1&&support.levels[0][26]==0,"story event addresses its own grid");
    const auto original=support;const auto* e=world.find(2);
    require(!set_architecture_object_supported(support,world,{2,e->generation+1},*e->position,false,[]{return true;})&&support==original,"recycled identity cannot mutate history");
    require(!set_architecture_object_supported(support,world,{2,e->generation},*e->position,false,[]{return false;})&&support==original,"foreign event cannot mutate history");
    auto wrong=*e->position;++wrong.x;
    require(!set_architecture_object_supported(support,world,{2,e->generation},wrong,false,[]{return true;})&&support==original,"event pose must match live pose");
    VmFrame frame;frame.context.caller=2;require(world.write(frame,3,59,7),"change actual category");
    require(!event(2,false)&&support==original,"noncategory8 event is rejected");
    require(world.write(frame,3,59,8)&&world.attach(1,0,2),"contain source object");
    require(!event(2,false)&&support==original,"contained object returns before SetObjectSupported");
    require(world.detach(1,0)&&world.set_ui_ghost(2,true),"make actual preview ghost");
    require(!event(2,false)&&support==original,"preview ghost cannot produce architectural support");
    require(world.set_ui_ghost(2,false)&&world.place_out_of_world(2),"take object out of world");
    require(!set_architecture_object_supported(support,world,{2,e->generation},{-32768,-32768,1,0},false,[]{return true;})&&support==original,"out-of-world object cannot produce support");
    auto bad=support;bad.levels[1][0]=2;malformed([&]{validate_architecture_object_support(bad);},"unknown support marker rejected");
    bad=support;bad.levels[0].pop_back();malformed([&]{validate_architecture_object_support(bad);},"partial grid rejected");
    malformed([]{make_architecture_object_support(65,65);},"allocation extent bounded before allocation");
    malformed([]{make_architecture_object_support(8,7);},"extent matches prepared TS1 square domain");
    PreparedArchitecture lot;lot.width=8;lot.height=8;lot.buildable.assign(64,1);
    for(auto& level:lot.levels){level.floors.assign(64,0);level.walls.resize(64);}
    const auto rooms=generate_prepared_room_map(lot);
    require(event(1,true),"ground support event succeeds after overlapping clear");
    const auto regenerated=regenerate_architecture_support(lot,rooms,support.levels[0]);
    require(regenerated&&(*regenerated)[26]==1&&(*regenerated)[27]==0,"event-owned grid feeds actual upper-floor support derivation");
    require(event(1,false),"source removal clears ground support");
    const auto cleared=regenerate_architecture_support(lot,rooms,support.levels[0]);
    require(cleared&&(*cleared)[26]==0,"source removal invalidates support above column");
    std::cout<<"architecture object support lifecycle passed\n";
}
