#pragma once

#include "saved_interactions.hpp"
#include "saved_wall_appearance.hpp"
#include "odh/saved_world.hpp"
#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

struct SavedSemiglobalCodeBank {
    std::string archive,entry,name;
    odh::RoutineSet routines;
};

struct SavedGlobalCodeBank {
    std::string archive,entry;
    odh::RoutineSet routines;
};

// Evaluate the small, read-only BHAV profile used by the two known Superstar
// chair entry-point conditions. This deliberately accepts only expression
// instructions: a condition that calls a service or writes World memory stays
// unsupported until that behavior has an explicit host implementation.
inline bool evaluate_saved_entry_point_condition(std::uint16_t condition_id,const odh::Bhav& condition,
    odh::World& world,odh::VmContext context,std::uint64_t instruction_budget=256) {
    if(!condition.metadata||condition.instructions.empty())
        throw odh::FormatError("entry-point condition has no executable metadata or instructions");
    for(const auto& instruction:condition.instructions) {
        if(instruction.opcode!=2)
            throw odh::FormatError("entry-point condition uses an unsupported service or call");
        for(const auto target:{instruction.on_true,instruction.on_false})
            if(target<0xFD&&target>=condition.instructions.size())
                throw odh::FormatError("entry-point condition branches outside its routine");
    }
    auto memory=world.vm_memory();
    memory.write={}; // The condition probe may read the live World, never change it.
    const auto key=odh::routine_key(condition_id);
    odh::Vm evaluator(odh::RoutineSet{{key,condition}},std::move(memory));
    context.code_owner=0; // The isolated probe has exactly one explicitly supplied routine.
    if(evaluator.start(key,context)!=odh::VmStatus::running)
        throw odh::FormatError("entry-point condition could not start");
    const auto result=evaluator.run(instruction_budget);
    if(result==odh::VmStatus::returned_true)return true;
    if(result==odh::VmStatus::returned_false)return false;
    throw odh::FormatError("entry-point condition did not finish within the read-only profile");
}

inline SavedGlobalCodeBank load_saved_global_code_bank(
    const std::string& archive,const std::string& entry) {
    const auto filename=std::filesystem::path(entry).filename().string();
    if(filename!=entry||filename.size()<5||filename.substr(filename.size()-4)!=".iff")
        throw odh::FormatError("global entry must be a flat .iff resource name");
    SavedGlobalCodeBank result{archive,entry,{}};
    std::size_t instructions=0;
    with_far_entry(archive,entry,[&](const odh::View& view){
        odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="BHAV"||odh::routine_key(chunk.id).scope!=odh::RoutineScope::global)return;
            if(result.routines.size()>=1024)throw odh::FormatError("global BHAV bank exceeds routine budget");
            auto routine=odh::read_bhav(chunk.payload);
            if(routine.instructions.size()>32768-instructions)
                throw odh::FormatError("global BHAV bank exceeds instruction budget");
            instructions+=routine.instructions.size();
            if(!result.routines.emplace(odh::routine_key(chunk.id),std::move(routine)).second)
                throw odh::FormatError("duplicate global BHAV in linked resource");
        },limits);
    });
    if(result.routines.empty())throw odh::FormatError("explicit global resource contains no routines");
    return result;
}

inline SavedSemiglobalCodeBank load_saved_semiglobal_code_bank(
    const std::string& archive,const std::string& entry) {
    const auto filename=std::filesystem::path(entry).filename().string();
    if(filename!=entry||filename.size()<5||filename.substr(filename.size()-4)!=".iff")
        throw odh::FormatError("semiglobal entry must be a flat .iff resource name");
    SavedSemiglobalCodeBank result{archive,entry,filename.substr(0,filename.size()-4),{}};
    std::size_t instructions=0;
    with_far_entry(archive,entry,[&](const odh::View& view){
        odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="BHAV"||odh::routine_key(chunk.id).scope!=odh::RoutineScope::semiglobal)return;
            if(result.routines.size()>=512)throw odh::FormatError("semiglobal BHAV bank exceeds routine budget");
            auto routine=odh::read_bhav(chunk.payload);
            if(routine.instructions.size()>16384-instructions)
                throw odh::FormatError("semiglobal BHAV bank exceeds instruction budget");
            instructions+=routine.instructions.size();
            if(!result.routines.emplace(odh::routine_key(chunk.id),std::move(routine)).second)
                throw odh::FormatError("duplicate semiglobal BHAV in linked resource");
        },limits);
    });
    if(result.routines.empty())throw odh::FormatError("linked semiglobal contains no routines");
    return result;
}

