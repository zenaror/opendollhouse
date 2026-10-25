#include "saved_object_subroutine.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
odh::Bhav tree(std::uint8_t branch){
    return {0x8002,{{1,branch,branch,{}}},{},odh::Bhav::Metadata{0,4,0,0}};
}
}
int main(){try{
    constexpr odh::CodeOwner owner=2933422533U;
    const auto main=odh::routine_key(4096),child=odh::routine_key(4097);
    std::array<std::int16_t,20> caller_temps{};caller_temps[0]=42;caller_temps[1]=17;
    for(const auto status:{odh::VmStatus::returned_true,odh::VmStatus::returned_false,
                          odh::VmStatus::budget_exhausted,odh::VmStatus::fault,odh::VmStatus::waiting}){
        const auto branch=static_cast<std::uint8_t>(status==odh::VmStatus::returned_false?255:
            status==odh::VmStatus::budget_exhausted?0:254);
        odh::Vm target({}, {}, {}, [&](const auto& frame,const auto&,const auto& temps,const auto&){
            require(frame.context.caller==71&&frame.context.callee==207&&frame.context.stack_object==207&&
                frame.context.code_owner==owner,"callee uses top saved Main caller and exact target owner");
            require(frame.arguments[0]==42&&frame.arguments[1]==17&&temps[0]==9,
                "caller Temps become arguments while target Temps remain its own");
            if(status==odh::VmStatus::fault)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"synthetic fault"};
            if(status==odh::VmStatus::waiting)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
            odh::PrimitiveOutcome outcome{odh::PrimitiveDisposition::goto_true,{}};
            outcome.temp_write=odh::VmTempWrite{1,99};
            return outcome;
        },{{owner,{{main,tree(254)},{child,tree(branch)}}}});
        std::array<std::int16_t,20> target_temps{};target_temps[0]=9;
        target.restore_dormant_state({{main,0,{70,72,73,owner},{1},{2}},
            {main,0,{71,74,75,owner},{3},{4}}},target_temps);
        const auto before=target.snapshot();
        SavedObjectSubroutineReport report;
        const auto outcome=run_saved_object_subroutine(target,207,owner,child,caller_temps,report,3);
        require(report.result.started&&report.result.status==status,"subroutine records real execution status");
        const auto expected=status==odh::VmStatus::returned_true?odh::PrimitiveDisposition::goto_true:
            status==odh::VmStatus::returned_false?odh::PrimitiveDisposition::goto_false:odh::PrimitiveDisposition::unsupported;
        require(outcome.disposition==expected,"only completed true/false become a parent branch");
        require(report.trace.size()==report.result.steps&&report.result.steps==(status==odh::VmStatus::budget_exhausted?3:1),
            "bounded trace and step count include every executed instruction");
        auto expected_temps=target_temps;
        if(status!=odh::VmStatus::fault&&status!=odh::VmStatus::waiting)expected_temps[1]=99;
        require(report.temps_before==target_temps&&report.temps_after==expected_temps&&caller_temps[1]==17,
            "report retains target register writes without publishing them to the calling thread");
        require(target.status()==odh::VmStatus::dormant&&target.steps()==0&&!target.pending()&&target.stack().size()==2&&
            target.stack().back().context.stack_object==75&&target.stack().back().arguments==before.stack.back().arguments&&
            target.stack().back().locals==before.stack.back().locals&&target.temps()==expected_temps,
            "true false budget fault and wait all preserve dormant Main and retain the target registers");
    }
    odh::Vm empty({});SavedObjectSubroutineReport rejected;
    require(run_saved_object_subroutine(empty,207,owner,child,caller_temps,rejected).disposition==
        odh::PrimitiveDisposition::unsupported&&!rejected.result.started,
        "missing saved Main cannot be presented as a completed false result");
    for(const auto budget:{1U,3U}){
        std::int16_t world_attribute=0;
        odh::VmMemory memory;
        memory.read=[&](const auto&,auto,auto)->std::optional<std::int16_t>{return world_attribute;};
        memory.write=[&](const auto&,auto,auto,std::int16_t value){world_attribute=value;return true;};
        auto effect=tree(254);
        effect.instructions={{2,1,253,{0,0,7,0,0,5,0,7}},{49,254,255,{}}};
        odh::Vm target({},memory,{},[](const auto&,const auto&,const auto&,const auto&){
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"unscheduled NotifyOutOfIdle"};
        },{{owner,{{main,tree(254)},{child,effect}}}});
        target.restore_dormant_state({{main,0,{207,207,0,owner},{},{}}},{});
        SavedObjectSubroutineReport report;
        const auto outcome=run_saved_object_subroutine(target,207,owner,child,caller_temps,report,budget);
        require(outcome.disposition==odh::PrimitiveDisposition::unsupported&&world_attribute==7&&
            report.result.status==(budget==1?odh::VmStatus::budget_exhausted:odh::VmStatus::fault)&&
            target.status()==odh::VmStatus::dormant&&target.steps()==0,
            "external writes persist across budget/fault while the parent receives no false success");
    }
    auto closure=tree(254);
    closure.instructions[0].opcode=4098;
    require(saved_phone_update_closure(closure,4097),"audited private dependency is admitted");
    for(const auto opcode:{280,4099,8192,0,28}){
        closure.instructions[0].opcode=static_cast<std::uint16_t>(opcode);
        require(!saved_phone_update_closure(closure,4097),"unresolved children and foreign primitives are rejected");
    }
    closure.instructions[0].opcode=26;
    require(saved_phone_update_closure(closure,4098),"relation-only child is admitted");
    closure.instructions[0].opcode=4097;
    require(!saved_phone_update_closure(closure,4098),"recursive child closure is rejected");
    std::cout<<"saved object subroutine context/returns/budget/fault/wait passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
