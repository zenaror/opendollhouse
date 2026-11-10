#pragma once
#include "odh/animation.hpp"
#include "odh/definition_index.hpp"
#include "odh/entity_next.hpp"
#include "odh/inventory_find.hpp"
#include "odh/neighbor_inventory.hpp"
#include "odh/lot_context.hpp"
#include "odh/neighbor_context.hpp"
#include "odh/object_type.hpp"
#include "odh/relationship.hpp"
#include "odh/random.hpp"
#include "odh/look_towards.hpp"
#include "odh/ts1_budget.hpp"
#include "odh/tuning.hpp"
#include "odh/world.hpp"
#include "odh/interaction.hpp"
#include "resource.hpp"
#include "saved_semiglobal_resource.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

// TTAB/TTAs discovery is static. The separate evaluator below reports only an
// ODH VM TestFunction result; neither operation exposes a pie menu or
// implies that an action is currently executable.
struct SavedObjectInteractions {
    enum class State { no_tree_table_id, missing_ttab, found };
    enum class TableOrigin { private_resource, master_resource, semiglobal_resource };
    State state=State::missing_ttab;
    // source/asset always identify the selected definition and its private code.
    // A table inherited from a master or GLOB does not become a new code owner.
    std::string source,asset;
    std::string table_source,table_asset;
    TableOrigin table_origin=TableOrigin::private_resource;
    std::uint16_t table_id=0;
    std::optional<odh::Ttab> table;
    odh::StringTable labels{odh::StringTableState::absent,{}, {},0};
    std::uint8_t language=1;
    std::vector<std::optional<std::string>> row_labels;
    // The selected entry may be English fallback even when the requested
    // language is different; preserve its source encoding for the host UI.
    std::vector<std::uint8_t> row_label_languages;
    std::vector<bool> row_global; // Empty legacy vector means private table rows.
    std::string global_table_source,global_table_asset;
    std::optional<std::uint16_t> global_table_id,global_label_table_id;
};

inline SavedObjectInteractions saved_interactions_from_table_resource(
    const odh::DefinitionCandidate& definition,const std::string& table_source,const std::string& table_asset,
    std::uint16_t table_id,std::uint8_t language,SavedObjectInteractions::TableOrigin origin,bool load_labels=true) {
    SavedObjectInteractions result;
    result.source=definition.source;result.asset=definition.asset;
    result.table_source=table_source;result.table_asset=table_asset;result.table_origin=origin;
    result.table_id=table_id;result.language=language;
    if(!result.table_id){result.state=SavedObjectInteractions::State::no_tree_table_id;return result;}
    std::optional<odh::Ttab> table;std::optional<odh::StringTable> labels;
    const auto scan=[&](const odh::View& view){odh::iff_chunks(view,[&](const odh::Chunk& chunk){
        if(chunk.id!=result.table_id)return;
        if(chunk.type=="TTAB"){
            if(table)throw odh::FormatError("ambiguous object TTAB ID");
            table=odh::read_ttab(chunk.payload);
        }else if(load_labels&&chunk.type=="TTAs"){
            if(labels)throw odh::FormatError("ambiguous object TTAs ID");
            labels=odh::read_strings(chunk.payload);
        }
    });};
    if(table_asset.empty())with_iff_file(table_source,scan);
    else with_far_entry(table_source,table_asset,scan);
    if(!table)return result;
    result.state=SavedObjectInteractions::State::found;result.table=std::move(table.value());
    if(!load_labels)return result;
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

inline SavedObjectInteractions saved_object_interactions(const odh::DefinitionCandidate& definition,
                                                          std::uint8_t language=1,
                                                          const odh::DefinitionCandidate* table_owner=nullptr,bool load_labels=true) {
    const auto& owner=table_owner?*table_owner:definition;
    return saved_interactions_from_table_resource(definition,owner.source,owner.asset,owner.definition.tree_table_id,
        language,table_owner?SavedObjectInteractions::TableOrigin::master_resource:
            SavedObjectInteractions::TableOrigin::private_resource,load_labels);
}

// Load executable code only from the exact resource that supplied the selected
// definition. Archive precedence and global/semiglobal fallback are forbidden.
inline constexpr std::size_t saved_resource_chunk_limit=4096;
inline constexpr std::size_t saved_resource_routine_limit=4096;
inline constexpr std::size_t saved_resource_instruction_limit=262144;
inline std::string saved_resource_description(const odh::DefinitionCandidate& definition){
    return definition.source+(definition.asset.empty()?std::string{}:std::string(" :: ")+definition.asset);
}
inline odh::RoutineSet load_saved_private_routines(const odh::DefinitionCandidate& definition,
        std::size_t routine_limit=saved_resource_routine_limit,
        std::size_t instruction_limit=saved_resource_instruction_limit) {
    odh::RoutineSet routines;std::size_t decoded_instructions=0;
    const auto scan=[&](const odh::View& view){odh::Limits limits;limits.iff_chunks=saved_resource_chunk_limit;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="BHAV")return;
            const auto key=odh::routine_key(chunk.id);
            if(key.scope!=odh::RoutineScope::private_object)return;
            if(!routines.count(key)&&routines.size()>=routine_limit)
                throw odh::FormatError("private BHAV routine budget exceeded resource="+saved_resource_description(definition)+
                    " id="+std::to_string(chunk.id)+" limit="+std::to_string(routine_limit));
            auto routine=odh::read_bhav(chunk.payload);
            if(routine.instructions.size()>instruction_limit-decoded_instructions)
                throw odh::FormatError("private BHAV decoded-instruction budget exceeded resource="+saved_resource_description(definition)+
                    " id="+std::to_string(chunk.id)+" limit="+std::to_string(instruction_limit));
            decoded_instructions+=routine.instructions.size();
            // GameIffResource.Recache assigns every chunk in physical order.
            // The last payload for a numeric ID wins, even with another label.
            routines.insert_or_assign(key,std::move(routine));
        },limits);};
    if(definition.asset.empty())with_iff_file(definition.source,scan);
    else with_far_entry(definition.source,definition.asset,scan);
    return routines;
}

