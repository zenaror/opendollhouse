#include "odh/look_towards.hpp"
#include <iostream>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
}
int main(){
    try{
        {
            odh::World world;
            require(world.create(105,odh::EntityKind::avatar),"create avatar");
            require(world.replace_person_data(105,std::vector<std::int16_t>(80)),"install person data");
            auto primitive=odh::look_towards_primitive(world);
            odh::VmFrame frame;frame.context={105,105,49,105};
            const odh::Instruction instruction{22,254,0,{0,0,0,0,0,0,0,0}};
            const auto outcome=primitive(frame,instruction,{},odh::VmDispatch{nullptr,0});
            require(outcome.disposition==odh::PrimitiveDisposition::goto_true,"mode0 branches true");
            const auto* saved=world.find(105);
            require(saved->person_data[41]==49&&saved->person_data[42]==1&&saved->person_data[43]==0&&
                saved->person_data[44]==1&&saved->person_data[45]==0,"mode0 publishes bounded head-seek policy");
        }
        {
            odh::World world;
            require(world.create(1,odh::EntityKind::avatar),"create avatar");
            require(world.replace_person_data(1,std::vector<std::int16_t>(80)),"install person data");
            auto primitive=odh::look_towards_primitive(world);
            odh::VmFrame frame;frame.context={1,1,2,1};
            const odh::Instruction instruction{22,254,0,{1,0,0,0,0,0,0,0}};
            const auto outcome=primitive(frame,instruction,{},odh::VmDispatch{nullptr,0});
            require(outcome.disposition==odh::PrimitiveDisposition::unsupported&&world.find(1)->person_data[42]==0,
                "unsupported body/camera mode fails without state mutation");
        }
        std::cout<<"LookTowards checks: "<<checks<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<"LookTowards test failed: "<<e.what()<<'\n';return 1;}
}
