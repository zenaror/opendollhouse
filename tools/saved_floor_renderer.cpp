#include "saved_floor_renderer.hpp"
#include "odh/terrain.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <string_view>
namespace {
const odh::View& unique(const std::vector<odh::Chunk>& chunks,std::string_view type,std::uint16_t id){
    const odh::Chunk* found=nullptr;
    for(const auto& c:chunks)if(c.type==type&&c.id==id){if(found)throw odh::FormatError("ambiguous floor resource chunk");found=&c;}
    if(!found)throw odh::FormatError("floor resource chunk missing for ID "+std::to_string(id));
    return found->payload;
}
}
struct SavedFloorRenderer::Impl {
    std::ifstream stream;std::vector<odh::Chunk> chunks;
    struct Material {odh::BitmapImage texture;SavedFloorMaterial source;};
    std::map<std::pair<std::uint16_t,std::uint8_t>,Material> materials;std::uint64_t pixels=0;
    explicit Impl(const std::string& path):stream(path,std::ios::binary){
        if(path.size()>4096||!stream)throw odh::FormatError("floor resource path/open failure");
        odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(odh::View(stream,std::filesystem::file_size(path)),[&](const auto& c){chunks.push_back(c);},limits);
    }
    const Material& material(std::uint16_t pattern,std::uint8_t zoom){
        const auto key=std::make_pair(pattern,zoom);auto found=materials.find(key);if(found!=materials.end())return found->second;
        if(materials.size()>=48)throw odh::FormatError("floor cache material budget exceeded");
        const auto id=std::uint32_t{pattern}+std::uint32_t{zoom-1U}*256U;
        if(id>65535||pattern==0||pattern>=256)throw odh::FormatError("floor global pattern outside profile");
        const auto& raw=unique(chunks,"SPR2",static_cast<std::uint16_t>(id));
        if(raw.le32(0)!=1000)throw odh::FormatError("floor SPR2 version unsupported");
        const auto palette_id=static_cast<std::uint16_t>(raw.le32(8));auto palette=odh::read_palt(unique(chunks,"PALT",palette_id));
        odh::SpriteLimits bounds;bounds.pixels=static_cast<std::uint32_t>(1048576-pixels);
        auto frame=odh::read_spr2_frame(raw,0,palette,bounds);odh::BitmapLimits texture_limits;texture_limits.pixels=bounds.pixels;
        auto texture=odh::floor_texture(frame,texture_limits);const auto count=texture.pixels.size();
        auto inserted=materials.emplace(key,Material{std::move(texture),{pattern,static_cast<std::uint16_t>(id),palette_id,0,0}});
        inserted.first->second.source.width=inserted.first->second.texture.width;inserted.first->second.source.height=inserted.first->second.texture.height;
        pixels+=count;return inserted.first->second;
    }
};
SavedFloorRenderer::SavedFloorRenderer(const std::string& path):impl_(std::make_unique<Impl>(path)){}
SavedFloorRenderer::~SavedFloorRenderer()=default;
SavedFloorResult SavedFloorRenderer::render(const odh::SavedArchitecture& a,const odh::WorldProjection& camera,bool terrain,std::int32_t base_alt){
    odh::project_tile(camera,camera.center_tile);
    if(a.width<2||a.height<2||a.width>64||a.height>64)throw odh::FormatError("floor architecture extent unsupported");
    const auto count=std::size_t(a.width)*a.height;
    for(const auto& l:a.levels)if(l.floors.size()!=count||l.walls.size()!=count)throw odh::FormatError("floor grid extent mismatch");
    for(const auto* pool:{&a.pools,&a.water})if(*pool){if((*pool)->size()!=count)throw odh::FormatError("floor water/pool extent mismatch");
        for(auto value:**pool)if(value!=0&&value!=255)throw odh::FormatError("pool/water floor geometry unsupported");}
    std::map<std::uint16_t,std::size_t> selected;std::vector<odh::BitmapImage> textures;std::vector<odh::RasterTriangle> triangles;SavedFloorResult result;
    auto draw=[&](std::uint32_t x,std::uint32_t y,std::uint8_t level,std::uint16_t pattern,odh::FloorHalf half){
        if(pattern==0)return;
        if(x+1>=a.width||y+1>=a.height)throw odh::FormatError("painted floor requires in-grid corners");
        auto found=selected.find(pattern);std::size_t index;
        if(found==selected.end()){
            if(textures.size()>=16)throw odh::FormatError("floor draw material budget exceeded");
            const auto& material=impl_->material(pattern,camera.zoom);index=textures.size();
            textures.push_back(material.texture);result.materials.push_back(material.source);selected.emplace(pattern,index);
        }else index=found->second;
        std::array<float,4> altitude{};
        if(terrain)altitude={odh::terrain_corner_altitude(a,x,y,base_alt),odh::terrain_corner_altitude(a,x+1,y,base_alt),
                            odh::terrain_corner_altitude(a,x+1,y+1,base_alt),odh::terrain_corner_altitude(a,x,y+1,base_alt)};
        // The strict pure geometry API scans texture alpha. Bound repeated work
        // independently of cache size and viewport clipping.
        if(textures[index].pixels.size()>16U*1024U*1024U-result.geometry_texture_pixel_visits)
            throw odh::FormatError("floor geometry texture validation work exceeds budget");
        result.geometry_texture_pixel_visits+=textures[index].pixels.size();
        auto geometry=odh::floor_triangles(camera,static_cast<float>(x),static_cast<float>(y),level,textures[index],index,half,true,altitude);
        if(geometry.size()>odh::RasterLimits{}.triangles-triangles.size())throw odh::FormatError("floor triangle budget exceeded");
        triangles.insert(triangles.end(),geometry.begin(),geometry.end());result.draws.push_back({x,y,level,pattern,half,altitude});
    };
    for(std::size_t l=0;l<2;++l)for(std::uint32_t y=0;y<a.height;++y)for(std::uint32_t x=0;x<a.width;++x){
        const auto i=std::size_t(y)*a.width+x;const auto& wall=a.levels[l].walls[i];const auto pattern=a.levels[l].floors[i];
        const auto diagonal=wall.segments&(odh::wall_horizontal_diagonal|odh::wall_vertical_diagonal);
        if(diagonal){
            if(diagonal==(odh::wall_horizontal_diagonal|odh::wall_vertical_diagonal))throw odh::FormatError("ambiguous double-diagonal floor");
            auto first=wall.top_left_pattern,second=wall.top_left_style;if(first==0&&second==0)first=second=pattern;
            const bool vertical=(diagonal&odh::wall_vertical_diagonal)!=0;
            draw(x,y,static_cast<std::uint8_t>(l+1),first,vertical?odh::FloorHalf::vertical_first:odh::FloorHalf::horizontal_first);
            draw(x,y,static_cast<std::uint8_t>(l+1),second,vertical?odh::FloorHalf::vertical_second:odh::FloorHalf::horizontal_second);
        }else draw(x,y,static_cast<std::uint8_t>(l+1),pattern,odh::FloorHalf::whole);
    }
    result.triangles=triangles.size();for(const auto& texture:textures)result.texture_bytes+=texture.pixels.size()*4;
    result.image=odh::rasterize(camera.width,camera.height,triangles,textures);return result;
}
