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
odh::RoutineSet routine(unsigned mode=3,unsigned index=0) {
    return {{odh::routine_key(4096),odh::Bhav{0x8002,{{46,254,255,{static_cast<std::uint8_t>(index),static_cast<std::uint8_t>(index>>8),static_cast<std::uint8_t>(mode),0,4,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
}
odh::SlotTable floor_table() {
    auto slots=table();slots.records.push_back(slots.records.back());
    for(const auto compass:{0,4,6,2}) {
        odh::SlotRecord route{};route.type=3;route.snap_target_slot=-1;
        route.proximity=std::array<std::int32_t,3>{16,16,16};route.max_size=0;route.unknown_i10=0;
        route.gradient_bits=0x3E400000;route.height=0;route.facing=-2;route.resolution=16;
        route.flags=4096U|(1U<<compass);
        route.offset_bits[compass==0 || compass==4 ? 1 : 0]=compass==0 || compass==6 ? 0xC1800000 : 0x41800000;
        slots.records.push_back(route);
    }
    return slots;
}
odh::SlotTable stair_table() {
    odh::SlotRecord route{};route.type=3;route.snap_target_slot=-1;route.flags=2048;
    route.proximity=std::array<std::int32_t,3>{16,16,16};route.max_size=0;route.unknown_i10=0;
    route.gradient_bits=0x3E400000;route.height=0;route.facing=-2;route.resolution=16;
    return {0,10,{route},0};
}
odh::SlotTable shoo_sitting_exit_table() {
    auto slots=table();odh::SlotRecord route{};route.type=3;route.snap_target_slot=-1;
    route.offset_bits={0,0xC1800000,0};route.proximity=std::array<std::int32_t,3>{16,16,16};
    route.max_size=0;route.unknown_i10=0;route.gradient_bits=0x3E400000;
    route.height=0;route.facing=-2;route.resolution=16;slots.records.push_back(route);return slots;
}
odh::RoutineSet shoo_sitting_exit_routine() {
    auto result=routine(3,1);result.begin()->second.instructions[0].operands[4]=2;return result;
}
odh::RoutineSet stair_routine(unsigned flags=2,unsigned index=0) {
    return {{odh::routine_key(4113),odh::Bhav{0x8002,{{46,254,255,{static_cast<std::uint8_t>(index),
        static_cast<std::uint8_t>(index>>8),3,0,static_cast<std::uint8_t>(flags),0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
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
struct FloorFixture : Fixture {
    explicit FloorFixture(odh::Snap::PlacementValidator validator=[](auto,const auto&){return true;}) {
        slots=floor_table();snap.reset();snap.emplace(world,2,odh::ObjdBinding{138,0x12345678,129,128},slots,128,std::move(validator));
    }
};
void seat(Fixture& f) {
    require(f.world.place_in_slot(2,0,1),"floor test starts with positioned actor in chair");
    require(f.world.set_person_data(1,0,1),"floor test sitting posture");
}
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
    {Fixture f;
     require(f.world.create(3,odh::EntityKind::avatar,{0,88,1})&&f.world.set_position(3,{11,21,1,4}),"second resident fixture");
     odh::Snap other(f.world,2,odh::ObjdBinding{138,0x12345678,129,128},f.slots,128);
     odh::Vm first(routine(),f.world.vm_memory(),{},f.snap->primitive());
     odh::Vm second(routine(),f.world.vm_memory(),{},other.primitive());
     require(f.snap->bind(first)&&other.bind(second),"same target has distinct VM-bound Snap controllers");
     first.start(odh::routine_key(4096),{1,2,2});
     require(first.run(1)==odh::VmStatus::returned_true&&f.world.find(2)->slots[0]==1&&
             f.world.find(1)->attachment&&f.world.find(1)->attachment->container==2&&
             f.world.find(1)->attachment->slot==0,"first resident occupies target slot");
     const auto second_position=*f.world.find(3)->position;
     second.start(odh::routine_key(4096),{3,2,2});
     require(second.run(1)==odh::VmStatus::returned_false&&f.world.find(2)->slots[0]==1&&
             f.world.find(1)->attachment&&!f.world.find(3)->attachment&&
             *f.world.find(3)->position==second_position,"occupied slot rejects second resident atomically");
     require(f.world.detach(2,0)&&f.world.find(2)->slots[0]==0&&!f.world.find(1)->attachment,
             "explicit World detach vacates both ends of first resident relation");
     second.start(odh::routine_key(4096),{3,2,2});
     require(second.run(1)==odh::VmStatus::returned_true&&f.world.find(2)->slots[0]==3&&
             !f.world.find(1)->attachment&&f.world.find(3)->attachment&&
             f.world.find(3)->attachment->container==2&&f.world.find(3)->attachment->slot==0&&
             f.world.find(3)->attachment->positioned&&*f.world.find(3)->position==odh::EntityPosition{30,20,1,4},
             "same second VM succeeds after vacancy with exact target attachment and position");}
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
    for(const auto direction:{0,2,4,6}) for(unsigned index=2;index<6;++index) {
        FloorFixture f;f.world.set_position(2,{100,100,1,static_cast<std::uint8_t>(direction)});seat(f);
        f.world.create(3,odh::EntityKind::object,{0,0,1});f.world.set_position(3,{0,0,1,7});f.world.place_in_slot(1,0,3);
        f.world.create(4,odh::EntityKind::object);f.world.set_position(4,{0,0,1,5});f.world.place_in_slot(3,0,4);
        const auto generation=f.world.find(1)->generation;const auto words=f.world.word_count();
        const std::array<int,4> compass{0,4,6,2};const std::array<int,8> dx{0,16,16,16,0,-16,-16,-16},dy{-16,-16,0,16,16,16,0,-16};
        const auto absolute=(direction+compass[index-2])%8;
        const odh::EntityPosition expected{100+dx[absolute],100+dy[absolute],1,static_cast<std::uint8_t>(absolute)};
        auto routines=routine(3,index);routines.begin()->second.instructions[0].operands[4]=static_cast<std::uint8_t>(direction==2 ? 0 : 4);
        odh::Vm vm(routines,{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(1)==odh::VmStatus::returned_true,"all four native exits rotate for every cardinal object direction");
        require(*f.world.find(1)->position==expected && !f.world.find(1)->attachment && f.world.find(2)->slots[0]==0,"floor exit updates position/direction and detaches both ends");
        require(f.world.find(3)->position->x==expected.x && f.world.find(3)->position->y==expected.y && f.world.find(3)->position->direction==7 &&
                f.world.find(4)->position->x==expected.x && f.world.find(4)->position->y==expected.y && f.world.find(4)->position->direction==5,
                "floor exit propagates descendants and retains their own orientation");
        require(f.world.find(1)->slots[0]==3 && f.world.find(3)->attachment->container==1 && f.world.find(3)->slots[0]==4 &&
                f.world.find(4)->attachment->container==3,"floor exit retains descendant relationships");
        require(f.world.find(1)->person_data[0]==1 && f.world.find(1)->generation==generation && f.world.word_count()==words,"floor snap leaves posture, identity and word budget unchanged");
        vm.start(odh::routine_key(4096),{1,2,2});require(vm.run(1)==odh::VmStatus::returned_true && *f.world.find(1)->position==expected,"free actor floor snap is idempotent");
    }
    {FloorFixture f([](auto,const auto&){return false;});seat(f);
     const auto pose=*f.world.find(1)->position;
     odh::Vm vm(routine(3,2),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_false && *f.world.find(1)->position==pose && f.world.find(1)->attachment->container==2 &&
             f.world.find(1)->attachment->positioned && f.world.find(2)->slots[0]==1 && f.world.find(1)->person_data[0]==1,"blocked floor exit leaves complete seated state unchanged");}
    {FloorFixture f(odh::Snap::PlacementValidator{});seat(f);
     odh::Vm vm(routine(3,2),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::fault && f.world.find(1)->attachment && f.world.find(2)->slots[0]==1,"missing validator is unsupported without detaching");}
    for(const auto direction:{1,3,5,7}) {FloorFixture f;f.world.set_position(2,{30,20,1,static_cast<std::uint8_t>(direction)});seat(f);
     odh::Vm vm(routine(3,2),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::fault && f.world.find(1)->attachment && f.world.find(1)->position->direction==2,"diagonal floor geometry unsupported and atomic");}
    for(unsigned index=2;index<6;++index) {int calls=0;FloorFixture f([&](auto,const auto&){++calls;return true;});
     f.world.set_position(2,{index==4 ? 0 : 32767,index==2 ? 0 : 32767,1,0});seat(f);const auto pose=*f.world.find(1)->position;
     odh::Vm vm(routine(3,index),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_false && calls==0 && *f.world.find(1)->position==pose && f.world.find(2)->slots[0]==1,"out of World bounds is false before observer or detach");}
    {FloorFixture f;f.world.create(3,odh::EntityKind::object,{0,0,1});f.world.set_position(3,{40,40,1,0});f.world.place_in_slot(3,0,1);
     const auto pose=*f.world.find(1)->position;
     odh::Vm vm(routine(3,2),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_false && *f.world.find(1)->position==pose && f.world.find(3)->slots[0]==1 && f.world.find(1)->attachment->container==3,"floor snap cannot transfer from another container");}
    {FloorFixture f;seat(f);bool observed=false;f.snap.reset();
     f.snap.emplace(f.world,2,odh::ObjdBinding{138,0x12345678,129,128},f.slots,128,[&](auto actor,const auto& goal){
         observed=actor==1 && goal==odh::EntityPosition{30,4,1,0} && f.world.find(2)->slots[0]==1 && f.world.find(1)->attachment &&
                  *f.world.find(1)->position==odh::EntityPosition{30,20,1,2};return true;});
     odh::Vm vm(routine(3,2),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_true && observed,"validator sees complete pre-placement relation and pose");}
    {FloorFixture f;seat(f);f.snap.reset();
     f.snap.emplace(f.world,2,odh::ObjdBinding{138,0x12345678,129,128},f.slots,128,[&](auto,const auto&){f.world.remove(1);f.world.create(1,odh::EntityKind::avatar,{0,88,0});f.world.set_position(1,{11,12,1,4});return true;});
     odh::Vm vm(routine(3,2),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_false && *f.world.find(1)->position==odh::EntityPosition{11,12,1,4},"callback reused actor ID prevents placement against stale generation");}
    {odh::World world;auto slots=shoo_sitting_exit_table();world.create(1,odh::EntityKind::avatar,{0,88,1});
     world.create(2,odh::EntityKind::object,{0,0,1});world.set_position(1,{30,20,1,2});world.set_position(2,{30,20,1,0});
     world.place_in_slot(2,0,1);world.set_person_data(1,0,1);bool observed=false;
     odh::Snap snap{world,2,odh::ObjdBinding{138,3586796916U,129,128},slots,128,
         [&](auto actor,const auto& point){observed=actor==1&&point==odh::EntityPosition{30,4,1,2}&&
             world.find(1)->attachment&&world.find(2)->slots[0]==1;return true;}};
     odh::Vm vm(shoo_sitting_exit_routine(),world.vm_memory(),{},snap.primitive());snap.bind(vm);
     vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_true&&observed,"exact semiglobal sitting-exit Snap validates outside goal before moving");
     require(*world.find(1)->position==odh::EntityPosition{30,4,1,2}&&!world.find(1)->attachment&&
             world.find(2)->slots[0]==0&&world.find(1)->person_data[0]==1,"sitting-exit Snap detaches the resident without changing posture");}
    {odh::World world;auto slots=shoo_sitting_exit_table();world.create(1,odh::EntityKind::avatar,{0,88,1});
     world.create(2,odh::EntityKind::object,{0,0,1});world.set_position(1,{30,20,1,2});world.set_position(2,{30,20,1,0});
     world.place_in_slot(2,0,1);world.set_person_data(1,0,1);odh::Snap snap{world,2,
         odh::ObjdBinding{138,3586796916U,129,128},slots,128,[](auto,const auto&){return false;}};
     odh::Vm vm(shoo_sitting_exit_routine(),world.vm_memory(),{},snap.primitive());snap.bind(vm);
     vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_false&&world.find(1)->attachment&&world.find(2)->slots[0]==1&&
             world.find(1)->person_data[0]==1,"blocked semiglobal sitting exit preserves the complete seat state");}
    for(const auto position:{odh::EntityPosition{-1,20,1,0},odh::EntityPosition{30,-1,1,0},odh::EntityPosition{32768,20,1,0},
                             odh::EntityPosition{30,32768,1,0},odh::EntityPosition{30,20,0,0},odh::EntityPosition{30,20,1,8}}) {
        FloorFixture f;seat(f);f.world.create(3,odh::EntityKind::object);f.world.set_position(3,{0,0,1,6});f.world.place_in_slot(1,0,3);
        const auto pose=*f.world.find(1)->position,child=*f.world.find(3)->position;
        require(!f.world.place_on_floor(1,position) && *f.world.find(1)->position==pose && *f.world.find(3)->position==child &&
                f.world.find(2)->slots[0]==1 && f.world.find(1)->attachment->positioned,"invalid floor World position preserves relation and descendants atomically");
    }
    {FloorFixture f;seat(f);f.world.create(3,odh::EntityKind::object);f.world.set_position(3,{9,8,1,6});f.world.attach(1,0,3);
     require(f.world.place_on_floor(1,{40,50,2,4}) && f.world.find(3)->attachment && *f.world.find(3)->position==odh::EntityPosition{9,8,1,6},"floor World move does not physically move unpositioned attachment");
     require(!f.world.place_on_floor(99,{40,50,1,0}),"floor World rejects missing identity");}
    {odh::World world;auto slots=stair_table();world.create(1,odh::EntityKind::avatar,{0,88,1});
     world.create(2,odh::EntityKind::object,{0,0,4});world.set_position(1,{312,424,1,6});world.set_position(2,{312,344,2,0});
     odh::ObjdBinding binding{138,0x79E2B825,129,128};binding.master_id=4;std::vector<odh::EntityPosition> checked;
     odh::Snap snap{world,2,binding,slots,128,{},[&](auto actor,auto target,const auto& point){
         require(actor==1&&target==2,"stair validator receives actor and StackObject identities");
         checked.push_back(point);return odh::Snap::StairPlacementStatus::accepted;}};
     odh::Vm vm(stair_routine(),world.vm_memory(),{},snap.primitive());snap.bind(vm);vm.start(odh::routine_key(4113),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_true,"exact stair Snap moves actor to adjacent-floor StackObject");
     require(*world.find(1)->position==odh::EntityPosition{312,344,2,6}&&
             checked==std::vector<odh::EntityPosition>{{312,344,2,0},{312,344,2,6}},
             "stair Snap validates north placement then preserves actor facing at exact target position");}
    {odh::World world;auto slots=stair_table();world.create(1,odh::EntityKind::avatar,{0,88,1});
     world.create(2,odh::EntityKind::object,{0,0,1});world.set_position(1,{312,424,1,6});world.set_position(2,{312,344,2,0});
     odh::ObjdBinding binding{138,0x92F1647E,129,128};binding.master_id=1;bool checked=false;
     odh::Snap snap{world,2,binding,slots,128,{},[&](auto actor,auto target,const auto& point){
         checked=checked||(actor==1&&target==2&&point==odh::EntityPosition{312,344,2,0});
         return odh::Snap::StairPlacementStatus::accepted;}};
     odh::Vm vm(stair_routine(),world.vm_memory(),{},snap.primitive());snap.bind(vm);vm.start(odh::routine_key(4113),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_true&&checked&&world.find(1)->position->level==2,
             "validated saved stair Snap accepts its nonzero House02 master ID");}
    {odh::World world;auto slots=stair_table();world.create(1,odh::EntityKind::avatar,{0,88,1});
     world.create(2,odh::EntityKind::object,{0,0,1});world.set_position(1,{312,424,1,6});world.set_position(2,{312,344,2,0});
     odh::ObjdBinding binding{138,0x92F1647E,129,128};bool checked=false;
     odh::Snap snap{world,2,binding,slots,128,{},[&](auto,auto,const auto&){
         checked=true;return odh::Snap::StairPlacementStatus::accepted;}};
     odh::Vm vm(stair_routine(),world.vm_memory(),{},snap.primitive());snap.bind(vm);vm.start(odh::routine_key(4113),{1,2,2});
     require(vm.run(1)==odh::VmStatus::fault&&!checked&&world.find(1)->position->level==1,
             "saved stair Snap rejects a missing multi-tile master identity");}
    {odh::World world;auto slots=stair_table();world.create(1,odh::EntityKind::avatar,{0,88,1});
     world.create(2,odh::EntityKind::object,{0,0,4});world.set_position(1,{312,424,1,6});world.set_position(2,{312,344,2,0});
     odh::ObjdBinding binding{138,0x79E2B825,129,128};binding.master_id=4;
     odh::Snap snap{world,2,binding,slots,128,{},[](auto,auto,const auto&){return odh::Snap::StairPlacementStatus::blocked;}};
     odh::Vm vm(stair_routine(),world.vm_memory(),{},snap.primitive());snap.bind(vm);vm.start(odh::routine_key(4113),{1,2,2});
     require(vm.run(1)==odh::VmStatus::returned_false&&*world.find(1)->position==odh::EntityPosition{312,424,1,6},
             "occupied stair destination takes false branch without moving actor");}
    {odh::World world;auto slots=stair_table();world.create(1,odh::EntityKind::avatar,{0,88,1});
     world.create(2,odh::EntityKind::object,{0,0,4});world.set_position(1,{312,424,1,6});world.set_position(2,{312,344,2,0});
     odh::ObjdBinding binding{138,0x79E2B825,129,128};binding.master_id=4;
     odh::Snap snap{world,2,binding,slots,128,{},[](auto,auto,const auto&){return odh::Snap::StairPlacementStatus::unsupported;}};
     odh::Vm vm(stair_routine(),world.vm_memory(),{},snap.primitive());snap.bind(vm);vm.start(odh::routine_key(4113),{1,2,2});
     require(vm.run(1)==odh::VmStatus::fault&&*world.find(1)->position==odh::EntityPosition{312,424,1,6},
             "incomplete stair placement evidence faults without moving actor");}
    {odh::World world;auto slots=stair_table();world.create(1,odh::EntityKind::avatar,{0,88,1});
     world.create(2,odh::EntityKind::object,{0,0,4});world.set_position(1,{312,424,1,6});world.set_position(2,{312,344,2,0});
     odh::ObjdBinding binding{138,0x79E2B825,129,128};binding.master_id=4;
     odh::Snap snap{world,2,binding,slots,128,{},[](auto,auto,const auto&){return odh::Snap::StairPlacementStatus::accepted;}};
     auto bad=stair_routine();bad.begin()->second.instructions[0].operands[4]=0;
     odh::Vm vm(bad,world.vm_memory(),{},snap.primitive());snap.bind(vm);vm.start(odh::routine_key(4113),{1,2,2});
     require(vm.run(1)==odh::VmStatus::fault&&*world.find(1)->position==odh::EntityPosition{312,424,1,6},
             "stair exact-point profile does not absorb a different Shoo operand");}
    for(unsigned mutation=0;mutation<16;++mutation) {FloorFixture f;seat(f);auto& route=f.slots.records[3];
     switch(mutation) {
     case 0:f.slots.version=9;break;case 1:route.offset_bits[1]=0x7FC00000;break;case 2:route.offset_bits[1]=0xC1700001;break;
     case 3:route.offset_bits[0]=0x41700000;break;case 4:route.offset_bits[2]=0x41700000;break;case 5:route.flags=4096;break;
     case 6:route.flags=4097|4;break;case 7:route.flags=4100;break;case 8:route.standing=1;break;case 9:route.sitting=1;break;
     case 10:route.ground=1;break;case 11:route.proximity=std::array<std::int32_t,3>{16,15,16};break;
     case 12:route.height=7;break;case 13:route.facing=-1;break;case 14:route.resolution=8;break;case 15:route.gradient_bits.reset();break;
     }
     odh::Vm vm(routine(3,2),{}, {},f.snap->primitive());f.snap->bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
     require(vm.run(1)==odh::VmStatus::fault && f.world.find(1)->attachment && f.world.find(2)->slots[0]==1 && f.world.find(1)->position->direction==2,"unimplemented floor profile faults without state mutation");
    }
    std::cout<<checks<<" snap/prefix checks passed (synthetic fixtures only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Snap test failed: "<<e.what()<<'\n';return 1;}}
