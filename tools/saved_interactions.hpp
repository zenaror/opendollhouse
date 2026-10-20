#pragma once
#include "odh/animation.hpp"
#include "odh/definition_index.hpp"
#include "resource.hpp"

// Static discovery only: this does not evaluate tests, expose a pie menu, or
// imply that any decoded action is currently executable.
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
