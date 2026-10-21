#include "odh/world.hpp"
#include <cmath>
#include <iostream>
#include <limits>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
odh::VmFrame frame(std::int16_t caller=1,std::int16_t object=2) {
    return {odh::routine_key(4096),0,{caller,object,object},{},{}};
}
}
int main() {
    try {
        odh::World spatial;
        require(spatial.create(1,odh::EntityKind::object,{0,0,1}) &&
                    spatial.create(2,odh::EntityKind::avatar,{0,88,0}) &&
                    spatial.set_position(1,{16,16,1,0}) && spatial.set_position(2,{32,16,1,0}),
                "spatial revision fixture setup");
        auto spatial_revision=spatial.spatial_revision();
        require(spatial.set_position(2,{48,16,1,0}) && spatial.spatial_revision()>spatial_revision,
                "moving an entity advances spatial revision");
        spatial_revision=spatial.spatial_revision();
        require(spatial.attach(1,0,2) && spatial.spatial_revision()>spatial_revision,
                "changing containment advances spatial revision");
        spatial_revision=spatial.spatial_revision();
        require(spatial.set_static_footprint(1,false) && spatial.spatial_revision()>spatial_revision,
                "changing an entity footprint advances spatial revision");

        odh::World w;
        require(w.create(1,odh::EntityKind::avatar,{2,88,1}),"create avatar");
        require(w.find(1)->kill_timeout==std::optional<std::int32_t>{-1} &&
                    !w.set_avatar_kill_timeout(2,0) && !w.set_avatar_kill_timeout(1,-2) &&
                    w.set_avatar_kill_timeout(1,0) && w.find(1)->kill_timeout==std::optional<std::int32_t>{0} &&
                    w.set_avatar_kill_timeout(1,-1),
                "avatar lifecycle starts alive and accepts only valid runtime updates");
        require(w.create(2,odh::EntityKind::object,{3,0,2}),"create object");
        require(w.create(3,odh::EntityKind::avatar,{0,88,0}),"create second avatar");
        const auto f=frame();
        require(w.read(f,3,11)==1 && w.read(f,4,11)==2,"derived identity follows context");
        require(w.read(f,4,67)==2 && w.read(f,3,67)==1,"derived slot count");
        require(w.read(f,3,2)==0 && w.read(f,3,3)==-1,"unattached container/slot profile");
        require(w.read(f,16,0)==0 && !w.read(f,16,2),"empty differs from invalid slot");
        require(!w.read(f,3,0) && !w.write(f,3,0,9),"undeclared object word is unsupported");
        require(w.declare_object_word(1,0,7) && w.read(f,3,0)==7,"explicit word declaration");
        require(w.write(f,3,0,9) && w.read(f,3,0)==9,"raw stored word write");
        const auto revision_before_position=w.spatial_revision();
        require(w.declare_object_word(1,1,0) && w.set_position(1,{16,16,1,0}) &&
                    w.spatial_revision()>revision_before_position &&
                    w.write(f,3,1,2) && w.find(1)->position->direction==2 && w.read(f,3,1)==2,
                "position and ObjectData direction writes advance live spatial revision");
        require(!w.write(f,3,1,8) && w.find(1)->position->direction==2,
                "invalid ObjectData direction leaves live orientation unchanged");
        require(w.set_position(1,{16,16,1,4}) && w.read(f,3,1)==4,
                "live position updates the ObjectData direction");
        constexpr float fractional_turn=0.3f;
        require(w.set_radian_direction(1,fractional_turn) &&
                    w.find(1)->radian_direction==fractional_turn &&
                    w.find(1)->position->direction==0 && w.read(f,3,1)==0,
                "avatar keeps continuous radians while exposing the rounded direction notch");
        require(w.set_position(1,{17,16,1,0}) &&
                    w.find(1)->radian_direction==fractional_turn,
                "movement within the same direction notch preserves continuous avatar angle");
        require(!w.set_radian_direction(2,0.5f) &&
                    !w.set_radian_direction(1,std::numeric_limits<float>::quiet_NaN()) &&
                    w.find(1)->radian_direction==fractional_turn,
                "radian direction rejects non-avatars and non-finite values atomically");
        require(w.set_radian_direction(1,0.9f) && w.find(1)->position->direction==1 &&
                    w.read(f,3,1)==1,
                "continuous angle rounds to the source eight-notch direction");
        require(w.set_position(1,{18,16,1,3}) &&
                    std::abs(w.find(1)->radian_direction-3.0f*0.7853981633974483f)<0.00001f,
                "explicit notch changes reset avatar radians to the canonical notch angle");
        for (const auto index : {2,3,11,67}) {
            require(!w.declare_object_word(1,static_cast<std::int16_t>(index),9),"derived words cannot be replaced");
            require(!w.write(f,3,static_cast<std::int16_t>(index),9),"derived identity/relation cannot be written");
        }
        require(w.write(f,0,1,17) && w.read(f,0,1)==17 && w.read(f,1,1)==0,"attributes belong to proper entity");
        require(!w.write(f,0,-1,17) && !w.write(f,0,2,17),"attribute bounds");
        require(w.set_person_data(1,0,1) && w.read(f,18,0)==1,"posture word");
        require(w.set_person_data(3,0,2) && w.read(frame(1,3),19,0)==2 &&
                w.read(frame(1,3),18,0)==1,"person scopes separate caller and stack object");
        require(w.attach(1,0,3) && w.read(f,20,0)==3 && w.read(f,16,0)==0 && w.detach(1,0),
                "caller slots separate from stack object slots");
        require(!w.read(f,19,0) && !w.set_person_data(2,0,1),"objects have no person data");
        require(!w.read(frame(9,9),18,0) && !w.write(frame(9,9),18,0,1),"missing entity");
        require(!w.read(f,18,88) && !w.write(f,18,-1,1),"person data bounds");
        require(!w.read(f,6,0) && !w.write(f,16,0,1),"unsupported scopes and direct slot writes");
        require(w.attach(2,0,1),"attach occupant");
        require(w.read(f,16,0)==1 && w.read(f,3,2)==2 && w.read(f,3,3)==0,"bidirectional attachment");
        require(!w.attach(2,1,1) && !w.attach(2,0,3),"one container and no overwrite");
        require(!w.attach(1,0,2),"containment cycle rejected");
        require(!w.attach(2,0,2) && !w.attach(2,2,3) && !w.attach(2,1,99),"invalid attachment unchanged");
        require(w.read(f,16,0)==1 && w.read(f,16,1)==0,"failed attach leaves both slots unchanged");
        require(w.detach(2,0) && w.read(f,16,0)==0 && w.read(f,3,3)==-1,"detach clears both ends");
        require(w.detach(2,0) && !w.detach(2,2),"idempotent empty detach and bounds");
        require(w.attach(2,1,3) && w.remove(3) && w.read(f,16,1)==0,"remove occupant detaches");
        require(w.attach(2,0,1) && w.remove(2) && !w.find(1)->attachment,"remove container releases occupant");
        require(w.word_count()==93,"word accounting after removals");
        require(w.remove(1) && w.word_count()==0 && !w.remove(1),"remove accounting and missing ID");
        require(!w.create(0,odh::EntityKind::object) && !w.create(-1,odh::EntityKind::object),"null/negative ID rejected");
        require(!w.create(1,odh::EntityKind::object,{0,1,0}),"person bank requires avatar");
        odh::World limited({1,5,5});
        require(!limited.create(1,odh::EntityKind::avatar,{3,3,0}) && limited.word_count()==0,"aggregate budget failure atomic");
        require(limited.create(1,odh::EntityKind::avatar,{2,2,0}) && !limited.create(2,odh::EntityKind::object),"entity limit");
        require(!limited.create(1,odh::EntityKind::object),"duplicate ID");
        require(limited.declare_object_word(1,0,7) && !limited.declare_object_word(1,1,8),"sparse word budget");
        require(limited.declare_object_word(1,0,9) && limited.word_count()==5,"replace word does not allocate");
        require(limited.remove(1) && limited.word_count()==0,"budget recovered");
        odh::World overflow({2,std::numeric_limits<std::size_t>::max(),std::numeric_limits<std::size_t>::max()});
        require(!overflow.create(1,odh::EntityKind::avatar,{std::numeric_limits<std::size_t>::max(),1,1}),"oversized layout rejected before allocation");
        require(!overflow.create(1,static_cast<odh::EntityKind>(99)),"unknown entity kind rejected");
        odh::World chain;
        require(chain.create(1,odh::EntityKind::object,{0,0,1}) &&
                chain.create(2,odh::EntityKind::object,{0,0,1}) &&
                chain.create(3,odh::EntityKind::object,{0,0,1}) &&
                chain.attach(1,0,2) && chain.attach(2,0,3),"nested container chain");
        require(!chain.attach(3,0,1) && chain.find(3)->slots[0]==0,"long containment cycle rejected");

        odh::World banks;
        require(banks.create(1,odh::EntityKind::avatar,{1,1,1,16}) &&
                banks.create(2,odh::EntityKind::avatar,{0,0,0,7}) &&
                banks.create(3,odh::EntityKind::object),"explicit motive bank layouts");
        require(banks.read(f,14,6)==0 && banks.read(f,15,6)==0,
                "allocated comfort words start at zero");
        require(banks.write(f,14,6,45) && banks.write(f,15,6,-35) &&
                banks.read(f,14,6)==45 && banks.read(f,15,6)==-35,
                "motive scopes separate caller and stack avatar");
        require(banks.write(f,14,6,std::numeric_limits<std::int16_t>::max()) &&
                banks.read(f,14,6)==100 && banks.write(f,15,6,std::numeric_limits<std::int16_t>::min()) &&
                banks.read(f,15,6)==-100,"motive writes clamp to fixed default bounds");
        require(banks.set_motive(1,6,25) && banks.read(f,14,6)==25 &&
                banks.write(f,14,6,120) && banks.read(f,14,6)==100,
                "direct motive setter shares clipping and allows decreases");
        require(banks.write(f,14,15,1) && banks.read(f,14,15)==1 &&
                !banks.read(f,14,16) && !banks.write(f,14,16,7) &&
                !banks.read(f,15,7) && !banks.write(f,15,7,7) &&
                !banks.read(f,14,-1) && !banks.set_motive(1,-1,7),"motive allocated index bounds");
        require(!banks.read(frame(3,3),14,0) && !banks.read(frame(3,3),15,0) &&
                !banks.write(frame(3,3),14,0,1) && !banks.write(frame(3,3),15,0,1) &&
                !banks.set_motive(3,0,1),"objects have no motive bank");
        require(!banks.read(frame(9,9),14,0) && !banks.write(frame(9,9),15,0,1),
                "missing motive entity is unsupported");
        require(banks.create(4,odh::EntityKind::avatar,{1,1,1}) && banks.find(4)->motives.empty() &&
                !banks.read(frame(4,4),14,0) && !banks.write(frame(4,4),15,0,1),
                "legacy three field layout leaves motives absent");
        const auto before_invalid=banks.word_count();
        require(!banks.create(5,odh::EntityKind::avatar,{0,0,0,17}) &&
                !banks.create(5,odh::EntityKind::object,{0,0,0,1}) &&
                banks.word_count()==before_invalid && !banks.find(5),"invalid motive layouts fail atomically");
        require(!banks.read(f,21,9) && !banks.read(frame(3,3),21,9),
                "definition object type is never inferred from entity kind");
        require(banks.declare_definition_word(1,9,2) && !banks.read(f,21,9) &&
                banks.declare_definition_word(2,9,42) && banks.read(f,21,9)==42 &&
                banks.read(frame(2,1),21,9)==2,"definition scope selects exact stack object bank");
        const auto before_repeat=banks.word_count();
        require(banks.declare_definition_word(2,9,42) && !banks.declare_definition_word(2,9,2) &&
                banks.word_count()==before_repeat && banks.read(f,21,9)==42,
                "definition declaration is immutable and idempotent");
        require(!banks.write(f,21,9,42) && !banks.write(f,21,9,7) && !banks.write(f,21,10,7) &&
                banks.read(f,21,9)==42 && !banks.read(f,21,10),"definition scope is always read only");
        require(!banks.read(f,21,11) && banks.read(f,4,11)==2 &&
                banks.declare_definition_word(2,11,81) && banks.read(f,21,11)==81 && banks.read(f,4,11)==2,
                "definition index never falls back to derived object fields");
        require(banks.declare_definition_word(2,0,123) && banks.declare_definition_word(2,255,-123) &&
                banks.read(f,21,0)==123 && banks.read(f,21,255)==-123 &&
                !banks.declare_definition_word(2,-1,1) && !banks.declare_definition_word(2,256,1) &&
                !banks.read(f,21,-1) && !banks.read(f,21,256) &&
                !banks.declare_definition_word(99,0,1),"explicit raw definition index profile bounds");
        const auto generation=banks.find(2)->generation;
        const auto before_remove=banks.word_count();
        require(banks.remove(2) && banks.word_count()==before_remove-11 &&
                banks.create(2,odh::EntityKind::avatar,{0,0,0,7}) &&
                banks.find(2)->generation!=generation && banks.read(f,15,6)==0 && !banks.read(f,21,9),
                "removal releases motives and definitions and reused IDs inherit no values");

        odh::World bank_limit({4,20,2});
        require(!bank_limit.create(1,odh::EntityKind::avatar,{0,0,0,3}) && bank_limit.word_count()==0 &&
                bank_limit.create(1,odh::EntityKind::avatar,{0,0,0,2}),"motive per bank budget");
        require(bank_limit.declare_definition_word(1,9,2) && bank_limit.declare_definition_word(1,255,8) &&
                !bank_limit.declare_definition_word(1,0,7) && bank_limit.word_count()==4 &&
                !bank_limit.read(frame(1,1),21,0),"definition per bank budget failure leaves no entry");
        require(bank_limit.declare_definition_word(1,9,2) && !bank_limit.declare_definition_word(1,9,3) &&
                bank_limit.word_count()==4,"full definition bank still allows identical declaration");
        odh::World total_limit({3,4,16});
        require(!total_limit.create(1,odh::EntityKind::avatar,{1,1,1,2}) &&
                total_limit.word_count()==0 && !total_limit.find(1),"motive global budget failure atomic");
        require(total_limit.create(1,odh::EntityKind::avatar,{0,0,0,3}) &&
                total_limit.create(2,odh::EntityKind::object) && total_limit.declare_definition_word(2,9,7) &&
                !total_limit.declare_definition_word(1,9,2) && total_limit.word_count()==4 &&
                !total_limit.read(frame(1,1),21,9),"definition global budget failure atomic");
        require(total_limit.declare_definition_word(2,9,7) && total_limit.set_motive(1,0,12) &&
                total_limit.word_count()==4,"existing definition and motive writes consume no words");
        require(total_limit.remove(1) && total_limit.remove(2) && total_limit.word_count()==0 &&
                total_limit.create(1,odh::EntityKind::avatar,{0,0,0,4}),"new banks release global budget");

        // Independent synthetic program reads caller ID and a stack object slot.
        require(w.create(1,odh::EntityKind::avatar,{0,88,0}) && w.create(2,odh::EntityKind::object,{0,0,1}),"VM world setup");
        odh::Bhav r{0x8002,{{2,254,255,{0,0,11,0,0,2,16,3}}},{},odh::Bhav::Metadata{0,4,0,0}};
        odh::Vm vm({{odh::routine_key(4096),r}},w.vm_memory());
        vm.start(odh::routine_key(4096),f.context);
        require(vm.run(1)==odh::VmStatus::returned_false,"empty slot does not match caller");
        require(w.attach(2,0,1),"VM occupy slot");
        vm.start(odh::routine_key(4096),f.context);
        require(vm.run(1)==odh::VmStatus::returned_true,"bound world reads live occupation");
        w.remove(2); vm.start(odh::routine_key(4096),f.context);
        require(vm.run(1)==odh::VmStatus::fault,"deleted entity never becomes implicit zero");
        // Synthetic copy of the observed Doors.iff 4113 -> 4115 Route45 decision.
        odh::World door_world;
        require(door_world.create(40,odh::EntityKind::object,{2,0,0,0}),
                "create object with bounded attributes for the portal Route45 profile");
        odh::Bhav portal_condition{0x8002,{{2,255,254,{1,0,0,0,0,2,1,7}}},{},
                                   odh::Bhav::Metadata{0,0,0,0}};
        odh::Bhav portal_routes{0x8002,{
            {4115,1,2,{255,255,255,255,255,255,255,255}},
            {45,254,255,{0,0,1,0,1,0,0,0}},
            {45,254,255,{1,0,1,0,1,0,0,0}}},{},odh::Bhav::Metadata{0,0,0,0}};
        unsigned route_calls=0,route_data=99,route_scope=99,route_flags=0;
        std::int16_t route_stack_object=0;
        odh::VmPrimitive route_probe=[&](const odh::VmFrame& vm_frame,const odh::Instruction& instruction,
                                         const auto&,const auto&) {
            if(instruction.opcode!=45)return odh::PrimitiveOutcome{};
            ++route_calls;
            route_data=static_cast<unsigned>(instruction.operands[0]|(instruction.operands[1]<<8));
            route_scope=static_cast<unsigned>(instruction.operands[2]|(instruction.operands[3]<<8));
            route_flags=instruction.operands[4];
            route_stack_object=vm_frame.context.stack_object;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        };
        odh::Vm portal_vm({{odh::routine_key(4113),portal_routes},
                           {odh::routine_key(4115),portal_condition}},
                          door_world.vm_memory(),{},route_probe);
        const auto portal_context=frame(1,40).context;
        portal_vm.start(odh::routine_key(4113),portal_context);
        require(portal_vm.run(10)==odh::VmStatus::returned_true&&route_calls==1&&
                    route_data==1&&route_scope==1&&route_flags==1&&route_stack_object==40,
                "4113 routes to literal slot 1 when StackObject attribute 1 equals zero");
        require(door_world.write(frame(1,40),1,1,1),"change portal StackObject attribute");
        route_calls=0;portal_vm.start(odh::routine_key(4113),portal_context);
        require(portal_vm.run(10)==odh::VmStatus::returned_true&&route_calls==1&&
                    route_data==0&&route_scope==1&&route_flags==1&&route_stack_object==40,
                "4113 routes to literal slot 0 when StackObject attribute 1 is nonzero");
        require(door_world.remove(40),"remove StackObject used by the 4113 profile");
        route_calls=0;portal_vm.start(odh::routine_key(4113),portal_context);
        require(portal_vm.run(10)==odh::VmStatus::fault&&route_calls==0,
                "4113 condition faults closed when the StackObject identity is unavailable");
        std::cout<<checks<<" world checks passed (synthetic fixtures only)\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"World test failed: "<<e.what()<<'\n'; return 1; }
}
