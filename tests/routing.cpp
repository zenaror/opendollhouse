#include "odh/routing.hpp"
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool v,const char* message) {++checks;if(!v)throw std::runtime_error(message);}
odh::SlotTable slot() {
    odh::SlotRecord r{};r.type=3;r.standing=1;r.flags=0x55;r.snap_target_slot=-1;
    r.proximity=std::array<std::int32_t,3>{4,4,4};r.max_size=0;r.unknown_i10=0;
    r.gradient_bits=0x3E400000;r.height=0;r.facing=-2;r.resolution=16;
    return {0,10,{r},0};
}
odh::RoutineSet routines() {
    return {{odh::routine_key(4096),odh::Bhav{0x8002,{{45,254,255,{0,0,2,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
}
struct Fixture {
    odh::World world;odh::RouteGrid grid{64,32};odh::SlotTable slots=slot();
    odh::Routing routing{world,grid,slots};
    odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive()};
    Fixture() {
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
        world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});routing.bind(vm);
    }
    void start() {vm.start(odh::routine_key(4096),{1,2,2});vm.run(10);}
};
}
int main() {
    try {
        {Fixture f;f.start();require(f.vm.status()==odh::VmStatus::waiting,"route dispatch waits");
         require(f.world.find(1)->position->x==4,"planning does not teleport actor");
         require(f.routing.tick(1) && f.world.find(1)->position->x==5,"first tick advances one unit");
         require(!f.routing.tick(1) && !f.routing.tick(0) && !f.routing.tick(3) &&
                 f.world.find(1)->position->x==5,"duplicates/backwards/gaps do not consume movement");
         std::size_t movements=1;
         for(std::uint64_t tick=2;tick<=20;++tick) require(f.routing.tick(tick,[&](const auto&){++movements;}),"consecutive tick accepted");
         const auto& p=*f.world.find(1)->position;
         require(p.x==24 && p.y==12 && p.direction==2 && movements==20,"arrives and faces object");
         require(f.world.find(1)->person_data[37]==64 && f.vm.status()==odh::VmStatus::returned_true &&
                 !f.routing.active(),"arrival updates route flags and completes exact wait");}
        {Fixture f;f.start();f.vm.start(odh::routine_key(4096),{1,2,2});
         require(!f.routing.tick(1) && f.world.find(1)->position->x==4 && !f.routing.active(),"restart cancels before movement");
         require(f.vm.status()==odh::VmStatus::running && f.vm.steps()==0,"old job cannot complete new invocation");}
        {Fixture f;f.start();f.grid.block({5,12});
         require(f.routing.tick(1) && f.vm.status()==odh::VmStatus::returned_false &&
                 f.world.find(1)->position->x==4,"new obstacle invalidates next segment");}
        {Fixture f;f.start();f.world.set_position(2,{29,12,1,0});
         require(f.routing.tick(1) && f.vm.status()==odh::VmStatus::returned_false,"target movement cancels route");}
        {Fixture f;f.start();const auto generation=f.world.find(1)->generation;
         f.world.remove(1);f.world.create(1,odh::EntityKind::avatar,{0,88,0});f.world.set_position(1,{4,12,1,0});
         require(f.world.find(1)->generation!=generation && f.routing.tick(1) &&
                 f.world.find(1)->position->x==4 && f.vm.status()==odh::VmStatus::returned_false,"reused ID cannot receive old movement");}
        {Fixture f;f.start();f.world.remove(2);
         require(f.routing.tick(1) && f.vm.status()==odh::VmStatus::returned_false,"target removal cancels");}
        {Fixture f;f.slots.records[0].flags=0xFF;f.start();
         require(f.vm.status()==odh::VmStatus::fault && !f.routing.active(),"unsupported SLOT profile never becomes success");}
        {Fixture f;f.world.attach(2,0,1);f.start();
         require(f.vm.status()==odh::VmStatus::fault,"attached avatar cannot walk as a free actor");}
        {Fixture f;f.world.set_position(1,{4,12,2,0});f.start();
         require(f.vm.status()==odh::VmStatus::fault,"cross-floor route unsupported explicitly");}
        {Fixture f;f.grid.block({4,11});f.grid.block({4,13});f.grid.block({3,12});f.grid.block({5,12});f.start();
         require(f.vm.status()==odh::VmStatus::returned_false && !f.routing.active(),"no path is false without movement");}
        {Fixture f;odh::Vm other(routines(),f.world.vm_memory(),{},f.routing.primitive());other.start(odh::routine_key(4096),{1,2,2});
         require(other.run(1)==odh::VmStatus::fault && !f.routing.active(),"controller refuses foreign VM identity");}
        {Fixture f;f.start();bool guarded=false;
         f.routing.tick(1,[&](const auto&){try{f.routing.tick(2);}catch(const std::logic_error&){guarded=true;}});
         require(guarded && f.world.find(1)->position->x==5 && f.routing.tick(2),"movement notification cannot reenter tick");}
        {Fixture f;f.start();f.routing.tick(1,[&](const auto&){f.vm.start(odh::routine_key(4096),{1,2,2});});
         require(!f.routing.tick(2) && f.world.find(1)->position->x==5,"observer cancellation stops subsequent movement");}
        {Fixture f;require(!f.world.set_position(1,{-1,0,1,0}) && !f.world.set_position(1,{0,0,0,0}) &&
                            !f.world.set_position(1,{0,0,1,8}),"position profile bounds");}
        std::cout<<checks<<" routing checks passed (constructed world only)\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Routing test failed: "<<e.what()<<'\n';return 1;}
}
