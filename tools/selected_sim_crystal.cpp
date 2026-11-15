// SPDX-License-Identifier: MPL-2.0
#include "selected_sim_crystal.hpp"
#include "odh/compositor.hpp"
#include "odh/raster_region.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace odh_host {
odh::RasterFrame overlay_selected_sim_crystal(const odh::RasterFrame& base,
        const odh::RasterFrame& layer,odh::RasterLimits limits){
    auto result=odh::merge_raster(base,layer,limits);
    result.owner_ids=base.owner_ids;
    return result;
}
namespace {
std::string fold(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return s;}
template<class F> auto resource(const std::string& path,const std::string& name,
        const SelectedSimCrystalLimits& limits,F decode){
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw odh::FormatError("selected Sim crystal archive unavailable");
    const auto bytes=std::filesystem::file_size(path);
    if(bytes>limits.archive_bytes)throw odh::FormatError("selected Sim crystal archive exceeds budget");
    odh::View archive(stream,bytes);odh::Limits far_limits;far_limits.far_entries=limits.archive_entries;
    far_limits.far_name_bytes=1024*1024;far_limits.far_name_length=1024;
    std::optional<odh::FarEntry> selected;
    for(const auto& entry:odh::far1_index(archive,far_limits))if(fold(entry.name)==fold(name)){
        if(selected)throw odh::FormatError("ambiguous selected Sim crystal resource");
        selected=entry;
    }
    if(!selected||selected->length>limits.asset_bytes)
        throw odh::FormatError("selected Sim crystal resource absent or oversized");
    return decode(archive.subview(selected->offset,selected->length));
}
float finite(double value){
    const auto converted=static_cast<float>(value);
    if(!std::isfinite(converted))throw odh::FormatError("selected Sim crystal transform is nonfinite");
    return converted;
}
}
std::array<float,3> selected_sim_crystal_color(std::int16_t mood){
    const double t=std::clamp(double(mood)*.012,-1.,1.);
    std::array<float,3> color;
    if(t>=0){color={float(1-t),1,float(1-t)};if(t<.4)color[2]-=float(.4-t);}
    else{color={1,float(1-t*t),float(1-t*t)};if(t>-.4)color[2]-=float(t+.4);}
    return color;
}
std::array<float,3> selected_sim_crystal_light(std::int16_t mood,
        std::array<float,3> normal,std::array<float,3> toward_light){
    double n2=0,l2=0,dot=0;
    for(unsigned i=0;i<3;++i){
        if(!std::isfinite(normal[i])||!std::isfinite(toward_light[i]))
            throw odh::FormatError("selected Sim crystal light is nonfinite");
        n2+=double(normal[i])*normal[i];l2+=double(toward_light[i])*toward_light[i];
        dot+=double(normal[i])*toward_light[i];
    }
    if(l2==0)throw odh::FormatError("selected Sim crystal light direction is zero");
    const double diffuse=n2==0?0:std::clamp(dot/std::sqrt(n2*l2),0.,1.);
    auto color=selected_sim_crystal_color(mood);
    for(auto& channel:color){
        // 00585fbd/005cedcb: colored light, independent ambient, then
        // D3DLIGHTSTATE_AMBIENT packed by truncation to eight-bit channels.
        const auto ambient=std::floor((double(channel)*.25+.05)*255.)/255.;
        channel=float(std::clamp(ambient+double(channel)*diffuse,0.,1.));
    }
    return color;
}
SelectedSimCrystal::SelectedSimCrystal(const std::string& animations,const std::string& textures,
        SelectedSimCrystalLimits limits){
    odh::MeshLimits mesh_limits;mesh_limits.bones=1;mesh_limits.bindings=8;
    mesh_limits.vertices=limits.vertices;mesh_limits.faces=limits.triangles;mesh_limits.blends=limits.vertices;
    mesh_limits.appearances=1;mesh_limits.appearance_bindings=1;mesh_limits.text_bytes=4096;
    const auto appearance=resource(animations,"head-arrow.cmx.bcf",limits,[&](const auto& view){return odh::read_bcf_appearances(view,mesh_limits);});
    if(appearance.remaining_bytes||appearance.appearances.size()!=1||
       fold(appearance.appearances[0].name)!="head-arrow"||appearance.appearances[0].bindings.size()!=1)
        throw odh::FormatError("selected Sim crystal appearance is not the authored single binding");
    const auto& binding=appearance.appearances[0].bindings[0];
    if(fold(binding.bone)!="root"||fold(binding.mesh_name)!="xskin-head-arrow-root-arrow")
        throw odh::FormatError("selected Sim crystal appearance binding differs");
    const auto mesh=resource(animations,"xskin-head-arrow-ROOT-ARROW.bmf",limits,[&](const auto& view){return odh::read_bmf(view,mesh_limits);});
    if(mesh.remaining_bytes||fold(mesh.skin_name)!=fold(binding.mesh_name)||fold(mesh.texture_name)!="arrow"||
       mesh.bones.size()!=1||fold(mesh.bones[0])!="root"||mesh.faces.empty())
        throw odh::FormatError("selected Sim crystal mesh identity differs");
    odh::SkeletonPose pose{};pose.skeleton="ROOT";
    pose.bones={{"ROOT",-1,{0,0,0},{0,0,0,1},{1,0,0,0,0,1,0,0,0,0,1,0},{0,0,0}}};
    mesh_=odh::skin_mesh(mesh,pose);
    if(!mesh_.clamped_binding_indices.empty())throw odh::FormatError("selected Sim crystal bone binding was clamped");
    auto texture=resource(textures,"arrow.BMP",limits,[&](const auto& view){
        odh::BitmapLimits bitmap_limits;bitmap_limits.width=bitmap_limits.height=256;
        bitmap_limits.pixels=limits.texture_pixels;return odh::read_bmp(view,bitmap_limits);});
    if(texture.remaining_bytes)throw odh::FormatError("selected Sim crystal texture has trailing data");
    textures_.push_back(std::move(texture));
}
odh::RasterFrame SelectedSimCrystal::render(const odh::WorldProjection& camera,std::array<float,3> head,
        float rotation,std::int16_t mood,float scale,std::array<float,3> offset,
        std::array<float,3> basis,odh::RasterLimits limits) const{
    return render(camera,head,rotation,mood,scale,offset,basis,limits,nullptr);
}
odh::RasterFrame SelectedSimCrystal::render(const odh::WorldProjection& camera,std::array<float,3> head,
        float rotation,std::int16_t mood,float scale,std::array<float,3> offset,
        std::array<float,3> basis,odh::RasterLimits limits,std::array<std::uint32_t,2>* origin) const{
    if(!std::isfinite(rotation)||!std::isfinite(scale)||scale<=0||scale>1024)
        throw odh::FormatError("selected Sim crystal scale/rotation invalid");
    for(unsigned i=0;i<3;++i){
        if(!std::isfinite(head[i])||!std::isfinite(offset[i])||!std::isfinite(basis[i]))
            throw odh::FormatError("selected Sim crystal anchor/basis invalid");
        head[i]=finite(double(head[i])+offset[i]);
    }
    (void)odh::project_tile(camera,head);
    const double c=std::cos(double(rotation)),s=std::sin(double(rotation));
    const auto oriented=[&](std::array<float,3> input){
        std::array<double,3> v{input[0],input[1],input[2]};
        for(unsigned axis=0;axis<3;++axis){
            const auto a=(axis+1)%3,b=(axis+2)%3;
            const auto ca=std::cos(double(basis[axis])),sa=std::sin(double(basis[axis]));
            const auto va=v[a],vb=v[b];v[a]=va*ca-vb*sa;v[b]=va*sa+vb*ca;
        }
        return std::array<float,3>{finite(v[0]*c+v[2]*s),finite(v[1]),finite(-v[0]*s+v[2]*c)};
    };
    // Explicit approximation: light toward the orthographic viewer. Original
    // uses a camera-derived vector whose runtime transform is still unknown.
    constexpr float diagonal=.6123724357f;
    const std::array<std::array<float,3>,4> lights{{
        {diagonal,.5f,diagonal},{diagonal,.5f,-diagonal},
        {-diagonal,.5f,-diagonal},{-diagonal,.5f,diagonal}}};
    const auto light=lights.at(camera.rotation);
    std::vector<odh::RasterVertex> vertices;vertices.reserve(mesh_.vertices.size());
    for(const auto& vertex:mesh_.vertices){const auto p=oriented(vertex.position);
        const std::array<float,3> world{finite(double(scale)*p[0]+3.*head[0]),
            finite(double(scale)*p[1]+3.*head[2]),finite(double(scale)*p[2]+3.*head[1])};
        const auto normal=oriented(vertex.normal);
        vertices.push_back({odh::project_world(camera,world),vertex.texcoord,normal,
            selected_sim_crystal_light(mood,normal,light)});
    }
    std::vector<odh::RasterTriangle> triangles;triangles.reserve(mesh_.faces.size());
    for(const auto& face:mesh_.faces)triangles.push_back({{vertices[face[0]],vertices[face[1]],vertices[face[2]]},0,0});
    odh::RasterOptions options;options.vertex_colors=true;options.lighting=false;
    return odh::rasterize(camera.width,camera.height,triangles,textures_,options,limits,origin);
}
std::array<std::array<float,3>,2> SelectedSimCrystal::asset_bounds() const{
    auto low=mesh_.vertices.front().position,high=low;
    for(const auto& vertex:mesh_.vertices)for(unsigned i=0;i<3;++i){
        low[i]=std::min(low[i],vertex.position[i]);high[i]=std::max(high[i],vertex.position[i]);
    }
    return {low,high};
}
}
