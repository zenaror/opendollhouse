// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/native_personal_catalog.hpp"
#include "odh/ts1_character_family.hpp"
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
    const odh::NativePersonalCatalog* persisted=nullptr,const TrustedSessionResourceRoots* trusted=nullptr,
    const odh::Ts1CharacterFamilySnapshot* created=nullptr,
    const std::vector<odh::NeighborRecord>& owned_seeds={}){
    if(created){odh::validate_ts1_character_family_snapshot(*created);if(created->neighborhood_identity!=neighborhood)throw odh::FormatError("owned character catalog neighborhood differs");}
    const auto provider=index.provider_snapshot_candidates();
    std::set<std::uint32_t> verified_created;
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
        const odh::Ts1CreatedCharacter* owned=nullptr;
        if(created)for(const auto& c:created->characters)if(c.identity.guid==d.guid){owned=&c;break;}
        if(owned){
            const auto& exact=owned->identity;
            if(d.object_type!=2||d.definition_chunk_id!=exact.definition_chunk_id||d.source!=exact.character_asset||!d.asset.empty()||d.fingerprint!=exact.character_fingerprint)
                throw odh::FormatError("owned created character resource differs from exact creation receipt");
            verified_created.insert(d.guid);continue;
        }
        // A CAS file already present in the persisted producer keeps its exact
        // catalog slot. A later receipt-owned file is authenticated separately
        // and cannot enlarge/reorder that original producer's input.
        const auto owned_seed=std::find_if(owned_seeds.begin(),owned_seeds.end(),[&](const auto& n){return n.guid==d.guid;});
        if(owned_seed!=owned_seeds.end() && persisted &&
           std::none_of(persisted->definitions.begin(),persisted->definitions.end(),[&](const auto& old){return old.guid==d.guid;}))continue;
        definitions.push_back(std::move(d));
    }
    if(created&&verified_created.size()!=created->characters.size())throw odh::FormatError("owned created character is absent from selected resources");
    if(!persisted){
        std::vector<odh::NeighborRecord> prior;
        for(const auto& n:owned_seeds)if(std::any_of(definitions.begin(),definitions.end(),[&](const auto& d){return d.guid==n.guid;}))prior.push_back(n);
        return odh::build_native_personal_catalog(odh::merge_owned_neighbor_seeds(raw,prior),neighborhood,definitions,reserved);
    }
    odh::validate_native_personal_catalog(*persisted);
    if(persisted->neighborhood_identity!=neighborhood||persisted->definitions.size()!=definitions.size())throw odh::FormatError("persisted hydrated catalog resource set changed");
    // Persisted producer order is authoritative on cold. Reordering the same
    // exact current resource set cannot reallocate any saved neighbor identity.
    for(const auto& old:persisted->definitions){const auto now=std::find_if(definitions.begin(),definitions.end(),[&](const auto& d){return d.guid==old.guid;});
        if(now==definitions.end()||now->source!=old.source||now->asset!=old.asset||now->definition_chunk_id!=old.definition_chunk_id||now->object_type!=old.object_type||now->fingerprint!=old.fingerprint)throw odh::FormatError("persisted hydrated catalog resource identity/content changed");
    }
    (void)odh::hydrate_owned_native_personal_neighbors(raw,*persisted,owned_seeds,reserved);return *persisted;
}
} //namespace odh_host
