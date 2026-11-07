// SPDX-License-Identifier: MPL-2.0
#include "odh/ts1_placement_entry_point.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
odh::Bhav tree(std::initializer_list<odh::Instruction> code){return {0x8002,code,{},odh::Bhav::Metadata{0,4,0,0}};}
odh::Instruction set(unsigned lhs,unsigned rhs,unsigned scope,unsigned next=1){return {2,static_cast<std::uint8_t>(next),255,
    {static_cast<std::uint8_t>(lhs),0,static_cast<std::uint8_t>(rhs),0,0,5,static_cast<std::uint8_t>(scope),7}};}
void run(unsigned mode){
    odh::World world;require(world.create(1,odh::EntityKind::avatar,{1,88,3,16})&&world.create(2,odh::EntityKind::object),"EP9 identities");
    std::vector<odh::EntryPoint> entries(27);entries[9].action=mode==2?0:mode==3?4101:4100;
    require(world.define_type(1,1001)&&world.define_entry_points(1,entries)&&world.set_position(1,{100,100,1,0}),"avatar placement metadata");
    odh::RoutineSet owner{{odh::routine_key(4096),tree({{46,254,255,{}}})},
        {odh::routine_key(4100),tree({set(0,7,0),set(3,17,8,2),{95,254,255,{}}})}};
    odh::Vm* active=nullptr;odh::Ts1PlacementEntryPointResult result;bool context=false,main_preserved=false;
    odh::Vm vm({},world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
        odh::Ts1PlacementInvocation invocation{frame,instruction,dispatch,[&]{return active->matches_active_primitive(frame,instruction,dispatch);}};
        odh::Ts1AvatarPlacementEntryPointRequest request;request.world=&world;request.invocation=&invocation;
        request.avatar={1,world.find(1)->generation};request.code_owner=1;request.global_program=active->shared_program_for_owner(0);
        request.owner_program=active->shared_program_for_owner(1);request.live_memory=world.vm_memory();request.register_owner=active;
        request.identities_still_match=[&]{return world.find(1)&&world.find(1)->generation==request.avatar.generation;};
        request.check_primitive=[&](const auto& child,const auto& operation,const auto&,const auto& proof){
            context=child.context.caller==1&&child.context.callee==1&&child.context.stack_object==0&&child.context.code_owner==1&&
                child.arguments==std::vector<std::int16_t>{0,0,0,0}&&proof.check_mode&&proof.origin->matches_active_query(child,operation,proof);
            return odh::PrimitiveOutcome{mode==4?odh::PrimitiveDisposition::wait:odh::PrimitiveDisposition::goto_false,{}};};
        auto stale=instruction;stale.operands[0]=1;
        odh::Ts1PlacementInvocation forged{frame,stale,dispatch,invocation.source_still_matches};
        auto invalid=request;invalid.invocation=&forged;
        require(odh::evaluate_ts1_avatar_placement_entry_point(invalid).state==odh::Ts1PlacementEntryPointState::unsupported&&
            active->temps()[3]==0&&world.find(1)->attributes[0]==0,"forged source instruction cannot run placement effects");
        if(mode==1){request.owner_program=std::make_shared<const odh::VmProgram>(owner);}
        result=odh::evaluate_ts1_avatar_placement_entry_point(request);
        main_preserved=active->stack().size()==1&&active->stack().back().key.id==4096&&active->stack().back().pc==0&&
            active->stack().back().arguments==std::vector<std::int16_t>{31,32,33,34}&&!active->pending()&&
            active->matches_active_primitive(frame,instruction,dispatch);
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
    },{{1,owner}});active=&vm;
    require(vm.start(odh::routine_key(4096),{1,1,2,1},{31,32,33,34},true)==odh::VmStatus::running&&
        vm.run(1)==odh::VmStatus::returned_true&&main_preserved,"placement check retains original executing Main frame/ticket");
    if(mode==0)require(result.state==odh::Ts1PlacementEntryPointState::evaluated_false&&context&&vm.temps()[3]==17&&
        world.find(1)->attributes[0]==7&&vm.steps()==1,"authored false EP9 still publishes live avatar World/Temps effects without ticking Main");
    if(mode==1)require(result.state==odh::Ts1PlacementEntryPointState::unsupported&&!context&&world.find(1)->attributes[0]==0,
        "resource program must be exact bank installed in real avatar Thread");
    if(mode==2||mode==3)require(result.state==odh::Ts1PlacementEntryPointState::evaluated_false&&result.steps==0&&!context,
        "known missing action or routine is completed source false, not missing capability");
    if(mode==4)require(result.state==odh::Ts1PlacementEntryPointState::unsupported&&context&&vm.temps()[3]==17&&
        world.find(1)->attributes[0]==7,"unsupported asynchronous check keeps preceding source effects and leaves Main intact");
}
}
int main(){try{for(unsigned mode=0;mode<5;++mode)run(mode);std::cout<<"TS1 avatar placement entry point checks passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
