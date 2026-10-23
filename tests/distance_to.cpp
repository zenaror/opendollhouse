#include "odh/distance_to.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message) {
    ++checks;
    if(!value)throw std::runtime_error(message);
}
odh::RoutineSet routine(std::array<std::uint8_t,8> operands={0,0,0,0,0,0,0,0}) {
    return {{odh::routine_key(4096),odh::Bhav{0x8002,
        {{11,254,255,operands}},{},odh::Bhav::Metadata{0,4,0,0}}}};
}
}
int main() {
    try {
        odh::World world;
        world.create(1,odh::EntityKind::avatar,{0,88,0});
        world.create(2,odh::EntityKind::object,{0,0,1});
        world.set_position(1,{0,0,1,0});world.set_position(2,{48,64,1,0});
        odh::Vm vm{routine(),world.vm_memory(),{},odh::get_distance_to_primitive(world)};
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(1)==odh::VmStatus::returned_true&&vm.temps()[0]==5,
                "TS1 same-level distance floors Euclidean 1/16 coordinates to whole tiles");
        world.set_position(2,{47,63,1,0});
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(1)==odh::VmStatus::returned_true&&vm.temps()[0]==4,
                "GetDistanceTo floors values immediately below the next tile boundary");
        const std::array<std::uint8_t,8> saved_go_here{0,0,255,3,11,0,255,255};
        auto saved_action=routine(saved_go_here);
        odh::Vm saved_action_vm{std::move(saved_action),world.vm_memory(),{},odh::get_distance_to_primitive(world)};
        saved_action_vm.start(odh::routine_key(4096),{1,2,2});
        require(saved_action_vm.run(1)==odh::VmStatus::returned_true&&saved_action_vm.temps()[0]==4,
                "Destination Go Here GetDistanceTo reads the Caller ID through MyObject[11]");
        const std::array<std::uint8_t,8> saved_shoo{0,0,1,3,11,0,0,0};
        auto shoo_action=routine(saved_shoo);
        odh::Vm shoo_action_vm{std::move(shoo_action),world.vm_memory(),{},odh::get_distance_to_primitive(world)};
        shoo_action_vm.start(odh::routine_key(4096),{1,2,2});
        require(shoo_action_vm.run(1)==odh::VmStatus::returned_true&&shoo_action_vm.temps()[0]==4,
                "Destination BHAV4107 GetDistanceTo reads the Caller ID through MyObject[11]");
        auto shoo_temp1=routine({1,0,1,3,11,0,0,0});
        odh::Vm shoo_temp1_vm{std::move(shoo_temp1),world.vm_memory(),{},odh::get_distance_to_primitive(world)};
        shoo_temp1_vm.start(odh::routine_key(4096),{1,2,2});
        require(shoo_temp1_vm.run(1)==odh::VmStatus::returned_true&&shoo_temp1_vm.temps()[1]==4&&
                shoo_temp1_vm.temps()[0]==0,
                "GetDistanceTo publishes into the selected temp register for the household shoo BHAV");
        world.set_position(2,{16,16,2,0});
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(1)==odh::VmStatus::fault&&vm.temps()[0]==0,
                "cross-level GetDistanceTo is rejected without publishing a temp result");
        saved_action_vm.start(odh::routine_key(4096),{1,2,2});
        require(saved_action_vm.run(1)==odh::VmStatus::returned_true&&saved_action_vm.temps()[0]==20,
                "saved Go Here applies the TS1 minimum distance when the destination is one floor away");
        world.set_position(2,{400,320,2,0});
        saved_action_vm.start(odh::routine_key(4096),{1,2,2});
        require(saved_action_vm.run(1)==odh::VmStatus::returned_true&&saved_action_vm.temps()[0]==37,
                "saved Go Here adds five tiles to a long cross-floor planar distance");
        auto unsupported=routine({0,0,1,0,0,0,0,0});
        odh::Vm unsupported_vm{std::move(unsupported),world.vm_memory(),{},odh::get_distance_to_primitive(world)};
        world.set_position(2,{48,64,1,0});unsupported_vm.start(odh::routine_key(4096),{1,2,2});
        require(unsupported_vm.run(1)==odh::VmStatus::fault&&unsupported_vm.temps()[0]==0,
                "non-default GetDistanceTo operand fields are rejected without temp write");
        std::cout<<checks<<" distance-to checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
