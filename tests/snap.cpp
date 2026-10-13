#include "odh/snap.hpp"
#include <iostream>
#include <sstream>
namespace {
int checks=0;
void require(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
odh::SlotTable table() {
    odh::SlotRecord storage{};storage.type=0;storage.height=7;
    odh::SlotRecord route{};route.type=3;route.snap_target_slot=0;
    return {0,10,{storage,route},0};
}
odh::RoutineSet routine(unsigned mode=3) {
    return {{odh::routine_key(4096),odh::Bhav{0x8002,{{46,254,255,{0,0,static_cast<std::uint8_t>(mode),0,4,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
}
struct Fixture {
    odh::World world;odh::SlotTable slots=table();
    std::optional<odh::Snap> snap;
    Fixture() {
        world.create(1,odh::EntityKind::avatar,{0,88,1});world.create(2,odh::EntityKind::object,{0,0,1});
        world.set_position(1,{10,20,1,2});world.set_position(2,{30,20,1,0});
        snap.emplace(world,2,odh::ObjdBinding{138,0x12345678,129,128},slots,128);
    }
};
}
int main(){try{
    std::string b(42,'\0');b[0]=static_cast<char>(138);b[26]=static_cast<char>(129);
    b[28]=0x78;b[29]=0x56;b[30]=0x34;b[31]=0x12;b[40]=static_cast<char>(128);
    auto parse=[](const std::string& bytes){std::istringstream s(bytes);return odh::read_objd_binding(odh::View(s,bytes.size()));};
    const auto binding=parse(b);
    require(binding.guid==0x12345678 && binding.slot_table==128 && binding.animation_table==129,"OBJD prefix offsets");
    for(std::size_t n=0;n<b.size();++n){bool failed=false;try{parse(b.substr(0,n));}catch(const odh::FormatError&){failed=true;}require(failed,"prefix truncation rejected");}
    auto bad=b;bad[0]=0;bool failed=false;try{parse(bad);}catch(const odh::FormatError&){failed=true;}require(failed,"unknown prefix version");
    {Fixture f;odh::Vm vm(routine(),f.world.vm_memory(),{},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_true,"literal snap succeeds");
     require(f.world.find(2)->slots[0]==1 && f.world.find(1)->attachment->positioned,"both relation ends and placement marked");
     require(f.world.find(1)->position->x==30 && f.world.find(1)->position->direction==2,"physical parent position and preserved direction");
     vm.start(odh::routine_key(4096),{1,2,2});require(vm.run(1)==odh::VmStatus::returned_true,"same contained snap idempotent");
     require(f.world.set_position(2,{40,25,1,4}) && f.world.find(1)->position->x==40 &&
             f.world.find(1)->position->y==25 && f.world.find(1)->position->direction==2,"parent move propagates physical position");
     require(!f.world.set_position(1,{0,0,1,0}),"contained entity cannot move independently");
     f.world.detach(2,0);require(!f.world.find(1)->attachment && f.world.find(1)->position->x==40,"detach keeps last physical position");}
    {Fixture f;f.world.create(3,odh::EntityKind::avatar,{0,88,0});f.world.attach(2,0,3);
     odh::Vm vm(routine(),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_false && f.world.find(1)->position->x==10 &&
             !f.world.find(1)->attachment && f.world.find(2)->slots[0]==3,"occupied destination failure is atomic");}
    for(const auto facing:{-3,-2,-1,0,1,2,3,4,5,6,7}) {
        Fixture f;f.slots.records[1].flags=4096;f.slots.records[1].facing=facing;
        f.world.set_position(2,{30,20,1,7});
        auto routines=routine();routines.begin()->second.instructions[0].operands[4]=0;
        odh::Vm vm(routines,{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(1)==odh::VmStatus::returned_true && f.world.find(1)->position->direction==(7+(facing<0?0:facing))%8,
                "SnapToDirection rotates relative to object with wrap and negative defaults");
        require(f.world.find(1)->attachment && f.world.find(2)->slots[0]==1,"operand flags zero still contains via SnapTargetSlot");
        vm.start(odh::routine_key(4096),{1,2,2});require(vm.run(1)==odh::VmStatus::returned_true,"repeated oriented snap is idempotent");
    }
    {Fixture f;f.slots.records[1].flags=4096;f.slots.records[1].facing=-2;
     auto routines=routine();routines.begin()->second.instructions[0].operands[4]=0;
     f.world.place_in_slot(2,0,1);f.world.create(3,odh::EntityKind::object);f.world.set_position(3,{0,0,1,6});f.world.place_in_slot(1,0,3);
     odh::Vm vm(routines,{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});vm.run(1);
     require(f.world.find(1)->position->direction==0&&f.world.find(3)->position->direction==6&&f.world.find(3)->position->x==30,
             "oriented re-snap retains child direction and physical containment");
     const auto pose=*f.world.find(1)->position;
     require(!f.world.place_in_slot(2,0,1,8)&&*f.world.find(1)->position==pose&&f.world.find(2)->slots[0]==1,"invalid orientation rejected before mutation");}
    for(const auto facing:{-4,8}){Fixture f;f.slots.records[1].flags=4096;f.slots.records[1].facing=facing;
     odh::Vm vm(routine(),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::fault&&!f.world.find(1)->attachment&&f.world.find(1)->position->direction==2,
             "unsupported facing faults without containment or direction mutation");}
    {Fixture f;f.slots.records[1].flags=4096;f.slots.records[1].facing=2;f.world.create(3,odh::EntityKind::avatar,{0,88,0});f.world.attach(2,0,3);
     odh::Vm vm(routine(),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_false&&f.world.find(1)->position->direction==2&&!f.world.find(1)->attachment,
             "occupied oriented snap failure retains caller orientation");}
    {Fixture f;f.slots.records[0].offset_bits[0]=0x3F800000;odh::Vm vm(routine(),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::fault && !f.world.find(1)->attachment,"unknown storage geometry not accepted");}
    {Fixture f;odh::Vm vm(routine(4),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::fault,"unsupported snap mode");}
    {Fixture f;f.world.remove(2);f.world.create(2,odh::EntityKind::object,{0,0,1});f.world.set_position(2,{30,20,1,0});
     odh::Vm vm(routine(),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::fault,"reused owner ID invalidates resource binding");}
    {Fixture f;bool rejected=false;try{odh::Snap wrong(f.world,2,binding,f.slots,129);}catch(const std::invalid_argument&){rejected=true;}
     require(rejected,"SLOT id must match OBJD binding");}
    {Fixture f;f.world.create(3,odh::EntityKind::object,{0,0,1});f.world.set_position(3,{0,0,1,6});
     require(f.world.place_in_slot(1,0,3) && f.world.place_in_slot(2,0,1),"nested positioned containment");
     require(f.world.find(3)->position->x==30 && f.world.set_position(2,{45,21,1,0}) &&
             f.world.find(3)->position->x==45 && f.world.find(3)->position->direction==6,"nested propagation without recursion");
     const auto pose=*f.world.find(2)->position;
     require(!f.world.place_in_slot(3,0,2) && *f.world.find(2)->position==pose,"cycle/invalid slot leaves positions untouched");}
    std::cout<<checks<<" snap/prefix checks passed (synthetic fixtures only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Snap test failed: "<<e.what()<<'\n';return 1;}}
