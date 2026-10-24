#pragma once
#include "odh/animation.hpp"
#include "odh/definition_index.hpp"
#include "odh/entity_next.hpp"
#include "odh/object_type.hpp"
#include "odh/relationship.hpp"
#include "odh/world.hpp"
#include "resource.hpp"
#include <algorithm>
#include <cstdint>
#include <map>

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
    // The selected entry may be English fallback even when the requested
    // language is different; preserve its source encoding for the host UI.
    std::vector<std::uint8_t> row_label_languages;
};

inline SavedObjectInteractions saved_object_interactions(const odh::DefinitionCandidate& definition,
                                                          std::uint8_t language=1,
                                                          const odh::DefinitionCandidate* table_owner=nullptr) {
    SavedObjectInteractions result;
    result.source=definition.source;result.asset=definition.asset;
    const auto& owner=table_owner?*table_owner:definition;
    result.table_id=owner.definition.tree_table_id;result.language=language;
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
    if(owner.asset.empty())with_iff_file(owner.source,scan);
    else with_far_entry(owner.source,owner.asset,scan);
    if(!table)return result;
    result.state=SavedObjectInteractions::State::found;result.table=std::move(table.value());
    if(labels)result.labels=std::move(labels.value());
    result.row_labels.reserve(result.table->interactions.size());
    result.row_label_languages.reserve(result.table->interactions.size());
    for(const auto& row:result.table->interactions){
        const auto* label=odh::string_in_language_or_english(result.labels,language,row.label_index);
        if(label){
            result.row_labels.emplace_back(label->value);
            // STR language 0 is the legacy alias for US English (1).
            result.row_label_languages.emplace_back(label->language==0?1:label->language);
        }else{
            result.row_labels.emplace_back(std::nullopt);
            result.row_label_languages.emplace_back(language);
        }
    }
    return result;
}

// Some multitile parts carry the 0xffff "no local TTAB" marker even though
// their exact master in the same FAR entry owns the shared interaction table.
// Resolve only that explicit same-resource relationship; never search other
// archives for a table with a matching number.
inline SavedObjectInteractions saved_object_interactions(const odh::DefinitionCandidate& definition,
                                                          const odh::DefinitionIndex& index,
                                                          std::uint8_t language=1) {
    if(definition.definition.tree_table_id==0xffff&&definition.definition.master_id!=0){
        const auto* master=index.unique_master(definition);
        if(master&&master->definition.tree_table_id!=0)
            return saved_object_interactions(definition,language,master);
    }
    return saved_object_interactions(definition,language);
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
    std::map<std::string,std::uint16_t> private_bhav_ids;
    std::optional<std::string> semiglobal_name;
    std::size_t semiglobal_bindings=0;
};

// TreeByName in the reference is assembled from local BHAV chunk labels plus
// the linked semiglobal's BHAV labels. Keep the link explicit so callers do not
// treat a partial local scan as a complete negative lookup.
inline SavedTreeNameCatalog load_saved_tree_name_catalog(const odh::DefinitionCandidate& definition) {
    SavedTreeNameCatalog result;std::size_t glob_count=0;
    const auto scan=[&](const odh::View& view){odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type=="GLOB"){
                ++glob_count;
                const auto glob=odh::read_glob(chunk.payload);
                if(glob.name.empty())return;
                if(result.semiglobal_name&&*result.semiglobal_name!=glob.name)
                    throw odh::FormatError("conflicting semiglobal links in exact object resource");
                result.semiglobal_name=glob.name;
            }
            if(chunk.type=="BHAV"&&odh::routine_key(chunk.id).scope==odh::RoutineScope::private_object){
                auto label=chunk.label.substr(0,chunk.label.find('\0'));
                result.private_bhav_labels.push_back(label);
                if(!label.empty())result.private_bhav_ids.emplace(std::move(label),chunk.id);
            }
        },limits);};
    if(definition.asset.empty())with_iff_file(definition.source,scan);
    else with_far_entry(definition.source,definition.asset,scan);
    result.semiglobal_bindings=glob_count;
    return result;
}

