#pragma once
#include "odh/animation.hpp"
#include "odh/definition_index.hpp"
#include "odh/world.hpp"
#include "resource.hpp"
#include <algorithm>

// TTAB/TTAs discovery is static. The separate evaluator below reports only an
// isolated ODH VM TestFunction result; neither operation exposes a pie menu or
// implies that an action is currently executable.
struct SavedObjectInteractions {
    enum class State { no_tree_table_id, missing_ttab, found };
    State state=State::missing_ttab;
    std::string source,asset;
    std::uint16_t table_id=0;
    std::optional<odh::Ttab> table;
    odh::StringTable labels{odh::StringTableState::absent,{}, {},0};
    std::uint8_t language=1;
    std::vector<std::optional<std::string>> row_labels;
};

inline SavedObjectInteractions saved_object_interactions(const odh::DefinitionCandidate& definition,
                                                          std::uint8_t language=1) {
    SavedObjectInteractions result;
    result.source=definition.source;result.asset=definition.asset;
    result.table_id=definition.definition.tree_table_id;result.language=language;
    if(!result.table_id){result.state=SavedObjectInteractions::State::no_tree_table_id;return result;}
    std::optional<odh::Ttab> table;std::optional<odh::StringTable> labels;
    const auto scan=[&](const odh::View& view){odh::iff_chunks(view,[&](const odh::Chunk& chunk){
        if(chunk.id!=result.table_id)return;
        if(chunk.type=="TTAB"){
            if(table)throw odh::FormatError("ambiguous object TTAB ID");
            table=odh::read_ttab(chunk.payload);
        }else if(chunk.type=="TTAs"){
            if(labels)throw odh::FormatError("ambiguous object TTAs ID");
            labels=odh::read_strings(chunk.payload);
        }
    });};
    if(definition.asset.empty())with_iff_file(definition.source,scan);
    else with_far_entry(definition.source,definition.asset,scan);
    if(!table)return result;
    result.state=SavedObjectInteractions::State::found;result.table=std::move(table.value());
    if(labels)result.labels=std::move(labels.value());
    result.row_labels.reserve(result.table->interactions.size());
    for(const auto& row:result.table->interactions){
        const auto* label=odh::string_in_language(result.labels,language,row.label_index);
        if(label)result.row_labels.emplace_back(label->value);else result.row_labels.emplace_back(std::nullopt);
    }
    return result;
}

struct SavedTestFunctionResult {
    enum class State { eligible, ineligible, unsupported };
    State state=State::unsupported;
    std::uint16_t routine=0;
    std::uint64_t steps=0;
    std::string diagnostic;
};

// Run TestFunction in an isolated VM, against live entities and a read-only
// World memory view. Only private routines from the exact object resource are
// supplied; external owners, writes, primitives and exhausted budgets remain
// unsupported. This is an ODH VM result, not a claim about the original EXE.
inline std::vector<SavedTestFunctionResult> evaluate_saved_test_functions(
    const odh::DefinitionCandidate& definition,const odh::World& world,
    std::int16_t caller,std::int16_t target,const std::vector<std::uint16_t>& tests,
    std::uint64_t total_budget=4096,std::uint64_t per_test_budget=128,
    const std::vector<std::int16_t>& arguments={0}) {
    constexpr std::size_t test_limit=32;
    if(tests.size()>test_limit||arguments.size()>256)throw odh::FormatError("interaction test arguments/count exceed bounded probe profile");
    std::vector<SavedTestFunctionResult> result;result.reserve(tests.size());
    const auto* actor=world.find(caller);const auto* object=world.find(target);
    if(!actor||actor->kind!=odh::EntityKind::avatar||!object||!object->definition||
       object->definition->guid!=definition.definition.guid){
        for(const auto test:tests)result.push_back({SavedTestFunctionResult::State::unsupported,test,0,"live actor/target identity does not match explicit definition"});
        return result;
    }
    odh::RoutineSet routines;bool invalid=false;std::string load_error;
    try {
        const auto scan=[&](const odh::View& view){odh::Limits limits;limits.iff_chunks=4096;
            odh::iff_chunks(view,[&](const odh::Chunk& chunk){
                if(chunk.type!="BHAV")return;
                const auto key=odh::routine_key(chunk.id);
                if(key.scope!=odh::RoutineScope::private_object)return;
                if(routines.size()>=4096||!routines.emplace(key,odh::read_bhav(chunk.payload)).second)
                    throw odh::FormatError("private BHAV owner is ambiguous or exceeds probe budget");
            },limits);};
        if(definition.asset.empty())with_iff_file(definition.source,scan);
        else with_far_entry(definition.source,definition.asset,scan);
    }catch(const odh::FormatError& error){invalid=true;load_error=error.what();}
    if(invalid){for(const auto test:tests)result.push_back({SavedTestFunctionResult::State::unsupported,test,0,load_error});return result;}

    odh::VmLimits limits;limits.stack_depth=16;limits.frame_variables=256;limits.code_owners=2;
    odh::VmMemory memory;memory.read=[&world](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t index){return world.read(frame,scope,index);};
    odh::RoutineOwners owners;owners.emplace(static_cast<odh::CodeOwner>(target),std::move(routines));
    odh::Vm probe({},std::move(memory),limits,{},std::move(owners));
    std::uint64_t spent=0;
    for(const auto test:tests){
        SavedTestFunctionResult item;item.routine=test;
        const auto key=odh::routine_key(test);
        if(key.scope!=odh::RoutineScope::private_object){item.diagnostic="test owner is not the selected object's private resource";result.push_back(std::move(item));continue;}
        if(spent>=total_budget){item.diagnostic="total TestFunction instruction budget exhausted";result.push_back(std::move(item));continue;}
        const auto budget=std::min(per_test_budget,total_budget-spent);
        const auto generation_actor=actor->generation,generation_object=object->generation;
        const auto started=probe.start(key,{caller,target,target,static_cast<odh::CodeOwner>(target)},arguments);
        const auto status=started==odh::VmStatus::running?probe.run(budget):started;
        item.steps=probe.steps();spent+=item.steps;
        if(status==odh::VmStatus::returned_true)item.state=SavedTestFunctionResult::State::eligible;
        else if(status==odh::VmStatus::returned_false)item.state=SavedTestFunctionResult::State::ineligible;
        else item.diagnostic=probe.diagnostic().empty()?std::string("TestFunction did not return within the supported profile"):probe.diagnostic();
        const auto* live_actor=world.find(caller);const auto* live_object=world.find(target);
        if(!live_actor||!live_object||live_actor->generation!=generation_actor||live_object->generation!=generation_object){
            item.state=SavedTestFunctionResult::State::unsupported;item.diagnostic="live actor/target identity changed during TestFunction evaluation";
        }
        result.push_back(std::move(item));
    }
    return result;
}
