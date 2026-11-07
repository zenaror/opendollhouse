// Classic TS1 2D water/pool artwork. Pool geometry in current FreeSO's 3D
// backend is separate. This adapter uses the player's floors.iff sprites.
#pragma once
#include "saved_floor_renderer.hpp"
#include "architecture_material_resources.hpp"
#include "odh/compositor.hpp"
#include "odh/ground_owner.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace odh_host {
class SavedLiquidFloor {
    using Pixel=std::array<std::uint8_t,4>;
    struct Frame {odh::Spr2Frame art;SavedFloorMaterial material;};
    std::map<std::pair<std::uint16_t,std::uint8_t>,Frame> frames_;
    std::size_t cached_pixels_=0;
    // SPR2.FloorCopyWater pads the row and extends the nearest non-key color
    // toward the exterior. Two linear scans avoid quadratic transparent runs.
    static void prepare(odh::Spr2Frame& art,const odh::Palette& palette,std::uint16_t id,
                        std::uint8_t zoom,int corner){
        if(!art.width||!art.height||art.width>256||art.height>128||art.transparent_index>=palette.colors.size())
            throw odh::FormatError("liquid SPR2 dimensions or transparency key outside profile");
        const bool water=id>=0x800;
        if(water){
            if(art.z.size()!=art.pixels.size())throw odh::FormatError("water SPR2 lacks opacity plane");
            for(std::size_t i=0;i<art.pixels.size();++i)art.pixels[i][3]=art.z[i]<32?0:art.z[i];
        }
        auto transparent=palette.colors[art.transparent_index];transparent[3]=0;
        const Pixel zero{};
        const auto missing=[&](const Pixel& p){return p==zero||p==transparent;};
        const unsigned width=art.width+(art.width&1),height=art.height;
        std::vector<Pixel> padded(std::size_t(width)*height);
        for(unsigned y=0;y<height;++y)std::copy_n(art.pixels.begin()+std::size_t(y)*art.width,art.width,padded.begin()+std::size_t(y)*width);
        auto converted=padded;
        for(unsigned y=0;y<height;++y){
            const auto row=std::size_t(y)*width;
            if(water||id<=0x430){
                Pixel right{},left{};
                for(unsigned x=width;x-- >0;){
                    if(!missing(padded[row+x]))right=padded[row+x];
                    if(x<width/2)converted[row+x]=right;
                }
                for(unsigned x=0;x<width;++x){
                    if(!missing(padded[row+x]))left=padded[row+x];
                    if(x>=width/2)converted[row+x]=left;
                }
            }else for(unsigned x=0;x<width;++x){
                const auto peer=padded[((y+height/2)%height)*width+(x+width/2)%width];
                if(peer[3]>=254)converted[row+x]=peer;
            }
        }
        const int scale=1<<(zoom-1);
        const unsigned dest_width=corner<0?static_cast<unsigned>(32*scale):width;
        const unsigned dest_height=corner<0?static_cast<unsigned>(16*scale):height;
        art.pixels.resize(dest_width*dest_height);art.z.resize(art.pixels.size());
        const float dz=1.525f*static_cast<float>(4/scale);
        const float first=corner<0?255.f:corner==0?254.f:corner==2?254.f-1.525f*41:254.f-1.525f*22;
        constexpr unsigned side_rows[3]={8,13,21},bottom_rows[3]={8,13,23},top_rows[3]={7,12,22};
        const auto depth_rows=corner<0?dest_height:corner==0?top_rows[zoom-1]:corner==2?bottom_rows[zoom-1]:side_rows[zoom-1];
        for(unsigned y=0;y<dest_height;++y)for(unsigned x=0;x<dest_width;++x){
            const auto sample_x=(2*x+1)*width/(2*dest_width),sample_y=(2*y+1)*height/(2*dest_height);
            const auto at=std::size_t(y)*dest_width+x;
            art.pixels[at]=converted[std::size_t(sample_y)*width+sample_x];
            if(corner<0){
                // The texture's extended edges are for sampling; the water
                // surface covers the projected tile diamond, not its rectangle.
                const double fx=(2.0*x+1)/dest_width-1,fy=(2.0*y+1)/dest_height-1;
                if(std::abs(fx)+std::abs(fy)>1)art.pixels[at][3]=0;
            }
            const auto depth_y=(2*y+1)*depth_rows/(2*dest_height);
            art.z[at]=static_cast<std::uint8_t>(std::clamp(std::nearbyint(first-dz*static_cast<float>(depth_y)),0.f,255.f));
        }
        art.width=static_cast<std::uint16_t>(dest_width);art.height=static_cast<std::uint16_t>(dest_height);
        art.flags|=3;art.indices.clear();
    }
    const Frame& frame(const ArchitectureMaterialAsset& globals,const std::string& path,std::uint16_t id,
                       std::uint8_t frame_number,std::uint16_t pattern,std::uint8_t zoom,int corner,std::uint64_t painted_pixels){
        const auto key=std::make_pair(id,frame_number);const auto found=frames_.find(key);
        if(found!=frames_.end())return found->second;
        if(frames_.size()>=128)throw odh::FormatError("liquid floor material count exceeds budget");
        const auto& raw=globals.require("SPR2",id);
        if(raw.le32(0)!=1000||raw.le32(8)>65535)throw odh::FormatError("invalid liquid floor SPR2 header");
        const auto palette_id=static_cast<std::uint16_t>(raw.le32(8));
        auto palette=odh::read_palt(globals.require("PALT",palette_id));
        odh::SpriteLimits bounds;bounds.width=256;bounds.height=128;bounds.pixels=32768;
        auto art=odh::read_spr2_frame(raw,frame_number,palette,bounds);
        prepare(art,palette,id,zoom,corner);
        if(art.pixels.size()>1048576-cached_pixels_-painted_pixels)throw odh::FormatError("liquid floor sprite pixel budget exceeded");
        cached_pixels_+=art.pixels.size();
        SavedFloorMaterial material{pattern,id,palette_id,art.width,art.height,path,"",frame_number};
        return frames_.emplace(key,Frame{std::move(art),std::move(material)}).first->second;
    }
public:
    std::size_t pixels() const noexcept{return cached_pixels_;}
    struct DrawList {
        std::vector<odh::SpriteLayer> layers;
        std::set<std::pair<std::uint16_t,std::uint8_t>> materials;
        std::uint64_t layer_pixels=0;
    };
    void append(const odh::SavedArchitecture& lot,const odh::WorldProjection& camera,unsigned x,unsigned y,unsigned level,
                std::uint16_t pattern,float altitude,std::array<float,3> light,const ArchitectureMaterialAsset& globals,
                const std::string& path,std::uint64_t painted_pixels,DrawList& list,SavedFloorResult& result){
        static constexpr int directions[8][2]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
        unsigned neighbors=0;
        for(unsigned n=0;n<8;++n){
            const int tx=static_cast<int>(x)+directions[n][0],ty=static_cast<int>(y)+directions[n][1];
            if(tx<=0||ty<=0||tx>=static_cast<int>(lot.width)-1||ty>=static_cast<int>(lot.height)-1||
               odh::rendered_floor_pattern(lot,level-1,static_cast<unsigned>(ty)*lot.width+static_cast<unsigned>(tx))==pattern)
                neighbors|=1u<<n;
        }
        const auto shift=2*camera.rotation;
        const auto mask=((neighbors<<shift)|(neighbors>>((8-shift)%8)))&255;
        const auto variant=(mask&1)|((mask&64)>>5)|((mask&16)>>2)|((mask&4)<<1);
        result.draws.push_back({x,y,static_cast<std::uint8_t>(level),pattern,odh::FloorHalf::whole,
            {altitude,altitude,altitude,altitude}});
        auto& draw=result.draws.back();
        const int scale=1<<(camera.zoom-1),width=32*scale,height=16*scale;
        const bool water=pattern==65534;
        const float z=static_cast<float>(level-1)*2.95f+altitude+(water?0.05f/3.f:0.f);
        const auto origin=odh::project_tile(camera,{static_cast<float>(x),static_cast<float>(y),z});
        const int offset_x[4]={-width/2,-width,-width/2,0};
        const int offset_y[4]={0,-height/2,-height,-height/2};
        // Use the full projected tile extent. The resource can be one texel
        // narrower; scaling its pixels must not shrink the tile footprint.
        const auto px=static_cast<int>(std::ceil(origin[0]-.5f))+offset_x[camera.rotation];
        const auto py=static_cast<int>(std::ceil(origin[1]-.5f))+offset_y[camera.rotation];
        static constexpr float back[4][2]={{.15f,.15f},{.15f,2.85f},{2.85f,2.85f},{2.85f,.15f}};
        const auto depth=odh::project_world(camera,{3.f*static_cast<float>(x)+back[camera.rotation][0],3*z,3.f*static_cast<float>(y)+back[camera.rotation][1]})[2];
        const auto add=[&](std::uint16_t id,std::uint8_t number,int corner){
            if(corner<0)draw.surface_sprite=id;else draw.corner_sprites[corner]=id;
            const auto& item=frame(globals,path,id,number,pattern,camera.zoom,corner,painted_pixels);
            if(list.materials.emplace(id,number).second){result.materials.push_back(item.material);result.texture_bytes+=item.art.pixels.size()*5;}
            int lx=px,ly=py;
            if(corner==0||corner==2)lx+=width/2-item.art.width/2;
            if(corner==1||corner==3)ly+=height/2+1-item.art.height/2;
            if(corner==2)ly+=height-item.art.height;
            if(corner==3)lx+=width-item.art.width;
            if(lx>=static_cast<int>(camera.width)||ly>=static_cast<int>(camera.height)||lx+item.art.width<=0||ly+item.art.height<=0)return;
            if(list.layers.size()>=4096||item.art.pixels.size()>16*1048576-list.layer_pixels)
                throw odh::FormatError("visible liquid floor layers exceed frame budget");
            list.layer_pixels+=item.art.pixels.size();
            list.layers.push_back({&item.art,lx,ly,depth,-3.6742346f,false,
                odh::ground_owner_id(x,y,static_cast<std::uint8_t>(level)),light});
        };
        const auto base=water?0x800:0x400+(camera.zoom-1)*16;
        add(static_cast<std::uint16_t>(base+variant),water?3-camera.zoom:0,-1);
        if(!water){
            static constexpr unsigned sides[4]={65,80,20,5},missing[4]={128,32,8,2};
            for(int n=0;n<4;++n)if((mask&sides[n])==sides[n]&&!(mask&missing[n]))
                add(static_cast<std::uint16_t>(0x430+(camera.zoom-1)*4+n),0,n);
        }
    }
    static void paint(DrawList& list,SavedFloorResult& result,bool picks){
        result.liquid_layers=list.layers.size();
        std::vector<odh::SpriteLayer> batch;
        const auto flush=[&]{
            auto composite=odh::composite_sprites(result.image,batch,{},picks);
            if(picks)for(std::size_t i=0;i<composite.owner_ids.size();++i)
                if(composite.owner_ids[i])composite.image.owner_ids[i]=composite.owner_ids[i];
            result.image=std::move(composite.image);batch.clear();
        };
        std::size_t pixels=0;
        for(const auto& layer:list.layers){
            if(layer.frame->pixels.size()>1048576-pixels){flush();pixels=0;}
            batch.push_back(layer);pixels+=layer.frame->pixels.size();
        }
        if(!batch.empty())flush();
    }
};
}
