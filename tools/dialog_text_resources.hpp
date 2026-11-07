// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "dialog_text_substitutions.hpp"
#include "odh/definition_index.hpp"
#include "odh/animation.hpp"
#include <map>
#include <tuple>
namespace odh_host {
// Shared exact STR/CTSS reader. It NEVER reopens paths. The provider supplies
// immutable bytes authenticated against the same metadata/session authority.
class DialogTextResources {
public:
    using Decoder=std::function<std::string(const std::string&,std::uint8_t)>;
    using Frozen=std::function<std::shared_ptr<const std::string>(const DialogTextTable&)>;
    // Explicit compatibility authority for already admitted native-CAS creations.
    // The host must prove overlay GUID/NID, exact OBJD and fingerprint against
    // these immutable bytes; arbitrary imports must never be authorized.
    using LegacyNativeCas=std::function<bool(const odh::DefinitionCandidate&,const std::shared_ptr<const std::string>&)>;
    DialogTextResources(const odh::DefinitionIndex& index,std::uint8_t language,Decoder decode,Frozen frozen,LegacyNativeCas legacy={})
        :index_(index),language_(language),decode_(std::move(decode)),frozen_(std::move(frozen)),legacy_(std::move(legacy)){}
    DialogTextValue string(const DialogTextTable&,std::uint16_t index);
    DialogTextValue catalog(std::uint32_t guid,unsigned index,DialogEntityCatalogPolicy=DialogEntityCatalogPolicy::own);
    // Object/DynamicObjectName prefer master CTSS0 then fall back to the own
    // ToString. Me/NameLocal/ObjectLocal always use own CTSS/label/GUID.
    DialogTextValue display(std::uint32_t guid,DialogEntityCatalogPolicy);
private:
    struct Catalog {DialogTextTable resource;std::uint16_t strings=0;std::string label;};
    using Key=std::tuple<std::string,std::string,std::string,std::uint16_t>;
    std::shared_ptr<const std::string> bytes(const DialogTextTable&);
    const odh::StringTable* table(const DialogTextTable&,const std::string& kind);
    const Catalog& metadata(const odh::DefinitionCandidate&);
    const odh::DefinitionCandidate* definition(std::uint32_t,DialogEntityCatalogPolicy) const;
    const odh::DefinitionIndex& index_;std::uint8_t language_;Decoder decode_;Frozen frozen_;LegacyNativeCas legacy_;
    std::map<Key,std::optional<odh::StringTable>> tables_;
    std::map<std::uint32_t,Catalog> catalogs_;
    std::map<std::pair<std::string,std::string>,std::shared_ptr<const std::string>> resources_;
    std::uint64_t decoded_bytes_=0,frozen_bytes_=0;
};
}
