#include "linux_vm_dialog.hpp"
#include "odh/vm.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}

int main(){
    try{
        require(!odh_host::vm_message_dialog_layout(200,600),"too-narrow frame accepted");
        const auto layout=odh_host::vm_message_dialog_layout(800,600);
        require(layout.has_value(),"ordinary game frame rejected");
        require(layout->x==140&&layout->y==184&&layout->width==520&&layout->height==232,
            "dialog panel is not centered at the bounded size");
        require(odh_host::vm_message_dialog_ok_hit(*layout,layout->ok_x,layout->ok_y),
            "OK button top-left was not accepted");
        require(!odh_host::vm_message_dialog_ok_hit(*layout,layout->x,layout->y),
            "panel area was confused with the OK button");
        require(odh_host::expand_vm_dialog_local_references(
            "That will be $$Local:1.",{0,99})=="That will be $99.",
            "game dialog Local reference or escaped dollar was not expanded");
        bool unknown_marker_rejected=false;
        try{(void)odh_host::expand_vm_dialog_local_references("$Unknown:1",{});}
        catch(const std::invalid_argument&){unknown_marker_rejected=true;}
        require(unknown_marker_rejected,"unknown game string marker was silently changed");

        odh::RasterFrame frame{};frame.width=800;frame.height=600;
        frame.pixels.assign(800U*600U,{128,128,128,255});
        frame.depth.assign(frame.pixels.size(),1.0f);
        odh_host::draw_vm_message_dialog(frame,"SimCity Pizza",
            "That will be $99. Your pizza should arrive soon.","OK",1);
        const auto pixel_at=[&](std::int32_t x,std::int32_t y)->const auto&{
            return frame.pixels[static_cast<std::size_t>(y)*frame.width+static_cast<std::size_t>(x)];
        };
        require(pixel_at(layout->x+5,layout->y+10)==std::array<std::uint8_t,4>{68,72,75,255},
            "dialog panel background was not drawn");
        require(pixel_at(layout->ok_x+4,layout->ok_y+4)==std::array<std::uint8_t,4>{102,76,43,255},
            "dialog OK button was not drawn");
        require(pixel_at(0,0)==std::array<std::uint8_t,4>{48,48,48,255},
            "scene behind the modal was not dimmed");

        const auto waiting_vm=[](){
            odh::Instruction request{36,254,255,{}};
            odh::Bhav routine{0x8002,{request},{},odh::Bhav::Metadata{0,4,0,0}};
            const odh::VmPrimitive primitive=[](const auto&,const auto& instruction,const auto&,const auto&){
                return instruction.opcode==36?odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}}:
                    odh::PrimitiveOutcome{};
            };
            return odh::Vm({{odh::routine_key(8312),std::move(routine)}},{},{},primitive);
        };
        auto accepted=waiting_vm();
        accepted.start(odh::routine_key(8312),{105,105,136,0});
        require(accepted.run(1)==odh::VmStatus::waiting&&accepted.pending(),
            "private dialog did not preserve the VM while awaiting a response");
        const auto ticket=accepted.pending()->ticket;
        require(!accepted.complete(ticket+1,true),"dialog accepted a stale VM response ticket");
        require(accepted.complete(ticket,true)&&accepted.status()==odh::VmStatus::returned_true,
            "OK response did not resume the blocked BHAV through its true branch");
        auto declined=waiting_vm();
        declined.start(odh::routine_key(8312),{105,105,136,0});
        require(declined.run(1)==odh::VmStatus::waiting&&declined.pending()&&
            declined.complete(declined.pending()->ticket,false)&&
            declined.status()==odh::VmStatus::returned_false,
            "false dialog response did not take the BHAV failure branch");
        std::cout<<"linux_vm_dialog ok\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"linux_vm_dialog: "<<error.what()<<'\n';
        return 1;
    }
}
