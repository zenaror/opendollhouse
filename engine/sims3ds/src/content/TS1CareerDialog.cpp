// SPDX-License-Identifier: MPL-2.0
#include "content/TS1CareerDialog.h"
#include "content/ContentManager.h"
#include "content/TS1JobProvider.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "career_dialog_text.hpp"
#include <map>
namespace FSO { namespace Content { namespace TS1 {
std::optional<SimAntics::Model::VMDialogText> ResolveCareerDialog(
    SimAntics::Engine::VMStackFrame* frame, const std::string& token,
    const std::vector<uint16_t>& selectors, std::string& error) {
    using namespace FSO::Files::Formats::IFF::Chunks;
    using PD=SimAntics::Model::VMPersonDataVariable;
    if (!frame) { error="career dialog has no source frame"; return {}; }
    odh_host::CareerDialogBindings bindings;
    bindings.language=uint8_t(STR::DefaultLangCode);
    // TimeLocal is a locale formatter and needs neither a person nor Work.iff.
    if (token=="TimeLocal:") {
        auto value=odh_host::career_dialog_text(token,selectors,frame->Locals,bindings,error);
        if (!value) return {};
        SimAntics::Model::VMDialogText result;
        result.Append(value->bytes,value->language,false);
        return result;
    }
    auto* person=dynamic_cast<SimAntics::VMAvatar*>(frame->Caller);
    auto* content=Content::Get();
    auto* jobs=content?content->Jobs.get():nullptr;
    if (!person || !jobs) { error="career dialog has no person/job provider"; return {}; }
    bindings.caller=odh_host::CareerDialogPerson{
        person->GetPersonData(PD::JobType),person->GetPersonData(PD::JobPromotionLevel),
        (uint16_t(person->GetPersonData(PD::Gender))&1U)!=0};
    auto job=bindings.caller->job;
    if (selectors.size()==2) {
        if (selectors[0]>=frame->Locals.size() || selectors[1]>=frame->Locals.size()) {
            error="career dialog source locals unavailable"; return {};
        }
        job=frame->Locals[selectors[0]];
    }
    auto* definition=jobs->GetJob(job);
    if (!definition) { error="career dialog job unavailable"; return {}; }
    // The portable selector needs only level validity, not a duplicate career VM.
    odh::Career shape; shape.levels.resize(definition->JobLevels.size());
    std::vector<std::pair<uint16_t,odh::Career>> definitions;
    definitions.emplace_back(uint16_t(job),std::move(shape));
    odh::CareerCatalog catalog(std::move(definitions));
    bindings.careers=&catalog;
    std::map<uint16_t,odh::StringTable> tables;
    std::map<uint16_t,STR*> resources;
    bindings.strings=[&](uint16_t id)->std::optional<const odh::StringTable*> {
        auto known=tables.find(id);
        if (known!=tables.end()) return &known->second;
        auto* resource=jobs->JobStrings(int16_t(id));
        if (!resource) return static_cast<const odh::StringTable*>(nullptr);
        resources[id]=resource;
        odh::StringTable table{};table.state=odh::StringTableState::decoded;
        table.format=resource->SourceFormat;
        const uint8_t requested=bindings.language?bindings.language:1;
        for (uint8_t language: {uint8_t(1),requested}) {
            if (language==1 && requested==1 && !table.entries.empty()) continue;
            if (!language || size_t(language)>resource->LanguageSets.size()) continue;
            for (const auto& item:resource->LanguageSets[size_t(language)-1].Strings)
                table.entries.push_back({language,item.Value,item.Comment});
        }
        return &tables.emplace(id,std::move(table)).first->second;
    };
    auto value=odh_host::career_dialog_text(token,selectors,frame->Locals,bindings,error);
    if (!value) return {};
    STR* resource=value->table?resources[*value->table]:nullptr;
    const bool utf8=resource && (resource->SourceFormat==-1 || resource->SourceFormat==-4);
    SimAntics::Model::VMDialogText result;
    result.Append(value->bytes,value->language,utf8);
    return result;
}
}}}
