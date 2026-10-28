#include "odh/property_object_assets.hpp"
#include <limits>
#include <map>
#include <set>

namespace odh {
namespace {
int source_tile(std::int32_t coordinate) noexcept {
    const auto wide=static_cast<std::int64_t>(coordinate);
    const auto divided=wide>=0?wide/16:-((-wide+15)/16);
    const auto byte=(divided%256+256)%256;
    return static_cast<int>(byte<128?byte:byte-256);
}
}
bool property_object_position_in_world(const std::optional<EntityPosition>& position,unsigned dimension) noexcept {
    if(!position || !dimension || dimension>64)return false;
    const auto x=source_tile(position->x),y=source_tile(position->y);
    return x>0 && y>0 && x<static_cast<int>(dimension)-1 && y<static_cast<int>(dimension)-1;
}
PropertyObjectAssets inspect_property_object_assets(const World& world,const DefinitionIndex& index,
    const std::vector<SavedMultitileGroup>& partition,bool complete_inventory,unsigned dimension) {
    using R=PropertyObjectAssetsReason;using B=PropertyObjectAssetBucket;
    PropertyObjectAssets result;
    const auto fail=[&](R reason){result.reason=reason;return result;};
    if(!complete_inventory)return fail(R::incomplete_inventory);
    if(!dimension || dimension>64)return fail(R::invalid_dimension);
    if(partition.size()>4096)return fail(R::inventory_budget);
    std::set<std::int16_t> live,seen;
    for(auto id=world.next_entity(0);id;id=world.next_entity(*id)) {
        if(*id<=0 || !live.insert(*id).second)return fail(R::invalid_partition);
        if(live.size()>4096)return fail(R::inventory_budget);
    }
    std::size_t members=0;
    // Check the entire partition before consulting definitions. A malformed
    // singleton must not mask duplicate IDs behind an identity mismatch.
    for(const auto& group:partition) {
        if(group.leader<=0 || group.members.empty())return fail(R::invalid_partition);
        if(group.members.size()>4096-members)return fail(R::inventory_budget);
        members+=group.members.size();
        bool leader=false;
        for(const auto id:group.members) {
            if(id<=0 || !live.count(id) || !seen.insert(id).second)return fail(R::invalid_partition);
            leader=leader || id==group.leader;
        }
        if(!leader)return fail(R::invalid_partition);
    }
    if(seen!=live)return fail(R::incomplete_inventory);
    std::map<std::int16_t,const DefinitionCandidate*> definitions;
    for(const auto& group:partition) {
        const DefinitionCandidate* provenance=nullptr;
        for(const auto id:group.members) {
            const auto* entity=world.find(id);
            if(!entity || !entity->definition || !entity->definition->guid)return fail(R::missing_identity);
            const auto guid=entity->definition->guid;
            const auto* own=index.unique(guid);
            if(!own)return fail(index.candidates(guid)?R::ambiguous_definition:R::missing_identity);
            const auto& d=own->definition;
            if(d.is_multitile!=(d.master_id!=0))return fail(R::invalid_definition);
            if(entity->definition->master_guid!=group.master_guid)return fail(R::identity_mismatch);
            const auto type=entity->definition_words.find(9);
            if(type!=entity->definition_words.end() && static_cast<std::uint16_t>(type->second)!=d.object_type)
                return fail(R::definition_type_mismatch);
            if(d.is_multitile) {
                const auto* master=index.unique_master(*own);
                if(!master)return fail(R::missing_master);
                if(!master->definition.is_multitile)return fail(R::invalid_definition);
                if(group.master_guid!=master->definition.guid)return fail(R::identity_mismatch);
            } else if(group.master_guid || group.members.size()!=1)return fail(R::identity_mismatch);
            if(provenance && (provenance->source!=own->source || provenance->asset!=own->asset ||
                provenance->definition.master_id!=d.master_id))return fail(R::identity_mismatch);
            provenance=own;definitions.emplace(id,own);
        }
    }
    std::int32_t primary=0,architecture=0;
    R first_failure=R::none;
    const auto bad=[&](PropertyObjectAssetGroup& row,R reason){
        row.reason=reason;if(first_failure==R::none)first_failure=reason;
    };
    for(const auto& group:partition) {
        PropertyObjectAssetGroup row;row.leader=group.leader;
        const auto* entity=world.find(group.leader);
        const auto* own=definitions.at(group.leader);
        if(own->definition.object_type==7) {
            row.bucket=B::excluded;row.reason=R::own_type7;
            result.groups.push_back(row);continue;
        }
        row.in_world=property_object_position_in_world(entity->position,dimension);
        bool primary_candidate=false;
        if(*row.in_world) {
            const auto flags=entity->object_words.find(43);
            if(flags==entity->object_words.end()) {
                bad(row,R::missing_flags);result.groups.push_back(row);continue;
            }
            const auto disposition=inspect_property_disposition(index,own->definition.guid,
                own->definition.object_type,static_cast<std::uint16_t>(flags->second));
            if(disposition.disposition==PropertyDisposition::unresolved) {
                const auto reason=disposition.reason==PropertyDispositionReason::missing_build_mode?R::missing_build_mode:
                    disposition.reason==PropertyDispositionReason::missing_master?R::missing_master:R::invalid_definition;
                bad(row,reason);result.groups.push_back(row);continue;
            }
            primary_candidate=disposition.disposition==PropertyDisposition::remove;
        }
        if(!primary_candidate) {
            const auto* effective=own->definition.is_multitile?index.unique_master(*own):own;
            if(!effective || !effective->definition.build_mode_type) {
                bad(row,effective?R::missing_build_mode:R::missing_master);result.groups.push_back(row);continue;
            }
            const auto build=*effective->definition.build_mode_type;
            if(build==0 || build==4) {
                row.bucket=B::excluded;row.reason=build==0?R::excluded_build0:R::excluded_build4;
                result.groups.push_back(row);continue;
            }
        }
        const auto value=inspect_property_group_value(world,group,true);
        if(value.status!=PropertyValueStatus::resolved) {
            row.value_reason=value.reason;bad(row,R::group_value_unresolved);result.groups.push_back(row);continue;
        }
        row.value=value.value;
        auto& sum=primary_candidate?primary:architecture;
        const auto wide=static_cast<std::int64_t>(sum)+*row.value;
        if(wide<std::numeric_limits<std::int32_t>::min() || wide>std::numeric_limits<std::int32_t>::max()) {
            bad(row,R::arithmetic_overflow);result.groups.push_back(row);continue;
        }
        sum=static_cast<std::int32_t>(wide);
        row.bucket=primary_candidate?B::primary:B::architecture_objects;
        row.reason=primary_candidate?R::primary_candidate:R::architecture_fallback;
        result.groups.push_back(row);
    }
    if(first_failure!=R::none)return fail(first_failure);
    result.status=PropertyObjectAssetsStatus::resolved;result.reason=R::none;
    result.primary=primary;result.architecture_objects=architecture;return result;
}
const char* property_object_asset_bucket_name(PropertyObjectAssetBucket bucket) noexcept {
    switch(bucket){case PropertyObjectAssetBucket::unresolved:return "unresolved";case PropertyObjectAssetBucket::excluded:return "excluded";
    case PropertyObjectAssetBucket::primary:return "primary";case PropertyObjectAssetBucket::architecture_objects:return "architecture_objects";}return "invalid";
}
const char* property_object_assets_reason_name(PropertyObjectAssetsReason reason) noexcept {
    switch(reason){
#define ODH_ASSET_REASON(name) case PropertyObjectAssetsReason::name:return #name;
    ODH_ASSET_REASON(none) ODH_ASSET_REASON(incomplete_inventory) ODH_ASSET_REASON(invalid_dimension)
    ODH_ASSET_REASON(inventory_budget) ODH_ASSET_REASON(invalid_partition) ODH_ASSET_REASON(missing_identity)
    ODH_ASSET_REASON(ambiguous_definition) ODH_ASSET_REASON(identity_mismatch) ODH_ASSET_REASON(missing_master)
    ODH_ASSET_REASON(definition_type_mismatch) ODH_ASSET_REASON(missing_flags) ODH_ASSET_REASON(missing_build_mode)
    ODH_ASSET_REASON(invalid_definition) ODH_ASSET_REASON(group_value_unresolved) ODH_ASSET_REASON(arithmetic_overflow)
    ODH_ASSET_REASON(own_type7) ODH_ASSET_REASON(primary_candidate) ODH_ASSET_REASON(excluded_build0)
    ODH_ASSET_REASON(excluded_build4) ODH_ASSET_REASON(architecture_fallback)
#undef ODH_ASSET_REASON
    }return "invalid";
}
} // namespace odh
