#include "saved_object_renderer.hpp"
#include "odh/installed_lot_limits.hpp"
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <tuple>
#include <cmath>

namespace {
constexpr std::uint64_t pixel_budget=2*1048576, chunk_budget=65536;
struct Source {
    std::ifstream stream;
    std::vector<odh::FarEntry> entries;
    bool standalone;
    Source(const std::string& path,bool direct):stream(path,std::ios::binary),standalone(direct) {
        if(!stream)throw odh::FormatError("cannot open saved object resource source");
        if(!direct){odh::Limits limits;limits.far_entries=8192;limits.far_name_bytes=256*1024;
            entries=odh::far1_index(odh::View(stream,std::filesystem::file_size(path)),limits);}
    }
};
struct Asset {
    std::size_t cache_id=0;
    std::vector<odh::Chunk> chunks;
    const odh::Chunk* find(const std::string& type,std::uint16_t id) const {
        const odh::Chunk* found=nullptr;
        for(const auto& chunk:chunks)if(chunk.type==type&&chunk.id==id){
            if(found)throw odh::FormatError("ambiguous saved object "+type+" chunk");
            found=&chunk;
        }
        return found;
    }
    const odh::View& require(const std::string& type,std::uint16_t id) const {
        const auto* chunk=find(type,id);
        if(!chunk)throw odh::FormatError("missing saved object "+type+" chunk");
        return chunk->payload;
    }
};
using AssetKey=std::pair<std::string,std::string>;
using GroupKey=std::tuple<std::string,std::string,std::uint16_t>;
using FrameKey=std::tuple<std::size_t,std::uint16_t,std::uint32_t>;
}

