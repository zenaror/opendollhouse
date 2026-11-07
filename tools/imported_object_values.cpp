// SPDX-License-Identifier: MPL-2.0
#include "imported_object_values.hpp"
#include "resource.hpp"
#include "session_generic_resource_path.hpp"
#include "odh/sha256.hpp"
#include "odh/object_value_registry.hpp"
#include "odh/object_runtime_resources.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace odh_host {
namespace {
using namespace odh;
void need(bool value,const char* why){if(!value)throw FormatError(why);}
bool same(const ObjectDefinition& a,const ObjectDefinition& b){
    return std::tie(a.version,a.guid,a.object_type,a.master_id,a.sub_index,a.is_multitile,a.header_extent,a.remaining_bytes,
        a.tree_table_id,a.level_offset,a.tile_width,a.footprint_mask,a.slot_table_id,a.num_attributes,a.build_mode_type,a.global_flag,a.type_attribute_guid)==
        std::tie(b.version,b.guid,b.object_type,b.master_id,b.sub_index,b.is_multitile,b.header_extent,b.remaining_bytes,
        b.tree_table_id,b.level_offset,b.tile_width,b.footprint_mask,b.slot_table_id,b.num_attributes,b.build_mode_type,b.global_flag,b.type_attribute_guid);
}
struct Metadata {std::optional<std::uint16_t> price,lead;std::optional<bool> catalog_listed;std::array<std::uint8_t,32> digest{};};
using AssetCandidates=std::map<std::pair<std::string,std::string>,std::vector<const DefinitionCandidate*>>;
std::map<const DefinitionCandidate*,Metadata> read_metadata_assets(const AssetCandidates& assets){
    std::map<const DefinitionCandidate*,Metadata> metadata;
    std::uint64_t payload_budget=16*1024*1024;
    for(const auto& asset:assets){
        const auto read=[&](const View& resource){
            need(resource.size()<=64*1024*1024,"imported value IFF exceeds bounded resource size");
            std::map<std::uint16_t,const DefinitionCandidate*> wanted;
            for(const auto* candidate:asset.second)
                need(wanted.emplace(candidate->chunk_id,candidate).second,"imported value indexed OBJD chunk is ambiguous");
            std::set<std::uint16_t> found;
            iff_chunks(resource,[&](const Chunk& chunk){
                if(chunk.type!="OBJD")return;const auto item=wanted.find(chunk.id);if(item==wanted.end())return;
                need(found.insert(chunk.id).second,"imported value OBJD chunk duplicated");
                need(chunk.payload.size()<=65536&&chunk.payload.size()<=payload_budget,"imported value OBJD payload budget exceeded");
                payload_budget-=chunk.payload.size();
                need(same(read_object_definition(chunk.payload),item->second->definition),"imported value OBJD changed since indexing");
                Metadata value;std::vector<std::uint8_t> bytes(static_cast<std::size_t>(chunk.payload.size()));
                chunk.payload.read(0,bytes.data(),bytes.size());value.digest=sha256(bytes.data(),bytes.size());
                if(chunk.payload.size()>=38)value.price=chunk.payload.le16(36);
                if(chunk.payload.size()>=128)value.lead=chunk.payload.le16(126);
                if(chunk.payload.size()>=140){const auto& definition=item->second->definition;
                    value.catalog_listed=(chunk.payload.le16(80)||chunk.payload.le16(138))&&!chunk.payload.le16(32)&&
                        (definition.is_multitile||chunk.payload.le16(8)>0)&&(!definition.master_id||definition.sub_index==-1);
                }
                metadata.emplace(item->second,std::move(value));
            });
            need(found.size()==wanted.size(),"imported value indexed OBJD absent from exact IFF");
        };
        if(asset.first.second.empty())with_iff_file(asset.first.first,read);
        else {Limits limits;limits.far_entries=16384;limits.far_name_bytes=1024*1024;
            with_far_entry(asset.first.first,asset.first.second,read,limits);}
    }
    return metadata;
}
std::int16_t offset(std::int32_t value,int factor){
    const auto scaled=std::int64_t(value)*factor;
    need(scaled>=-32768&&scaled<=32767,"imported value offset exceeds signed16 VM coordinates");
    return static_cast<std::int16_t>(scaled);
}
}
odh::SessionObjectValueGroup prepare_constructed_object_values(const odh::World& world,
        const odh::DefinitionIndex& definitions,const odh::ObjectGroup& physical,ImportedObjectSourceKey source_key){
    using namespace odh;
    need(bool(source_key),"constructed values require portable resource authority");
    need(physical.members.size()<=64,"constructed value member budget exceeded");
    validate_object_groups(ObjectGroupSnapshot{{physical}},world.snapshot());
    const auto* requested=definitions.unique(physical.definition_guid);
    need(requested,"constructed value requested definition missing or ambiguous");
    ObjectGroupConstructionPlan plan;
    const auto plan_read=[&](const View& resource){
        need(resource.size()<=64*1024*1024,"constructed value IFF exceeds resource budget");
        plan=plan_object_group_construction(resource,*requested,definitions,64);
    };
    if(requested->asset.empty())with_iff_file(requested->source,plan_read);
    else {Limits limits;limits.far_entries=16384;limits.far_name_bytes=1024*1024;
        with_far_entry(requested->source,requested->asset,plan_read,limits);}
    need(plan.multitile==physical.multitile&&plan.parts.size()==physical.members.size(),
         "constructed value physical group differs from its exact source plan");
    need(physical.base_id==physical.members.at(plan.base_index).id,
         "constructed value BaseObject differs from authenticated plan");
    AssetCandidates assets;std::map<const DefinitionCandidate*,std::string> portable;
    std::map<std::string,std::string> physical_for_key;
    const auto admit=[&](const DefinitionCandidate* candidate){
        need(candidate&&definitions.unique(candidate->definition.guid)==candidate,"constructed value member definition unavailable");
        if(portable.count(candidate))return;
        auto key=source_key(candidate->source);validate_generic_session_source_key(key);
        need(key.size()<=4096&&candidate->asset.size()<=4096&&key.find('\0')==std::string::npos&&
            candidate->asset.find('\0')==std::string::npos,"constructed value resource identity exceeds bounds");
        const auto mapped=physical_for_key.emplace(key,candidate->source);
        need(mapped.second||mapped.first->second==candidate->source,"constructed value portable source key aliases different inputs");
        portable.emplace(candidate,std::move(key));assets[{candidate->source,candidate->asset}].push_back(candidate);
    };
    admit(requested);
    for(std::size_t n=0;n<physical.members.size();++n){
        const auto& member=physical.members[n];const auto* entity=world.find(member.id);const auto& part=plan.parts[n];
        need(entity&&entity->definition&&entity->definition->guid==part.candidate->definition.guid&&
            member.offset_x==part.offset_x&&member.offset_y==part.offset_y&&member.offset_level==part.offset_level,
             "constructed value member order, resource or offset differs from exact source plan");
        admit(part.candidate);
    }
    const auto metadata=read_metadata_assets(assets);const auto& price=metadata.at(requested);
    need(price.price&&price.catalog_listed,"constructed value catalog metadata incomplete");
    SessionObjectValueGroup group;group.multitile=physical.multitile;
    for(std::size_t n=0;n<physical.members.size();++n){
        const auto& member=physical.members[n];const auto* entity=world.find(member.id);const auto* candidate=plan.parts[n].candidate;
        SessionObjectValueMember value;value.object={member.id,member.generation,candidate->definition.guid,
            portable.at(candidate),candidate->asset,candidate->chunk_id,entity->definition->master_guid};
        value.offset_x=member.offset_x;value.offset_y=member.offset_y;value.offset_level=member.offset_level;
        value.lead_object=metadata.at(candidate).lead;value.wear_known=true;value.wear=std::uint16_t{0};value.disabled=std::uint8_t{0};
        group.members.push_back(std::move(value));
    }
    group.base=object_value_source_base(group.members);
    need(group.base&&group.base->id==physical.base_id,"constructed value source BaseObject is unavailable or inconsistent");
    group.initial_price=*price.catalog_listed?*price.price:0;
    group.price_origin={SessionObjectPriceSource::constructed_catalog,requested->definition.guid,portable.at(requested),
        requested->asset,requested->chunk_id,price.digest,{}};
    return group;
}
odh::SessionObjectTradeLedger prepare_imported_object_values(const odh::SavedWorld& saved,
        const odh::DefinitionIndex& definitions,ImportedObjectSourceKey source_key){
    using namespace odh;
    need(saved.world&&source_key,"imported values require fresh World and source-key authority");
    need(saved.records.size()<=4096&&saved.multitile_groups.size()<=4096,"imported value record/group budget exceeded");
    std::map<std::int16_t,const SavedWorldEntity*> records;
    std::map<const DefinitionCandidate*,Metadata> metadata;
    AssetCandidates assets;
    std::map<const DefinitionCandidate*,std::string> portable;
    std::map<std::string,std::string> physical_for_key;
    const auto admit=[&](const DefinitionCandidate* candidate){
        need(candidate&&definitions.unique(candidate->definition.guid)==candidate,"imported value definition unavailable or ambiguous");
        if(portable.count(candidate))return;
        auto key=source_key(candidate->source);validate_generic_session_source_key(key);
        need(key.size()<=4096&&candidate->asset.size()<=4096&&key.find('\0')==std::string::npos&&
            candidate->asset.find('\0')==std::string::npos,"imported value resource identity exceeds bounds");
        const auto mapped=physical_for_key.emplace(key,candidate->source);
        need(mapped.second||mapped.first->second==candidate->source,"imported value portable source key aliases different inputs");
        portable.emplace(candidate,std::move(key));assets[{candidate->source,candidate->asset}].push_back(candidate);
    };
    for(const auto& record:saved.records){
        const auto id=static_cast<std::int16_t>(record.saved.object_id);
        need(id>0&&records.emplace(id,&record).second,"imported value record identity duplicated or invalid");
        need(record.saved.prefix.object_data[11]==id&&record.definition.guid==record.saved.guid,
             "imported value saved identity banks disagree");
        const auto* entity=saved.world->find(id);
        need(entity&&entity->generation==record.imported_generation&&entity->definition&&
            entity->definition->guid==record.saved.guid,"imported value World identity differs from source record");
        if(entity->kind!=EntityKind::object)continue;
        need(!entity->dead&&!entity->ui_ghost,"imported value object is not a live source object");
        const auto* candidate=definitions.unique(record.saved.guid);admit(candidate);
        need(same(candidate->definition,record.definition),"imported value record definition differs from index");
        const auto* master=record.tail.multitile?definitions.unique_master(*candidate):nullptr;
        need(entity->definition->master_guid==(master?std::optional<std::uint32_t>{master->definition.guid}:std::nullopt),
             "imported value World master differs from exact resource index");
        if(master)admit(master);
    }
    metadata=read_metadata_assets(assets);
    SessionObjectTradeLedger result;result.version=2;
    std::set<std::int16_t> assigned;
    std::vector<std::vector<std::int16_t>> groups;
    for(const auto& group:saved.multitile_groups){
        need(!group.members.empty()&&group.members.size()<=4096,"imported value source group incomplete");
        auto members=group.members;
        std::set<std::int16_t> unique;for(const auto id:members)need(unique.insert(id).second,"imported value source group repeats member");
        const auto lead=std::find(members.begin(),members.end(),group.leader);
        need(lead!=members.end(),"imported value source parent is absent");
        for(const auto id:members){need(records.count(id)&&records.at(id)->tail.multitile,
            "imported value physical member lacks original multitile record");
            const auto parent=records.at(id)->tail.multitile->parent_id;
            need((parent?parent:id)==group.leader,"imported value membership differs from saved parent linkage");}
        std::rotate(members.begin(),lead,lead+1);groups.push_back(std::move(members));
    }
    for(const auto& group:groups)for(const auto id:group){
        need(records.count(id)&&assigned.insert(id).second,"imported value group member absent or repeated");
        need(saved.world->find(id)->kind==EntityKind::object&&records.at(id)->tail.multitile,
             "imported value source physical group has unsupported member kind");
    }
    for(const auto& item:records)if(saved.world->find(item.first)->kind==EntityKind::object&&!assigned.count(item.first)){
        need(!item.second->tail.multitile,"imported value multipart object lacks its complete physical group");
        groups.push_back({item.first});
    }
    std::sort(groups.begin(),groups.end(),[](const auto& a,const auto& b){return *std::min_element(a.begin(),a.end())<*std::min_element(b.begin(),b.end());});
    for(const auto& ids:groups){
        SessionObjectValueGroup group;group.group_id=result.groups.size()+1;group.revision=group.group_id;
        group.multitile=records.at(ids.front())->tail.multitile.has_value();
        for(const auto id:ids){
            const auto& record=*records.at(id);const auto* entity=saved.world->find(id);
            const auto* candidate=definitions.unique(record.saved.guid);const auto& meta=metadata.at(candidate);
            SessionObjectValueMember member;member.object={id,entity->generation,record.saved.guid,portable.at(candidate),candidate->asset,candidate->chunk_id,entity->definition->master_guid};
            member.lead_object=meta.lead;member.wear_known=true;member.wear=std::uint16_t{0};member.disabled=std::uint8_t{0};
            if(record.tail.multitile){const auto& mt=*record.tail.multitile;
                member.offset_x=offset(mt.group_x,16);member.offset_y=offset(mt.group_y,16);member.offset_level=offset(mt.group_level,1);}
            group.members.push_back(std::move(member));
        }
        group.base=object_value_source_base(group.members);
        const auto price_member=*std::min_element(ids.begin(),ids.end());
        const auto* price_definition=definitions.unique(records.at(price_member)->saved.guid);
        if(group.multitile)price_definition=definitions.unique_master(*price_definition);
        if(price_definition){const auto& meta=metadata.at(price_definition);if(meta.price){
            group.initial_price=*meta.price;group.price_origin={SessionObjectPriceSource::fresh_import_objd,price_definition->definition.guid,
                portable.at(price_definition),price_definition->asset,price_definition->chunk_id,meta.digest,{}};
        }}
        result.groups.push_back(std::move(group));
    }
    result.revision=result.groups.size();
    validate_object_value_groups(result,saved.world->snapshot());
    return result;
}
}
