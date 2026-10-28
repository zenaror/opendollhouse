#pragma once
#include "odh/architecture_price_metadata.hpp"
#include "odh/property_architecture_value.hpp"
#include "architecture_material_resources.hpp"
#include "saved_architecture_resources.hpp"
#include <set>

namespace odh_tools {
inline std::map<std::uint16_t,odh::StringTable> build_strings(const std::string& path) {
    std::map<std::uint16_t,odh::StringTable> result;std::uint64_t bytes=0;
    with_iff_file(path,[&](const auto& view){odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const auto& chunk){
            if(chunk.type!="STR#" || chunk.id<0x81 || chunk.id>0x85 || chunk.id==0x84)return;
            if(chunk.payload.size()>1024*1024-bytes)throw odh::FormatError("build price string budget exceeded");
            bytes+=chunk.payload.size();
            if(!result.emplace(chunk.id,odh::read_strings(chunk.payload)).second)
                throw odh::FormatError("ambiguous build price STR#");
        },limits);
    });
    for(const auto id:{0x81,0x82,0x83,0x85})if(!result.count(static_cast<std::uint16_t>(id)))
        throw odh::FormatError("required build price STR# absent");
    return result;
}
inline std::set<std::uint16_t> sprite_ids(const std::string& path,const std::string& type) {
    std::set<std::uint16_t> result;
    with_iff_file(path,[&](const auto& view){odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const auto& chunk){if(chunk.type==type && !result.insert(chunk.id).second)
            throw odh::FormatError("ambiguous global material sprite ID");},limits);
    });return result;
}
inline std::size_t language_size(const odh::StringTable& table,std::uint8_t language) {
    // Match TS1 set fallback, without per-entry fallback or repeated scans.
    auto selected=language==0?std::uint8_t{1}:language;
    const auto count=[&](std::uint8_t id){std::size_t n=0;for(const auto& entry:table.entries)
        if((entry.language==0?1:entry.language)==id)++n;return n;};
    auto size=count(selected);if(!size && selected!=1)size=count(1);return size;
}
}

