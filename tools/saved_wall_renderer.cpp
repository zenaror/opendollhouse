#include "saved_wall_renderer.hpp"
#include "architecture_material_resources.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <tuple>
#include <set>

namespace {
using FaceKey=std::tuple<std::uint8_t,std::uint32_t,std::uint32_t,std::uint8_t>;
using AssetKey=std::pair<std::string,std::string>;
using FrameKey=std::tuple<bool,std::string,std::string,std::uint16_t,std::uint32_t>;
constexpr std::uint64_t pixel_budget=1048576;
struct Source {
    std::ifstream stream;
    std::vector<odh::FarEntry> entries;
    bool direct;
    Source(const std::string& path,bool standalone):stream(path,std::ios::binary),direct(standalone) {
        if(!stream)throw odh::FormatError("cannot open saved wall resource");
        if(!direct){odh::Limits limits;limits.far_entries=8192;limits.far_name_bytes=256*1024;
            entries=odh::far1_index(odh::View(stream,std::filesystem::file_size(path)),limits);}
    }
};
using Asset=ArchitectureMaterialAsset;
std::int32_t integer(double value) {
    const auto v=std::trunc(value);
    if(!std::isfinite(v)||v<INT32_MIN||v>INT32_MAX)throw odh::FormatError("saved wall placement overflow");
    return static_cast<std::int32_t>(v);
}
}

