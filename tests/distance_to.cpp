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
        world.set_position(2,{16,16,2,0});
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(1)==odh::VmStatus::fault&&vm.temps()[0]==0,
                "cross-level GetDistanceTo is rejected without publishing a temp result");
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
