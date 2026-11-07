#include "odh/drop_onto.hpp"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::array<std::uint8_t,8> operands(std::uint16_t source_mode,std::uint16_t source_value,
                                  std::uint16_t destination_mode,std::uint16_t destination_value) {
    return {static_cast<std::uint8_t>(source_mode),static_cast<std::uint8_t>(source_mode>>8),
        static_cast<std::uint8_t>(source_value),static_cast<std::uint8_t>(source_value>>8),
        static_cast<std::uint8_t>(destination_mode),static_cast<std::uint8_t>(destination_mode>>8),
        static_cast<std::uint8_t>(destination_value),static_cast<std::uint8_t>(destination_value>>8)};
}
odh::RoutineSet program(std::array<std::uint8_t,8> data) {
    return {{odh::routine_key(4096),odh::Bhav{0x8002,{{43,254,255,data}},{},
        odh::Bhav::Metadata{0,4,4,0}}}};
}
struct Fixture {
    odh::World world;
    explicit Fixture(bool avatar_caller=false,bool avatar_destination=false,bool nested=false) {
        const auto caller_kind=avatar_caller?odh::EntityKind::avatar:odh::EntityKind::object;
        const auto destination_kind=avatar_destination?odh::EntityKind::avatar:odh::EntityKind::object;
        require(world.create(10,caller_kind,{0,avatar_caller?88u:0u,3,0}),"create DropOnto Caller");
        require(world.create(20,destination_kind,{0,avatar_destination?88u:0u,3,0}),"create DropOnto StackObject");
        require(world.create(30,odh::EntityKind::object,{0,0,1,0}),"create item with a child slot");
        require(world.set_position(10,{10,20,1,0})&&world.set_position(20,{30,25,1,static_cast<std::uint8_t>(avatar_destination?0:6)})&&
                world.set_position(30,{10,20,1,3}),"position DropOnto fixture");
        require(world.place_in_slot(10,0,30),"place item in Caller source slot");
        if(nested) {
            require(world.create(31,odh::EntityKind::object,{0,0,0,0})&&
                    world.set_position(31,{10,20,1,5})&&world.place_in_slot(30,0,31),
                    "place nested child under item");
        }
    }
};
std::unique_ptr<odh::Vm> make_vm(Fixture& fixture,odh::DropOnto& drop,std::uint16_t caller=10,std::uint16_t target=20,
                std::array<std::uint8_t,8> data=operands(0,0,0,1)) {
    auto vm=std::make_unique<odh::Vm>(program(data),fixture.world.vm_memory(),odh::VmLimits{},drop.primitive());
    require(drop.bind(*vm),"bind DropOnto to its VM");
    vm->start(odh::routine_key(4096),{static_cast<std::int16_t>(caller),static_cast<std::int16_t>(caller),
                                     static_cast<std::int16_t>(target),0});
    return vm;
}
void assert_unmoved(const Fixture& f,const char* message) {
    const auto* caller=f.world.find(10);const auto* target=f.world.find(20);const auto* item=f.world.find(30);
    require(caller&&target&&item&&caller->slots[0]==30&&target->slots[1]==0&&item->attachment&&
            item->attachment->container==10&&item->attachment->slot==0,message);
}
}

