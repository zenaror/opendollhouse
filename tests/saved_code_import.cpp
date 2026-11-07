// SPDX-License-Identifier: MPL-2.0
#include "../tools/saved_code_import.hpp"
#include <iostream>
#include <stdexcept>
int main(){try{
    odh::Objt types{};types.version=1;
    types.entries.push_back({42,odh::ObjtMetadata{},"test",{}, {}});
    odh::DefinitionIndex definitions({});
    int loads=0;
    const odh_host::SavedCodeImportLoader loader=[&](const auto&)->std::shared_ptr<const odh::VmProgram>{++loads;throw std::runtime_error("resource loader must not run");};
    odh::Bhav tree{};tree.version=0x8002;tree.instructions.push_back({2,0xfe,0xfd,{}});
    auto globals=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{odh::routine_key(256),tree}});
    const auto rejected=[&](const odh_host::SavedCodeImportPlan& p,const std::string& reason){
        if(p.comparison.state!=odh::SavedCodePreflightState::unsupported||p.comparison.types.size()!=1||
           p.comparison.types[0].state!=odh::SavedCodeTypeState::unsupported_resources||
           p.unsupported.size()!=1||p.unsupported.at(1).find(reason)==std::string::npos||loads)
            throw std::runtime_error("global budget must reject before reading any definition resource");
    };
    odh::SavedCodePreflightLimits limit{};limit.routine_ids=0;
    rejected(odh_host::prepare_saved_code_import(types,definitions,globals,loader,true,limit),"global routine budget");
    limit={};limit.instructions=0;
    rejected(odh_host::prepare_saved_code_import(types,definitions,globals,loader,true,limit),"global instruction budget");
    rejected(odh_host::prepare_saved_code_import(types,definitions,{},loader),"global code bank unavailable");
    auto empty=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
    rejected(odh_host::prepare_saved_code_import(types,definitions,empty,loader),"definition missing");
    odh::Objt sentinels{};sentinels.version=1;sentinels.entries.push_back({});
    auto no_resources=odh_host::prepare_saved_code_import(sentinels,definitions,{},loader);
    if(no_resources.comparison.state!=odh::SavedCodePreflightState::complete||
       no_resources.comparison.types[0].state!=odh::SavedCodeTypeState::sentinel||!no_resources.resources.empty()||loads)
        throw std::runtime_error("serialized sentinel must not retain or require code resources");
    std::cout<<"saved code import budgets passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
