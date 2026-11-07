// SPDX-License-Identifier: MPL-2.0
#include "dialog_text_resources.hpp"
#include <sstream>
#include <iomanip>
namespace odh_host {
std::shared_ptr<const std::string> DialogTextResources::bytes(const DialogTextTable& resource){
    const auto key=std::make_pair(resource.archive,resource.entry);
    if(const auto found=resources_.find(key);found!=resources_.end())return found->second;
    if(!frozen_||resource.archive.empty()||resource.archive.size()>4096||resource.entry.size()>4096||resources_.size()>=512)
        throw odh::FormatError("dialog immutable resource authority unavailable");
    const auto frozen=frozen_(resource);
    if(!frozen||frozen->size()>16U*1024U*1024U-frozen_bytes_)throw odh::FormatError("dialog immutable resource missing or budget exhausted");
    frozen_bytes_+=frozen->size();resources_.emplace(key,frozen);return frozen;
}
const odh::StringTable* DialogTextResources::table(const DialogTextTable& resource,const std::string& kind){
    const Key key{resource.archive,resource.entry,kind,resource.id};
    if(const auto it=tables_.find(key);it!=tables_.end())return it->second?&*it->second:nullptr;
    if(tables_.size()>=512)throw odh::FormatError("dialog decoded table cache budget exhausted");
    const auto frozen=bytes(resource);std::istringstream input(*frozen,std::ios::binary);
    std::optional<odh::StringTable> found;odh::Limits limits;limits.iff_chunks=8192;
    odh::iff_chunks(odh::View(input,frozen->size()),[&](const odh::Chunk& chunk){
        if(chunk.type!=kind||chunk.id!=resource.id)return;
        if(found)throw odh::FormatError("ambiguous dialog string resource");
        if(chunk.payload.size()>16U*1024U*1024U-decoded_bytes_)throw odh::FormatError("dialog decoded string budget exhausted");
        auto parsed=odh::read_strings(chunk.payload);
        if(parsed.state!=odh::StringTableState::decoded)throw odh::FormatError("dialog string resource is undecoded");
        decoded_bytes_+=chunk.payload.size();found=std::move(parsed);
    },limits);
    const auto inserted=tables_.emplace(key,std::move(found)).first;return inserted->second?&*inserted->second:nullptr;
}
const odh::DefinitionCandidate* DialogTextResources::definition(std::uint32_t guid,DialogEntityCatalogPolicy policy) const{
    const auto* result=index_.unique(guid);if(!result)return nullptr;
    if(policy==DialogEntityCatalogPolicy::prefer_master&&result->definition.master_id&&result->definition.sub_index!=-1)
        return index_.unique_master(*result);
    return result;
}
const DialogTextResources::Catalog& DialogTextResources::metadata(const odh::DefinitionCandidate& definition){
    if(const auto it=catalogs_.find(definition.definition.guid);it!=catalogs_.end())return it->second;
    if(catalogs_.size()>=512)throw odh::FormatError("dialog catalog metadata cache budget exhausted");
    const DialogTextTable resource{definition.source,definition.asset,0};const auto frozen=bytes(resource);
    std::istringstream input(*frozen,std::ios::binary);std::optional<Catalog> found;odh::Limits limits;limits.iff_chunks=8192;
    odh::iff_chunks(odh::View(input,frozen->size()),[&](const odh::Chunk& chunk){
        if(chunk.type!="OBJD"||chunk.id!=definition.chunk_id)return;
        if(found)throw odh::FormatError("ambiguous dialog catalog OBJD");
        const auto words=odh::read_object_definition_words(chunk.payload);const auto actual=odh::read_object_definition(chunk.payload);
        if(words.size()<=41||actual.guid!=definition.definition.guid||actual.version!=definition.definition.version||
           actual.object_type!=definition.definition.object_type||actual.master_id!=definition.definition.master_id||actual.sub_index!=definition.definition.sub_index)
            throw odh::FormatError("dialog frozen catalog differs from admitted definition metadata");
        auto label=chunk.label;if(const auto end=label.find('\0');end!=std::string::npos)label.resize(end);
        auto strings=static_cast<std::uint16_t>(words[41]);
        // NativeCAS legacy wrote CatalogStringsID one word too early. Only an
        // independently authenticated creation may retain that compatibility.
        // Do not normalize OBJD or silently grant this to Catalog0 imports.
        if(!strings&&actual.version==138&&actual.object_type==2&&words[40]==2000&&legacy_&&legacy_(definition,frozen)){
            auto legacy_resource=resource;legacy_resource.id=2000;
            if(!table(legacy_resource,"CTSS"))throw odh::FormatError("authenticated legacy CAS catalog is absent");
            strings=2000;
        }
        found=Catalog{resource,strings,std::move(label)};
    },limits);
    if(!found)throw odh::FormatError("dialog catalog OBJD is absent");
    return catalogs_.emplace(definition.definition.guid,std::move(*found)).first->second;
}
DialogTextValue DialogTextResources::string(const DialogTextTable& resource,std::uint16_t index){
    try{
        const auto* strings=table(resource,"STR#");if(!strings)return {DialogTextAvailability::absent,{}};
        const auto* value=odh::string_in_language_or_english(*strings,language_,index);
        if(!value)return {DialogTextAvailability::value,"Missing String!!!"};
        if(!decode_)return {};return {DialogTextAvailability::value,decode_(value->value,value->language?value->language:std::uint8_t{1})};
    }catch(const std::exception&){return {};}
}
DialogTextValue DialogTextResources::catalog(std::uint32_t guid,unsigned index,DialogEntityCatalogPolicy policy){
    try{
        if(index>1)return {};const auto* def=definition(guid,policy);if(!def)return {};
        const auto& source=metadata(*def);if(!source.strings)return {DialogTextAvailability::absent,{}};
        auto resource=source.resource;resource.id=source.strings;const auto* strings=table(resource,"CTSS");
        if(!strings)return {DialogTextAvailability::absent,{}};
        const auto* value=odh::string_in_language_or_english(*strings,language_,index);
        if(!value)return {DialogTextAvailability::value,{}}; // present CTSS.GetString null is source empty output
        if(!decode_)return {};return {DialogTextAvailability::value,decode_(value->value,value->language?value->language:std::uint8_t{1})};
    }catch(const std::exception&){return {};}
}
DialogTextValue DialogTextResources::display(std::uint32_t guid,DialogEntityCatalogPolicy policy){
    try{
        const auto selected=catalog(guid,0,policy);
        if(selected.availability!=DialogTextAvailability::absent)return selected;
        if(policy==DialogEntityCatalogPolicy::prefer_master){const auto own=catalog(guid,0,DialogEntityCatalogPolicy::own);
            if(own.availability!=DialogTextAvailability::absent)return own;}
        const auto* def=definition(guid,DialogEntityCatalogPolicy::own);if(!def||!decode_)return {};
        const auto& own=metadata(*def);
        if(!own.label.empty())return {DialogTextAvailability::value,decode_(own.label,1)};
        std::ostringstream text;text<<std::uppercase<<std::hex<<guid;return {DialogTextAvailability::value,text.str()};
    }catch(const std::exception&){return {};}
}
}
