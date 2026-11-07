#include "odh/avatar_tick.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::int16_t counter(const odh::World& world,std::int16_t id){return world.find(id)->person_data[27];}
}
int main(){try{
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,28})&&
            world.create(2,odh::EntityKind::object)&&
            world.create(3,odh::EntityKind::avatar,{0,88})&&
            world.create(4,odh::EntityKind::avatar,{0,28}),"mixed entity fixture");
    require(world.set_position(1,{0,0,1,0})&&world.set_ui_ghost(4,true)&&
            world.set_person_data(1,27,32767)&&world.set_person_data(3,27,-1),"counter seeds");
    require(!world.find(3)->position,"ordinary out-of-world avatar");
    const auto words=world.word_count();
    require(odh::tick_ts1_avatar_counters(world)&&counter(world,1)==-32768&&counter(world,3)==0&&counter(world,4)==1,
            "one tick advances positioned, OOW and ghost avatars with source short wrap");
    require(world.find(2)->person_data.empty()&&world.word_count()==words,"objects and word budgets unchanged");
    odh::VmFrame frame{odh::routine_key(4096),0,{1,3,3},{},{}};
    require(world.write(frame,18,27,123)&&world.write(frame,19,27,456)&&
            odh::tick_ts1_avatar_counters(world)&&counter(world,1)==124&&counter(world,3)==457,
            "My/StackObject writable counter restarts from authored values");
    const auto bytes=odh::encode_world_snapshot(world.snapshot());odh::World resumed;
    require(resumed.restore_snapshot(odh::decode_world_snapshot(bytes))&&
            odh::encode_world_snapshot(resumed.snapshot())==bytes,"fresh restore never advances or rebases counters");
    for(unsigned i=0;i<201;++i)require(odh::tick_ts1_avatar_counters(world)&&odh::tick_ts1_avatar_counters(resumed),"continued logical ticks");
    require(odh::encode_world_snapshot(world.snapshot())==odh::encode_world_snapshot(resumed.snapshot())&&counter(world,3)==658,
            "restored continuous wait200 expires on same writable counter without new schema");
    const auto stable=odh::encode_world_snapshot(world.snapshot());
    require(!odh::tick_ts1_avatar_counters(world,2)&&odh::encode_world_snapshot(world.snapshot())==stable,
            "avatar budget rejected before any counter changes");
    require(!odh::tick_ts1_avatar_counters(world,4097)&&odh::encode_world_snapshot(world.snapshot())==stable,
            "unsupported service budget rejected atomically");
    require(world.create(5,odh::EntityKind::avatar,{0,27}),"late invalid avatar bank");
    const auto invalid=odh::encode_world_snapshot(world.snapshot());
    require(!odh::tick_ts1_avatar_counters(world)&&odh::encode_world_snapshot(world.snapshot())==invalid,
            "invalid later avatar cannot partially advance earlier counters");
    odh::WorldLimits limits;limits.words=56;odh::World full(limits);
    require(full.create(1,odh::EntityKind::avatar,{0,28})&&full.create(2,odh::EntityKind::avatar,{0,28})&&
            full.word_count()==56&&odh::tick_ts1_avatar_counters(full)&&counter(full,1)==1&&counter(full,2)==1,
            "counter update requires no spare word budget");
    odh::World empty;require(odh::tick_ts1_avatar_counters(empty,0),"empty world has no counter work");
    std::cout<<"TS1 avatar tick counter checks passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