std::vector<SavedWallFace> saved_wall_faces(const odh::SavedArchitecture& a,
    const odh::WallAppearance& appearance,std::uint8_t rotation,odh::WallView view) {
    if(rotation>3||!a.width||!a.height||a.width>64||a.height>64)
        throw odh::FormatError("saved wall architecture/rotation invalid");
    const auto cells=std::size_t(a.width)*a.height;
    for(const auto& level:a.levels){
        if(level.walls.size()!=cells)throw odh::FormatError("saved wall grid extent invalid");
        for(const auto& tile:level.walls){
            if(tile.segments>63 || ((tile.segments&48) && tile.segments!=16 && tile.segments!=32))
                throw odh::FormatError("saved wall mixed/unknown segments unsupported");
        }
    }
    const auto cuts=odh::wall_view_cuts(a,view);
    if(appearance.faces.size()>16384)throw odh::FormatError("saved wall appearance budget exceeded");
    std::map<FaceKey,const odh::WallAppearanceFace*> overrides;
    for(const auto& f:appearance.faces){
        if(f.x>=a.width||f.y>=a.height||f.level<1||f.level>2||(f.segment!=1&&f.segment!=2)||
           !f.generation||f.owner<=0||f.door_owner<0||
           ((f.door_owner==0)!=(f.door_generation==0))||(f.door&&f.door_owner==0)||
           !overrides.emplace(FaceKey{f.level,f.x,f.y,f.segment},&f).second)
            throw odh::FormatError("saved wall appearance identity/grid invalid");
        if(f.style.raw_style>=256 || (f.style.resource_guid &&
           (f.style.raw_style<=21||f.style.raw_style>=256)))
            throw odh::FormatError("saved wall dynamic style outside profile");
        if(!f.style.resource_guid&&f.style.raw_style>21)
            throw odh::FormatError("saved wall appearance dynamic style missing GUID");
    }
    std::vector<SavedWallFace> result;
    for(std::uint8_t level=1;level<=view.level;++level)
        for(std::uint32_t y=0;y<a.height;++y)for(std::uint32_t x=0;x<a.width;++x){
            const auto& t=a.levels[level-1].walls[std::size_t(y)*a.width+x];
            auto add=[&](std::uint8_t orientation,std::uint16_t pattern,
                         std::uint32_t ox,std::uint32_t oy,std::uint8_t side,bool override_allowed=true){
                SavedWallFace f{x,y,ox,oy,level,orientation,side,pattern,{0,0,{}}};
                auto rotated_cuts=cuts[level-1][std::size_t(y)*a.width+x];
                if(orientation<2){
                    const auto original=rotated_cuts;
                    if(rotation==1){rotated_cuts={0,original.top_left};
                        if(y+1<a.height)rotated_cuts.top_left=cuts[level-1][std::size_t(y+1)*a.width+x].top_right;
                    }else if(rotation==2){rotated_cuts={0,original.top_left};
                        if(y+1<a.height)rotated_cuts.top_right=cuts[level-1][std::size_t(y+1)*a.width+x].top_right;
                        if(x+1<a.width)rotated_cuts.top_left=cuts[level-1][std::size_t(y)*a.width+x+1].top_left;
                    }else if(rotation==3){rotated_cuts={original.top_right,0};
                        if(x+1<a.width)rotated_cuts.top_right=cuts[level-1][std::size_t(y)*a.width+x+1].top_left;
                    }
                }
                f.down=(orientation==0?rotated_cuts.top_left:rotated_cuts.top_right)!=0;
                if(ox<a.width&&oy<a.height){
                    const auto& owner=a.levels[level-1].walls[std::size_t(oy)*a.width+ox];
                    f.style.raw_style=side==1?owner.top_left_style:owner.top_right_style;
                    if((owner.segments&48)==0){
                        if((owner.segments&side)==0)f.style.raw_style=0;
                        else if(f.style.raw_style!=2&&f.style.raw_style!=12&&
                                f.style.raw_style!=13&&f.style.raw_style!=14)f.style.raw_style=1;
                    }
                    if(f.style.raw_style>=256)throw odh::FormatError("saved wall base dynamic style unsupported");
                    const auto found=overrides.find(FaceKey{level,ox,oy,side});
                    if(override_allowed&&found!=overrides.end()){
                        f.door=found->second->door;
                        if(!f.down&&f.style.raw_style==1&&found->second->style.raw_style!=0){
                            f.style=found->second->style;f.object_owner=found->second->owner;
                        }
                    }
                }
                if(result.size()>=8192)throw odh::FormatError("saved wall face budget exceeded");
                result.push_back(f);
            };
            if(t.segments==16||t.segments==32){
                const bool horizontal=t.segments==16;
                const bool flip=horizontal?(rotation==1||rotation==2):(rotation==2||rotation==3);
                const bool out_horizontal=horizontal!=(rotation%2!=0);
                add(out_horizontal?2:3,flip?t.bottom_left_pattern:t.bottom_right_pattern,x,y,2,rotation==0);
            }else{
                // RotateWall maps source sides to visible orientations. Style
                // ownership is intentionally independent of paint ownership.
                switch(rotation){
                case 0:
                    if(t.segments&1)add(0,t.top_left_pattern,x,y,1);
                    if(t.segments&2)add(1,t.top_right_pattern,x,y,2);
                    break;
                case 1:
                    if(t.segments&8)add(0,t.bottom_left_pattern,x,y+1,2);
                    if(t.segments&1)add(1,t.top_left_pattern,x,y,1);
                    break;
                case 2:
                    if(t.segments&4)add(0,t.bottom_right_pattern,x+1,y,1);
                    if(t.segments&8)add(1,t.bottom_left_pattern,x,y+1,2);
                    break;
                case 3:
                    if(t.segments&2)add(0,t.top_right_pattern,x,y,2);
                    if(t.segments&4)add(1,t.bottom_right_pattern,x+1,y,1);
                    break;
                }
            }
        }
    return result;
}

