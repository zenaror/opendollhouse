#pragma once

#include "saved_interactions.hpp"
#include "saved_wall_appearance.hpp"
#include "odh/saved_world.hpp"
#include <algorithm>

struct SavedPortalVmCallPlan {
    odh::VmCall call;
    odh::RoutineSet owner_routines;
    SavedPortalFunction function;
    std::vector<std::uint16_t> global_dependencies;
};

// Prepare the private-only portion of a saved portal call. The host allocates
// `code_owner` from the exact resource identity and installs owner_routines at
// a VM safe point before dispatching call through Routing::call_external_frame.
// Entry-point conditions and semiglobal/global bank discovery stay explicit;
// this helper never maps an entity ID to a CodeOwner.
inline SavedPortalVmCallPlan saved_portal_vm_call_plan(
    const odh::SavedWorldEntity& record,const odh::World& world,
    const odh::DefinitionIndex& definitions,std::int16_t caller_id,
    std::int16_t portal_id,odh::CodeOwner code_owner) {
    if(caller_id<=0||portal_id<=0||code_owner==0)
        throw odh::FormatError("portal call identities and resource CodeOwner must be explicit and valid");
    if(record.saved.object_id!=static_cast<std::uint16_t>(portal_id)||record.serialized_object_type!=8)
        throw odh::FormatError("saved World record is not the selected portal entity");
    const auto* caller=world.find(caller_id);
    const auto* portal=world.find(portal_id);
    if(!caller||caller->kind!=odh::EntityKind::avatar||!portal||portal->kind!=odh::EntityKind::object||
       !portal->definition||portal->definition->guid!=record.saved.guid||
       !portal->entry_points||portal->entry_points->size()<=15)
        throw odh::FormatError("live caller or portal identity/EntryPoint table unavailable");
    const auto* candidate=definitions.unique(record.saved.guid);
    if(!candidate||candidate->definition.guid!=record.definition.guid)
        throw odh::FormatError("portal definition resource is missing or ambiguous");

    const auto function=saved_portal_function(*candidate);
    const auto& live_function=portal->entry_points->at(15);
    if(live_function.condition!=function.condition||live_function.action!=function.action)
        throw odh::FormatError("live Portal15 differs from the selected definition resource");
    if(function.condition!=0)
        throw odh::FormatError("portal EntryPoint 15 condition requires an explicit evaluator");
    if(function.action==0||function.action<4096||function.action>=8192)
        throw odh::FormatError("portal EntryPoint 15 action is not a private BHAV");

    auto routines=load_saved_action_routines(*candidate,function.action);
    const auto key=odh::routine_key(function.action);
    const auto entry=routines.find(key);
    if(entry==routines.end()||!entry->second.metadata)
        throw odh::FormatError("portal action routine or metadata absent from exact owner closure");
    std::vector<std::uint16_t> global_dependencies;
    for(const auto& item:routines)for(const auto& instruction:item.second.instructions){
        if(instruction.opcode<256)continue; // VM primitives are dispatched by the host.
        const auto dependency=odh::routine_key(instruction.opcode);
        if(dependency.scope==odh::RoutineScope::semiglobal)
            throw odh::FormatError("portal action closure requires an unresolved semiglobal BHAV");
        if(dependency.scope==odh::RoutineScope::global&&instruction.opcode!=0&&
           std::find(global_dependencies.begin(),global_dependencies.end(),instruction.opcode)==global_dependencies.end())
            global_dependencies.push_back(instruction.opcode);
    }
    std::sort(global_dependencies.begin(),global_dependencies.end());
    odh::VmCall call{key,{caller_id,portal_id,portal_id,code_owner},
        std::vector<std::int16_t>(entry->second.metadata->arguments,0)};
    return {std::move(call),std::move(routines),function,std::move(global_dependencies)};
}