int main() {try {
    {
        Fixture f;odh::DropOnto drop(f.world);auto vm=make_vm(f,drop);
        const auto old=f.world.find(20)->generation;
        require(f.world.remove_retaining_body({20,old})&&f.world.create(20,odh::EntityKind::object,{0,0,3})&&
            f.world.set_position(20,{30,25,1,6}),"replace DropOnto destination after its real frame bound the old generation");
        require(vm->run(1)==odh::VmStatus::fault,"DropOnto refuses a retained destination instead of writing its reused numeric ID");
        assert_unmoved(f,"retained destination cannot move the source into a replacement container");
    }
    {
        Fixture f(false,false,true);unsigned calls=0;
        require(f.world.declare_object_word(10,60,0),"declare fixture-owned source effect field");
        odh::DropOnto drop(f.world,{},[&](const odh::VmFrame& frame,const odh::Instruction& instruction,
                const odh::VmDispatch& dispatch,const odh::PreparedSlotMove& move){
            ++calls;require(dispatch.origin&&dispatch.origin->matches_active_primitive(frame,instruction,dispatch)&&
                instruction.opcode==43&&move.occupant()==30&&move.container()==20&&move.slot()==1&&
                move.occupant_generation()==f.world.find(30)->generation,
                "source lifecycle receives actual authenticated DropOnto and frozen item identity");
            require(f.world.write(frame,3,60,7),"source consumer can publish its explicitly backed fixture field");
            require(move.source_attachment()&&f.world.detach(move.source_attachment()->container,move.source_attachment()->slot),
                "source Pre detaches the original slot before scalar publication");
            require(f.world.place_in_slot(move.container(),move.slot(),move.occupant()),"source slot consumer publishes the physical relation");
            return odh::DropOnto::SlotTransferResult{true,true,{}};
        });
        auto vm=make_vm(f,drop);
        const auto before_item=f.world.find(30)->position,before_child=f.world.find(31)->position;
        const auto status=vm->run(1);
        const bool accepted=status==odh::VmStatus::returned_true&&calls==1&&
            f.world.find(20)->slots[1]==30&&f.world.find(31)->position->x==30;
        if(!accepted){
            const auto print_position=[](const std::optional<odh::EntityPosition>& position){
                if(!position){std::cerr<<"none";return;}
                std::cerr<<position->x<<","<<position->y<<","<<position->level<<","<<unsigned(position->direction);
            };
            std::cerr<<"DropOnto provider fixture status="<<static_cast<unsigned>(status)<<" calls="<<calls
                <<" diagnostic="<<vm->diagnostic()<<" item_before=";print_position(before_item);
            std::cerr<<" child_before=";print_position(before_child);
            std::cerr<<" item_after=";print_position(f.world.find(30)->position);
            std::cerr<<" child_after=";print_position(f.world.find(31)->position);
            std::cerr<<" source_slot="<<f.world.find(10)->slots[0]<<" destination_slot="<<f.world.find(20)->slots[1]<<"\n";
        }
        require(accepted,"bound source consumer replaces raw World commit and retains nested physical publication");
    }
    for(const bool completed:{false,true}){
        Fixture f;require(f.world.declare_object_word(10,60,0),"declare rejected-source fixture effect field");
        odh::DropOnto drop(f.world,{},[&](const auto& frame,const auto&,const auto&,const auto&){
            require(f.world.write(frame,3,60,7),"source rejection can retain a real Pre side effect");
            return odh::DropOnto::SlotTransferResult{completed,false,"fixture source post unsupported"};
        });
        auto vm=make_vm(f,drop);require(vm->run(1)==(completed?odh::VmStatus::returned_false:odh::VmStatus::fault),
            "source rejection and unsupported phases remain distinct dispositions");
        odh::VmFrame frame;frame.context.caller=10;require(f.world.read(frame,3,60)==std::optional<std::int16_t>{7},
            "source side effects are not rolled back on false or unsupported");
        assert_unmoved(f,"source rejection does not invent a physical transfer");
    }
    {
        Fixture f;odh::DropOnto drop(f.world,{},[&](const auto&,const auto&,const auto&,const auto& move){
            require(move.source_attachment()&&f.world.detach(move.source_attachment()->container,move.source_attachment()->slot),
                "source Pre detaches before the failing Post case");
            require(f.world.place_in_slot(move.container(),move.slot(),move.occupant()),"source publishes before unsupported Post");
            return odh::DropOnto::SlotTransferResult{false,false,"fixture Post rejected after publication"};
        });
        auto vm=make_vm(f,drop);require(vm->run(1)==odh::VmStatus::fault&&f.world.find(20)->slots[1]==30&&
            f.world.find(10)->slots[0]==0,"unsupported source Post preserves the already published containment effect");
    }
    {
        Fixture f;unsigned calls=0;odh::DropOnto drop(f.world,{},[&](const auto&,const auto&,const auto&,const auto&){
            ++calls;return odh::DropOnto::SlotTransferResult{true,true,{}};
        });
        auto vm=make_vm(f,drop);odh::VmFrame frame;frame.context={10,10,20,0};
        const auto forged=drop.primitive()(frame,{43,254,255,operands(0,0,0,1)},{},{});
        require(forged.disposition==odh::PrimitiveDisposition::unsupported&&calls==0,
            "forged dispatch never reaches the source transfer consumer");
        require(vm->run(1)==odh::VmStatus::fault&&calls==1,"a provider cannot claim placement without the exact reciprocal publication");
        assert_unmoved(f,"fabricated provider success does not mutate the item");
    }
    for(const auto mode:std::array<std::uint16_t,4>{0,2,0x0100,0xffff}) {
        Fixture f;odh::DropOnto drop(f.world);auto vm=make_vm(f,drop,10,20,operands(mode,0,mode,1));
        vm->temps()[0]=123;const auto before=vm->temps();
        require(vm->run(1)==odh::VmStatus::returned_true,"non-parameter DropOnto modes use literal slot numbers");
        const auto* item=f.world.find(30);const auto* child=f.world.find(31);
        require(f.world.find(10)->slots[0]==0&&f.world.find(20)->slots[1]==30&&item->attachment&&
                item->attachment->container==20&&item->attachment->slot==1&&
                item->position==std::optional<odh::EntityPosition>{{30,25,1,3}},
                "successful DropOnto publishes reciprocal containment and destination XYZ while preserving orientation");
        if(child)require(child->position==std::optional<odh::EntityPosition>{{30,25,1,5}},
            "DropOnto propagates parent XYZ through nested items and preserves child orientation");
        require(vm->temps()==before&&!vm->pending()&&vm->steps()==1,
            "DropOnto is synchronous, consumes one instruction, and leaves Temps unchanged");
    }
    {
        Fixture f;odh::DropOnto drop(f.world);
        auto vm=make_vm(f,drop,10,20,operands(1,0,1,1));
        vm->start(odh::routine_key(4096),{10,10,20,0},{0,1});
        require(vm->run(1)==odh::VmStatus::returned_true&&f.world.find(20)->slots[1]==30,
            "mode one resolves source and destination slot numbers from Args");
    }
    {
        Fixture f;odh::DropOnto drop(f.world);
        auto vm=make_vm(f,drop,10,20,operands(1,0,0,1));
        vm->start(odh::routine_key(4096),{10,10,20,0},{-1});
        require(vm->run(1)==odh::VmStatus::returned_false,"negative parameter slot is interpreted as unsigned ushort 65535");
        assert_unmoved(f,"unsigned parameter miss does not mutate containment");
    }
    {
        Fixture f;odh::DropOnto drop(f.world);auto vm=make_vm(f,drop,10,20,operands(1,9,0,1));
        require(vm->run(1)==odh::VmStatus::fault,"parameter number outside Args is explicitly unsupported");
        assert_unmoved(f,"invalid parameter read precedes World mutation");
    }
    {
        Fixture f;require(f.world.detach(10,0),"clear source for empty-slot case");
        odh::DropOnto drop(f.world);auto vm=make_vm(f,drop);
        require(vm->run(1)==odh::VmStatus::returned_false,"empty Caller source slot returns false");
        require(f.world.find(20)->slots[1]==0,"empty source leaves destination unchanged");
    }
    {
        Fixture f;require(f.world.create(40,odh::EntityKind::object)&&
            f.world.set_position(40,{20,20,1,0})&&f.world.place_in_slot(20,1,40),"occupy destination slot");
        odh::DropOnto drop(f.world);auto vm=make_vm(f,drop);
        require(vm->run(1)==odh::VmStatus::returned_false,"occupied destination returns false before moving source item");
        require(f.world.find(10)->slots[0]==30&&f.world.find(20)->slots[1]==40&&
            f.world.find(30)->attachment->container==10,"occupied failure preserves both slot relations");
    }
    {
        Fixture f;odh::DropOnto drop(f.world);auto vm=make_vm(f,drop,10,20,operands(0,7,0,1));
        require(vm->run(1)==odh::VmStatus::returned_false,"out-of-range object source slot returns false");
        assert_unmoved(f,"out-of-range source slot leaves World unchanged");
    }
    {
        Fixture f(true);odh::DropOnto drop(f.world);auto vm=make_vm(f,drop,10,20,operands(0,3,0,1));
        require(vm->run(1)==odh::VmStatus::fault,"out-of-range avatar source slot is explicitly unsupported");
        assert_unmoved(f,"avatar direct-index failure leaves World unchanged");
    }
    {
        Fixture f;odh::DropOnto drop(f.world);auto vm=make_vm(f,drop,10,20,operands(0,0,0,9));
        require(vm->run(1)==odh::VmStatus::fault,"out-of-range destination slot is explicitly unsupported before detach");
        assert_unmoved(f,"invalid destination bounds leave source containment unchanged");
    }
    {
        Fixture f;require(f.world.set_ui_ghost(30,true),"mark source item as UI ghost");
        odh::DropOnto drop(f.world);auto vm=make_vm(f,drop);
        require(vm->run(1)==odh::VmStatus::fault,"ghost item profile is an explicit capability boundary");
        assert_unmoved(f,"unsupported ghost item is not moved");
    }
    {
        Fixture f;require(f.world.place_in_slot(30,0,20),"make destination a descendant of source item");
        odh::DropOnto drop(f.world);auto vm=make_vm(f,drop);
        require(vm->run(1)==odh::VmStatus::returned_false,"cyclic containment attempt returns false");
        require(f.world.find(10)->slots[0]==30&&f.world.find(30)->slots[0]==20&&
            f.world.find(20)->attachment->container==30,"cycle rejection preserves the full containment chain");
    }
    {
        Fixture f(true);odh::DropOnto drop(f.world);auto vm=make_vm(f,drop);
        require(vm->run(1)==odh::VmStatus::fault,"avatar source change requires a host Playback capability");
        assert_unmoved(f,"missing carry capability rejects before World publication");
    }
    for(const bool source_bound:{false,true}){
        Fixture f(true);odh::Vm* source_vm=nullptr;
        odh::Playback playback(f.world,{});
        odh::DropOnto drop(f.world,[&](std::int16_t id)->std::optional<odh::DropOnto::CarrierController>{
            const auto* actor=f.world.find(id);if(id!=10||!actor||!source_vm)return {};
            return odh::DropOnto::CarrierController{&playback,actor->generation,source_vm->run_generation()};
        },source_bound?odh::DropOnto::SlotTransfer{[&](const auto&,const auto&,const auto&,const auto& move){
            require(move.source_attachment()&&f.world.detach(move.source_attachment()->container,move.source_attachment()->slot)&&
                f.world.attach(move.container(),move.slot(),move.occupant())&&
                f.world.refresh_contained_position(move.occupant()),"source Pre and scalar publication use ordinary physical kernel");
            return odh::DropOnto::SlotTransferResult{true,true,{}};
        }}:odh::DropOnto::SlotTransfer{});
        auto vm=make_vm(f,drop);source_vm=vm.get();require(playback.bind(*vm),"bind source Playback controller");
        auto clip=std::make_shared<odh::AnimationHeader>();
        odh::PlaybackSnapshot saved;odh::PlaybackResourceKey key;key.source=1;key.id=7;key.caller=10;
        key.actor_age=f.world.find(10)->person_data[58];saved.carry=key;
        require(playback.restore_snapshot(saved,*vm,[&](const auto&){return clip;}),"seed source carry for removal test");
        require(playback.carry_clip()==clip,"source avatar begins with an active carry clip");
        require(vm->run(1)==odh::VmStatus::returned_true&&f.world.find(20)->slots[1]==30&&
            !playback.carry_clip(),"successful avatar-slot transfer clears source carry after World commit");
    }
    {
        Fixture f(true);odh::Vm* source_vm=nullptr;odh::Playback playback(f.world,{});
        odh::DropOnto drop(f.world,[&](std::int16_t id)->std::optional<odh::DropOnto::CarrierController>{
            const auto* actor=f.world.find(id);if(id!=10||!actor||!source_vm)return {};
            // The external carrier lookup changes the destination generation
            // after World preparation. Playback preflight must reject the stale plan.
            require(f.world.remove(20)&&f.world.create(20,odh::EntityKind::object,{0,0,3,0})&&
                f.world.set_position(20,{30,25,1,6}),"mutate destination generation during carrier preflight");
            return odh::DropOnto::CarrierController{&playback,actor->generation,source_vm->run_generation()};
        });
        auto vm=make_vm(f,drop);source_vm=vm.get();require(playback.bind(*vm),"bind stale-plan source Playback");
        require(vm->run(1)==odh::VmStatus::fault&&f.world.find(10)->slots[0]==30&&f.world.find(20)->slots[1]==0,
            "destination generation change during carry preparation prevents DropOnto publication");
    }
    {
        Fixture f(false,true);
        auto clip=std::make_shared<odh::AnimationHeader>();
        odh::Vm destination_vm(program(operands(0,0,0,1)),f.world.vm_memory());
        destination_vm.start(odh::routine_key(4096),{20,20,20,0});
        odh::Playback destination_playback(f.world,{}, {},[&](const auto&,std::int16_t){return clip;});
        require(destination_playback.bind(destination_vm),"bind destination avatar Playback");
        odh::DropOnto drop(f.world,[&](std::int16_t id)->std::optional<odh::DropOnto::CarrierController>{
            if(id!=20)return {};
            return odh::DropOnto::CarrierController{&destination_playback,f.world.find(20)->generation,
                destination_vm.run_generation()};
        });
        auto vm=make_vm(f,drop,10,20,operands(0,0,0,0));
        require(vm->run(1)==odh::VmStatus::returned_true&&f.world.find(20)->slots[0]==30&&
            destination_playback.carry_clip()==clip,
            "destination avatar slot0 installs its own resolved carry after cross-avatar-context transfer");
    }
    std::cout<<"DropOnto: "<<checks<<" checks passed\n";return 0;
} catch(const std::exception& error) {std::cerr<<"DropOnto: "<<error.what()<<"\n";return 1;}}
