#include "odh/special_effect.hpp"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
using Bytes=std::array<std::uint8_t,8>;
odh::RoutineSet program(Bytes bytes) {
    return {{odh::routine_key(4096),odh::Bhav{0x8002,{{35,254,255,bytes}}, {},
        odh::Bhav::Metadata{0,0,0,0}}}};
}
odh::VmStatus run(odh::World& world,Bytes bytes,odh::SpecialEffectServices services,
                  odh::VmContext context={1,1,2}) {
    require(world.bind_context_live(context),"bind source context");
    odh::Vm vm{program(bytes),world.vm_memory(),{},odh::special_effect_primitive(world,std::move(services))};
    vm.start(odh::routine_key(4096),context);
    return vm.run(1);
}
}
int main(){try{
    const auto parsed=odh::parse_special_effect_operands({255,255,128,255,128,255,90,165});
    require(parsed.timeout==65535&&parsed.size==-128&&parsed.zoom==-1&&parsed.flags==-128&&
        parsed.string_index==-1&&parsed.unused==std::array<std::uint8_t,2>{90,165},
        "all operand bytes retain their source signedness and opaque tail");
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{})&&world.create(2,odh::EntityKind::object,{}),"fixture");
    unsigned calls=0;
    std::function<bool()> retained;
    odh::WorldEntityReference focused;
    odh::SpecialEffectServices visual{true,[&](const odh::SpecialEffectFocusRequest& request){
        ++calls;focused=request.target;retained=request.source_still_matches;
        require(retained(),"focus carries live actual dispatch authority");
        return odh::SpecialEffectFocusResult{true,{}};
    }};
    require(run(world,{10,0,1,2,0x43,0,0,0},visual,{1,1,99})==odh::VmStatus::returned_true&&calls==0,
        "actual failure-sleep operand returns True without dereferencing a nil StackObject");
    auto headless=visual;headless.use_world=false;
    require(run(world,{255,255,255,255,255,255,255,255},headless,{1,1,99})==odh::VmStatus::returned_true&&calls==0,
        "headless skips all visual flags and ignores timeout without a wait");
    require(run(world,{0,0,0,0,8,0,0,0},visual)==odh::VmStatus::returned_true&&calls==1&&
        focused==odh::WorldEntityReference{2,world.find(2)->generation}&&!retained(),
        "visual focus targets exact identity once and authority expires after return");
    require(run(world,{0,0,0,0,8,0,0,0},{true,{}})==odh::VmStatus::fault,
        "required visual backend absence remains unsupported");
    require(run(world,{0,0,0,0,8,0,0,0},visual,{1,1,99})==odh::VmStatus::fault&&calls==1,
        "visual nil StackObject never becomes an invented camera origin");
    auto reject=visual;reject.center_to=[](const auto&){return odh::SpecialEffectFocusResult{false,"missing pelvis"};};
    require(run(world,{0,0,0,0,8,0,0,0},reject)==odh::VmStatus::fault,"backend inability is not fake success");
    auto throws=visual;throws.center_to=[](const auto&)->odh::SpecialEffectFocusResult{throw std::runtime_error("backend");};
    require(run(world,{0,0,0,0,8,0,0,0},throws)==odh::VmStatus::fault,"backend exception is a bounded unsupported result");
    // Reject fabricated calls even when the effect would otherwise be headless.
    auto handler=odh::special_effect_primitive(world,headless);
    odh::VmFrame fake{odh::routine_key(4096),0,{1,1,2},{},{}};
    odh::Instruction instruction{35,254,255,{}};
    require(handler(fake,instruction,{},{}).disposition==odh::PrimitiveDisposition::unsupported,
        "no fake dispatch admission");
    odh::VmPrimitive authenticated=odh::special_effect_primitive(world,visual);
    odh::Vm proof_vm{program({0,0,0,0,8,0,0,0}),world.vm_memory(),{},
        [&](const auto& actual_frame,const auto& actual_instruction,const auto& temps,const auto& dispatch){
            auto forged_frame=actual_frame;++forged_frame.context.stack_object_generation;
            require(authenticated(forged_frame,actual_instruction,temps,dispatch).disposition==odh::PrimitiveDisposition::unsupported,
                "changed generation cannot reuse actual dispatch");
            auto forged_instruction=actual_instruction;forged_instruction.operands[0]^=1;
            require(authenticated(actual_frame,forged_instruction,temps,dispatch).disposition==odh::PrimitiveDisposition::unsupported,
                "changed authored operands cannot reuse actual dispatch");
            auto forged_dispatch=dispatch;++forged_dispatch.ticket;
            require(authenticated(actual_frame,actual_instruction,temps,forged_dispatch).disposition==odh::PrimitiveDisposition::unsupported,
                "changed ticket cannot reuse actual dispatch");
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        }};
    proof_vm.start(odh::routine_key(4096),{1,1,2});
    require(proof_vm.run(1)==odh::VmStatus::returned_true&&calls==1,"forgery checks do not invoke visual backend");
    // Real check execution uses query authentication rather than primitive-only proof.
    odh::Vm check_vm{program({0,0,0,0,8,0,0,0}),world.vm_memory()};
    odh::VmContext context{1,1,2};require(world.bind_context_live(context),"check context");
    auto checked=check_vm.evaluate_check_at_safe_point(odh::routine_key(4096),context,{},4,
        odh::special_effect_primitive(world,visual));
    require(checked.status==odh::VmStatus::returned_true&&calls==2&&!retained(),"actual synchronous check can request the same visual effect");
    // Old generation references remain exact after immediate numeric ID reuse.
    const auto old=world.find(2)->generation;
    context={1,1,2};require(world.bind_context_live(context)&&world.remove_retaining_body({2,old})&&
        world.create(2,odh::EntityKind::object,{}),"retained and replacement fixture");
    require(run(world,{0,0,0,0,8,0,0,0},visual,context)==odh::VmStatus::returned_true&&focused.generation==old&&
        focused.generation!=world.find(2)->generation,"old exact visual identity never binds the replacement");
    // A physical deletion/reuse during the callback cannot bless a replacement.
    auto mutate=visual;mutate.center_to=[&](const auto& request){
        require(request.source_still_matches(),"mutation enters valid scope");
        require(world.remove(2)&&world.create(2,odh::EntityKind::object,{}),"callback replacement");
        return odh::SpecialEffectFocusResult{true,{}};
    };
    require(run(world,{0,0,0,0,8,0,0,0},mutate)==odh::VmStatus::fault,"callback replacement invalidates focus proof");
    std::cout<<checks<<" special effect checks passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"special_effect: "<<e.what()<<'\n';return 1;}}
