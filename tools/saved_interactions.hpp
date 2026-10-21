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

// Load executable code only from the exact resource that supplied the selected
// definition. Archive precedence and global/semiglobal fallback are forbidden.
inline odh::RoutineSet load_saved_private_routines(const odh::DefinitionCandidate& definition,
                                                    std::size_t routine_limit=4096) {
    odh::RoutineSet routines;
    const auto scan=[&](const odh::View& view){odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="BHAV")return;
            const auto key=odh::routine_key(chunk.id);
            if(key.scope!=odh::RoutineScope::private_object)return;
            if(routines.size()>=routine_limit||!routines.emplace(key,odh::read_bhav(chunk.payload)).second)
                throw odh::FormatError("private BHAV owner is ambiguous or exceeds resource budget");
        },limits);};
    if(definition.asset.empty())with_iff_file(definition.source,scan);
    else with_far_entry(definition.source,definition.asset,scan);
    return routines;
}

struct SavedTreeNameCatalog {
    std::vector<std::string> private_bhav_labels;
    std::optional<std::string> semiglobal_name;
};

// TreeByName in the reference is assembled from local BHAV chunk labels plus
// the linked semiglobal's BHAV labels. Keep the link explicit so callers do not
// treat a partial local scan as a complete negative lookup.
inline SavedTreeNameCatalog load_saved_tree_name_catalog(const odh::DefinitionCandidate& definition) {
    SavedTreeNameCatalog result;
    const auto scan=[&](const odh::View& view){odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type=="GLOB"&&!chunk.label.empty()){
                if(result.semiglobal_name&&*result.semiglobal_name!=chunk.label)
                    throw odh::FormatError("conflicting semiglobal links in exact object resource");
                result.semiglobal_name=chunk.label;
            }
            if(chunk.type=="BHAV"&&odh::routine_key(chunk.id).scope==odh::RoutineScope::private_object)
                result.private_bhav_labels.push_back(chunk.label);
        },limits);};
    if(definition.asset.empty())with_iff_file(definition.source,scan);
    else with_far_entry(definition.source,definition.asset,scan);
    return result;
}

// Load the statically reachable private-call closure for one selected action.
// Global/semiglobal calls are left to explicit shared owners; no file search
// order or implicit fallback is introduced. The limits bound retained code.
inline odh::RoutineSet load_saved_action_routines(const odh::DefinitionCandidate& definition,
                                                  std::uint16_t action,
                                                  std::size_t routine_limit=256,
                                                  std::size_t instruction_limit=16384) {
    odh::RoutineSet routines;std::size_t instruction_count=0;
    const auto scan=[&](const odh::View& view){
        odh::Limits limits;limits.iff_chunks=4096;
        std::map<std::uint16_t,odh::View> private_payloads;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="BHAV"||odh::routine_key(chunk.id).scope!=odh::RoutineScope::private_object)return;
            if(!private_payloads.emplace(chunk.id,chunk.payload).second)
                throw odh::FormatError("duplicate private BHAV in exact action owner");
        },limits);
        std::vector<std::uint16_t> pending{action};
        for(std::size_t cursor=0;cursor<pending.size();++cursor){
            const auto id=pending[cursor];const auto key=odh::routine_key(id);
            if(key.scope!=odh::RoutineScope::private_object)continue;
            if(routines.count(key))continue;
            const auto found=private_payloads.find(id);
            if(found==private_payloads.end())throw odh::FormatError("private action dependency absent from exact owner");
            if(routines.size()>=routine_limit)throw odh::FormatError("private action closure exceeds routine budget");
            auto routine=odh::read_bhav(found->second);
            if(routine.version!=0x8002||!routine.metadata)
                throw odh::FormatError("private action closure requires BHAV 8002 metadata");
            if(routine.instructions.size()>instruction_limit-instruction_count)
                throw odh::FormatError("private action closure exceeds instruction budget");
            instruction_count+=routine.instructions.size();
            for(const auto& instruction:routine.instructions)
                if(instruction.opcode>=4096&&instruction.opcode<8192)
                    pending.push_back(instruction.opcode);
            routines.emplace(key,std::move(routine));
        }
    };
    if(definition.asset.empty())with_iff_file(definition.source,scan);
    else with_far_entry(definition.source,definition.asset,scan);
    return routines;
}

struct SavedTestFunctionResult {
    enum class State { eligible, ineligible, unsupported };
    State state=State::unsupported;
    std::uint16_t routine=0;
    std::uint64_t steps=0;
    std::string diagnostic;
};

// Current manual host action profile supports ordinary private actions, the
// TS1 AllowVisitors flag, and a matching successful ODH TestFunction. All
// other TTAB permission bits remain unsupported.
inline std::optional<std::size_t> first_eligible_saved_interaction(
    const SavedObjectInteractions& interactions,const std::vector<SavedTestFunctionResult>& tests,
    const odh::Entity* actor) {
    if(!interactions.table||interactions.table->status!=odh::TtabStatus::decoded)return {};
    // Simitone CheckTS1Action reads these saved person-data fields directly:
    // PersonType, GreetStatus, and Gender (pet bits). Missing state is unknown.
    if(!actor||actor->kind!=odh::EntityKind::avatar||actor->person_data.size()<=65)return {};
    constexpr std::uint32_t allow_visitors=0x00000001U;
    constexpr std::int16_t person_type=32,greet_status=34,gender=65;
    const auto is_visitor=actor->person_data[person_type]==1&&actor->person_data[greet_status]<2;
    const auto gender_flags=static_cast<std::uint16_t>(actor->person_data[gender]);
    const auto is_pet=(gender_flags&(8U|16U))!=0;
    if(is_pet)return {}; // Pet-specific interactions are outside this profile.
    const auto count=std::min<std::size_t>({interactions.table->interactions.size(),tests.size(),32});
    for(std::size_t n=0;n<count;++n){
        const auto& row=interactions.table->interactions[n];
        if((row.flags&~allow_visitors)!=0||(is_visitor&&(row.flags&allow_visitors)==0)||
           tests[n].routine!=row.test||tests[n].state!=SavedTestFunctionResult::State::eligible||
           odh::routine_key(row.action).scope!=odh::RoutineScope::private_object)continue;
        return n;
    }
    return {};
}

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
    try {routines=load_saved_private_routines(definition);}
    catch(const odh::FormatError& error){invalid=true;load_error=error.what();}
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
