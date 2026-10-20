#include "odh/routing.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool v,const char* message) {++checks;if(!v)throw std::runtime_error(message);}
void reference_slot_lattice_tests() {
    odh::SlotRecord ranged{};ranged.type=3;ranged.flags=0xA0FF;
    ranged.proximity=std::array<std::int32_t,3>{16,64,16};ranged.resolution=8;
    std::vector<odh::SlotRecord> records(10);
    records[0].type=0;
    for(std::size_t i=1;i<9;++i) records[i].type=3;
    records[9]=ranged;
    const odh::SlotTable table{0,10,records,0};
    const auto offsets=odh::enumerate_reference_slot_range_lattice(table,8);
    require(offsets.size()==289,"House07 SLOT8 reference lattice has 17x17 points");
    require(offsets.front()==odh::RoutePoint{-64,-64} && offsets[16]==odh::RoutePoint{-64,64} &&
            offsets[17]==odh::RoutePoint{-56,-64} && offsets.back()==odh::RoutePoint{64,64},
            "reference lattice preserves x-outer/y-inner inclusive order");
    const auto proximity=odh::filter_reference_slot_range_proximity(table,8,offsets);
    require(proximity.size()==204,"House07 proximity band filters the 289 offsets to 204 Euclidean candidates");
    require(proximity.front()==odh::RoutePoint{-64,-8} && proximity.back()==odh::RoutePoint{64,8},
            "proximity filtering preserves the source enumeration order");
    auto square=table;square.records[9].flags|=(1U<<30);
    const auto square_candidates=odh::filter_reference_slot_range_proximity(square,8,offsets);
    require(square_candidates.size()==280,"FSOSquare uses the max-axis distance band");
    bool limited=false;
    try { (void)odh::enumerate_reference_slot_range_lattice(table,8,288); }
    catch(const std::length_error&) { limited=true; }
    require(limited,"reference lattice enforces candidate allocation budget");
    bool invalid=false;
    auto malformed=table;malformed.records[9].resolution=0;
    try { (void)odh::enumerate_reference_slot_range_lattice(malformed,8); }
    catch(const std::invalid_argument&) { invalid=true; }
    require(invalid,"reference lattice rejects zero resolution");
    invalid=false;malformed=table;malformed.records[9].proximity=std::array<std::int32_t,3>{64,16,16};
    try { (void)odh::enumerate_reference_slot_range_lattice(malformed,8); }
    catch(const std::invalid_argument&) { invalid=true; }
    require(invalid,"reference lattice rejects reversed proximity range");
    invalid=false;malformed=table;malformed.version=9;
    try { (void)odh::enumerate_reference_slot_range_lattice(malformed,8); }
    catch(const std::invalid_argument&) { invalid=true; }
    require(invalid,"reference lattice rejects incomplete version profile");
    invalid=false;malformed=table;malformed.records[9].proximity=std::array<std::int32_t,3>{16,0,16};
    try { (void)odh::enumerate_reference_slot_range_lattice(malformed,8); }
    catch(const std::invalid_argument&) { invalid=true; }
    require(invalid,"max-zero fallback to min rejects a non-ranged profile");
}
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
        reference_slot_lattice_tests();
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
        {Fixture f;f.world.set_position(1,{24,12,1,0});f.start();
         require(f.vm.status()==odh::VmStatus::waiting&&f.routing.active(),"validated start at SLOT goal creates turn-only route");
         require(f.routing.tick(1)&&f.vm.status()==odh::VmStatus::returned_true&&!f.routing.active(),
                 "validated turn-only route completes without movement");
         const auto* actor=f.world.find(1);
         require(actor->position->x==24&&actor->position->y==12&&actor->position->direction==2&&actor->person_data[37]==64,
                 "turn-only route preserves position and applies facing/entry flags");}
        {Fixture f;f.world.set_position(1,{24,12,1,0});f.grid.block({24,12});f.start();
         require(f.vm.status()==odh::VmStatus::returned_false&&!f.routing.active(),
                 "coincident but blocked SLOT goal is rejected before turn-only shortcut");}
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
        {Fixture f;auto& slot=f.slots.records[0];slot.flags=0xA0FF;
         slot.proximity=std::array<std::int32_t,3>{16,64,16};slot.gradient_bits=0;
         slot.facing=-3;slot.resolution=8;f.start();
         require(f.vm.status()==odh::VmStatus::fault && !f.routing.active() &&
                 f.world.find(1)->position->x==4 && f.world.find(1)->position->y==12,
                 "real House07 SLOT8 profile remains fail-closed until goal/clearance rules exist");}
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
