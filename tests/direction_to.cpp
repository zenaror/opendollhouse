#include "odh/direction_to.hpp"
#include "odh/routing.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message) {
    ++checks;if(!value)throw std::runtime_error(message);
}
odh::VmStatus run(odh::Vm& vm) {
    vm.start(odh::routine_key(4096),{1,2,2});return vm.run(1);
}
odh::RoutineSet routine(std::array<std::uint8_t,8> operands) {
    return {{odh::routine_key(4096),odh::Bhav{0x8002,
        {{12,254,255,operands}},{},odh::Bhav::Metadata{0,4,0,0}}}};
}
}
int main() {
    try {
        odh::World world;
        require(world.create(1,odh::EntityKind::avatar,{0,88,0})&&
            world.create(2,odh::EntityKind::object,{0,0,1}),"create direction fixture entities");
        require(world.set_position(1,{32,32,1,0})&&world.set_position(2,{32,16,1,0}),
            "position direction fixture");
        auto vm_for=[&](std::array<std::uint8_t,8> operands){
            return std::make_unique<odh::Vm>(routine(operands),world.vm_memory(),
                odh::VmLimits{},odh::get_direction_to_primitive(world,world.vm_memory()));
        };
        auto vm=vm_for({0,0,8,0,0,0,0,0});
        require(run(*vm)==odh::VmStatus::returned_true&&vm->temps()[0]==0,
            "GetDirectionTo maps north to notch zero");
        for(const auto [position,expected]:{
            std::pair<odh::RoutePoint,std::int16_t>{{48,32},2},{{48,48},3},
            {{32,48},4},{{16,32},6},{{39,15},0},{{40,15},1}}) {
            require(world.set_position(2,{position.x,position.y,1,0}),"move direction target");
            vm=vm_for({0,0,8,0,0,0,0,0});
            require(run(*vm)==odh::VmStatus::returned_true&&vm->temps()[0]==expected,
                "GetDirectionTo rounds the source vector to the expected clockwise notch");
        }
        require(world.set_position(2,{32,32,1,0}),"restore target position");
        vm=vm_for({0,0,8,0,1,7,1,0});
        require(run(*vm)==odh::VmStatus::returned_true&&vm->temps()[0]==0,
            "GetDirectionTo supports an explicit literal source selector");
        vm=vm_for({0,0,25,0,0,0,0,0});
        require(run(*vm)==odh::VmStatus::fault&&vm->temps()[0]==0,
            "GetDirectionTo rejects unsupported destination scopes without a temp write");
        std::cout<<checks<<" direction-to checks passed\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