// Explicit named entries retain the original ushort ID in a separate physical
// namespace. Numeric calls continue to use only the usual private/semi subset.
inline odh::RoutineSet load_saved_named_routines(const odh::DefinitionCandidate& definition,bool linked,
        std::size_t routine_limit=saved_resource_routine_limit,
        std::size_t instruction_limit=saved_resource_instruction_limit){
    std::map<std::uint16_t,odh::Bhav> cache;std::size_t decoded=0;
    const auto scan=[&](const odh::View& view){odh::Limits limits;limits.iff_chunks=saved_resource_chunk_limit;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="BHAV")return;
            if(!cache.count(chunk.id)&&cache.size()>=routine_limit)
                throw odh::FormatError("named BHAV routine budget exceeded resource="+saved_resource_description(definition)+
                    " id="+std::to_string(chunk.id)+" limit="+std::to_string(routine_limit));
            auto routine=odh::read_bhav(chunk.payload);
            if(routine.instructions.size()>instruction_limit-decoded)
                throw odh::FormatError("named BHAV decoded-instruction budget exceeded resource="+saved_resource_description(definition)+
                    " id="+std::to_string(chunk.id)+" limit="+std::to_string(instruction_limit));
            decoded+=routine.instructions.size();cache.insert_or_assign(chunk.id,std::move(routine));
        },limits);};
    if(definition.asset.empty())with_iff_file(definition.source,scan);else with_far_entry(definition.source,definition.asset,scan);
    odh::RoutineSet result;
    for(auto& item:cache){
        const auto numeric=odh::routine_key(item.first);
        if(numeric.scope==(linked?odh::RoutineScope::semiglobal:odh::RoutineScope::private_object))result.emplace(numeric,item.second);
        result.emplace(odh::named_routine_key(item.first,linked),std::move(item.second));
    }
    return result;
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
            if(chunk.type=="BHAV"){
                auto label=chunk.label.substr(0,chunk.label.find('\0'));
                result.private_bhav_labels.push_back(label);
                result.private_bhav_ids.emplace(std::move(label),chunk.id);
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

// VMEntity first loads the selected object's private/GLOB-linked TTAB. Only
// when neither supplies that table does UseTreeTableOf inherit the master's
// private/GLOB-linked table, for any missing nonzero child ID (not just 0xffff).
// The caller supplies the selected Global FAR; discovery never searches other
// archives or invents a definition. Multitile masters must be unique and in the
// exact same resource. The explicit no-table-ID-zero profile stays unchanged.
// Omitting the archive retains the historical private-only discovery profile.
inline SavedObjectInteractions saved_object_interactions(const odh::DefinitionCandidate& definition,
    const odh::DefinitionIndex& index,std::uint8_t language=1,const std::string& semiglobal_archive={},
    bool load_labels=true,bool require_master=false) {
    const auto resolve=[&](const odh::DefinitionCandidate& owner,bool inherited){
        auto result=saved_object_interactions(definition,language,inherited?&owner:nullptr,load_labels);
        if(result.state!=SavedObjectInteractions::State::missing_ttab||semiglobal_archive.empty())return result;
        const auto link=unique_saved_semiglobal_name(load_saved_tree_name_catalog(owner));
        if(!link)return result;
        if(link->empty()||link->find_first_of("/\\:.")!=std::string::npos||link->find('\0')!=std::string::npos)
            throw odh::FormatError("interaction semiglobal requires a unique flat GLOB resource name");
        return saved_interactions_from_table_resource(definition,semiglobal_archive,
            resolve_saved_semiglobal_entry(semiglobal_archive,*link+".iff"),result.table_id,
            language,SavedObjectInteractions::TableOrigin::semiglobal_resource,load_labels);
    };
    auto result=resolve(definition,false);
    if(result.state!=SavedObjectInteractions::State::missing_ttab||definition.definition.master_id==0||
       definition.definition.sub_index==-1)return result;
    const auto* master=index.unique_master(definition);
    if(!master&&require_master)throw odh::FormatError("interaction table fallback requires an exact unique master");
    return master&&master->definition.tree_table_id!=0?resolve(*master,true):result;
}

// Load the statically reachable private-call closure for one selected action.
// Global/semiglobal calls are left to explicit shared owners; no file search
// order or implicit fallback is introduced. The limits bound retained code.
inline odh::RoutineSet load_saved_action_routines(const odh::DefinitionCandidate& definition,
                                                  std::uint16_t action,
                                                  std::size_t routine_limit=saved_resource_routine_limit,
                                                  std::size_t instruction_limit=saved_resource_instruction_limit) {
    odh::RoutineSet routines;std::size_t instruction_count=0;
    const auto scan=[&](const odh::View& view){
        odh::Limits limits;limits.iff_chunks=4096;
        std::map<std::uint16_t,odh::View> private_payloads;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="BHAV"||odh::routine_key(chunk.id).scope!=odh::RoutineScope::private_object)return;
            private_payloads.insert_or_assign(chunk.id,chunk.payload);
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

// Source Context selects the first global TTAB and TTAs independently. The
// target still owns executable private/semiglobal code for these menu rows.
inline void append_saved_global_interactions(SavedObjectInteractions& menu,
    const odh::DefinitionCandidate& globals) {
    if(!menu.table||menu.table->status!=odh::TtabStatus::decoded)return;
    std::optional<odh::Ttab> table;std::optional<odh::StringTable> strings;
    const auto scan=[&](const odh::View& view){odh::iff_chunks(view,[&](const odh::Chunk& chunk){
        if(chunk.type=="TTAB"&&!table){table=odh::read_ttab(chunk.payload);menu.global_table_id=chunk.id;}
        if(chunk.type=="TTAs"&&!strings){strings=odh::read_strings(chunk.payload);menu.global_label_table_id=chunk.id;}
    });};
    if(globals.asset.empty())with_iff_file(globals.source,scan);else with_far_entry(globals.source,globals.asset,scan);
    if(!table)return; // Proven absent global table is empty.
    if(table->status==odh::TtabStatus::empty)return;
    if(table->status!=odh::TtabStatus::decoded)throw odh::FormatError("global menu TTAB encoding unsupported");
    menu.global_table_source=globals.source;menu.global_table_asset=globals.asset;
    menu.row_global.resize(menu.table->interactions.size(),false);
    for(const auto& row:table->interactions){
        menu.table->interactions.push_back(row);menu.row_global.push_back(true);
        const auto* label=strings?odh::string_in_language_or_english(*strings,menu.language,row.label_index):nullptr;
        menu.row_labels.push_back(label?std::optional<std::string>{label->value}:std::nullopt);
        menu.row_label_languages.push_back(label?(label->language?label->language:1):menu.language);
    }
}

// Old queues can predate InteractionNumber. Only a single matching row
// across the available local and global tables identifies its static label.
inline std::optional<std::size_t> saved_queued_interaction_row(const SavedObjectInteractions& menu,
        const odh::InteractionActionSnapshot& action){
    if(!menu.table||menu.table->status!=odh::TtabStatus::decoded)return std::nullopt;
    if(!action.interaction_number&&action.check)return std::nullopt;
    std::optional<std::size_t> match;
    for(std::size_t n=0;n<menu.table->interactions.size();++n){
        const auto& row=menu.table->interactions[n];
        if(action.interaction_number){
            const auto number=static_cast<std::uint32_t>(*action.interaction_number);
            if(row.label_index!=(number&0x7fffffffU)||((n<menu.row_global.size()&&menu.row_global[n])!=(number>>31)))continue;
        }
        if(row.action!=action.call.key.id||row.test!=(action.check?action.check->id:0))continue;
        if(match)return std::nullopt;
        match=n;
    }
    return match;
}

// Some older saves keep the chosen action but not its name. A leaf check
// made only of forward expression branches and one private ChangeActionString
// can prove the unique label without evaluating any gameplay instruction.
inline std::optional<std::pair<std::uint16_t,std::uint32_t>> saved_leaf_action_string(const odh::Bhav& check){
    const auto& code=check.instructions;
    if(code.empty()||code.size()>253)return std::nullopt;
    std::optional<std::pair<std::uint16_t,std::uint32_t>> label;
    std::vector<bool> reached(code.size());reached[0]=true;
    for(std::size_t n=0;n<code.size();++n){
        if(!reached[n])continue;
        const auto& op=code[n];
        if(op.opcode==50){
            if(label||op.on_true!=254||op.operands[2]||op.operands[3]||!op.operands[4])return std::nullopt;
            label=std::make_pair(static_cast<std::uint16_t>(op.operands[0]|(op.operands[1]<<8)),
                static_cast<std::uint32_t>(op.operands[4]-1));
            continue;
        }
        if(op.opcode!=2)return std::nullopt;
        for(auto next:{op.on_true,op.on_false}){
            if(next==253||next==255)continue;
            if(next<=n||next>=code.size())return std::nullopt;
            reached[next]=true;
        }
    }
    return label;
}

struct SavedMenuVariant {
    std::optional<std::string> name;
    std::int16_t param0=0;
    std::uint8_t language=0; // Zero retains the TTAs fallback language.
    std::optional<odh::InteractionLabelOrigin> origin={};
};
struct SavedMenuStringResult {
    bool supported=false;
    std::optional<std::string> name;
    std::string diagnostic;
    std::uint8_t language=0;
    std::optional<odh::InteractionLabelOrigin> origin={};
};
using SavedMenuStringResolver=std::function<SavedMenuStringResult(const odh::VmFrame&,const odh::Instruction&)>;
inline constexpr std::size_t saved_menu_row_limit=8192; // Local and global each admit Limits::ttab_interactions (4096).
// FreeSO7e76 VMThread.Tick permits MAX_LOOP_COUNT(500000)+1 executed
// instructions before its post-increment guard faults. The legacy default
// evaluator arguments below remain128; the live full-lot consumer opts in.
inline constexpr std::uint64_t saved_menu_check_budget=500001;
// Independent fixed aggregate ceiling: never multiply8192 rows by the larger
// source per-check guard (also keep this representable on32-bit targets).
inline constexpr std::uint64_t saved_menu_total_budget=1048576;
inline std::vector<std::uint16_t> saved_menu_tests(const odh::Ttab& table) {
    if(table.status!=odh::TtabStatus::decoded||table.interactions.size()>saved_menu_row_limit)
        throw odh::FormatError("menu TTAB exceeds decoded row profile");
    std::vector<std::uint16_t> result;result.reserve(table.interactions.size());
    for(const auto& row:table.interactions)result.push_back(row.test);
    return result;
}
struct SavedTestFunctionResult {
    enum class State { eligible, ineligible, unsupported };
    State state=State::unsupported;
    std::uint16_t routine=0;
    std::uint64_t steps=0;
    std::string diagnostic;
    std::vector<SavedMenuVariant> variants;
};

// The player can select among multiple TTAB rows that call the same action
// BHAV. Keep every row's referenced check in that action owner's bank so a
// later selection never reuses an incomplete owner. The session descriptor
// records the resulting bank shape; no check identity is inferred on restore.
struct SavedMenuActionRoutinePlan {
    std::vector<odh::RoutineKey> checks;
    bool complete_private=false;
    bool linked_semiglobal=false;
};

inline std::optional<SavedMenuActionRoutinePlan> saved_menu_action_routine_plan(
    const odh::Ttab& table,std::uint16_t action) {
    if(table.status!=odh::TtabStatus::decoded)return {};
    const auto action_key=odh::routine_key(action);
    if(action_key.scope!=odh::RoutineScope::private_object&&
       action_key.scope!=odh::RoutineScope::semiglobal)return {};
    SavedMenuActionRoutinePlan result;
    result.linked_semiglobal=action_key.scope==odh::RoutineScope::semiglobal;
    for(const auto& row:table.interactions){
        if(row.action!=action||row.test==0)continue;
        const auto key=odh::routine_key(row.test);
        if(std::find_if(result.checks.begin(),result.checks.end(),[&](const auto old){
            return old.scope==key.scope&&old.id==key.id;
        })!=result.checks.end())continue;
        result.checks.push_back(key);
        // A check in a shared bank can call back into the selected object's
        // private code, so any executable check requires its full private bank.
        result.complete_private=true;
        result.linked_semiglobal|=key.scope==odh::RoutineScope::semiglobal;
    }
    result.complete_private&=!result.linked_semiglobal;
    return result;
}

// VMEntity.GetPieMenu suppresses a caller outside the lot or Hidden==1.
// HideInteraction is a separate per-check value, evaluated below, not a live
// field whose old value may suppress a new menu evaluation.
inline bool saved_menu_actor_visible(const odh::Entity* actor) {
    if(!actor||!actor->position)return false;
    const auto hidden=actor->object_words.find(34);
    return hidden==actor->object_words.end()||hidden->second!=1;
}

// Manual menu and runtime dispatch share the same TS1 permission rule.
// Check BHAV results and executable code availability remain separate gates.
inline std::vector<std::size_t> eligible_saved_interaction_rows(
    const SavedObjectInteractions& interactions,const std::vector<SavedTestFunctionResult>& tests,
    const odh::Entity* actor,const odh::RoutineSet* linked_semiglobal=nullptr,
    const odh::RoutineSet* linked_global=nullptr) {
    std::vector<std::size_t> result;
    if(!saved_menu_actor_visible(actor)||!interactions.table||
       interactions.table->status!=odh::TtabStatus::decoded)return result;
    const auto count=std::min(interactions.table->interactions.size(),tests.size());
    if(interactions.table->interactions.size()>saved_menu_row_limit)
        throw odh::FormatError("menu TTAB exceeds decoded row profile");
    for(std::size_t n=0;n<count;++n){
        const auto& row=interactions.table->interactions[n];
        const auto action_key=odh::routine_key(row.action);
        const bool action_owner_supported=action_key.scope==odh::RoutineScope::private_object||
            (action_key.scope==odh::RoutineScope::semiglobal&&linked_semiglobal&&
             linked_semiglobal->count(action_key))||
            (action_key.scope==odh::RoutineScope::global&&linked_global&&linked_global->count(action_key));
        bool check_owner_supported=true;
        if(action_key.scope==odh::RoutineScope::global&&row.test){
            const auto check_key=odh::routine_key(row.test);
            if(check_key.scope==odh::RoutineScope::global)
                check_owner_supported=linked_global&&linked_global->count(check_key);
            else if(check_key.scope==odh::RoutineScope::semiglobal)
                check_owner_supported=linked_semiglobal&&linked_semiglobal->count(check_key);
        }
        if(odh::ts1_interaction_permission(actor,row.flags)!=odh::InteractionPermission::allowed||
           tests[n].routine!=row.test||tests[n].state!=SavedTestFunctionResult::State::eligible||
           !action_owner_supported||!check_owner_supported)continue;
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
    const odh::Entity* actor,const odh::RoutineSet* linked_semiglobal=nullptr,
    const odh::RoutineSet* linked_global=nullptr) {
    const auto rows=eligible_saved_interaction_rows(interactions,tests,actor,linked_semiglobal,linked_global);
    return rows.empty()?std::nullopt:std::optional<std::size_t>(rows.front());
}

// Run TestFunction in a separate VM, with register isolation outside a live
// scheduler dispatch and source-shared registers inside it. An explicit
// read-only host owns a complete cloned register Thread, contextual resources
// and nested execution; legacy ordinary checks retain source register loans.
// Inventory effects execute in a disposable owner shared by this menu pass.
// Code is limited to exact private and explicitly linked shared banks. Unknown
// primitives and exhausted budgets remain unsupported; this is not EXE parity.
// global_tuning_loaded attests a complete selected bank, including an empty one.
inline std::vector<SavedTestFunctionResult> evaluate_saved_test_functions(
    const odh::DefinitionCandidate& definition,const odh::World& world,
    std::int16_t caller,std::int16_t target,const std::vector<std::uint16_t>& tests,
    std::uint64_t total_budget=4096,std::uint64_t per_test_budget=128,
    const std::vector<std::int16_t>& arguments={0},
    const odh::RoutineSet* linked_semiglobal=nullptr,
    const std::vector<std::vector<std::int16_t>>& multitile_groups={},
    const odh::RelationshipState* relationship_state=nullptr,
    const odh::Ngbh* neighborhood_inventory=nullptr,
    const std::function<std::optional<std::int16_t>(std::int16_t)>& read_global={},
    const odh::RoutineSet* linked_global=nullptr,const odh::LotContext* linked_lot=nullptr,
    const odh::NeighborContext* linked_neighbors=nullptr,
    std::optional<std::int32_t> family_budget=std::nullopt,
    const odh::TuningResources* linked_tuning=nullptr,bool global_tuning_loaded=false,
    const SavedMenuStringResolver& resolve_string={},const odh::VmPrimitiveFactory& host_factory={},
    odh::Vm* register_owner=nullptr,bool scheduler_running_now=false,
    const std::vector<std::uint32_t>& permission_flags={},const odh::VmMemory* source_memory=nullptr,
    odh::NeighborInventory* live_inventory=nullptr,const odh::Vm::OwnerResolver& owner_resolver={},
    const odh::RandomState* source_random=nullptr,
    const std::function<odh::VmPrimitive(const odh::VmMemory&,const odh::VmPrimitive&,odh::Vm*)>& readonly_host_factory={}) {
    constexpr std::size_t test_limit=saved_menu_row_limit;
    if(tests.size()>test_limit||arguments.size()>256||(!permission_flags.empty()&&permission_flags.size()!=tests.size()))throw odh::FormatError("interaction test arguments/count exceed bounded probe profile");
    std::vector<SavedTestFunctionResult> result;result.reserve(tests.size());
    const auto* actor=world.find(caller);const auto* object=world.find(target);
    if(!actor||actor->kind!=odh::EntityKind::avatar||!object||!object->definition||
       object->definition->guid!=definition.definition.guid){
        for(const auto test:tests)result.push_back({SavedTestFunctionResult::State::unsupported,test,0,"live actor/target identity does not match explicit definition"});
        return result;
    }
    const auto caller_generation=actor->generation,target_generation=object->generation;
    odh::RoutineSet routines;bool invalid=false;std::string load_error;
    try {routines=load_saved_private_routines(definition);}
    catch(const odh::FormatError& error){invalid=true;load_error=error.what();}
    if(invalid){for(const auto test:tests)result.push_back({SavedTestFunctionResult::State::unsupported,test,0,load_error});return result;}

    if(linked_semiglobal)for(const auto& [key,routine]:*linked_semiglobal)
        if(key.scope==odh::RoutineScope::semiglobal&&!routines.emplace(key,routine).second){
            for(const auto test:tests)result.push_back({SavedTestFunctionResult::State::unsupported,test,0,"private and linked semiglobal routine keys collide"});
            return result;
        }
    odh::RoutineSet global_routines;
    if(linked_global)for(const auto& [key,routine]:*linked_global)
        if(key.scope==odh::RoutineScope::global&&!global_routines.emplace(key,routine).second){
            for(const auto test:tests)result.push_back({SavedTestFunctionResult::State::unsupported,test,0,"duplicate key in linked global routine bank"});
            return result;
        }
    odh::VmLimits limits;limits.frame_variables=256;
    // Closed-bank legacy probes need only default+target. An explicit named
    // resource provider uses the kernel's bounded configured owner ceiling.
    if(!owner_resolver)limits.code_owners=2;
    std::optional<odh::BconTuning> tuning;
    if(linked_tuning)tuning.emplace(*linked_tuning);
    // Menu checks may publish HideInteraction on the caller. Isolate that one
    // documented output from the live World and reset it before every row, as
    // VMEntity.GetPieMenu does. Other world writes remain unsupported.
    std::int16_t hide_interaction=0;
    std::vector<SavedMenuVariant> variants;
    const auto caller_hide_field=[caller](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t index){
        return index==50&&((scope==3&&frame.context.caller==caller)||
                          (scope==4&&frame.context.stack_object==caller));
    };
    // A query consumes the same RNG sequence without advancing the session.
    // All rows share this disposable stream, exactly as the source menu pass.
    std::optional<odh::RandomState> probe_random;
    if(source_random)probe_random=*source_random;
    // LookTowards mode0 is a synchronous source output; retain it only here.
    std::map<odh::WorldEntityReference,std::array<std::int16_t,5>> head_targets;
    odh::VmMemory memory;
    if(source_memory){
        memory.job_data=source_memory->job_data;
        // Scope23 is a typed contextual READ: Vm passes THIS probe's Temp0,
        // including nested global/named checks. Retain the authenticated live
        // room provider without lending source writes or borrowing VM registers.
        // Missing/unresolved geometry must remain unsupported, never indoors.
        memory.room_by_temp0=source_memory->room_by_temp0;
    }
    memory.resolve_live_entity=[&world](std::int16_t id)->std::optional<odh::WorldEntityReference>{
        const auto* e=world.find(id);return e?std::optional<odh::WorldEntityReference>{{id,e->generation}}:std::nullopt;
    };
    memory.read=[&world,&read_global,&tuning,linked_tuning,global_tuning_loaded,source_memory,
                 &hide_interaction,&caller_hide_field,&head_targets](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t index){
        if(caller_hide_field(frame,scope,index))return std::optional<std::int16_t>{hide_interaction};
        // NeighborInStackObject uses the raw numeric neighbor ID, not an
        // entity-reference role. Reuse the host's complete fields0..9 provider.
        if(scope==24||scope==36||scope==37)return source_memory&&source_memory->read?
            source_memory->read(frame,scope,index):std::nullopt;
        if(scope==6&&read_global)return read_global(index);
        if(scope==26){
            const auto address=odh::BconTuning::address(index);
            // Named plugin checks can execute a resource other than the
            // selected object's private bank. Reuse the host's authenticated
            // read provider for that CodeOwner; unknown remains nullopt, never
            // the selected object's constants or an unproven zero default.
            if(address.scope!=odh::RoutineScope::global&&
               (!linked_tuning||!linked_tuning->owners.count(frame.context.code_owner)))
                return source_memory&&source_memory->read?
                    source_memory->read(frame,scope,index):std::nullopt;
            if(!tuning||!linked_tuning)return std::optional<std::int16_t>{};
            if(address.scope==odh::RoutineScope::global){
                if(const auto value=tuning->read(frame.context.code_owner,index))
                    return std::optional<std::int16_t>{value->value};
                // An empty unproven global bank is not a loaded bank with a
                // missing constant. The caller explicitly attests full loading.
                return global_tuning_loaded?std::optional<std::int16_t>{0}:std::nullopt;
            }
            const auto owner=linked_tuning->owners.find(frame.context.code_owner);
            if(owner==linked_tuning->owners.end())return std::optional<std::int16_t>{};
            if(address.scope==odh::RoutineScope::semiglobal&&
               owner->second.semiglobal_state==odh::TuningSemiState::unknown)
                return std::optional<std::int16_t>{};
            if(const auto value=tuning->read(frame.context.code_owner,index))
                return std::optional<std::int16_t>{value->value};
            // Exact loaded private/semiglobal banks use the source zero default;
            // a proven absent semiglobal falls back to the private table bank.
            return std::optional<std::int16_t>{0};
        }
        if((scope==18||scope==19)&&index>=41&&index<=45){
            const auto* e=world.frame_entity(frame,scope==18?odh::VmEntityRole::caller:odh::VmEntityRole::stack_object);
            if(e){const auto found=head_targets.find({e->id,e->generation});
                if(found!=head_targets.end())return std::optional<std::int16_t>{found->second[index-41]};}
        }
        const auto value=world.read_only(frame,scope,index);
        // Simitone initializes every declared attribute to zero and reads
        // beyond the declared length as zero. Preserve that read-only rule.
        if(!value&&index>=0&&(scope==0||scope==1))return std::optional<std::int16_t>{0};
        return value;
    };
    memory.write=[&hide_interaction,&caller_hide_field](const odh::VmFrame& frame,
        std::uint8_t scope,std::int16_t index,std::int16_t value){
        if(!caller_hide_field(frame,scope,index))return false;
        hide_interaction=value;return true;
    };
    odh::RoutineOwners owners;owners.emplace(static_cast<odh::CodeOwner>(target),std::move(routines));
    std::optional<odh::RelationshipState> relationship_probe;
    if(relationship_state){
        const auto snapshot=relationship_state->snapshot();
        relationship_probe.emplace(snapshot.limits);
        if(!relationship_probe->restore_snapshot(snapshot))
            throw odh::FormatError("could not isolate relationship data for read-only TestFunction evaluation");
    }
    std::unique_ptr<odh::Vm> register_probe;
    if(readonly_host_factory&&register_owner){
        register_probe=register_owner->clone_for_check(memory);
        register_owner=register_probe.get();
    }
    auto object_type=odh::object_type_primitive(world);
    auto lot=linked_lot?linked_lot->primitive():odh::VmPrimitive{};
    std::optional<odh::NeighborInventory> inventory_probe;
    if(live_inventory)inventory_probe=*live_inventory;
    auto inventory_find=inventory_probe?odh::neighbor_inventory_primitive(*inventory_probe,world):neighborhood_inventory
        ?odh::inventory_find_primitive(*neighborhood_inventory,world):odh::VmPrimitive{};
    odh::VmPrimitive primitive=[&,object_type=std::move(object_type),
        inventory_find=std::move(inventory_find),lot=std::move(lot)](
        const odh::VmFrame& frame,const odh::Instruction& instruction,const auto& temps,const auto& dispatch) {
        const auto& effective=dispatch.origin?dispatch.origin->effective_memory():memory;
        if(instruction.opcode==50){
            if(!resolve_string)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"menu string resource resolver unavailable"};
            const auto text=resolve_string(frame,instruction);
            if(!text.supported)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,text.diagnostic};
            if(text.name){
                if(variants.size()>=saved_menu_row_limit)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"menu variant budget exhausted"};
                variants.push_back({text.name,frame.context.stack_object,text.language,text.origin});
            }
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        }
        // A relationship READ can extend a source vector with a zero. Keep
        // that source behavior in the disposable clone even when the host has
        // a complete primitive dispatcher; it must not bypass this owner.
        auto relationship=relationship_probe?relationship_probe->primitive(world,effective):odh::VmPrimitive{};
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
        if(instruction.opcode==51&&inventory_find)
            return inventory_find(frame,instruction,temps,dispatch);
        if(instruction.opcode==8){
            if(!probe_random)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "source RNG snapshot unavailable to read-only TestFunction probe"};
            return odh::random_primitive(*probe_random,effective)(frame,instruction,temps,dispatch);
        }
        if(instruction.opcode==22){
            odh::VmMemory output;
            output.write=[&](const odh::VmFrame& source,std::uint8_t scope,std::int16_t index,std::int16_t value){
                if(scope!=18||index<41||index>45)return false;
                const auto* avatar=world.frame_entity(source,odh::VmEntityRole::caller,true);
                if(!avatar||avatar->person_data.size()<=45)return false;
                const auto key=odh::WorldEntityReference{avatar->id,avatar->generation};
                auto found=head_targets.find(key);
                if(found==head_targets.end()){
                    std::array<std::int16_t,5> initial{};
                    std::copy_n(avatar->person_data.begin()+41,5,initial.begin());
                    found=head_targets.emplace(key,initial).first;
                }
                found->second[index-41]=value;return true;
            };
            return odh::look_towards_primitive(world,std::move(output))(frame,instruction,temps,dispatch);
        }
        if(readonly_host_factory){const auto host=readonly_host_factory(effective,primitive,register_probe.get());if(host)return host(frame,instruction,temps,dispatch);}
        if(host_factory){const auto host=host_factory(effective);if(host)return host(frame,instruction,temps,dispatch);}
        auto entity_next=odh::entity_next_primitive(world,effective,{},multitile_groups);
        auto neighbor=linked_neighbors?linked_neighbors->primitive(effective):odh::VmPrimitive{};
        auto budget=odh::ts1_budget_primitive(effective,[family_budget](const odh::VmFrame&){return family_budget;});
        if(instruction.opcode==25)return budget(frame,instruction,temps,dispatch);
        if(instruction.opcode==1&&instruction.operands[0]==28&&lot)
            return lot(frame,instruction,temps,dispatch);
        if(instruction.opcode==31){
            const auto mode=instruction.operands[4]&127U;
            if(mode<=4||mode==6||mode==8||mode==9||mode==12)
                return entity_next(frame,instruction,temps,dispatch);
            if(neighbor)return neighbor(frame,instruction,temps,dispatch);
        }
        if(instruction.opcode==32)return object_type(frame,instruction,temps,dispatch);
        if(instruction.opcode==51&&inventory_find)
            return inventory_find(frame,instruction,temps,dispatch);
        return entity_next(frame,instruction,temps,dispatch);
    };
    auto probe_globals=std::make_shared<const odh::VmProgram>(std::move(global_routines));
    odh::SharedRoutineOwners probe_owners;
    for(auto& owner:owners)probe_owners.emplace(owner.first,std::make_shared<const odh::VmProgram>(std::move(owner.second)));
    std::uint64_t spent=0;std::size_t published_variants=0;
    for(std::size_t row_index=0;row_index<tests.size();++row_index){
        const auto test=tests[row_index];
        SavedTestFunctionResult item;item.routine=test;hide_interaction=0;variants.clear();
        actor=world.find(caller);object=world.find(target);
        if(!actor||!object||actor->generation!=caller_generation||object->generation!=target_generation){
            item.diagnostic="live actor/target identity changed before TestFunction evaluation";
            result.push_back(std::move(item));continue;
        }
        if(!permission_flags.empty()){
            const auto permission=odh::ts1_interaction_permission(actor,permission_flags[row_index]);
            if(permission!=odh::InteractionPermission::allowed){
                item.state=permission==odh::InteractionPermission::denied?SavedTestFunctionResult::State::ineligible:SavedTestFunctionResult::State::unsupported;
                item.diagnostic=permission==odh::InteractionPermission::denied?"source actor permission denied":"source actor permission unknown";
                result.push_back(std::move(item));continue;
            }
        }
        // TTAB test zero means no TestFunction, rather than global BHAV zero.
        if(test==0){
            item.state=saved_menu_actor_visible(actor)?SavedTestFunctionResult::State::eligible:
                SavedTestFunctionResult::State::ineligible;
            if(item.state==SavedTestFunctionResult::State::ineligible)item.diagnostic="caller is hidden or outside the lot";
            result.push_back(std::move(item));continue;
        }
        const auto key=odh::routine_key(test);
        if(key.scope!=odh::RoutineScope::private_object&&
           !(key.scope==odh::RoutineScope::semiglobal&&linked_semiglobal&&linked_semiglobal->count(key))&&
           !(key.scope==odh::RoutineScope::global&&linked_global&&linked_global->count(key))){
            item.diagnostic="test owner is not the selected object's private resource or its explicitly linked semiglobal/global bank";
            result.push_back(std::move(item));continue;
        }
        if(spent>=total_budget){item.diagnostic="total TestFunction instruction budget exhausted";result.push_back(std::move(item));continue;}
        const auto budget=std::min(per_test_budget,total_budget-spent);
        const auto generation_actor=actor->generation,generation_object=object->generation;
        // Source EvaluateCheck creates a new Thread for every menu row; code
        // banks are shared, while advertisement changes must never leak rows.
        odh::Vm probe(probe_globals,odh::ShareVmProgramTag{},memory,limits,primitive,probe_owners,odh::ShareVmOwnerProgramsTag{});
        // Explicit immutable resource context, independent of which real Thread
        // supplies Temp registers. A missing provider retains the closed banks.
        if(!probe.set_owner_resolver(owner_resolver))throw odh::FormatError("TestFunction resource context could not bind");
        const auto started=probe.start(key,{caller,target,target,static_cast<odh::CodeOwner>(target)},arguments,static_cast<bool>(host_factory)||static_cast<bool>(readonly_host_factory));
        std::optional<odh::VmTrace> last_trace;
        std::uint64_t category_stack_searches=0,category_cursor_rewinds=0;
        std::optional<std::int16_t> previous_category_cursor;
        std::array<std::int16_t,16> category_cursors{};
        odh::VmSubroutineResult execution;
        if(started==odh::VmStatus::running){
            const auto trace=[&](const odh::VmTrace& value){
                last_trace=value;
                const auto& operation=value.instruction;
                const auto owner=(operation.operands[4]&128)?operation.operands[5]:std::uint8_t{10};
                if(operation.opcode==31&&(operation.operands[4]&127)==6&&owner==10&&!probe.stack().empty()){
                    const auto cursor=probe.stack().back().context.stack_object;
                    if(previous_category_cursor&&cursor<=*previous_category_cursor)++category_cursor_rewinds;
                    previous_category_cursor=cursor;
                    category_cursors[static_cast<std::size_t>(category_stack_searches%category_cursors.size())]=cursor;
                    ++category_stack_searches;
                }
            };
            execution=register_owner?probe.run_synchronous_check_with_registers(*register_owner,
                scheduler_running_now,budget,primitive,trace):probe.run_synchronous_check(budget,primitive,trace);
        }else{execution.status=started;execution.diagnostic=probe.diagnostic();}
        const auto status=execution.status;
        item.steps=execution.steps;spent+=item.steps;item.variants=std::move(variants);
        if(item.variants.size()>saved_menu_row_limit-published_variants){
            item.variants.clear();item.diagnostic="aggregate menu variant pool exhausted";
            result.push_back(std::move(item));continue;
        }
        published_variants+=item.variants.size();
        if(status==odh::VmStatus::returned_true){
            item.state=saved_menu_actor_visible(world.find(caller))&&hide_interaction!=1?
                SavedTestFunctionResult::State::eligible:SavedTestFunctionResult::State::ineligible;
            if(item.state==SavedTestFunctionResult::State::ineligible)
                item.diagnostic=hide_interaction==1?"TestFunction hid this interaction":"caller is hidden or outside the lot";
        }
        else if(status==odh::VmStatus::returned_false||
                (execution.started&&status==odh::VmStatus::fault&&execution.source_error)){
            item.state=SavedTestFunctionResult::State::ineligible;
            if(last_trace)item.diagnostic=std::string(execution.source_error?"source ERROR rejected check at BHAV PC ":"returned false at BHAV PC ")+std::to_string(last_trace->pc)+
                " opcode "+std::to_string(last_trace->instruction.opcode);
        }
        else {
            item.diagnostic=execution.diagnostic.empty()?std::string("TestFunction did not return within the supported profile"):execution.diagnostic;
            if(last_trace){
                const auto scope=[](odh::RoutineScope value){switch(value){
                    case odh::RoutineScope::global:return "global";
                    case odh::RoutineScope::private_object:return "private";
                    case odh::RoutineScope::semiglobal:return "semiglobal";
                    case odh::RoutineScope::named_private:return "named-private";
                    case odh::RoutineScope::named_semiglobal:return "named-semiglobal";
                    }return "invalid";};
                item.diagnostic+=" at BHAV "+std::to_string(last_trace->key.id)+
                    " PC "+std::to_string(last_trace->pc)+" opcode "+std::to_string(last_trace->instruction.opcode)+
                    " owner "+std::to_string(last_trace->code_owner)+" scope "+scope(last_trace->key.scope);
            }
            item.diagnostic+=" steps="+std::to_string(item.steps)+" budget="+std::to_string(budget);
            if(!probe.stack().empty())item.diagnostic+=" cursor="+std::to_string(probe.stack().back().context.stack_object);
            item.diagnostic+=" temps0..3=";
            for(std::size_t n=0;n<4;++n){if(n)item.diagnostic+=",";item.diagnostic+=std::to_string(probe.temps()[n]);}
            item.diagnostic+=" category_stack_searches="+std::to_string(category_stack_searches)+
                " category_cursor_rewinds="+std::to_string(category_cursor_rewinds)+" category_cursor_tail=";
            const auto retained=std::min<std::uint64_t>(category_stack_searches,category_cursors.size());
            for(std::uint64_t n=category_stack_searches-retained;n<category_stack_searches;++n){
                if(n!=category_stack_searches-retained)item.diagnostic+=",";
                item.diagnostic+=std::to_string(category_cursors[static_cast<std::size_t>(n%category_cursors.size())]);
            }
        }
        const auto* live_actor=world.find(caller);const auto* live_object=world.find(target);
        if(!live_actor||!live_object||live_actor->generation!=generation_actor||live_object->generation!=generation_object){
            item.state=SavedTestFunctionResult::State::unsupported;item.diagnostic="live actor/target identity changed during TestFunction evaluation";
        }
        result.push_back(std::move(item));
    }
    return result;
}
