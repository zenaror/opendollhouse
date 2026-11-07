#include "odh/neighbor_inventory.hpp"
#include <iostream>

namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
odh::Instruction op(std::uint8_t mode,std::uint8_t type=6,std::uint8_t flags=0,std::uint32_t guid=42){
    return {51,254,255,{mode,type,flags,1,static_cast<std::uint8_t>(guid),static_cast<std::uint8_t>(guid>>8),
        static_cast<std::uint8_t>(guid>>16),static_cast<std::uint8_t>(guid>>24)}};
}
void run(){
    odh::Ngbh seed{};seed.inventory_by_id[-2]={{6,42,65535},{6,42,9},{-12,3,0}};
    odh::NeighborInventory inv(seed);odh::Ts1Temps t{};
    require(!inv.changed(),"unmodified imported authority preserves legacy saves");
    auto exec=[&](odh::Instruction i){return inv.execute(-2,i,t,42);};
    exec(op(0));require(inv.items(-2)->at(0).count==0&&inv.items(-2)->at(1).count==9,"add wraps first exact duplicate count");
    auto r=exec(op(3,6,16));require(r.disposition==odh::PrimitiveDisposition::goto_false&&r.temp_write->value==0&&r.second_temp_write->value==0,"zero count found and index outputs remain on false");
    t[0]=-2;exec(op(0,6,2));require(inv.items(-2)->at(0).count==65534,"signed count converts to ushort");
    t[0]=-1;exec(op(1,0,2));require(inv.items(-2)->at(0).count==9&&inv.items(-2)->size()==2,"remove all wildcard erases first only");
    t[0]=10;r=exec(op(1,6,2));require(r.disposition==odh::PrimitiveDisposition::goto_false&&inv.items(-2)->at(0).count==9,"named removal rejects short count");
    t[1]=0;exec(op(2,6,128|2));require(inv.items(-2)->at(0).count==65535,"indexed removal underflows without count guard");
    t[0]=-1;r=exec(op(2,6,128|2));require(r.temp_write->index==1&&r.temp_write->value==-1&&inv.items(-2)->size()==1,"indexed all removal stages prior index");
    inv.set_data(-2,0,123);inv.set_data(-2,1,321);t[0]=88;
    exec(op(5));require(inv.get_data(-2,0)==1&&!inv.get_data(-2,10),"pinned autofollow collision uses count and GUID0");
    exec(op(6,6,2));require(inv.get_data(-2,1)==88&&!inv.get_data(-2,11),"pinned followhome collision uses GUID1");
    inv.consume_data(-2,0);exec(op(5));require(inv.get_data(-2,10)==88,"absent collision appends actual companion key");
    inv.set_data(20,7,65500);require(inv.consume_data(20,7)==65500&&!inv.get_data(20,7),"travel data consume keeps raw signed bits");
    odh::Ngbh ordered{};ordered.inventory_by_id[1]={{6,1,40000},{2,9,1},{6,2,40000},{6,2,3}};
    odh::NeighborInventory next(ordered);t[1]=-1;r=next.execute(1,op(4,6,128),t,42);
    require(r.temp_write->value==14467&&r.second_temp_write->value==0,"next type sums all raw counts with signed wrap");
    t[1]=3;r=next.execute(1,op(4,6,128),t,42);require(r.disposition==odh::PrimitiveDisposition::goto_false&&r.temp_write&&!r.second_temp_write,"exhausted next still writes sum");
    const auto before=next.snapshot();auto corrupt=before;corrupt.version=2;
    try{next.restore(corrupt);throw std::runtime_error("accepted bad version");}catch(const odh::FormatError&){}
    require(next.items(1)->at(0).count==40000,"failed restore keeps live state");
    odh::NeighborInventory small(odh::Ngbh{}, {1,1});small.set_data(-32768,0,32768);
    try{small.set_data(2,0,1);throw std::runtime_error("accepted resource overflow");}catch(const odh::FormatError&){}
    require(small.snapshot().inventory_by_id.size()==1&&small.get_data(-32768,0)==32768,"failed growth atomic and signed keys preserved");
    auto oversized=small.snapshot();oversized.inventory_by_id[-32768].push_back({-2147483647,0xffffffffU,0});
    try{small.restore(oversized);throw std::runtime_error("accepted oversized snapshot");}catch(const odh::FormatError&){}
    require(small.items(-32768)->size()==1&&small.get_data(-32768,0)==32768,"failed oversized restore preserves exact authority");
    odh::World world;require(world.create(1,odh::EntityKind::avatar,{0,32,0})&&world.set_person_data(1,31,-2),"live caller");
    require(world.create(2,odh::EntityKind::object)&&world.define_type(2,42),"stack identity");
    auto primitive=odh::neighbor_inventory_primitive(inv,world);odh::VmFrame f{odh::routine_key(4096),0,{1,2,2,0},{},{}};
    r=primitive(f,op(0),t,{});require(r.disposition==odh::PrimitiveDisposition::goto_true,"actual world consumer mutation");
    r=primitive(f,op(3),t,{});require(r.temp_write&&r.temp_write->value==1,"actual world query sees same authority");
    r=primitive(f,op(8),t,{});require(r.disposition==odh::PrimitiveDisposition::unsupported,"source unknown no-op not accepted as parity");
}
}
int main(){try{run();std::cout<<"neighbor inventory checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