struct SavedObjectRenderer::Impl {
    const odh::DefinitionIndex& index;
    struct CachedFrame { odh::Spr2Frame image; bool used=false; };
    std::map<std::string,std::unique_ptr<Source>> sources;
    std::map<AssetKey,Asset> assets;
    std::map<GroupKey,odh::Dgrp> groups;
    std::map<FrameKey,CachedFrame> frames;
    struct FrameInfo {const odh::View* sprite;const odh::View* palette;odh::Spr2Frame geometry;bool verified=false;};
    std::map<FrameKey,FrameInfo> frame_info;
    std::uint64_t chunks=0,images=0,group_sprites=0,pixels=0,frame_bytes=0,provenance_bytes=0;
    explicit Impl(const odh::DefinitionIndex& definitions):index(definitions){}
    bool evict_unused_frames(){
        bool evicted=false;
        for(auto it=frames.begin();it!=frames.end();){
            if(it->second.used){++it;continue;}
            const auto& image=it->second.image;
            pixels-=image.pixels.size();
            frame_bytes-=4*image.pixels.size()+image.indices.size()+image.z.size();
            it=frames.erase(it);evicted=true;
        }
        return evicted;
    }
    Asset& asset(const odh::DefinitionCandidate& candidate) {
        const AssetKey key{candidate.source,candidate.asset};
        auto existing=assets.find(key);if(existing!=assets.end())return existing->second;
        const auto name_bytes=candidate.source.size()+candidate.asset.size();
        if(assets.size()>=odh::installed_lot_limits().definition_catalogs||name_bytes>256*1024-provenance_bytes)
            throw odh::FormatError("saved object asset/provenance budget exceeded");
        auto source_it=sources.find(candidate.source);
        const bool direct=candidate.asset.empty();
        if(source_it==sources.end()){
            if(sources.size()>=128||candidate.source.size()>4096)
                throw odh::FormatError("saved object source budget exceeded");
            auto source=std::make_unique<Source>(candidate.source,direct);
            source_it=sources.emplace(candidate.source,std::move(source)).first;
        }
        auto& source=*source_it->second;
        if(source.standalone!=direct)throw odh::FormatError("saved object source type conflict");
        const odh::View file(source.stream,std::filesystem::file_size(candidate.source));
        auto view=file;
        if(!direct){
            const odh::FarEntry* selected=nullptr;
            for(const auto& entry:source.entries)if(entry.name==candidate.asset){
                if(selected)throw odh::FormatError("ambiguous saved object archive asset");
                selected=&entry;
            }
            if(!selected)throw odh::FormatError("saved object archive asset absent");
            view=file.subview(selected->offset,selected->length);
        }
        Asset result;result.cache_id=assets.size();odh::Limits limits;
        limits.iff_chunks=static_cast<std::uint32_t>(std::min<std::uint64_t>(4096,chunk_budget-chunks));
        odh::iff_chunks(view,[&](const auto& chunk){result.chunks.push_back(chunk);},limits);
        const auto count=result.chunks.size();
        auto inserted=assets.emplace(key,std::move(result));
        chunks+=count;provenance_bytes+=name_bytes;
        return inserted.first->second;
    }
    const odh::Dgrp& group(const odh::DefinitionCandidate& candidate,Asset& resource,std::uint16_t id) {
        const GroupKey key{candidate.source,candidate.asset,id};
        auto existing=groups.find(key);if(existing!=groups.end())return existing->second;
        if(groups.size()>=4096)throw odh::FormatError("saved object DGRP cache budget exceeded");
        odh::DgrpLimits limits;
        limits.images=static_cast<std::uint32_t>(4096-images);
        limits.sprites=static_cast<std::uint32_t>(65536-group_sprites);
        auto result=odh::read_dgrp(resource.require("DGRP",id),limits);
        if(result.remaining_bytes)throw odh::FormatError("saved object DGRP trailing bytes");
        std::size_t sprites=0;for(const auto& image:result.images)sprites+=image.sprites.size();
        const auto count=result.images.size();
        auto inserted=groups.emplace(key,std::move(result));images+=count;group_sprites+=sprites;
        return inserted.first->second;
    }
    FrameInfo& inspect(Asset& resource,std::uint16_t id,std::uint32_t number) {
        const FrameKey key{resource.cache_id,id,number};
        const auto cached=frame_info.find(key);if(cached!=frame_info.end())return cached->second;
        const auto* chunk=resource.find("SPR2",id);
        if(!chunk){
            if(resource.find("SPR#",id))throw odh::FormatError("saved object SPR# rendering unsupported");
            throw odh::FormatError("saved object sprite absent");
        }
        if(chunk->payload.le32(0)!=1000)throw odh::FormatError("saved object SPR2 version unsupported");
        const auto& palette=resource.require("PALT",static_cast<std::uint16_t>(chunk->payload.le32(8)));
        odh::SpriteLimits limits;limits.pixels=static_cast<std::uint32_t>(pixel_budget);
        auto geometry=odh::inspect_spr2_frame(chunk->payload,number,odh::read_palt(palette),limits,odh::Spr2Inspection::geometry);
        if((geometry.flags&3U)!=3U)throw odh::FormatError("saved object sprite requires pixel and Z flags");
        // Layer pointers refer only to decoded frame entries. Header metadata
        // has independent lifetime and can be rebuilt from immutable assets.
        if(frame_info.size()>=4096)frame_info.clear();
        return frame_info.emplace(key,FrameInfo{&chunk->payload,&palette,std::move(geometry)}).first->second;
    }
    void validate_culled(FrameInfo& info,std::uint32_t number){
        if(info.verified)return;
        odh::SpriteLimits limits;limits.pixels=static_cast<std::uint32_t>(pixel_budget);
        (void)odh::inspect_spr2_frame(*info.sprite,number,odh::read_palt(*info.palette),limits,odh::Spr2Inspection::full);
        info.verified=true;
    }
    const odh::Spr2Frame& visible_frame(Asset& resource,std::uint16_t id,std::uint32_t number,FrameInfo& info) {
        const FrameKey key{resource.cache_id,id,number};
        const auto cached=frames.find(key);
        if(cached!=frames.end()){cached->second.used=true;info.verified=true;return cached->second.image;}
        const auto extent=std::uint64_t{info.geometry.width}*info.geometry.height;
        if(frames.size()>=4096||extent>pixel_budget-pixels)evict_unused_frames();
        if(frames.size()>=4096)throw odh::FormatError("saved object visible frame count exceeds cache budget");
        if(extent>pixel_budget-pixels)throw odh::FormatError("saved object visible frame pixel budget exceeded; requested="+
            std::to_string(extent)+" remaining="+std::to_string(pixel_budget-pixels));
        odh::SpriteLimits limits;limits.pixels=static_cast<std::uint32_t>(pixel_budget-pixels);
        auto result=odh::read_spr2_frame(*info.sprite,number,odh::read_palt(*info.palette),limits);
        info.verified=true;
        const auto count=result.pixels.size();
        const auto bytes=4*count+result.indices.size()+result.z.size();
        auto inserted=frames.emplace(key,CachedFrame{std::move(result),true});pixels+=count;frame_bytes+=bytes;
        return inserted.first->second.image;
    }
};

