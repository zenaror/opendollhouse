// SPDX-License-Identifier: MPL-2.0
#include "house_panel_finance.hpp"
#include "architecture_edit_resources.hpp"
#include "property_architecture_price_resources.hpp"
#include <set>
#include <cstring>
namespace odh_host {
HousePropertyPolicy read_house_property_policy(const odh::View& global,const odh::Simi& simi,
        odh::PropertyValuationProfile profile,odh::NamedFloatLimits limits){
    if(profile!=odh::PropertyValuationProfile::comparison_pc24)
        throw odh::FormatError("House property requires an explicit supported numeric profile");
    if(global.size()>16u*1024u*1024u)throw odh::FormatError("House property selected Global exceeds byte budget");
    std::optional<odh::NamedFloatConstants> financial;
    odh::Limits iff_limits;iff_limits.iff_chunks=4096;
    odh::iff_chunks(global,[&](const odh::Chunk& chunk){
        if(chunk.type=="FCNS"&&chunk.id==4){
            if(financial)throw odh::FormatError("House property selected Global has duplicate FCNS4");
            financial=odh::read_named_float_constants(chunk.payload,limits);
        }
    },iff_limits);
    HousePropertyPolicy out{simi.lot_value,0x3f333333u,profile,true};
    if(financial)for(const auto& record:financial->records)if(record.name=="architecture value multiplier"){
        static_assert(sizeof(record.value)==sizeof(out.multiplier_bits),"binary32 policy bits");
        std::memcpy(&out.multiplier_bits,&record.value,sizeof(out.multiplier_bits));
        out.multiplier_defaulted=false;break;
    }
    return out;
}
odh::PropertyValuation inspect_house_property(const HousePropertyInput& input){
    const auto partition=house_asset_partition(input.world,input.original_world,input.definitions,
        input.dynamic_groups,input.standalone_creations);
    const auto architecture=house_metric_architecture(input.retained_terrain,input.current_geometry,input.original_underlay);
    return odh::inspect_property_valuation({input.world,input.definitions,partition,true,architecture,
        input.prices,input.policy.lot_base,input.policy.multiplier_bits,input.policy.profile});
}
std::vector<odh::SavedMultitileGroup> house_asset_partition(const odh::World& world,
    const odh::SavedWorld& source,const odh::DefinitionIndex& definitions,const std::vector<odh::ObjectGroup>& dynamic,
    const std::vector<odh::WorldEntityReference>& standalone){
    if(source.records.size()>4096||source.multitile_groups.size()>4096||dynamic.size()>4096||standalone.size()>4096)
        throw odh::FormatError("House financial inventory budget exceeded");
    std::map<std::int16_t,const odh::SavedWorldEntity*> origins;
    for(const auto& row:source.records){
        const auto id=static_cast<std::int16_t>(row.saved.object_id);
        if(!origins.emplace(id,&row).second)throw odh::FormatError("House valuation duplicated saved origin");
    }
    std::vector<odh::SavedMultitileGroup> result;std::set<std::int16_t> assigned;
    const auto admit=[&](odh::SavedMultitileGroup group){
        for(auto id:group.members)if(!assigned.insert(id).second)
            throw odh::FormatError("House valuation physical membership overlaps");
        result.push_back(std::move(group));
    };
    // Current event-owned groups supersede the imported topology. They may
    // contain an imported chain unchanged, split it, or join new members.
    for(const auto& group:dynamic){
        if(group.members.empty()||group.members.size()>4096||group.base_id<=0||
           (group.kind==odh::ObjectGroupKind::live_association?group.definition_guid!=0:
            !group.definition_guid||(!group.multitile&&group.members.size()!=1)))
            throw odh::FormatError("House current group shape is invalid");
        std::size_t surviving=0;std::vector<std::int16_t> ids;bool base=false;
        for(const auto& part:group.members){
            const auto* e=world.find(part.id);if(!e)continue;
            if(e->generation!=part.generation)throw odh::FormatError("House current group identity was reused");
            if(e->kind!=odh::EntityKind::object||e->dead||e->ui_ghost||!e->definition)
                throw odh::FormatError("House current group lacks actual live object definition");
            if(group.kind==odh::ObjectGroupKind::live_association){
                if(!part.origin||part.origin->guid!=e->definition->guid||part.origin->master_guid!=e->definition->master_guid)
                    throw odh::FormatError("House current association resource identity differs");
            }else if(group.kind!=odh::ObjectGroupKind::constructed||
                (group.multitile?e->definition->master_guid!=std::optional<std::uint32_t>{group.definition_guid}:
                 e->definition->guid!=group.definition_guid||e->definition->master_guid))
                throw odh::FormatError("House current group resource identity differs");
            ids.push_back(part.id);++surviving;base=base||part.id==group.base_id;
        }
        if(!surviving)continue;
        if(surviving!=group.members.size()||!base)throw odh::FormatError("House current group is partial or lacks its actual base");
        const auto master=group.kind==odh::ObjectGroupKind::constructed&&group.multitile?
            std::optional<std::uint32_t>{group.definition_guid}:std::nullopt;
        admit({group.base_id,master,std::move(ids)});
    }
    std::set<std::int16_t> original_members;
    for(const auto& group:source.multitile_groups){
        if(group.members.empty()||group.members.size()>4096)throw odh::FormatError("House source group member budget");
        std::size_t surviving=0,covered=0;bool leader=false;
        for(auto id:group.members){
            if(!original_members.insert(id).second)throw odh::FormatError("House imported physical membership overlaps");
            leader=leader||id==group.leader;
            const auto origin=origins.find(id);if(origin==origins.end())throw odh::FormatError("House group has no saved origin");
            const auto* e=world.find(id);
            if(!e||e->generation!=origin->second->imported_generation)continue;
            if(!e->definition||e->definition->guid!=origin->second->saved.guid)
                throw odh::FormatError("House group saved identity changed");
            ++surviving;if(assigned.count(id))++covered;
        }
        if(!leader)throw odh::FormatError("House imported group lacks its saved root");
        if(!surviving)continue;
        if(covered==surviving)continue; // Actual topology owns every surviving imported body.
        if(covered)throw odh::FormatError("House current topology covers only part of an imported group");
        if(surviving!=group.members.size())throw odh::FormatError("House valuation physical group partially retired");
        admit(group);
    }
    for(auto id=world.next_entity(0);id;id=world.next_entity(*id)){
        if(assigned.count(*id))continue;
        const auto* e=world.find(*id);const auto* own=e&&e->definition?definitions.unique(e->definition->guid):nullptr;
        if(!e||!e->definition)throw odh::FormatError("House valuation has no current definition");
        const auto origin=origins.find(*id);
        const bool original_singleton=origin!=origins.end()&&origin->second->imported_generation==e->generation&&
            origin->second->saved.guid==e->definition->guid&&!origin->second->tail.multitile;
        const bool created_singleton=std::any_of(standalone.begin(),standalone.end(),[&](const auto& row){return row.id==*id&&row.generation==e->generation;});
        if(!own||e->definition->master_guid||own->definition.is_multitile||(!original_singleton&&!created_singleton))
            throw odh::FormatError("House valuation lacks certified physical group or standalone origin: id="+std::to_string(*id)+" guid="+std::to_string(e->definition->guid));
        admit({*id,{}, {*id}});
    }
    return result;
}
odh::SavedArchitecture house_retained_terrain(const odh::SavedArchitecture& terrain,
    const odh::SavedArchitecture& geometry){
    if(!geometry.width||geometry.width>64||geometry.width!=geometry.height||!terrain.width||terrain.width>64||terrain.width!=terrain.height||geometry.width>terrain.width||geometry.height>terrain.height)
        throw odh::FormatError("House metric architecture extent mismatch");
    auto out=geometry;const auto cells=std::size_t(geometry.width)*geometry.height;
    const auto crop=[&](const auto& input,auto& output){
        if(!input){output.reset();return;}
        if(input->size()!=std::size_t(terrain.width)*terrain.height)throw odh::FormatError("House retained terrain extent mismatch");
        output.emplace();output->reserve(cells);
        for(std::uint32_t y=0;y<geometry.height;++y)for(std::uint32_t x=0;x<geometry.width;++x)
            output->push_back((*input)[std::size_t(y)*terrain.width+x]);
    };
    crop(terrain.pools,out.pools);crop(terrain.water,out.water);
    for(unsigned level=0;level<2;++level){
        if(geometry.levels[level].floors.size()!=cells||geometry.levels[level].walls.size()!=cells)
            throw odh::FormatError("House metric live grids incomplete");
        crop(terrain.levels[level].flags,out.levels[level].flags);
    }
    return out;
}
odh::SavedArchitecture house_metric_architecture(const odh::SavedArchitecture& terrain,
    const odh::SavedArchitecture& geometry,const odh::PreparedArchitecture& underlay){
    auto out=house_retained_terrain(terrain,geometry);
    const auto cells=std::size_t(out.width)*out.height;
    if(underlay.width!=out.width||underlay.height!=out.height||underlay.levels[0].floors.size()!=cells)
        throw odh::FormatError("House metric underlay extent mismatch");
    // Sentinel surfaces hide the original underlay in the renderer only. No
    // editable underlay layer exists beneath them in the current editor. When
    // an actual floor edit replaces the sentinel, keep that new prepared ID.
    for(std::size_t cell=0;cell<cells;++cell){
        const auto floor=geometry.levels[0].floors[cell];
        if(floor==65534||floor==65535)out.levels[0].floors[cell]=underlay.levels[0].floors[cell];
    }
    return out;
}
odh::PropertyArchitecturePrices house_prepared_prices(const std::filesystem::path& build,
    const std::filesystem::path& floors,const std::filesystem::path& walls,const odh::ArchitectureResourceCatalog& catalog){
    const auto strings=odh_tools::build_strings(build.string());
    const auto floor_sprites=odh_tools::sprite_ids(floors.string(),"SPR2");
    const auto wall_sprites=odh_tools::sprite_ids(walls.string(),"SPR#");
    // A full admitted provider makes missing IDs known lookup misses. Its own
    // structural dictionary is deliberately not copied into financial prices.
    ArchitectureEditResources resources(build,floors,walls,catalog,1);
    odh::PropertyArchitecturePrices prices;
    for(std::uint16_t id=0;id<256;++id)prices.structural[id]=0;
    for(const auto pair:{std::pair<std::uint16_t,std::size_t>{1,0},{2,3},{13,6},{12,9},{14,12}})
        prices.structural[pair.first]=odh::architecture_price_from_strings(strings.at(0x81),pair.second);
    prices.structural[255]=prices.structural.at(1);
    prices.pool_tile_price=odh::architecture_price_from_strings(strings.at(0x85),0);
    for(const auto& item:resources.descriptors()){
        using Kind=odh::ArchitectureEditMaterialKind;
        if(item.kind==Kind::floor&&((item.id>0&&item.id<30&&floor_sprites.count(item.id))||(item.id>=256&&item.id<=65533)))
            prices.floors[item.id]=item.price;
        if(item.kind==Kind::wallpaper&&((item.id>0&&item.id<31&&wall_sprites.count(std::uint16_t(1536+item.id)))||(item.id>=256&&item.id<=65533)))
            prices.wallpaper[item.id]=item.price;
    }
    prices.wallpaper[0]=0;prices.floor_lookup_complete=true;prices.wallpaper_lookup_complete=true;
    return prices;
}
}
