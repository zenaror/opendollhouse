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

struct SavedPortalVmCallPlan {
    odh::VmCall call;
    odh::RoutineSet owner_routines;
    SavedPortalFunction function;
    std::vector<std::uint16_t> global_dependencies;
    std::optional<std::string> semiglobal_name;
};

// Prepare the private-only portion of a saved portal call. The host allocates
// `code_owner` from the exact resource identity and installs owner_routines at
// a VM safe point before dispatching call through Routing::call_external_frame.
// Entry-point conditions and semiglobal/global bank discovery stay explicit;
// this helper never maps an entity ID to a CodeOwner.
inline SavedPortalVmCallPlan saved_portal_vm_call_plan(
    const odh::SavedWorldEntity& record,const odh::World& world,
    const odh::DefinitionIndex& definitions,std::int16_t caller_id,
    std::int16_t portal_id,odh::CodeOwner code_owner,
    const SavedSemiglobalCodeBank* semiglobal=nullptr) {
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
            throw odh::FormatError("portal action has semiglobal calls but GLOB128 is absent");
        if(!semiglobal)
            throw odh::FormatError("portal action requires its explicitly linked semiglobal bank");
        auto expected=*function.semiglobal_name+".iff";
        auto actual=std::filesystem::path(semiglobal->entry).filename().string();
        auto fold_ascii=[](std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;};
        if(fold_ascii(expected)!=fold_ascii(actual)||semiglobal->name.empty()||
           fold_ascii(semiglobal->name)!=fold_ascii(*function.semiglobal_name))
            throw odh::FormatError("explicit semiglobal resource does not match portal GLOB128");
        const auto key=odh::routine_key(id);
        const auto found=semiglobal->routines.find(key);
        if(found==semiglobal->routines.end())
            throw odh::FormatError("semiglobal BHAV dependency absent from linked resource");
        if(routines.size()>=512)throw odh::FormatError("portal code-owner closure exceeds routine budget");
        routines.emplace(key,found->second);
        odh::RoutineSet one{{key,found->second}};
        scan_dependencies(one,true);
    }
    std::sort(global_dependencies.begin(),global_dependencies.end());
    odh::VmCall call{key,{caller_id,portal_id,portal_id,code_owner},
        std::vector<std::int16_t>(entry->second.metadata->arguments,0)};
    return {std::move(call),std::move(routines),function,std::move(global_dependencies),
            function.semiglobal_name};
}