struct SavedWallRenderer::Impl {
    const odh::DefinitionIndex& index;
    std::string global;
    ArchitectureMaterialResources catalog;
    std::map<std::uint16_t,Asset> external_assets;
    std::set<std::string> source_paths;
    std::map<std::string,std::unique_ptr<Source>> sources;
    std::map<AssetKey,Asset> assets;
    struct Frame {odh::SprFrame image;std::uint16_t palette;bool palette_missing;};
    std::map<FrameKey,Frame> frames;
    std::map<std::pair<std::uint8_t,std::uint8_t>,odh::WallDepth> depths;
    std::map<std::uint8_t,odh::WallDepth> junction_depths;
    std::uint64_t chunks=0,pixels=0,bytes=0,provenance=0;
    Impl(const std::string& path,const odh::DefinitionIndex& definitions,const odh::ArchitectureResourceCatalog& resources)
        :index(definitions),global(path),catalog(resources){
        asset(path,"");
    }
    Asset& asset(const std::string& path,const std::string& name){
        const AssetKey key{path,name};auto found=assets.find(key);if(found!=assets.end())return found->second;
        if(path.size()>4096||assets.size()+external_assets.size()>=256||path.size()+name.size()>256*1024-provenance)
            throw odh::FormatError("saved wall asset/provenance budget exceeded");
        auto source=sources.find(path);const bool direct=name.empty();
        if(source==sources.end()){
            if(!source_paths.count(path)&&source_paths.size()>=16)throw odh::FormatError("saved wall source budget exceeded");
            source=sources.emplace(path,std::make_unique<Source>(path,direct)).first;
            source_paths.insert(path);
        }
        if(source->second->direct!=direct)throw odh::FormatError("saved wall source type conflict");
        auto& s=*source->second;odh::View view(s.stream,std::filesystem::file_size(path));
        if(!direct){const odh::FarEntry* selected=nullptr;
            for(const auto& e:s.entries)if(e.name==name){if(selected)throw odh::FormatError("ambiguous saved wall FAR asset");selected=&e;}
            if(!selected)throw odh::FormatError("saved wall FAR asset absent");
            view=view.subview(selected->offset,selected->length);}
        Asset value;odh::Limits limits;limits.iff_chunks=static_cast<std::uint32_t>(std::min<std::uint64_t>(4096,65536-chunks));
        odh::iff_chunks(view,[&](const auto& c){value.chunks.push_back(c);},limits);
        const auto count=value.chunks.size();auto inserted=assets.emplace(key,std::move(value));
        chunks+=count;provenance+=path.size()+name.size();return inserted.first->second;
    }
    const Frame& frame(const std::string& path,const std::string& name,Asset& a,std::uint16_t id,std::uint32_t number,bool external=false){
        const FrameKey key{external,path,name,id,number};auto found=frames.find(key);if(found!=frames.end())return found->second;
        if(frames.size()>=4096)throw odh::FormatError("saved wall frame cache budget exceeded");
        const auto& raw=a.require("SPR#",id);const auto header=odh::read_spr_header(raw);
        const auto* palette_chunk=a.find("PALT",header.palette_id);
        odh::Palette palette;
        if(palette_chunk)palette=odh::read_palt(palette_chunk->payload);
        else palette.colors.assign(256,{0,0,0,255});
        odh::SpriteLimits limits;limits.pixels=static_cast<std::uint32_t>(pixel_budget-pixels);
        auto image=odh::read_spr_frame(raw,number,palette,limits);
        if(!image.width||!image.height)throw odh::FormatError("saved wall empty source profile unsupported");
        const auto count=image.pixels.size(),image_bytes=4*count+image.indices.size();
        auto inserted=frames.emplace(key,Frame{std::move(image),header.palette_id,!palette_chunk});
        pixels+=count;bytes+=image_bytes;return inserted.first->second;
    }
};
SavedWallRenderer::SavedWallRenderer(const std::string& path,const odh::DefinitionIndex& index,
    const odh::ArchitectureResourceCatalog& catalog):impl_(std::make_unique<Impl>(path,index,catalog)){}
SavedWallRenderer::~SavedWallRenderer()=default;
SavedWallRenderer::SavedWallRenderer(SavedWallRenderer&&) noexcept=default;
SavedWallRenderer& SavedWallRenderer::operator=(SavedWallRenderer&&) noexcept=default;

