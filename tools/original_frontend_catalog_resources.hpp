// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/native_personal_catalog.hpp"
#include "odh/definition_index.hpp"
#include "odh/sha256.hpp"
#include "session_generic_resource_path.hpp"
#include "trusted_session_resource_path.hpp"
#include "resource.hpp"
#include <algorithm>
#include <tuple>
namespace odh_host {
// Selected DefinitionIndex is already an admitted immutable resource set. Rehash
// exact IFF entries, not FAR path strings; no saved locator grants file access.
inline odh::NativePersonalCatalog admit_original_frontend_catalog_resources(
    const odh::DefinitionIndex& index,const std::filesystem::path& game_root,const odh::Nbrs& raw,
    const std::array<std::uint8_t,32>& neighborhood,const std::set<std::int16_t>& reserved,
    const odh::NativePersonalCatalog* persisted=nullptr,const TrustedSessionResourceRoots* trusted=nullptr){
    const auto provider=index.provider_snapshot_candidates();
    std::vector<odh::NativePersonalCatalogDefinition> definitions;
    std::uint64_t remaining=64u*1024u*1024u;
    for(std::size_t ordinal=0;ordinal<provider.size();++ordinal){const auto& candidate=provider[ordinal];
        if(!odh::native_personal_catalog_type(candidate.definition.object_type))continue;
        if(!index.unique(candidate.definition.guid))throw odh::FormatError("hydrated person catalog resource GUID is ambiguous");
        odh::NativePersonalCatalogDefinition d;d.provider_ordinal=static_cast<std::uint32_t>(ordinal);d.guid=candidate.definition.guid;d.object_type=candidate.definition.object_type;d.definition_chunk_id=candidate.chunk_id;
        d.source=trusted?trusted_session_source_key(*trusted,candidate.source):generic_session_source_key(game_root,candidate.source);d.asset=candidate.asset;
        const auto inspect=[&](const odh::View& view){
            if(view.size()>16u*1024u*1024u||view.size()>remaining)throw odh::FormatError("hydrated person catalog IFF work budget exceeded");remaining-=view.size();
            bool definition=false;odh::iff_chunks(view,[&](const odh::Chunk& c){if(c.type=="OBJD"&&c.id==candidate.chunk_id){if(definition)throw odh::FormatError("hydrated person catalog OBJD is ambiguous");definition=true;const auto actual=odh::read_object_definition(c.payload);const auto metadata=[](const odh::ObjectDefinition& v){return std::tie(v.version,v.guid,v.object_type,v.master_id,v.sub_index,v.is_multitile,v.header_extent,v.remaining_bytes,v.tree_table_id,v.level_offset,v.tile_width,v.footprint_mask,v.slot_table_id,v.num_attributes,v.build_mode_type,v.global_flag,v.type_attribute_guid);};if(metadata(actual)!=metadata(candidate.definition))throw odh::FormatError("hydrated person catalog definition changed");}});
            if(!definition)throw odh::FormatError("hydrated person catalog OBJD is absent");
            odh::Sha256 hash;std::array<std::uint8_t,4096> block{};
            for(std::uint64_t at=0;at<view.size();){const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),view.size()-at));view.read(at,block.data(),n);hash.update(block.data(),n);at+=n;}d.fingerprint=hash.finish();
        };
        if(candidate.asset.empty())with_iff_file(candidate.source,inspect);else with_far_entry(candidate.source,candidate.asset,inspect);
        definitions.push_back(std::move(d));
    }
    if(!persisted)return odh::build_native_personal_catalog(raw,neighborhood,definitions,reserved);
    odh::validate_native_personal_catalog(*persisted);
    if(persisted->neighborhood_identity!=neighborhood||persisted->definitions.size()!=definitions.size())throw odh::FormatError("persisted hydrated catalog resource set changed");
    // Persisted producer order is authoritative on cold. Reordering the same
    // exact current resource set cannot reallocate any saved neighbor identity.
    for(const auto& old:persisted->definitions){const auto now=std::find_if(definitions.begin(),definitions.end(),[&](const auto& d){return d.guid==old.guid;});
        if(now==definitions.end()||now->source!=old.source||now->asset!=old.asset||now->definition_chunk_id!=old.definition_chunk_id||now->object_type!=old.object_type||now->fingerprint!=old.fingerprint)throw odh::FormatError("persisted hydrated catalog resource identity/content changed");
    }
    (void)odh::hydrate_native_personal_neighbors(raw,*persisted,reserved);return *persisted;
}
} //namespace odh_host
