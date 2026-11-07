// SPDX-License-Identifier: MPL-2.0
#include "odh/architecture_snapshot.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace odh { namespace {
using Key=std::pair<ArchitectureResourceFamily,std::uint16_t>;
constexpr auto floor_kind=ArchitectureResourceFamily::floor,wall_kind=ArchitectureResourceFamily::wall;
bool dynamic(std::uint16_t id){return id>=256&&id<=65533;}
std::string key_name(std::string name){
    if(name.empty()||name.size()>4096)throw FormatError("architecture material name outside bounds");
    for(auto& c:name){const auto v=static_cast<unsigned char>(c);
        if(v<32||v>=127)throw FormatError("architecture material name is not a supported provider key");
        if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');}
    return name;
}
template<class F> void material_fields(PreparedArchitecture& p,F callback){
    for(auto& level:p.levels){
        for(auto& id:level.floors)callback(floor_kind,id);
        for(auto& wall:level.walls){
            callback(wall_kind,wall.bottom_left_pattern);callback(wall_kind,wall.bottom_right_pattern);
            if(wall.segments&(wall_horizontal_diagonal|wall_vertical_diagonal)){
                callback(floor_kind,wall.top_left_pattern);callback(floor_kind,wall.top_left_style);
            }else{callback(wall_kind,wall.top_left_pattern);callback(wall_kind,wall.top_right_pattern);}
        }
    }
}
std::set<Key> used_materials(const PreparedArchitecture& p){
    // Fixed maximum two stories of 64x64 cells. The temporary copy makes the
    // same field visitor usable for rebinding without aliasing const storage.
    auto copy=p;std::set<Key> result;
    material_fields(copy,[&](auto family,auto& id){if(dynamic(id))result.emplace(family,id);});return result;
}
void grids(const PreparedArchitecture& p,const ArchitectureSnapshotLimits& limits){
    if(!p.width||p.width!=p.height||p.width>64||p.width>limits.dimension)
        throw FormatError("architecture snapshot dimensions outside profile");
    const auto cells=static_cast<std::size_t>(p.width)*p.height;
    if(p.buildable.size()!=cells)throw FormatError("architecture snapshot buildable extent mismatch");
    for(const auto v:p.buildable)if(v>1)throw FormatError("architecture snapshot buildable marker invalid");
    for(const auto& level:p.levels){
        if(level.floors.size()!=cells||level.walls.size()!=cells)throw FormatError("architecture snapshot grid extent mismatch");
        for(const auto& wall:level.walls)if((wall.segments&0xc0)||((wall.segments&0x30)==0x30))
            throw FormatError("architecture snapshot wall segments invalid");
    }
    const auto& d=p.diagnostics;
    for(const auto count:{d.floor_lookups,d.floor_alias_misses,d.unresolved_floor_alias_references,d.wall_lookups,d.wall_alias_misses})
        if(count>65536)throw FormatError("architecture snapshot diagnostics exceed source preparation bounds");
    if(d.floor_alias_misses>d.floor_lookups||d.wall_alias_misses>d.wall_lookups||
       d.unresolved_floor_alias_references>d.floor_alias_misses)
        throw FormatError("architecture snapshot diagnostics inconsistent");
}
struct Writer{
    std::vector<std::uint8_t> data;std::size_t limit;
    void u8(std::uint8_t v){if(data.size()>=limit)throw FormatError("architecture snapshot byte budget exceeded");data.push_back(v);}
    void u16(std::uint16_t v){u8(static_cast<std::uint8_t>(v));u8(static_cast<std::uint8_t>(v>>8));}
    void u32(std::uint32_t v){u16(static_cast<std::uint16_t>(v));u16(static_cast<std::uint16_t>(v>>16));}
};
struct Reader{
    const std::vector<std::uint8_t>& data;std::size_t at=0;
    std::uint8_t u8(){if(at==data.size())throw FormatError("truncated architecture snapshot");return data[at++];}
    std::uint16_t u16(){const auto lo=u8();return static_cast<std::uint16_t>(lo|(std::uint16_t{u8()}<<8));}
    std::uint32_t u32(){const auto lo=u16();return lo|(std::uint32_t{u16()}<<16);}
    bool flag(){const auto v=u8();if(v>1)throw FormatError("invalid architecture snapshot flag");return v!=0;}
};
} // namespace
void validate_architecture_snapshot(const ArchitectureSnapshot& state,const ArchitectureSnapshotLimits& limits){
    grids(state.prepared,limits);
    if(state.materials.size()>limits.materials||state.materials.size()>16384)
        throw FormatError("architecture snapshot material budget exceeded");
    std::set<Key> keys;std::set<std::pair<ArchitectureResourceFamily,std::string>> names;std::size_t bytes=0;
    std::optional<Key> previous;
    for(const auto& b:state.materials){
        const Key key{b.family,b.id};
        if((b.family!=floor_kind&&b.family!=wall_kind)||!dynamic(b.id)||(previous&&key<=*previous))
            throw FormatError("architecture snapshot material identities invalid or unordered");
        const auto name=key_name(b.name);
        if(name!=b.name||!names.emplace(b.family,name).second)throw FormatError("architecture snapshot material names ambiguous");
        if(bytes>limits.name_bytes||name.size()>limits.name_bytes-bytes)throw FormatError("architecture snapshot name budget exceeded");
        bytes+=name.size();previous=key;keys.insert(key);
    }
    if(keys!=used_materials(state.prepared))throw FormatError("architecture snapshot material bindings do not exactly cover the grids");
}
std::vector<std::uint8_t> encode_architecture_snapshot(const ArchitectureSnapshot& state,const ArchitectureSnapshotLimits& limits){
    validate_architecture_snapshot(state,limits);Writer w{{},limits.bytes};w.u16(1);
    const auto& p=state.prepared;w.u16(static_cast<std::uint16_t>(p.width));w.u16(static_cast<std::uint16_t>(p.height));
    for(const auto& level:p.levels){for(const auto floor:level.floors)w.u16(floor);
        for(const auto& tile:level.walls){w.u8(tile.segments);w.u8(tile.unknown);
            for(const auto v:{tile.top_left_style,tile.top_right_style,tile.top_left_pattern,tile.top_right_pattern,tile.bottom_left_pattern,tile.bottom_right_pattern})w.u16(v);}}
    for(const auto value:p.buildable)w.u8(value);
    const auto& d=p.diagnostics;
    for(const auto value:{d.floor_lookups,d.floor_alias_misses,d.unresolved_floor_alias_references,d.wall_lookups,d.wall_alias_misses})w.u32(static_cast<std::uint32_t>(value));
    for(const auto* flags:{&d.missing_floor_aliases,&d.missing_wall_aliases})for(const bool flag:*flags)w.u8(flag?1:0);
    w.u16(static_cast<std::uint16_t>(state.materials.size()));
    for(const auto& b:state.materials){w.u8(b.family==floor_kind?0:1);w.u16(b.id);w.u16(static_cast<std::uint16_t>(b.name.size()));for(const auto c:b.name)w.u8(static_cast<std::uint8_t>(c));}
    return std::move(w.data);
}
ArchitectureSnapshot decode_architecture_snapshot(const std::vector<std::uint8_t>& bytes,const ArchitectureSnapshotLimits& limits){
    if(bytes.size()>limits.bytes)throw FormatError("architecture snapshot byte budget exceeded");Reader r{bytes};
    if(r.u16()!=1)throw FormatError("unsupported architecture snapshot version");ArchitectureSnapshot state;auto& p=state.prepared;
    p.width=r.u16();p.height=r.u16();
    if(!p.width||p.width!=p.height||p.width>64||p.width>limits.dimension)throw FormatError("architecture snapshot dimensions outside profile");
    const auto cells=static_cast<std::size_t>(p.width)*p.height;
    // Check full fixed-size payload before allocating grids.
    if(bytes.size()-r.at<cells*33+534)throw FormatError("truncated architecture snapshot grids");
    for(auto& level:p.levels){level.floors.resize(cells);level.walls.resize(cells);
        for(auto& floor:level.floors)floor=r.u16();
        for(auto& t:level.walls){t.segments=r.u8();t.unknown=r.u8();t.top_left_style=r.u16();t.top_right_style=r.u16();
            t.top_left_pattern=r.u16();t.top_right_pattern=r.u16();t.bottom_left_pattern=r.u16();t.bottom_right_pattern=r.u16();}}
    p.buildable.resize(cells);for(auto& b:p.buildable)b=r.flag()?1:0;
    auto& d=p.diagnostics;d.floor_lookups=r.u32();d.floor_alias_misses=r.u32();d.unresolved_floor_alias_references=r.u32();d.wall_lookups=r.u32();d.wall_alias_misses=r.u32();
    for(auto* flags:{&d.missing_floor_aliases,&d.missing_wall_aliases})for(auto&& flag:*flags)flag=r.flag();
    const auto count=r.u16();if(count>limits.materials||count>16384||count>(bytes.size()-r.at)/5)
        throw FormatError("architecture snapshot material count exceeds bounds");
    state.materials.reserve(count);std::size_t names=0;
    for(unsigned n=0;n<count;++n){ArchitectureMaterialBinding b;b.family=static_cast<ArchitectureResourceFamily>(r.u8());b.id=r.u16();
        const auto size=r.u16();if(size>4096||names>limits.name_bytes||size>limits.name_bytes-names||size>bytes.size()-r.at)
            throw FormatError("architecture snapshot material name exceeds bounds");
        names+=size;b.name.reserve(size);for(unsigned j=0;j<size;++j)b.name.push_back(static_cast<char>(r.u8()));state.materials.push_back(std::move(b));}
    if(r.at!=bytes.size())throw FormatError("architecture snapshot trailing bytes");validate_architecture_snapshot(state,limits);return state;
}
ArchitectureSnapshot bind_architecture_snapshot(const PreparedArchitecture& p,const ArchitectureResourceCatalog& catalog,const ArchitectureSnapshotLimits& limits){
    grids(p,limits);ArchitectureSnapshot state;state.prepared=p;
    std::map<Key,std::string> names;
    for(const auto& record:catalog.resources)if(!names.emplace(Key{record.family,record.catalog.id},key_name(record.catalog.name)).second)
        throw FormatError("architecture catalog material ID is ambiguous");
    for(const auto& key:used_materials(p)){const auto it=names.find(key);if(it==names.end())throw FormatError("architecture snapshot references an absent material");
        state.materials.push_back({key.first,key.second,it->second});}
    validate_architecture_snapshot(state,limits);return state;
}
PreparedArchitecture resolve_architecture_snapshot(const ArchitectureSnapshot& state,const ArchitectureResourceCatalog& catalog,const ArchitectureSnapshotLimits& limits){
    validate_architecture_snapshot(state,limits);
    std::map<std::pair<ArchitectureResourceFamily,std::string>,std::uint16_t> ids;std::set<Key> unique;
    for(const auto& record:catalog.resources)if(!dynamic(record.catalog.id)||!unique.emplace(record.family,record.catalog.id).second||
        !ids.emplace(std::make_pair(record.family,key_name(record.catalog.name)),record.catalog.id).second)
        throw FormatError("architecture catalog material identity is ambiguous");
    std::map<Key,std::uint16_t> remap;
    for(const auto& binding:state.materials){const auto it=ids.find({binding.family,binding.name});
        if(it==ids.end())throw FormatError("saved architecture material is unavailable: "+binding.name);
        remap.emplace(Key{binding.family,binding.id},it->second);}
    auto result=state.prepared;material_fields(result,[&](auto family,auto& id){if(dynamic(id))id=remap.at({family,id});});return result;
}
} // namespace odh