SavedWallRenderResult SavedWallRenderer::render(const odh::SavedArchitecture& architecture,
    const odh::WallAppearance& appearance,const odh::WorldProjection& camera,
    const odh::RasterFrame& base,bool terrain,std::int32_t base_alt,odh::WallView view){
    if(!impl_)throw odh::FormatError("moved saved wall renderer");
    if(base.width!=camera.width||base.height!=camera.height)throw odh::FormatError("saved wall camera/base mismatch");
    odh::project_tile(camera,camera.center_tile);
    const auto faces=saved_wall_faces(architecture,appearance,camera.rotation,view);
    const auto junctions=odh::wall_junctions(architecture,appearance,camera.rotation,view);
    const auto cells=std::size_t(architecture.width)*architecture.height;
    if(terrain&&(!architecture.altitude||architecture.altitude->size()!=cells))
        throw odh::FormatError("saved wall terrain altitude extent invalid");
    SavedWallRenderResult result;std::vector<odh::WallSpriteLayer> layers;
    result.unmapped_junctions=junctions.unmapped;
    auto& globals=impl_->asset(impl_->global,"");std::uint64_t names=0;
    // WallComponent destination arrays; medium/far cardinal rectangle heights
    // intentionally differ from generated Z-map heights (136/135, 68/67).
    struct Rect {int x,y;std::uint32_t w,h;};
    static constexpr Rect rects[3][4]={
        {{1,20,16,68},{17,20,16,68},{1,28,32,60},{15,37,4,58}},
        {{2,39,32,136},{34,39,32,136},{2,55,64,120},{30,73,8,116}},
        {{4,77,64,271},{68,77,64,271},{4,109,128,240},{60,145,16,232}}
    };
    const auto altitude_at=[&](std::uint32_t x,std::uint32_t y){
        float altitude=0;
        if(terrain&&x>0&&y>0){
            const auto x1=(x+1)%architecture.width,y1=(y+1)%architecture.height;
            const auto at=[&](std::uint32_t ax,std::uint32_t ay){return unsigned((*architecture.altitude)[std::size_t(ay)*architecture.width+ax][1])*10U;};
            const auto center=(at(x,y)+at(x1,y)+at(x,y1)+at(x1,y1))/4U;
            altitude=(static_cast<float>(center)-static_cast<float>(base_alt))*(3.f/160.f);
        }
        return altitude;
    };
    static constexpr Rect junction_rects[]={{1,80,32,16},{2,159,64,32},{4,317,128,64}};
    // _2DWorldBatch groups Z_BUFFER ahead of WALL. Junction textures provide
    // both their RGB and their own alpha, never object replacement masks.
    for(const auto& j:junctions.junctions){
        const auto id=static_cast<std::uint16_t>(4095+camera.zoom);
        const auto& frame=impl_->frame(impl_->global,"",globals,id,j.frame);
        auto z=impl_->junction_depths.find(camera.zoom);
        if(z==impl_->junction_depths.end())z=impl_->junction_depths.emplace(camera.zoom,odh::wall_junction_depth(camera.zoom)).first;
        const float story_height=j.down?.3f+float(j.level-1)*2.95f:float(j.level)*2.95f;
        const std::array<float,3> tile{float(j.x)-.5f,float(j.y)-.5f,story_height+altitude_at(j.x,j.y)};
        const auto origin=odh::project_tile(camera,tile);const auto rect=junction_rects[camera.zoom-1];
        const int scale=1<<(camera.zoom-1);const double width=32*scale;
        const std::array<std::array<double,2>,4> anchor{{{0,width/4},{-width/2,0},{0,-width/4},{width/2,0}}};
        const auto x=integer(double(origin[0])+rect.x-17*scale+anchor[camera.rotation][0]);
        const auto y=integer(double(origin[1])+rect.y-87*scale+anchor[camera.rotation][1]);
        integer(double(x)+rect.w);integer(double(y)+rect.h);
        static constexpr std::array<std::array<float,3>,4> back{{{.15f,0,.15f},{.15f,0,2.85f},{2.85f,0,2.85f},{2.85f,0,.15f}}};
        const auto b=back[camera.rotation];
        const auto back_depth=odh::project_world(camera,{3*tile[0]+b[0],3*tile[2],3*tile[1]+b[2]})[2];
        constexpr float front_delta=-3.6742346141747673f;
        layers.push_back({&frame.image,nullptr,&z->second,x,y,rect.w,rect.h,back_depth,front_delta});
        if(impl_->global.size()>256*1024-names)throw odh::FormatError("saved wall junction provenance budget exceeded");
        names+=impl_->global.size();
        result.junctions.push_back({j,impl_->global,id,frame.palette,frame.palette_missing,x,y,rect.w,rect.h,back_depth,front_delta});
    }
    for(const auto& f:faces){
        if(f.orientation<2&&f.down&&f.door){result.omitted_doors.push_back(f);continue;}
        std::string pattern_source=impl_->global,pattern_asset,pattern_key;
        Asset* paint=&globals;
        static constexpr std::uint16_t dynamic_ids[]={1,1793,2049};
        if(f.pattern>65533)throw odh::FormatError("saved wall pattern outside profile");
        if(f.pattern>=256){
            const auto& record=impl_->catalog.record(odh::ArchitectureResourceFamily::wall,f.pattern);
            pattern_source=impl_->catalog.path(record);pattern_asset=record.source.entry_name;
            // A separate cache namespace avoids collisions with masks and with
            // duplicate FAR names whose catalog winner has an exact index.
            pattern_key="architecture:"+std::to_string(record.source.entry_index)+":"+pattern_asset;
            auto found=impl_->external_assets.find(f.pattern);
            if(found==impl_->external_assets.end()){
                if(impl_->assets.size()+impl_->external_assets.size()>=256||
                   impl_->external_assets.size()>=16||
                   pattern_source.size()+pattern_asset.size()>256*1024-impl_->provenance)
                    throw odh::FormatError("saved wall external asset/provenance budget exceeded");
                if(!impl_->source_paths.count(pattern_source)&&impl_->source_paths.size()>=16)
                    throw odh::FormatError("saved wall source budget exceeded");
                auto value=impl_->catalog.open(record,static_cast<std::uint32_t>(std::min<std::uint64_t>(4096,65536-impl_->chunks)));
                impl_->source_paths.insert(pattern_source);
                impl_->chunks+=value.chunks.size();impl_->provenance+=pattern_source.size()+pattern_asset.size();
                found=impl_->external_assets.emplace(f.pattern,std::move(value)).first;
            }
            paint=&found->second;
        }
        const auto pid=f.pattern>=256?dynamic_ids[camera.zoom-1]:
            static_cast<std::uint16_t>(1536+f.pattern+(camera.zoom-1)*256);
        const auto& pattern=impl_->frame(pattern_source,pattern_key,*paint,pid,f.orientation,f.pattern>=256);
        std::string mask_source=impl_->global,mask_asset;
        Asset* resource=&globals;std::uint16_t mid=0;bool down_up_fallback=false;
        if(f.style.resource_guid){
            const auto* c=impl_->index.unique(f.style.resource_guid);
            if(!c)throw odh::FormatError("saved wall concrete mask definition missing/ambiguous");
            resource=&impl_->asset(c->source,c->asset);mask_source=c->source;mask_asset=c->asset;
            const auto& raw=resource->require("OBJD",c->chunk_id);
            const auto def=odh::read_object_definition(raw);
            const auto binding=odh::read_objd_wall_appearance(raw);
            if(def.guid!=f.style.resource_guid||def.version!=c->definition.version||
               def.master_id!=c->definition.master_id||def.sub_index!=c->definition.sub_index||
               def.object_type!=c->definition.object_type||binding.wall_style!=f.style.raw_style||!binding.sprite_base)
                throw odh::FormatError("saved wall object mask identity changed");
            for(unsigned i=0;i<6;++i)if(f.style.sprites[i]!=static_cast<std::uint16_t>(std::uint32_t(*binding.sprite_base)+i))
                throw odh::FormatError("saved wall object mask binding changed");
            mid=f.style.sprites[camera.zoom-1+(f.down?3:0)];
        }else{
            mid=static_cast<std::uint16_t>(2*f.style.raw_style+(camera.zoom-1)*512);
            if(f.down&&f.style.raw_style!=0){
                const auto far_down=static_cast<std::uint16_t>(2*f.style.raw_style+1);
                if(!globals.find("SPR#",far_down))down_up_fallback=true;
                else{
                    for(unsigned zoom=0;zoom<3;++zoom)globals.require("SPR#",static_cast<std::uint16_t>(far_down+zoom*512));
                    ++mid;
                }
            }
        }
        const Impl::Frame* mask=nullptr;
        // Style0 has no provider entry; missing selected SPR is pattern fallback.
        if(f.style.raw_style!=0&&resource->find("SPR#",mid))
            mask=&impl_->frame(mask_source,mask_asset,*resource,mid,f.orientation);
        const auto key=std::make_pair(camera.zoom,f.orientation);auto z=impl_->depths.find(key);
        if(z==impl_->depths.end())z=impl_->depths.emplace(key,odh::wall_sprite_depth(camera.zoom,f.orientation)).first;
        const float altitude=altitude_at(f.x,f.y);
        const std::array<float,3> tile{float(f.x),float(f.y),float(f.level-1)*2.95f+altitude};
        const auto origin=odh::project_tile(camera,tile);const auto rect=rects[camera.zoom-1][f.orientation];
        const int scale=1<<(camera.zoom-1);const double width=32*scale;
        const std::array<std::array<double,2>,4> anchor{{{0,width/4},{-width/2,0},{0,-width/4},{width/2,0}}};
        const auto x=integer(double(origin[0])+rect.x-17*scale+anchor[camera.rotation][0]);
        const auto y=integer(double(origin[1])+rect.y-87*scale+anchor[camera.rotation][1]);
        integer(double(x)+rect.w);integer(double(y)+rect.h);
        // Same unnormalized back/front profile as odh::place_sprite; no fake
        // SPR2 frame or DGRP is created to route wall resources through it.
        static constexpr std::array<std::array<float,3>,4> back{{{.15f,0,.15f},{.15f,0,2.85f},{2.85f,0,2.85f},{2.85f,0,.15f}}};
        const auto b=back[camera.rotation];
        const auto back_depth=odh::project_world(camera,{3*tile[0]+b[0],3*tile[2],3*tile[1]+b[2]})[2];
        constexpr float front_delta=-3.6742346141747673f;
        layers.push_back({&pattern.image,mask?&mask->image:nullptr,&z->second,x,y,rect.w,rect.h,back_depth,front_delta});
        if(pattern_source.size()+pattern_asset.size()+mask_source.size()+mask_asset.size()>256*1024-names)
            throw odh::FormatError("saved wall draw provenance budget exceeded");
        names+=pattern_source.size()+pattern_asset.size()+mask_source.size()+mask_asset.size();
        result.draws.push_back({f,pattern_source,mask_source,mask_asset,pattern_asset,pid,pattern.palette,mid,
            mask?mask->palette:std::uint16_t{0},!mask,pattern.palette_missing,
            mask&&mask->palette_missing,x,y,rect.w,rect.h,back_depth,front_delta,down_up_fallback});
        if(!mask)++result.mask_fallbacks;
    }
    result.composite=odh::composite_wall_sprites(base,layers);
    result.cached_assets=impl_->assets.size()+impl_->external_assets.size();result.cached_frames=impl_->frames.size();
    result.cached_pixels=impl_->pixels;result.cached_frame_bytes=impl_->bytes;
    return result;
}