SavedObjectRenderer::SavedObjectRenderer(const odh::DefinitionIndex& index):impl_(std::make_unique<Impl>(index)){}
SavedObjectRenderer::~SavedObjectRenderer()=default;
SavedObjectRenderer::SavedObjectRenderer(SavedObjectRenderer&&) noexcept=default;
SavedObjectRenderer& SavedObjectRenderer::operator=(SavedObjectRenderer&&) noexcept=default;

SavedObjectRenderResult SavedObjectRenderer::render(const std::vector<SavedObjectVisual>& objects,
    const odh::WorldProjection& projection,const odh::RasterFrame& base,bool record_picks,bool record_sprite_rgba) {
    if(!impl_)throw odh::FormatError("moved saved object renderer");
    if(objects.size()>4096)throw odh::FormatError("saved object visual count exceeds budget");
    if(base.width!=projection.width||base.height!=projection.height)
        throw odh::FormatError("saved object camera and base dimensions disagree");
    odh::project_tile(projection,projection.center_tile);
    for(auto& entry:impl_->frames)entry.second.used=false;
    SavedObjectRenderResult result;
    std::vector<odh::SpriteLayer> layers;std::set<std::int16_t> ids;
    std::uint64_t flags=0,record_provenance=0;
    for(const auto& object:objects){
        if(object.id<=0||!ids.insert(object.id).second||object.direction>7)
            throw odh::FormatError("invalid or duplicate saved object visual identity/direction");
        odh::project_tile(projection,object.tile_position);
        if(object.dynamic_sprite_flags.size()>65536-flags)
            throw odh::FormatError("saved object dynamic flag budget exceeded");
        flags+=object.dynamic_sprite_flags.size();
        odh::project_tile(projection,object.tile_position);
        const auto* candidate=impl_->index.unique(object.guid);
        if(!candidate)throw odh::FormatError("saved object definition missing or ambiguous");
        const auto name_bytes=candidate->source.size()+candidate->asset.size();
        if(name_bytes>256*1024-record_provenance)
            throw odh::FormatError("saved object draw record provenance budget exceeded");
        record_provenance+=name_bytes;
        auto& resource=impl_->asset(*candidate);
        const auto& objd=resource.require("OBJD",candidate->chunk_id);
        const auto identity=odh::read_object_definition(objd);
        if(identity.guid!=object.guid||identity.version!=candidate->definition.version||
           identity.master_id!=candidate->definition.master_id||identity.sub_index!=candidate->definition.sub_index||
           identity.object_type!=candidate->definition.object_type)
            throw odh::FormatError("saved object resource identity changed");
        const auto graphics=odh::read_objd_graphics(objd);
        // The reference selects the low16 sum; NumGraphics is metadata rather
        // than a gate. Some saved stair footprint parts declare no graphics.
        const auto dgrp_id=static_cast<std::uint16_t>(std::int32_t{graphics.base_graphic}+object.graphic);
        // ObjectComponent's pinned constructor leaves DrawGroup null when
        // BaseGraphicID is zero, independently of NumGraphics. Multi-tile
        // footprint-only parts can declare a nonzero count without artwork.
        // Preserve an actual selected DGRP0 (including wrapping selection),
        // and keep missing nonzero/positive-base artwork a strict error.
        const bool initial_no_graphic=graphics.base_graphic==0&&object.graphic==0;
        if((graphics.num_graphics==0||initial_no_graphic) && !resource.find("DGRP",dgrp_id)) {
            result.records.push_back({object.id,object.guid,candidate->source,candidate->asset,candidate->chunk_id,
                dgrp_id,0,projection.zoom,0,0,0,true});
            continue;
        }
        const auto dynamic_base=objd.le16(128),dynamic_count=objd.le16(130);
        const auto draw_context=" id="+std::to_string(object.id)+" guid="+std::to_string(object.guid)+
            " source="+candidate->source+" asset="+candidate->asset+
            " objd="+std::to_string(candidate->chunk_id)+" graphic="+std::to_string(object.graphic)+
            " base_graphic="+std::to_string(graphics.base_graphic)+
            " num_graphics="+std::to_string(graphics.num_graphics)+" dgrp="+std::to_string(dgrp_id)+
            " direction="+std::to_string(object.direction)+
            " tile="+std::to_string(object.tile_position[0])+","+std::to_string(object.tile_position[1])+","+
            std::to_string(object.tile_position[2]);
        const odh::Dgrp* group=nullptr;
        try {group=&impl_->group(*candidate,resource,dgrp_id);}
        catch(const odh::FormatError& error) {
            throw odh::FormatError(std::string(error.what())+draw_context);
        }
        const auto* image=odh::dgrp_image(*group,static_cast<std::uint8_t>(1U<<object.direction),projection.zoom,projection.rotation);
        if(!image)throw odh::FormatError("saved object DGRP view missing"+draw_context);
        SavedObjectDrawRecord record{object.id,object.guid,candidate->source,candidate->asset,candidate->chunk_id,
            static_cast<std::uint16_t>(dgrp_id),image->direction,image->zoom,0,0,0};
        for(const auto& sprite:image->sprites){
            if(sprite.sprite_id>=dynamic_base&&sprite.sprite_id<std::uint32_t{dynamic_base}+dynamic_count){
                const auto flag=sprite.sprite_id-dynamic_base;
                if(flag>=object.dynamic_sprite_flags.size()||object.dynamic_sprite_flags[flag]==0){++record.omitted_dynamic_layers;continue;}
            }
            if(layers.size()>=4096||sprite.sprite_id>65535)
                throw odh::FormatError("saved object sprite layer/reference budget exceeded");
            const auto sprite_id=static_cast<std::uint16_t>(sprite.sprite_id);
            auto& info=impl_->inspect(resource,sprite_id,sprite.frame);
            const auto& geometry=info.geometry;
            if(!geometry.width||!geometry.height){
                impl_->validate_culled(info,sprite.frame);
                ++record.omitted_empty_layers;continue;
            }
            // A DGRP can put a sprite far from its object's logical tile.
            // Use the actual frame extent and the same placement as drawing.
            constexpr std::array<float,8> radians{0,0,1.5707963267948966F,0,3.1415926535897932F,0,4.71238898038469F,0};
            const auto placement=odh::place_sprite(projection,sprite,geometry,object.tile_position,radians[object.direction]);
            ++record.layers;
            const auto left=std::int64_t{placement.x},top=std::int64_t{placement.y};
            const auto right=left+geometry.width,bottom=top+geometry.height;
            if(left>=projection.width||top>=projection.height||right<=0||bottom<=0){
                impl_->validate_culled(info,sprite.frame);
                ++record.offscreen_layers;++result.offscreen_layers;continue;
            }
            if(left<0||top<0||right>projection.width||bottom>projection.height)++result.clipped_layers;
            const auto& frame=impl_->visible_frame(resource,sprite_id,sprite.frame,info);
            layers.push_back({&frame,placement.x,placement.y,placement.back_depth,placement.front_depth_delta,placement.flip,static_cast<std::uint16_t>(object.id),object.linear_light,object.grayscale,object.invert});
            // DGRPRenderer: luminous sprite layers use special white room65535,
            // except the disabled grayscale path which has precedence.
            if((sprite.flags&4U)&&!object.grayscale&&!object.invert)layers.back().linear_light={1,1,1};
        }
        result.records.push_back(std::move(record));
    }
    std::uint64_t layer_pixels=0;for(const auto& layer:layers)layer_pixels+=layer.frame->pixels.size();
    if(layer_pixels>4*1048576)throw odh::FormatError("saved object repeated layer extent exceeds compositor profile: "+std::to_string(layer_pixels)+" pixels");
    odh::CompositorLimits compositor_limits;compositor_limits.pixels=4*1048576;
    result.composite=odh::composite_sprites(base,layers,compositor_limits,record_picks,record_sprite_rgba);
    impl_->evict_unused_frames();
    result.layers=layers.size();result.cached_assets=impl_->assets.size();result.cached_groups=impl_->groups.size();
    result.cached_frames=impl_->frames.size();result.cached_pixels=impl_->pixels;result.cached_frame_bytes=impl_->frame_bytes;
    return result;
}
