#include "odh/world.hpp"
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
        odh::World w;
        require(w.create(1,odh::EntityKind::avatar,{2,88,1}),"create avatar");
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
        require(w.word_count()==92,"word accounting after removals");
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
        std::cout<<checks<<" world checks passed (synthetic fixtures only)\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"World test failed: "<<e.what()<<'\n'; return 1; }
}
