// SPDX-License-Identifier: MPL-2.0
// Pinned FreeSO WorldFloorProvider.InitGlobals/InitTS1 and
// WorldWallProvider.InitGlobals/InitTS1, revision7e76c1960252ddd033d8937d5edb47d326f5b69c.
#include "architecture_edit_resources.hpp"
#include "architecture_material_resources.hpp"
#include "saved_architecture_resources.hpp"
#include "odh/architecture_price_metadata.hpp"
#include "odh/sprite.hpp"
#include "odh/spr.hpp"
#include <set>
#include <algorithm>

namespace odh_host {
namespace {
using namespace odh;
struct GlobalData {std::map<std::uint16_t,StringTable>strings;std::set<std::uint16_t>sprites;};
GlobalData read_global(const std::filesystem::path&path,const char*sprite,
    const ArchitectureEditResourceLimits&limits,std::size_t&bytes){
    GlobalData result;
    with_iff_file(path.string(),[&](const View&v){
        Limits l;l.iff_chunks=limits.chunks_per_file;
        iff_chunks(v,[&](const Chunk&c){
            if(c.type=="STR#"&&(c.id==0x81||c.id==0x82||c.id==0x83||c.id==0x85)){
                if(c.payload.size()>limits.metadata_bytes-bytes)throw FormatError("architecture edit metadata budget exceeded");
                bytes+=c.payload.size();if(!result.strings.emplace(c.id,read_strings(c.payload)).second)throw FormatError("ambiguous architecture edit STR#");
            }
            if(sprite&&c.type==sprite&&!result.sprites.insert(c.id).second)throw FormatError("ambiguous global architecture sprite");
        },l);
    });return result;
}
const StringTable&table(const GlobalData&d,std::uint16_t id){auto it=d.strings.find(id);if(it==d.strings.end())throw FormatError("architecture edit global STR# missing");return it->second;}
std::string text(const StringTable&t,std::size_t index,std::uint8_t language){auto*e=string_in_ts1_language_set(t,language,index);return e?e->value:std::string{};}
std::size_t english_length(const StringTable&t){return std::count_if(t.entries.begin(),t.entries.end(),[](const auto&e){return e.language==0||e.language==1;});}
}
ArchitectureEditResources::ArchitectureEditResources(const std::filesystem::path&build,
    const std::filesystem::path&floors,const std::filesystem::path&walls,
    const ArchitectureResourceCatalog&catalog,std::uint8_t language,
    const ArchitectureEditResourceLimits&limits):floors_(floors),walls_(walls),resource_files_(catalog){
    using Kind=ArchitectureEditMaterialKind;using Lookup=ArchitectureEditLookup;
    std::size_t bytes=0;
    const auto b=read_global(build,nullptr,limits,bytes),f=read_global(floors,"SPR2",limits,bytes),w=read_global(walls,"SPR#",limits,bytes);
    const auto insert=[&](Kind kind,std::uint16_t id,std::int32_t price,bool command){
        if(materials_.size()>=limits.materials)throw FormatError("architecture edit material count budget exceeded");
        if(!materials_.emplace(std::make_pair(kind,id),ArchitectureEditMaterial{Lookup::available,price,command}).second)throw FormatError("duplicate architecture edit material identity");
    };
    const auto descriptor=[&](Kind kind,std::uint16_t id,const StringTable&t,std::size_t price_index,std::size_t name_index,std::size_t description_index,std::uint16_t pattern){
        descriptors_.push_back({kind,id,text(t,name_index,language),text(t,description_index,language),architecture_price_from_strings(t,price_index,language),pattern});
    };
    const auto&styles=table(b,0x81);constexpr std::uint16_t ids[5]={1,2,13,12,14},patterns[5]={0,248,250,249,251};
    for(std::uint16_t id=1;id<256;++id){
        std::size_t index=0;for(std::size_t i=0;i<5;++i)if(ids[i]==id)index=i;
        // Source Dictionary.TryGetValue(out int) assigns0 on a miss. Its
        // initial -1 is overwritten: all loaded styles inherit triple0 unless
        // explicitly listed. RunCommands has the SAME out-parameter quirk.
        const auto price=architecture_price_from_strings(styles,index*3,language);
        const auto up=std::uint16_t(id*2),down=std::uint16_t(up+1);
        if(w.sprites.count(up)&&(!w.sprites.count(up+512)||!w.sprites.count(up+1024)))throw FormatError("source wall style has incomplete up sprites");
        if(w.sprites.count(down)&&(!w.sprites.count(down+512)||!w.sprites.count(down+1024)))throw FormatError("source wall style has incomplete down sprites");
        insert(Kind::wall_style,id,price,true);
    }
    for(std::size_t i=0;i<5;++i)descriptor(Kind::wall_style,ids[i],styles,i*3,i*3+1,i*3+2,patterns[i]);
    const auto&fs=table(b,0x82);const auto floor_count=english_length(fs)/3;
    for(std::size_t id=1;id<floor_count;++id){
        if(id>=256)throw FormatError("global floor extent overlaps dynamic IDs");
        if(!f.sprites.count(std::uint16_t(id))||!f.sprites.count(std::uint16_t(id+256))||!f.sprites.count(std::uint16_t(id+512)))throw FormatError("source global floor has incomplete sprites");
        insert(Kind::floor,std::uint16_t(id),architecture_price_from_strings(fs,(id-1)*3,language),true);
        descriptor(Kind::floor,std::uint16_t(id),fs,(id-1)*3,(id-1)*3+1,(id-1)*3+2,0);
    }
    const auto&ws=table(b,0x83);const auto wall_count=english_length(ws)/3;
    for(std::size_t id=1;id<256&&id<wall_count+1;++id){
        // The source adds wallpaper Entries even when its sprite is absent;
        // unlike floor initialization it never dereferences that missing SPR.
        insert(Kind::wallpaper,std::uint16_t(id),architecture_price_from_strings(ws,(id-1)*3,language),true);
        descriptor(Kind::wallpaper,std::uint16_t(id),ws,(id-1)*3,(id-1)*3+1,(id-1)*3+2,0);
    }
    const auto&water=table(b,0x85);
    for(std::size_t i=0;i<2;++i){const auto id=std::uint16_t(65535-i);insert(Kind::floor,id,architecture_price_from_strings(water,i*3,language),true);descriptor(Kind::floor,id,water,i*3,i*3+1,i*3+2,0);}
    for(const auto&r:catalog.resources){
        auto asset=resource_files_.open(r,limits.chunks_per_file);const auto&payload=asset.require("STR#",0);
        if(payload.size()>limits.metadata_bytes-bytes)throw FormatError("dynamic architecture metadata budget exceeded");bytes+=payload.size();
        const auto strings=read_strings(payload);const auto kind=r.family==ArchitectureResourceFamily::floor?Kind::floor:Kind::wallpaper;
        insert(kind,r.catalog.id,architecture_price_from_strings(strings,1,language),true);
        descriptor(kind,r.catalog.id,strings,1,0,2,0);
    }
    std::stable_sort(descriptors_.begin(),descriptors_.end(),[](const auto&a,const auto&b){return std::make_pair(a.kind,a.id)<std::make_pair(b.kind,b.id);});
}
std::optional<odh::BitmapImage> ArchitectureEditResources::thumbnail(odh::ArchitectureEditMaterialKind kind,std::uint16_t id)const{
    using Kind=odh::ArchitectureEditMaterialKind;
    if(!materials_.count({kind,id}))return {};
    ArchitectureMaterialAsset asset;
    if(id>=256&&id<=65533&&kind!=Kind::wall_style){
        asset=resource_files_.open(resource_files_.record(kind==Kind::floor?odh::ArchitectureResourceFamily::floor:odh::ArchitectureResourceFamily::wall,id),4096);
    }else{
        const auto& path=kind==Kind::floor?floors_:walls_;
        asset.stream=std::make_unique<std::ifstream>(path,std::ios::binary);
        if(!*asset.stream)throw odh::FormatError("build thumbnail resource unavailable");
        odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(odh::View(*asset.stream,std::filesystem::file_size(path)),[&](const auto& chunk){asset.chunks.push_back(chunk);},limits);
    }
    odh::BitmapLimits bitmap_limits;bitmap_limits.width=256;bitmap_limits.height=256;bitmap_limits.pixels=65536;
    if(kind==Kind::wall_style){
        const auto* icon=asset.find("BMP_",id);
        return icon?std::optional<odh::BitmapImage>{odh::read_bmp(icon->payload,bitmap_limits,odh::BmpExtentPolicy::iff_container)}:std::nullopt;
    }
    odh::SpriteLimits limits;limits.width=256;limits.height=256;limits.pixels=65536;
    odh::BitmapImage result{};result.bit_count=32;result.top_down=true;
    if(kind==Kind::floor){
        const auto sprite=id==65535?0x420:id==65534?0x800:id>=256?513:id+512;
        const auto* source=asset.find("SPR2",static_cast<std::uint16_t>(sprite));
        if(!source)return {};
        const auto palette=odh::read_palt(asset.require("PALT",static_cast<std::uint16_t>(source->payload.le32(8))),limits);
        auto frame=odh::read_spr2_frame(source->payload,0,palette,limits);
        result.width=frame.width;result.height=frame.height;result.pixels=std::move(frame.pixels);
    }else{
        const auto* source=asset.find("SPR#",id>=256?1793:static_cast<std::uint16_t>(id+1792));
        if(!source)return {};
        const auto header=odh::read_spr_header(source->payload,limits);
        const auto* raw_palette=asset.find("PALT",header.palette_id);
        if(!raw_palette)return {};
        auto frame=odh::read_spr_frame(source->payload,2,odh::read_palt(raw_palette->payload,limits),limits);
        result.width=frame.width;result.height=frame.height;result.pixels=std::move(frame.pixels);
    }
    return result;
}
odh::ArchitectureEditMaterial ArchitectureEditResources::material(odh::ArchitectureEditMaterialKind kind,std::uint16_t id)const{
    const auto found=materials_.find({kind,id});if(found!=materials_.end())return found->second;
    // Dynamic object-authored structural styles are a separate live provider,
    // not catalog floor/wallpaper IDs. A missing source style must not be
    // reported as a successful free no-op (GetWallStyle would be null).
    if(kind==odh::ArchitectureEditMaterialKind::wall_style)return {};
    return {odh::ArchitectureEditLookup::absent,0,false};
}
}
