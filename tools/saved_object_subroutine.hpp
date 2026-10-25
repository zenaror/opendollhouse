#pragma once

#include "odh/vm.hpp"

// Structural closure check only. Opcode15 is a Simitone-compatible no-op;
// opcode49 still needs a scheduled PhoneLine Main and is rejected until that
// saved thread is registered with the entity scheduler. The only child call
// permitted in update4097 is the read-only relation tree4098.
inline bool saved_phone_update_closure(const odh::Bhav& tree,std::uint16_t id) {
    if((id!=4097&&id!=4098)||tree.instructions.empty()||tree.instructions.size()>4096)return false;
    for(const auto& instruction:tree.instructions){
        const auto opcode=instruction.opcode;
        if(opcode==2)continue;
        if(id==4097&&(opcode==31||opcode==49||opcode==15||opcode==4098))continue;
        if(id==4098&&opcode==26)continue;
        return false;
    }
    return true;
}

// Bounded static admission for the PhoneLine 207 Main path exercised by the
// House07 probe. This is intentionally narrower than a general object BHAV
// loader: every child routine is supplied separately from the exact audited
// PhoneLine/Global resources, and only the opcodes used by this saved path are
// admitted here.
inline bool saved_phone_main_closure(const odh::Bhav& tree,std::uint16_t id) {
    if((id!=280&&id!=320&&id!=4096&&id!=4100&&id!=4104&&id!=4106)||
       tree.instructions.empty()||tree.instructions.size()>4096)return false;
    for(const auto& instruction:tree.instructions){
        const auto opcode=instruction.opcode;
        if(opcode==2)continue;
        if(id==280&&opcode==0)continue;
        if(id==4096&&(opcode==280||opcode==4100||opcode==4106||opcode==320))continue;
        if(id==4100&&opcode==8)continue;
        if(id==4106&&(opcode==4097||opcode==280||opcode==4104))continue;
        if(id==4104&&opcode==31&&(instruction.operands[4]&127)==0)continue;
        if(id==4104&&opcode==26)continue;
        if(id==4104&&opcode==28){
            const auto table=static_cast<std::uint16_t>(instruction.operands[0]|
                (static_cast<std::uint16_t>(instruction.operands[1])<<8));
            if(table==303&&instruction.operands[2]==0&&instruction.operands[4]>=1&&
               instruction.operands[4]<=2&&instruction.operands[5]==2)continue;
        }
        return false;
    }
    return true;
}

// The host must audit/admit the tree and its side effects before calling this
// adapter. This is branch/context plumbing, not permission to run arbitrary
// saved object code or to resume its Main.
struct SavedObjectSubroutineReport {
    odh::VmContext context;
    std::vector<std::int16_t> arguments;
    std::array<std::int16_t,20> temps_before{},temps_after{};
    odh::VmSubroutineResult result;
    std::vector<odh::VmTrace> trace;
};

inline odh::PrimitiveOutcome run_saved_object_subroutine(
    odh::Vm& target, std::int16_t target_id, odh::CodeOwner owner,
    odh::RoutineKey routine, const std::array<std::int16_t,20>& caller_temps,
    SavedObjectSubroutineReport& report, std::uint64_t budget = 4096) {
    report = {};
    if (target.stack().empty() || budget == 0 || budget > 4096)
        return {odh::PrimitiveDisposition::unsupported,
            "saved object subroutine requires a Main context and a budget in 1..4096"};
    report.context = target.stack().back().context;
    report.context.callee = target_id;
    report.context.stack_object = target_id;
    report.context.code_owner = owner;
    report.arguments.assign(caller_temps.begin(), caller_temps.begin()+4);
    report.temps_before=target.temps();
    report.result = target.run_subroutine_in_temporary_stack(routine, report.context,
        report.arguments, budget, [&](const auto& event) { report.trace.push_back(event); });
    report.temps_after=target.temps();
    if (report.result.started && report.result.status == odh::VmStatus::returned_true)
        return {odh::PrimitiveDisposition::goto_true,{}};
    if (report.result.started && report.result.status == odh::VmStatus::returned_false)
        return {odh::PrimitiveDisposition::goto_false,{}};
    return {odh::PrimitiveDisposition::unsupported,
        std::string("saved object subroutine did not return synchronously: ") +
        odh::vm_status_name(report.result.status) + "; " + report.result.diagnostic};
}
