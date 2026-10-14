#include "odh/relationship.hpp"
#include <cstdlib>
#include <iostream>
#include <new>

namespace { int fail_after=-1; }
void* operator new(std::size_t size) {
    if (fail_after==0) {fail_after=-1;throw std::bad_alloc();}
    if (fail_after>0) --fail_after;
    if (auto p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new(std::size_t size,const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);}
namespace {
int checks=0;
void require(bool value,const char* why) {++checks;if (!value) throw std::runtime_error(why);}
odh::NeighborContext neighbors() {
    odh::NeighborRecord record{};record.version=4;record.neighbor_id=9;record.relationships={{1,70000,{123,65535}}};
    return odh::NeighborContext(odh::Nbrs{0,62,{{1,record}},0});
}
void setup(odh::World& w) {
    for (const auto id : {1,2,3}) require(w.create(static_cast<std::int16_t>(id),odh::EntityKind::object),"create source");
}
void semantics() {
    odh::World world;setup(world);
    auto nbrs=neighbors();odh::RelationshipState state(nbrs);
    const std::vector<odh::SavedObjectRelationships> raw{{1,{{65538,{70000,65535,65536,-65537,32768,2147483647}},{-1,std::vector<std::int32_t>(300,0x1234FFFF)},{99,{}}}},{2,{}}};
    require(state.import_object_relationships(world,raw),"import saved OBJECT batch");
    require(state.source_count()==3&&state.pair_count()==4&&state.value_count()==308,"count NBRS and empty bound OBJECT source, pairs and tails");
    const std::int16_t expected[]{4464,-1,0,-1,-32768,-1};
    for (std::size_t i=0;i<6;++i) require(state.object_value(world,1,2,i)==expected[i],"signed low16 values without clamp");
    require(state.object_value(world,1,-1,299)==-1&&!world.find(-1),"missing ushort target retained with tail");
    require(!state.object_value(world,1,99,0)&&!world.find(99),"empty pair with absent target valid");
    require(raw[0].relationships[0].target==65538&&raw[0].relationships[0].values[0]==70000,"raw input retained");
    require(state.value(9,70000,0)==123&&state.value(9,70000,1)==-1&&!state.value(9,4464,0),"NBRS raw target and values preserved");
    require(!state.import_object_relationships(world,raw)&&state.source_count()==3,"already bound generation rejected");
    require(!state.import_object_relationships(world,{{2,{}}}),"empty binding repeat rejected");
    odh::VmFrame frame{};frame.context.caller=1;frame.context.stack_object=2;
    const odh::Instruction instruction{26,254,255,{0,0,0,0,8,0,0,0}};
    auto primitive=state.primitive(world);
    const auto outcome=primitive(frame,instruction,{},{});
    require(outcome.disposition==odh::PrimitiveDisposition::goto_true&&outcome.temp_write&&outcome.temp_write->value==4464,"primitive reads imported OBJECT value");
    const auto generation=world.find(1)->generation;
    require(world.remove(1)&&world.create(1,odh::EntityKind::object)&&world.find(1)->generation!=generation,"reused source receives new generation");
    require(!state.object_value(world,1,2,0),"old source binding hidden after ID reuse");
    require(state.import_object_relationships(world,{{1,{{2,{42}}}}})&&state.object_value(world,1,2,0)==42,"new generation can bind without replacing old");
    require(state.source_count()==4&&state.pair_count()==5&&state.value_count()==309,"old generations still count");
}
void validation() {
    odh::World world;setup(world);odh::RelationshipState state;
    const std::vector<std::vector<odh::SavedObjectRelationships>> invalid{
        {{1,{{2,{10}}}},{99,{}}},
        {{1,{{2,{10}}}},{1,{}}},
        {{1,{{2,{10}}}},{2,{{-1,{1}},{65535,{2}}}}},
        {{1,{{2,{10}},{65538,{20}}}}}};
    for (const auto& batch : invalid) {
        require(!state.import_object_relationships(world,batch),"invalid batch rejects");
        require(state.source_count()==0&&state.pair_count()==0&&state.value_count()==0&&!state.object_value(world,1,2,0),"batch failure publishes nothing");
    }
    require(state.import_object_relationships(world,{}),"empty batch accepted");
    auto nbrs=neighbors();
    for (const auto limits : {odh::RelationshipLimits{1,8,8},odh::RelationshipLimits{8,1,8},odh::RelationshipLimits{8,8,2}}) {
        odh::RelationshipState bounded(nbrs,limits);
        require(!bounded.import_object_relationships(world,{{1,{{2,{1}}}}}),"combined NBRS/OBJECT budget rejects");
        require(bounded.source_count()==1&&bounded.pair_count()==1&&bounded.value_count()==2&&bounded.value(9,70000,0)==123,"NBRS and counts unchanged after budget failure");
    }
    odh::RelationshipState exact(nbrs,{3,3,5});
    require(exact.import_object_relationships(world,{{1,{{2,{1,2}},{99,{3}}}},{2,{}}}),"exact combined budgets allowed");
    require(exact.source_count()==3&&exact.pair_count()==3&&exact.value_count()==5,"exact counts");
    require(!exact.import_object_relationships(world,{{3,{}}}),"empty source consumes source budget");
    odh::RelationshipState cumulative({3,2,3});
    require(cumulative.import_object_relationships(world,{{1,{{2,{1,2}}}}}),"first batch fits");
    require(!cumulative.import_object_relationships(world,{{2,{{3,{3,4}}}}})&&cumulative.object_value(world,1,2,1)==2&&cumulative.source_count()==1,"successive batches share actual value budget");
    require(cumulative.import_object_relationships(world,{{2,{{3,{3}}}}}),"remaining exact budget fits");
}
void allocation_atomicity() {
    odh::World world;setup(world);auto nbrs=neighbors();odh::RelationshipState state(nbrs);
    const std::vector<odh::SavedObjectRelationships> batch{{1,{{2,{101,102}},{65535,{103}}}},{2,{}}};
    bool complete=false;
    for (int allocation=0;allocation<128;++allocation) {
        fail_after=allocation;bool threw=false;
        try {complete=state.import_object_relationships(world,batch);}catch (const std::bad_alloc&) {threw=true;}
        fail_after=-1;
        if (complete) break;
        require(threw&&state.source_count()==1&&state.pair_count()==1&&state.value_count()==2&&!state.object_value(world,1,2,0)&&state.value(9,70000,0)==123,"allocation failure at every staging point preserves whole state");
    }
    require(complete&&state.object_value(world,1,2,1)==102&&state.source_count()==3,"eventual publication after allocation failures");
}
}
int main() {
    try {semantics();validation();allocation_atomicity();std::cout<<"relationship_import: "<<checks<<" checks passed\n";return 0;}
    catch (const std::exception& error) {std::cerr<<"relationship_import: "<<error.what()<<'\n';return 1;}
}