namespace odh_tools {
// Host-only read-only binding of one raw house and its explicit price sources.
// No whole-property authority or filesystem identity pinning is implied.
struct PropertyArchitecturePriceResources {
    SavedArchitectureResource saved;
    odh::PropertyArchitecturePrices prices;
    std::size_t loaded=0,catalog_resources=0,floor_map_resolved=0,wall_map_resolved=0;
    std::set<std::uint16_t> used_floors,used_walls,used_styles;
};
inline PropertyArchitecturePriceResources load_property_architecture_prices(
    const std::string& house,const std::string& build,const std::string& floors,
    const std::string& walls,const std::vector<odh::ArchitectureResourceSource>& sources) {
    auto raw=saved_architecture_resource(house);
    const auto strings=build_strings(build);
    const auto floor_sprites=sprite_ids(floors,"SPR2"),wall_sprites=sprite_ids(walls,"SPR#");
    odh::PropertyArchitecturePrices prices;
    const auto& structure=strings.at(0x81);
    // Bounded comparison-stock initialization: audited table entries start at
    // zero, then004cd2d8 writes five named styles and255. This is NOT proof
    // that every possible runtime/CC writer leaves that table unchanged.
    for(std::uint16_t style=0;style<256;++style)prices.structural[style]=0;
    // Comparison004cd2d8 initializes these five structural styles from STR#81.
    for(const auto pair:{std::pair<std::uint16_t,std::size_t>{1,0},{2,3},{13,6},{12,9},{14,12}})
        prices.structural[pair.first]=odh::architecture_price_from_strings(structure,pair.second);
    prices.structural[255]=prices.structural.at(1);
    prices.pool_tile_price=odh::architecture_price_from_strings(strings.at(0x85),0);
    const auto floor_count=language_size(strings.at(0x82),1)/3;
    // Pinned provider's last floor triple is outside its global floor loop.
    for(std::size_t id=1;id<floor_count && id<30;++id)if(floor_sprites.count(static_cast<std::uint16_t>(id)))
        prices.floors[static_cast<std::uint16_t>(id)]=odh::architecture_price_from_strings(strings.at(0x82),(id-1)*3);
    const auto wall_count=language_size(strings.at(0x83),1)/3;
    for(std::size_t id=1;id<=wall_count && id<31;++id)if(wall_sprites.count(static_cast<std::uint16_t>(1536+id)))
        prices.wallpaper[static_cast<std::uint16_t>(id)]=odh::architecture_price_from_strings(strings.at(0x83),(id-1)*3);
    // Wallpaper provider has no entry0; this is a demonstrated lookup miss.
    prices.wallpaper[0]=0;
    const auto catalog=odh::discover_architecture_resources(sources);
    ArchitectureMaterialResources materials(catalog);
    const auto* floor_map=selected_architecture_map(raw,"FLRm");const auto* wall_map=selected_architecture_map(raw,"WALm");
    const odh::ArchitectureMap empty{};
    const auto aliases=odh::resolve_architecture_aliases(floor_map?*floor_map:empty,wall_map?*wall_map:empty,catalog.floors(),catalog.walls());
    std::set<std::uint16_t> used_floors,used_walls,used_styles;
    for(const auto& level:raw.architecture.levels)for(std::size_t n=0;n<level.floors.size();++n){
        if(!level.flags || n>=level.flags->size())throw odh::FormatError("architecture flags absent");
        if((*level.flags)[n]&32U)continue;
        const auto& wall=level.walls.at(n);const auto floor=level.floors[n];
        if(floor && floor!=255)used_floors.insert(floor);
        else if(floor==255 && (wall.segments&48U)){
            if(wall.top_left_style)used_floors.insert(wall.top_left_style);
            if(wall.top_left_pattern)used_floors.insert(wall.top_left_pattern);
        }
        // Collect only prices the evaluator consumes. Stale, unused pattern
        // fields must not cause an unrelated material to be opened or rejected.
        const auto mask=static_cast<unsigned>(wall.segments);
        if(mask && !(mask&0xc0U)) {
            unsigned bit=1;while(!(mask&bit))bit<<=1;
            while(bit) {
                auto style=wall.top_right_style;std::uint16_t pattern=0;bool known=true;
                switch(bit) {
                case 1:style=wall.top_left_style;pattern=wall.top_left_pattern;break;
                case 2:pattern=wall.top_right_pattern;break;
                case 4:
                    if(n%raw.architecture.width+1>=raw.architecture.width || !(level.walls[n+1].segments&1U))known=false;
                    else style=level.walls[n+1].top_left_style;
                    pattern=wall.bottom_right_pattern;break;
                case 8:
                    if(n+raw.architecture.width>=level.walls.size() || !(level.walls[n+raw.architecture.width].segments&2U))known=false;
                    else style=level.walls[n+raw.architecture.width].top_right_style;
                    pattern=wall.bottom_left_pattern;break;
                case 16:pattern=wall.bottom_right_pattern;break;
                case 32:pattern=wall.bottom_left_pattern;break;
                default:known=false;break;
                }
                if(known)used_styles.insert(style);
                if(known && prices.structural.count(style) && (style==1 || style==7 || style==8 || style==9))used_walls.insert(pattern);
                bit<<=1;while(bit<32 && !(mask&bit))bit<<=1;if(bit>=32)break;
            }
        }
    }
    std::uint64_t metadata_bytes=0;std::size_t loaded=0;
    const auto bind=[&](odh::ArchitectureResourceFamily family,const auto& used,const auto& mappings,const auto& unresolved,auto& output){
        std::map<std::uint16_t,std::int32_t> cache;
        for(const auto raw_id:used){
            // Advanced saved IDs256..511 tag the same byte-keyed FLRm/WALm
            // entries. Preserve the FULL raw ID as the price-map key.
            if(raw_id==0 || raw_id>=512)continue;
            const auto alias_id=static_cast<std::uint8_t>(raw_id);
            if(unresolved[alias_id]){output.erase(raw_id);continue;}
            if(!mappings[alias_id])continue;
            const auto runtime_id=*mappings[alias_id];auto known=cache.find(runtime_id);
            if(known==cache.end()){
                const auto& record=materials.record(family,runtime_id);auto asset=materials.open(record,4096);
                const auto& payload=asset.require("STR#",0);
                if(payload.size()>64*1024*1024-metadata_bytes)throw odh::FormatError("material price metadata budget exceeded");
                metadata_bytes+=payload.size();
                known=cache.emplace(runtime_id,odh::architecture_price_from_strings(odh::read_strings(payload),1)).first;++loaded;
            }
            output[raw_id]=known->second;
        }
    };
    bind(odh::ArchitectureResourceFamily::floor,used_floors,aliases.aliases.floors,aliases.diagnostics.declared_unresolved_floor_aliases,prices.floors);
    bind(odh::ArchitectureResourceFamily::wall,used_walls,aliases.aliases.walls,aliases.diagnostics.declared_unresolved_wall_aliases,prices.wallpaper);
    return {std::move(raw),std::move(prices),loaded,catalog.resources.size(),
        aliases.diagnostics.floor_map_resolved,aliases.diagnostics.wall_map_resolved,
        std::move(used_floors),std::move(used_walls),std::move(used_styles)};
}
} // namespace odh_tools
