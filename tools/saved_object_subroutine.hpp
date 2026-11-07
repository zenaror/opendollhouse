#pragma once

#include "odh/vm.hpp"
#include <algorithm>

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

// The host has already resolved the name, string table, exact resource bank
// and target entity. This adapter only authenticates the source primitive and
// applies VMRunTreeByName/RunInMyStack context and argument rules.
struct SavedNamedTreeRequest {
    const odh::VmFrame& source_frame;
    const odh::Instruction& source_instruction;
    odh::VmDispatch dispatch;
    std::int16_t target;
    odh::CodeOwner owner;
    odh::RoutineKey routine;
    std::uint8_t destination; // Authored byte: 0=caller check, 2=child, all others=target check.
};

inline odh::PrimitiveOutcome run_saved_named_tree(odh::Vm& caller,odh::Vm* target_vm,
    const SavedNamedTreeRequest& request,const odh::Ts1Temps& caller_temps,
    SavedObjectSubroutineReport& report,const odh::VmPrimitive& check_primitive={},std::uint64_t budget=4096,
    const odh::VmPrimitiveFactory& primitive_factory={}){
    report={};
    const auto reject=[](const std::string& why){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,why};};
    if(request.target<=0||!budget||budget>4096||
       request.source_instruction.opcode!=28||request.source_instruction.operands[5]!=request.destination||
       request.source_frame.context.stack_object!=request.target||
       !caller.matches_active_query(request.source_frame,request.source_instruction,request.dispatch))
        return reject("named tree requires an exact active RunTreeByName dispatch and resolved target");
    if(request.source_frame.context.entity_references_bound&&
       !odh::vm_context_reference(request.source_frame.context,odh::VmEntityRole::stack_object).generation)
        return {odh::PrimitiveDisposition::goto_false,{}};
    const auto destination=request.destination==0?0:request.destination==2?2:1;
    auto* execution=destination==1?target_vm:&caller;
    if(destination==1&&execution&& &execution->entity_thread_frames()!=&execution->stack())
        return reject("named target Entity.Thread overlaps a logical Check; parked-thread execution bridge is unavailable");
    if(!execution||!execution->has_routine(request.routine,request.owner)||
       !odh::routine_key_valid(request.routine))
        return reject("named tree exact execution resource is unavailable");
    const auto program=execution->shared_program_for_owner(request.routine.scope==odh::RoutineScope::global?0:request.owner);
    if(!program)return reject("named tree immutable resource bank is unavailable");
    const auto found=program->routines().find(request.routine);
    if(found==program->routines().end())return reject("named tree routine is absent from its authenticated bank");
    if(found->second.instructions.empty()){
        // Source Push(empty) adds no child. RunInMyStack returns false;
        // destination2 CONTINUE leaves the real calling instruction in place.
        if(destination==2)return {odh::PrimitiveDisposition::continue_execution,{}};
        report.result={true,odh::VmStatus::returned_false,0,"known named routine is empty"};
        return {odh::PrimitiveDisposition::goto_false,{}};
    }
    auto metadata=found->second.metadata;
    if(!metadata&&(found->second.version==0x8000||found->second.version==0x8001))metadata=odh::Bhav::Metadata{0,4,0,0};
    if(!metadata)return reject("named tree routine metadata is unavailable");
    report.context=destination==1?(execution->stack().empty()?
        odh::VmContext{request.target,request.target,request.target,request.owner}:execution->stack().back().context):
        request.source_frame.context;
    if(destination==1&&execution->stack().empty()&&request.source_frame.context.entity_references_bound){
        report.context=request.source_frame.context;
        if(!odh::vm_context_copy_reference(report.context,odh::VmEntityRole::caller,request.source_frame.context,odh::VmEntityRole::stack_object))
            return reject("empty named executor lacks its exact entity reference");
    }
    report.context.code_owner=request.owner;
    if(destination!=2){
        if(!execution->bind_context_live(report.context))return reject("named executor parent references cannot be bound");
        if(request.source_frame.context.entity_references_bound){
            if(!report.context.entity_references_bound||
                !odh::vm_context_copy_reference(report.context,odh::VmEntityRole::callee,request.source_frame.context,odh::VmEntityRole::stack_object)||
                !odh::vm_context_copy_reference(report.context,odh::VmEntityRole::stack_object,request.source_frame.context,odh::VmEntityRole::stack_object))
                return reject("named target pointer cannot be copied into executor context");
        }else{
            // Legacy fixture without an entity resolver remains explicitly unbound.
            if(report.context.entity_references_bound)return reject("legacy named source lacks target-generation authority");
            report.context.callee=request.target;report.context.stack_object=request.target;
        }
    }
    report.arguments.assign(caller_temps.begin(),caller_temps.begin()+4);
    report.arguments.resize(std::max<std::size_t>(4,metadata->arguments),-1);
    report.temps_before=execution->temps();
    if(destination==2){
        odh::VmCall call{request.routine,report.context,report.arguments};
        if(!caller.can_accept_call(call))return reject("named tree child exceeds the active VM resource or frame budget");
        report.temps_after=execution->temps();
        return {odh::PrimitiveDisposition::call,{}, {},std::move(call)};
    }
    // The kernel preserves real parent stacks, pending services and register
    // ownership. No fake thread or independently seeded World is constructed.
    report.result=execution->run_named_in_stack(request.routine,report.context,report.arguments,budget,check_primitive,primitive_factory);
    report.temps_after=execution->temps();
    if(report.result.started&&report.result.status==odh::VmStatus::returned_true)return {odh::PrimitiveDisposition::goto_true,{}};
    if(report.result.started&&report.result.status==odh::VmStatus::returned_false)return {odh::PrimitiveDisposition::goto_false,{}};
    // RunInMyStack restores its caller and reports false for an authored ERROR.
    // Keep unsupported primitives, invalid resources, limits and host faults
    // distinct: those are incomplete execution, not a negative check result.
    if(destination!=2&&report.result.started&&report.result.status==odh::VmStatus::fault&&report.result.source_error)
        return {odh::PrimitiveDisposition::goto_false,{}};
    return reject("named tree did not return synchronously: "+std::string(odh::vm_status_name(report.result.status))+"; "+report.result.diagnostic);
}

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
