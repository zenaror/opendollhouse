// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/saved_code_compatibility.hpp"
#include "odh/vm.hpp"
#include "saved_wall_appearance.hpp"
#include <functional>
#include <memory>

namespace odh_host {
enum class OriginalImportRecoverySelection { none, home, destination, unsupported };
struct OriginalImportRecoveryBoundary {
    bool saved_world_main=false,fresh_original=false,whole_household=false;
    bool native_lifetime=false,occupancy_invalidated=false,position_diagnostic=false;
    bool authenticated_resources=false,original_native_groups=false,actorless_destination=false;
};
// Shared import selector, independent of explicit CLI flags and travel-option
// spelling. Native resumed life/property and position diagnostics never enter.
inline OriginalImportRecoverySelection select_original_import_recovery(
        const OriginalImportRecoveryBoundary& boundary,const odh::SavedCodePreflight& comparison){
    if(!boundary.saved_world_main||!boundary.fresh_original||!boundary.whole_household||
       boundary.native_lifetime||boundary.occupancy_invalidated||boundary.position_diagnostic)
        return OriginalImportRecoverySelection::none;
    if(!boundary.authenticated_resources||!boundary.original_native_groups||
       comparison.state!=odh::SavedCodePreflightState::complete)
        return OriginalImportRecoverySelection::unsupported;
    return boundary.actorless_destination?OriginalImportRecoverySelection::destination:OriginalImportRecoverySelection::home;
}
struct SavedCodeImportResource {
    odh::DefinitionCandidate definition;
    odh::EntryPointTable entries;
    std::shared_ptr<const odh::VmProgram> code;
};
struct SavedCodeImportPlan {
    odh::SavedCodePreflight comparison;
    std::map<std::uint32_t,SavedCodeImportResource> resources;
    std::map<std::uint16_t,std::string> unsupported;
    bool recovery_executed=false;
};
// Complete source resource banks only. The host may reuse an already frozen
// complete owner, but must not supply an action-specific execution closure.
using SavedCodeImportLoader=std::function<std::shared_ptr<const odh::VmProgram>(const odh::DefinitionCandidate&)>;
inline SavedCodeImportPlan prepare_saved_code_import(const odh::Objt& types,
        const odh::DefinitionIndex& definitions,std::shared_ptr<const odh::VmProgram> globals,
        const SavedCodeImportLoader& loader,bool enabled=true,
        const odh::SavedCodePreflightLimits& limits={}){
    SavedCodeImportPlan plan;
    std::uint64_t resident_instructions=0,resident_routines=0;
    std::string global_diagnostic;
    if(!globals)global_diagnostic="complete global code bank unavailable";
    else if(globals->routines().size()>limits.routine_ids)
        global_diagnostic="saved code global routine budget exceeded";
    else{
        resident_routines=globals->routines().size();
        for(const auto& [key,routine]:globals->routines()){
            (void)key;
            if(routine.instructions.size()>limits.instructions-resident_instructions){
                global_diagnostic="saved code global instruction budget exceeded";break;
            }
            resident_instructions+=routine.instructions.size();
        }
    }
    const auto provide=[&](std::uint16_t type,std::uint32_t guid)->odh::SavedCodeResources{
        try{
            if(!global_diagnostic.empty())throw odh::FormatError(global_diagnostic);
            auto found=plan.resources.find(guid);
            if(found==plan.resources.end()){
                if(!globals||!loader)throw odh::FormatError("complete code resource provider unavailable");
                const auto* candidate=definitions.unique(guid);
                if(!candidate)throw odh::FormatError("saved type definition missing or ambiguous");
                auto entry=saved_entry_point_resource(*candidate,false);
                auto code=loader(*candidate);
                if(!code)throw odh::FormatError("saved type complete code bank unavailable");
                std::uint64_t count=0;
                for(const auto& [key,routine]:code->routines()){
                    (void)key;
                    if(routine.instructions.size()>limits.instructions-count)
                        throw odh::FormatError("saved code resident instruction budget exceeded");
                    count+=routine.instructions.size();
                }
                if(count>limits.instructions-resident_instructions ||
                   code->routines().size()>limits.routine_ids-resident_routines)
                    throw odh::FormatError("saved code resident resource budget exceeded");
                resident_instructions+=count;resident_routines+=code->routines().size();
                found=plan.resources.emplace(guid,SavedCodeImportResource{*candidate,std::move(entry.table),std::move(code)}).first;
            }
            const auto& entry=found->second.entries.entries;
            auto code=found->second.code;
            return {true,entry.empty()?std::uint16_t{0}:entry[0].action,
                entry.size()<2?std::uint16_t{0}:entry[1].action,
                [code,globals](std::uint16_t id)->const odh::Bhav*{
                    const auto key=odh::routine_key(id);
                    const auto& bank=key.scope==odh::RoutineScope::global?globals->routines():code->routines();
                    const auto found=bank.find(key);return found==bank.end()?nullptr:&found->second;
                }};
        }catch(const odh::FormatError& error){plan.unsupported.emplace(type,error.what());return {};}
    };
    plan.comparison=odh::preflight_saved_code_compatibility(types,provide,enabled,limits);
    return plan;
}
} // namespace odh_host