struct SavedEntryPointVmCallPlan {
    odh::VmCall call;
    odh::RoutineSet owner_routines;
    SavedEntryPointFunction function;
    std::vector<std::uint16_t> global_dependencies;
    std::optional<std::string> semiglobal_name;
};
using SavedPortalVmCallPlan=SavedEntryPointVmCallPlan;

// Installs the exact private/semiglobal closure at a VM safe point. Shared
// global dependencies remain in the VM's default bank and must be checked by
// the caller before dispatching `call`.
inline bool install_saved_entry_point_vm_call_plan(
    odh::Vm& vm,const SavedEntryPointVmCallPlan& plan) {
    if(plan.call.context.code_owner==0||plan.owner_routines.empty())return false;
    return vm.install_owner(plan.call.context.code_owner,plan.owner_routines);
}
inline bool install_saved_portal_vm_call_plan(
    odh::Vm& vm,const SavedPortalVmCallPlan& plan) {
    return install_saved_entry_point_vm_call_plan(vm,plan);
}

// Prepare an avatar-to-object entry-point call with the resource owner kept explicit. The host allocates
// `code_owner` from the exact resource identity and installs owner_routines at
// a VM safe point before dispatching call through Routing::call_external_frame.
// Entry-point conditions and semiglobal/global bank discovery stay explicit;
// this helper never maps an entity ID to a CodeOwner.
inline SavedPortalVmCallPlan saved_avatar_entry_point_vm_call_plan(
    const odh::SavedWorldEntity& record,const odh::World& world,
    const odh::DefinitionIndex& definitions,std::int16_t caller_id,
    std::int16_t object_id,std::size_t entry_point,odh::CodeOwner code_owner,
    const SavedSemiglobalCodeBank* semiglobal=nullptr,
    const SavedGlobalCodeBank* globals=nullptr,
    bool condition_passed=false) {
    if(caller_id<=0||object_id<=0||code_owner==0)
        throw odh::FormatError("entry-point call identities and resource CodeOwner must be explicit and valid");
    if(record.saved.object_id!=static_cast<std::uint16_t>(object_id))
        throw odh::FormatError("saved World record is not the selected object entity");
    const auto* caller=world.find(caller_id);
    const auto* object=world.find(object_id);
    if(!caller||caller->kind!=odh::EntityKind::avatar||!object||object->kind!=odh::EntityKind::object||
       !object->definition||object->definition->guid!=record.saved.guid||
       !object->entry_points||object->entry_points->size()<=entry_point)
        throw odh::FormatError("live caller or object identity/EntryPoint table unavailable");
    const auto* candidate=definitions.unique(record.saved.guid);
    if(!candidate||candidate->definition.guid!=record.definition.guid)
        throw odh::FormatError("object definition resource is missing or ambiguous");

    const auto function=saved_entry_point_function(*candidate,entry_point);
    const auto& live_function=object->entry_points->at(entry_point);
    if(live_function.condition!=function.condition||live_function.action!=function.action)
        throw odh::FormatError("live EntryPoint differs from the selected definition resource");
    if(function.condition!=0&&!condition_passed)
        throw odh::FormatError("entry-point condition requires an explicit evaluator");
    if(function.action==0)
        throw odh::FormatError("entry-point action is zero");

    const auto key=odh::routine_key(function.action);
    odh::RoutineSet routines;
    if(key.scope==odh::RoutineScope::private_object) {
        routines=load_saved_action_routines(*candidate,function.action);
    } else if(key.scope==odh::RoutineScope::semiglobal) {
        if(!function.semiglobal_name||!semiglobal)
            throw odh::FormatError("semiglobal entry-point root requires its explicit linked bank");
        auto expected=*function.semiglobal_name+".iff";
        auto actual=std::filesystem::path(semiglobal->entry).filename().string();
        auto fold_ascii=[](std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;};
        if(fold_ascii(expected)!=fold_ascii(actual)||semiglobal->name.empty()||
           fold_ascii(semiglobal->name)!=fold_ascii(*function.semiglobal_name))
            throw odh::FormatError("explicit semiglobal resource does not match the object GLOB link");
        const auto root=semiglobal->routines.find(key);
        if(root==semiglobal->routines.end())
            throw odh::FormatError("semiglobal entry-point root is absent from its linked bank");
        routines.emplace(key,root->second);
    } else {
        throw odh::FormatError("global entry-point root requires the shared-owner call path");
    }
    const auto entry=routines.find(key);
    if(entry==routines.end()||!entry->second.metadata)
        throw odh::FormatError("entry-point action routine or metadata absent from exact owner closure");
    std::vector<std::uint16_t> global_dependencies;
    std::vector<std::uint16_t> pending_semiglobal;
    const auto add_global=[&](std::uint16_t id){
        if(id!=0&&std::find(global_dependencies.begin(),global_dependencies.end(),id)==global_dependencies.end())
            global_dependencies.push_back(id);
    };
    const auto scan_dependencies=[&](const odh::RoutineSet& bank,bool owner_bank){
        for(const auto& item:bank)for(const auto& instruction:item.second.instructions){
            if(instruction.opcode<256)continue; // VM primitives are dispatched by the host.
            const auto dependency=odh::routine_key(instruction.opcode);
            if(dependency.scope==odh::RoutineScope::global)add_global(instruction.opcode);
            else if(dependency.scope==odh::RoutineScope::semiglobal)
                pending_semiglobal.push_back(instruction.opcode);
            else if(owner_bank&&!routines.count(dependency))
                throw odh::FormatError("linked semiglobal calls an absent private owner BHAV");
        }
    };
    scan_dependencies(routines,false);
    std::set<std::uint16_t> loaded_semiglobal;
    for(std::size_t cursor=0;cursor<pending_semiglobal.size();++cursor){
        const auto id=pending_semiglobal[cursor];
        if(!loaded_semiglobal.insert(id).second)continue;
        if(!function.semiglobal_name)
            throw odh::FormatError("entry-point action has semiglobal calls but its GLOB link is absent");
        if(!semiglobal)
            throw odh::FormatError("entry-point action requires its explicitly linked semiglobal bank");
        auto expected=*function.semiglobal_name+".iff";
        auto actual=std::filesystem::path(semiglobal->entry).filename().string();
        auto fold_ascii=[](std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;};
        if(fold_ascii(expected)!=fold_ascii(actual)||semiglobal->name.empty()||
           fold_ascii(semiglobal->name)!=fold_ascii(*function.semiglobal_name))
            throw odh::FormatError("explicit semiglobal resource does not match the object GLOB link");
        const auto key=odh::routine_key(id);
        const auto found=semiglobal->routines.find(key);
        if(found==semiglobal->routines.end())
            throw odh::FormatError("semiglobal BHAV dependency absent from linked resource");
        if(routines.size()>=512)throw odh::FormatError("entry-point code-owner closure exceeds routine budget");
        routines.emplace(key,found->second);
        odh::RoutineSet one{{key,found->second}};
        scan_dependencies(one,true);
    }
    std::sort(global_dependencies.begin(),global_dependencies.end());
    if(!global_dependencies.empty()){
        if(!globals)throw odh::FormatError("entry-point closure requires its explicitly linked global bank");
        std::set<std::uint16_t> checked_globals;
        for(std::size_t cursor=0;cursor<global_dependencies.size();++cursor){
            const auto id=global_dependencies[cursor];
            if(!checked_globals.insert(id).second)continue;
            const auto found=globals->routines.find(odh::routine_key(id));
            if(found==globals->routines.end())throw odh::FormatError("global BHAV dependency absent from linked resource");
            for(const auto& instruction:found->second.instructions){
                if(instruction.opcode<256)continue;
                const auto dependency=odh::routine_key(instruction.opcode);
                if(dependency.scope!=odh::RoutineScope::global)
                    throw odh::FormatError("global BHAV crosses into a non-global code owner");
                if(std::find(global_dependencies.begin(),global_dependencies.end(),instruction.opcode)==global_dependencies.end())
                    global_dependencies.push_back(instruction.opcode);
            }
        }
        std::sort(global_dependencies.begin(),global_dependencies.end());
    }
    odh::VmCall call{key,{caller_id,object_id,object_id,code_owner},
        std::vector<std::int16_t>(entry->second.metadata->arguments,0)};
    return {std::move(call),std::move(routines),function,std::move(global_dependencies),
            function.semiglobal_name};
}


inline SavedPortalVmCallPlan saved_portal_vm_call_plan(
    const odh::SavedWorldEntity& record,const odh::World& world,
    const odh::DefinitionIndex& definitions,std::int16_t caller_id,
    std::int16_t portal_id,odh::CodeOwner code_owner,
    const SavedSemiglobalCodeBank* semiglobal=nullptr,
    const SavedGlobalCodeBank* globals=nullptr,bool condition_passed=false) {
    if(record.serialized_object_type!=8)
        throw odh::FormatError("saved World record is not a portal entity");
    return saved_avatar_entry_point_vm_call_plan(record,world,definitions,caller_id,portal_id,15,
        code_owner,semiglobal,globals,condition_passed);
}
