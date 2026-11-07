// SPDX-License-Identifier: MPL-2.0
#include "odh/autonomy_check.hpp"
#include <set>

namespace odh {
void validate_saved_autonomy_table(const odh::Ttab& table){
    if(table.status!=odh::TtabStatus::decoded&&table.status!=odh::TtabStatus::empty)
        throw odh::FormatError("autonomy TTAB encoding is unsupported");
    std::set<std::uint32_t> indices;
    for(const auto& row:table.interactions)if(!indices.insert(row.label_index).second)
        throw odh::FormatError("duplicate autonomy TTAB interaction index"); // Source Dictionary.Add.
}
bool saved_autonomy_action_available(const odh::Interaction& row,const odh::VmProgram& global,
    const odh::VmProgram& resource){
    const auto present=[&](std::uint16_t id){const auto key=odh::routine_key(id);
        return (key.scope==odh::RoutineScope::global?global.routines():resource.routines()).count(key)!=0;};
    return present(row.action)&&(!row.test||present(row.test));
}

// VMThread.CheckTS1Action(auto=true) uses a real disposable check thread,
// actual resource banks, shared caller registers and live World effects. It
// does not use GetPieMenu's visibility filter or read-only menu evaluator.
odh::AutonomyResult<odh::AutonomyCheck> evaluate_saved_autonomy_check(
    odh::World& world,odh::Vm& caller,std::int16_t actor,std::int16_t target,
    odh::CodeOwner owner,const odh::Interaction& row,
    const std::shared_ptr<const odh::VmProgram>& global,const std::shared_ptr<const odh::VmProgram>& resource,
    odh::VmMemory memory,const odh::VmPrimitive& primitive,const SavedAutonomyString& string,
    std::uint64_t budget,const SavedAutonomyPrimitiveFactory& factory,const Vm::OwnerResolver& owner_resolver){
    using namespace odh;
    const auto bad=[](std::string why){return AutonomyResult<AutonomyCheck>{AutonomyStatus::unsupported,{},std::move(why)};};
    const auto* person=world.find(actor);const auto* object=world.find(target);
    if(!person||!object||person->kind!=EntityKind::avatar||!global||!resource||!owner||!budget)
        return bad("autonomous check lacks real caller/target/resource facts");
    // Source FindBestAction clears the real Caller field even without a test.
    if(!world.declare_object_word(actor,50,0))return bad("autonomous HideInteraction reset failed");
    const auto key=routine_key(row.test);
    if(!saved_autonomy_action_available(row,*global,*resource))
        return {AutonomyStatus::ready,{false,{}},{}}; // Known null GetAction, not unavailable resources.
    const auto permission=ts1_interaction_permission(person,row.flags);
    if(permission==InteractionPermission::unsupported)return bad("autonomous check permissions are unknown");
    if(permission==InteractionPermission::denied)return {AutonomyStatus::ready,{false,{}},{}};
    if(!row.test)return {AutonomyStatus::ready,{true,{}},{}};
    std::vector<AutonomyVariant> variants;
    VmPrimitive effective_primitive;
    VmPrimitive checks=[&](const VmFrame& frame,const Instruction& instruction,const Ts1Temps& temps,const VmDispatch& dispatch){
        if(instruction.opcode==50){
            if(!string)return PrimitiveOutcome{PrimitiveDisposition::unsupported,"autonomous action-string provider unavailable"};
            const auto text=string(frame,instruction,temps);
            if(text.status!=AutonomyStatus::ready)return PrimitiveOutcome{PrimitiveDisposition::unsupported,text.diagnostic};
            if(text.value){if(variants.size()>=1024)return PrimitiveOutcome{PrimitiveDisposition::unsupported,"autonomous action-string variant budget"};
                variants.push_back({*text.value,frame.context.stack_object,{},text.origin});}
            return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
        }
        return effective_primitive?effective_primitive(frame,instruction,temps,dispatch):PrimitiveOutcome{};
    };
    SharedRoutineOwners owners{{owner,resource}};
    // Some real primitive factories retain const VmMemory&. Keep this local
    // adapter intact and alive; copy its closures into the disposable VM.
    Vm check(global,ShareVmProgramTag{},memory,{},checks,std::move(owners),ShareVmOwnerProgramsTag{});
    if(!check.set_owner_resolver(owner_resolver))return bad("autonomous check resource context could not bind");
    // Every operand-scope consumer sees the actual disposable Thread adapter.
    effective_primitive=factory?factory(check.effective_memory()):primitive;
    if(check.start(key,{actor,target,target,owner},{1,0,0,0},true)!=VmStatus::running)
        return bad("autonomous check could not start: "+check.diagnostic());
    const auto result=check.run_synchronous_check_with_registers(caller,true,budget,checks);
    // Same authored EvaluateCheck boundary as the native queued/menu checks.
    const bool source_false=result.started&&result.status==VmStatus::fault&&result.source_error;
    if(!result.started||(result.status!=VmStatus::returned_true&&result.status!=VmStatus::returned_false&&!source_false))
        return bad("autonomous check did not return synchronously: "+result.diagnostic);
    if(variants.empty())variants.push_back({{},0,check.advertisement_changes()});
    return {AutonomyStatus::ready,{result.status==VmStatus::returned_true,std::move(variants)},{}};
}

} // namespace odh