inline std::optional<std::string> unique_saved_semiglobal_name(const SavedTreeNameCatalog& catalog) {
    if(catalog.semiglobal_bindings>1)
        throw odh::FormatError("multiple GLOB bindings in exact object resource");
    return catalog.semiglobal_name;
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
// TS1 AllowVisitors permission, AutoFirstSelect's menu-default hint, and a
// matching successful ODH TestFunction. Other TTAB permission bits remain
// unsupported.
inline std::vector<std::size_t> eligible_saved_interaction_rows(
    const SavedObjectInteractions& interactions,const std::vector<SavedTestFunctionResult>& tests,
    const odh::Entity* actor,const odh::RoutineSet* linked_semiglobal=nullptr) {
    std::vector<std::size_t> result;
    if(!interactions.table||interactions.table->status!=odh::TtabStatus::decoded)return result;
    // Simitone CheckTS1Action reads these saved person-data fields directly:
    // PersonType, GreetStatus, and Gender (pet bits). Missing state is unknown.
    if(!actor||actor->kind!=odh::EntityKind::avatar||actor->person_data.size()<=65)return result;
    constexpr std::uint32_t allow_visitors=0x00000001U;
    constexpr std::uint32_t auto_first_select=0x00000100U;
    constexpr std::int16_t person_type=32,greet_status=34,gender=65;
    const auto is_visitor=actor->person_data[person_type]==1&&actor->person_data[greet_status]<2;
    const auto gender_flags=static_cast<std::uint16_t>(actor->person_data[gender]);
    const auto is_pet=(gender_flags&(8U|16U))!=0;
    if(is_pet)return result; // Pet-specific interactions are outside this profile.
    const auto count=std::min<std::size_t>({interactions.table->interactions.size(),tests.size(),32});
    for(std::size_t n=0;n<count;++n){
        const auto& row=interactions.table->interactions[n];
        const auto action_key=odh::routine_key(row.action);
        const bool action_owner_supported=action_key.scope==odh::RoutineScope::private_object||
            (action_key.scope==odh::RoutineScope::semiglobal&&linked_semiglobal&&
             linked_semiglobal->count(action_key));
        if((row.flags&~(allow_visitors|auto_first_select))!=0||
           (is_visitor&&(row.flags&allow_visitors)==0)||
           tests[n].routine!=row.test||tests[n].state!=SavedTestFunctionResult::State::eligible||
           !action_owner_supported)continue;
        result.push_back(n);
    }
    return result;
}

inline std::optional<std::size_t> cycle_eligible_saved_interaction_row(
    const std::vector<std::size_t>& eligible,std::optional<std::size_t> selected,int delta) {
    if(eligible.empty())return std::nullopt;
    const auto current=selected?std::find(eligible.begin(),eligible.end(),*selected):eligible.end();
    if(current==eligible.end())return eligible.front();
    if(delta==0)return *current;
    const auto count=static_cast<std::int64_t>(eligible.size());
    auto index=static_cast<std::int64_t>(std::distance(eligible.begin(),current));
    index=((index+static_cast<std::int64_t>(delta))%count+count)%count;
    return eligible[static_cast<std::size_t>(index)];
}

inline std::optional<std::size_t> first_eligible_saved_interaction(
    const SavedObjectInteractions& interactions,const std::vector<SavedTestFunctionResult>& tests,
    const odh::Entity* actor,const odh::RoutineSet* linked_semiglobal=nullptr) {
    const auto rows=eligible_saved_interaction_rows(interactions,tests,actor,linked_semiglobal);
    return rows.empty()?std::nullopt:std::optional<std::size_t>(rows.front());
}

// Run TestFunction in an isolated VM, against live entities and a read-only
// World memory view. Only private routines from the exact object resource are
// supplied; external owners, writes, primitives and exhausted budgets remain
// unsupported. This is an ODH VM result, not a claim about the original EXE.
inline std::vector<SavedTestFunctionResult> evaluate_saved_test_functions(
    const odh::DefinitionCandidate& definition,const odh::World& world,
    std::int16_t caller,std::int16_t target,const std::vector<std::uint16_t>& tests,
    std::uint64_t total_budget=4096,std::uint64_t per_test_budget=128,
    const std::vector<std::int16_t>& arguments={0},
    const odh::RoutineSet* linked_semiglobal=nullptr,
    const std::vector<std::vector<std::int16_t>>& multitile_groups={},
    const odh::RelationshipState* relationship_state=nullptr) {
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

    if(linked_semiglobal)for(const auto& [key,routine]:*linked_semiglobal)
        if(key.scope==odh::RoutineScope::semiglobal&&!routines.emplace(key,routine).second){
            for(const auto test:tests)result.push_back({SavedTestFunctionResult::State::unsupported,test,0,"private and linked semiglobal routine keys collide"});
            return result;
        }
    odh::VmLimits limits;limits.stack_depth=16;limits.frame_variables=256;limits.code_owners=2;
    odh::VmMemory memory;memory.read=[&world](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t index){
        const auto value=world.read(frame,scope,index);
        // Simitone initializes every declared attribute to zero and reads
        // beyond the declared length as zero. Preserve that read-only rule.
        if(!value&&index>=0&&(scope==0||scope==1))return std::optional<std::int16_t>{0};
        return value;
    };
    odh::RoutineOwners owners;owners.emplace(static_cast<odh::CodeOwner>(target),std::move(routines));
    std::optional<odh::RelationshipState> relationship_probe;
    if(relationship_state){
        const auto snapshot=relationship_state->snapshot();
        relationship_probe.emplace(snapshot.limits);
        if(!relationship_probe->restore_snapshot(snapshot))
            throw odh::FormatError("could not isolate relationship data for read-only TestFunction evaluation");
    }
    auto relationship=relationship_probe?relationship_probe->primitive(world,memory):odh::VmPrimitive{};
    auto entity_next=odh::entity_next_primitive(world,memory,{},multitile_groups);
    auto object_type=odh::object_type_primitive(world);
    odh::VmPrimitive primitive=[entity_next=std::move(entity_next),object_type=std::move(object_type),
        relationship=std::move(relationship)](
        const odh::VmFrame& frame,const odh::Instruction& instruction,const auto& temps,const auto& dispatch) {
        if(instruction.opcode==32)return object_type(frame,instruction,temps,dispatch);
        if(instruction.opcode==24||instruction.opcode==26){
            const auto& operands=instruction.operands;
            const auto set_mode=instruction.opcode==24?operands[0]:
                static_cast<std::uint8_t>(((operands[2]>>2)&1)|((operands[2]>>4)&2));
            if(set_mode!=0)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "TestFunction relationship writes are not supported by the read-only probe"};
            if(!relationship)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "relationship context unavailable to read-only TestFunction probe"};
            return relationship(frame,instruction,temps,dispatch);
        }
        return entity_next(frame,instruction,temps,dispatch);
    };
    odh::Vm probe({},std::move(memory),limits,std::move(primitive),std::move(owners));
    std::uint64_t spent=0;
    for(const auto test:tests){
        SavedTestFunctionResult item;item.routine=test;
        const auto key=odh::routine_key(test);
        if(key.scope!=odh::RoutineScope::private_object&&
           !(key.scope==odh::RoutineScope::semiglobal&&linked_semiglobal&&linked_semiglobal->count(key))){
            item.diagnostic="test owner is not the selected object's private resource or its explicitly linked semiglobal";result.push_back(std::move(item));continue;
        }
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
