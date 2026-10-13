#include "odh/raster.hpp"
#include "odh/skinning.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>

namespace {
std::string fold(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c+'a'-'A');return s;}
double number(const std::string& s){
    if(s.empty()||s.front()==' '||s.front()=='\t'||s.front()=='\n'||s.front()=='\r'||s.front()=='\v'||s.front()=='\f')throw odh::FormatError("invalid number");
    std::size_t end=0;double v=std::stod(s,&end);if(end!=s.size()||!std::isfinite(v))throw odh::FormatError("number must be finite and exact");return v;
}
std::uint32_t dimension(const std::string& s){
    if(s.empty())throw odh::FormatError("invalid dimension");std::uint64_t n=0;
    for(char c:s){if(c<'0'||c>'9')throw odh::FormatError("dimension must be an unsigned integer");n=n*10+static_cast<unsigned>(c-'0');if(n>2048)throw odh::FormatError("dimension exceeds limit");}
    if(!n)throw odh::FormatError("dimension must be positive");return static_cast<std::uint32_t>(n);
}
float finite_float(double n){if(!std::isfinite(n)||std::abs(n)>std::numeric_limits<float>::max())throw odh::FormatError("projection overflow");return static_cast<float>(n);}
struct Part {std::string entry,name,bmf,texture;};
struct Evidence {Part source;odh::Appearance appearance;std::size_t binding;std::string skin,default_texture;std::size_t vertices,triangles;std::vector<std::size_t> clamped;};
void ppm(const odh::RasterFrame& image,const std::string& path){
    std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out)throw odh::FormatError("cannot open PPM output");
    out<<"P6\n"<<image.width<<' '<<image.height<<"\n255\n";
    for(const auto& p:image.pixels){const char rgb[]={static_cast<char>(p[0]),static_cast<char>(p[1]),static_cast<char>(p[2])};out.write(rgb,3);}
    out.close();if(!out)throw odh::FormatError("cannot write PPM output");
}
}
int main(int argc,char** argv){try{
    if(argc<5)throw odh::FormatError("Usage: odh-render ANIMATION_FAR SKELETON_ENTRY SKELETON_NAME TEXTURES_FAR --part APPEARANCE_ENTRY APPEARANCE_NAME BMF_ENTRY TEXTURE_ENTRY [--part ...] [--animation BCF_ENTRY CLIP CFP_ENTRY] [--frame N] --output FILE.ppm [--width N --height N --scale N --lit --nearest]");
    std::vector<Part> parts;std::set<std::string> seen;std::string anim_entry,clip,cfp_entry,output;double frame=0,scale=55;std::uint32_t width=400,height=400;odh::RasterOptions options;
    for(int i=5;i<argc;){std::string opt=argv[i++];
        if(opt!="--part"&&!seen.insert(opt).second)throw odh::FormatError("duplicate option");
        auto need=[&](int count){if(argc-i<count)throw odh::FormatError("incomplete option");};
        if(opt=="--part"){need(4);if(parts.size()==16)throw odh::FormatError("part limit exceeded");parts.push_back({argv[i],argv[i+1],argv[i+2],argv[i+3]});i+=4;}
        else if(opt=="--animation"){need(3);anim_entry=argv[i++];clip=argv[i++];cfp_entry=argv[i++];}
        else if(opt=="--output"){need(1);output=argv[i++];}
        else if(opt=="--frame"){need(1);frame=number(argv[i++]);}
        else if(opt=="--width"){need(1);width=dimension(argv[i++]);}
        else if(opt=="--height"){need(1);height=dimension(argv[i++]);}
        else if(opt=="--scale"){need(1);scale=number(argv[i++]);if(scale<=0)throw odh::FormatError("scale must be positive");}
        else if(opt=="--lit")options.lighting=true;
        else if(opt=="--nearest")options.filter=odh::TextureFilter::nearest;
        else throw odh::FormatError("unknown option");
    }
    if(parts.empty())throw odh::FormatError("at least one --part is required");
    if(output.empty())throw odh::FormatError("--output is required");
    if(seen.count("--frame")&&!seen.count("--animation"))throw odh::FormatError("--frame requires --animation");
    odh::RasterLimits limits;if(std::uint64_t{width}*height>limits.pixels)throw odh::FormatError("framebuffer pixel limit exceeded");
    std::optional<odh::SkeletonCollection> skeletons;with_far_entry(argv[1],argv[2],[&](const auto& v){skeletons=odh::read_bcf_skeletons(v);});
    const odh::Skeleton* skeleton=nullptr;for(const auto& s:skeletons->skeletons)if(s.name==argv[3]){if(skeleton)throw odh::FormatError("ambiguous skeleton name");skeleton=&s;}
    if(!skeleton)throw odh::FormatError("exact skeleton name not found");
    std::optional<odh::AnimationCollection> animations;const odh::AnimationHeader* animation=nullptr;std::optional<odh::CfpPlanes> cfp;
    if(seen.count("--animation")){
        with_far_entry(argv[1],anim_entry,[&](const auto& v){animations=odh::read_bcf_animations(v);});
        for(const auto& a:animations->animations)if(a.name==clip){if(animation)throw odh::FormatError("ambiguous animation name");animation=&a;}
        if(!animation)throw odh::FormatError("exact animation name not found");
        if(fold(cfp_entry)!=fold(animation->xskill+".cfp"))throw odh::FormatError("CFP entry must match selected animation xskill");
        with_far_entry(argv[1],cfp_entry,[&](const auto& v){cfp=odh::read_cfp(v,animation->translations,animation->rotations);});
    }
    const auto pose=odh::sample_pose(*skeleton,animation,cfp?&*cfp:nullptr,frame);
    std::vector<odh::RasterTriangle> triangles;std::vector<odh::BitmapImage> textures;std::vector<Evidence> evidence;
    std::uint64_t geometry_vertices=0,vertex_count=0,texture_pixels=0;
    for(const auto& part:parts){
        std::optional<odh::AppearanceCollection> apps;with_far_entry(argv[1],part.entry,[&](const auto& v){apps=odh::read_bcf_appearances(v);});
        const odh::Appearance* app=nullptr;for(const auto& a:apps->appearances)if(a.name==part.name){if(app)throw odh::FormatError("ambiguous appearance name");app=&a;}
        if(!app)throw odh::FormatError("exact appearance name not found");
        std::optional<std::size_t> binding;for(std::size_t j=0;j<app->bindings.size();++j)if(fold(app->bindings[j].mesh_name+".bmf")==fold(part.bmf)){if(binding)throw odh::FormatError("ambiguous appearance mesh binding");binding=j;}
        if(!binding)throw odh::FormatError("appearance has no binding for selected BMF entry");
        std::optional<odh::Mesh> raw;with_far_entry(argv[1],part.bmf,[&](const auto& v){raw=odh::read_bmf(v);});
        const auto& bind=app->bindings[*binding];if(fold(raw->skin_name)!=fold(bind.mesh_name))throw odh::FormatError("BMF skin name does not match appearance binding");
        std::size_t bones=0;for(const auto& b:pose.bones)if(fold(b.name)==fold(bind.bone))++bones;
        if(bones!=1)throw odh::FormatError("appearance binding bone must resolve uniquely in pose");
        geometry_vertices+=raw->real_vertices.size()+raw->blend_vertices.size();
        if(geometry_vertices>odh::MeshLimits{}.vertices)throw odh::FormatError("cumulative geometry vertex limit exceeded");
        if(triangles.size()+raw->faces.size()>limits.triangles)throw odh::FormatError("cumulative triangle limit exceeded");
        auto mesh=odh::skin_mesh(*raw,pose);std::optional<odh::BitmapImage> texture;
        with_far_entry(argv[4],part.texture,[&](const auto& v){texture=odh::read_bmp(v);});
        texture_pixels+=texture->pixels.size();if(texture_pixels>limits.texture_pixels)throw odh::FormatError("cumulative texture pixel limit exceeded");
        std::vector<odh::RasterVertex> projected;projected.reserve(mesh.vertices.size());
        for(const auto& v:mesh.vertices){double x=v.position[0],y=v.position[1],z=v.position[2];projected.push_back({{
            finite_float(width/2.0+scale*(x-z)/std::sqrt(2.0)),
            finite_float(height/2.0+scale*((x+z)/(2*std::sqrt(2.0))-std::sqrt(3.0)/2*(y-3))),
            finite_float(-(std::sqrt(3.0/8)*x+.5*y+std::sqrt(3.0/8)*z))},v.texcoord,v.normal});}
        for(const auto& f:mesh.faces)triangles.push_back({{projected[f[0]],projected[f[1]],projected[f[2]]},textures.size()});
        vertex_count+=mesh.vertices.size();textures.push_back(std::move(*texture));
        evidence.push_back({part,*app,*binding,raw->skin_name,raw->texture_name,mesh.vertices.size(),mesh.faces.size(),mesh.clamped_binding_indices});
    }
    const auto image=odh::rasterize(width,height,triangles,textures,options,limits);
    std::size_t covered=0,colored=0;double near=std::numeric_limits<double>::infinity(),far=-near;
    for(std::size_t i=0;i<image.depth.size();++i){if(std::isfinite(image.depth[i])){++covered;near=std::min(near,static_cast<double>(image.depth[i]));far=std::max(far,static_cast<double>(image.depth[i]));}if(image.pixels[i]!=options.clear)++colored;}
    std::ostringstream out;out<<std::setprecision(std::numeric_limits<double>::max_digits10)
        <<"{\"evidence\":\"ODH_portable_CPU_textured_mesh_render\",\"camera\":{\"profile\":\"experimental_fixed_orthographic\",\"yaw_degrees\":45,\"elevation_degrees\":30,\"target\":[0,3,0],\"original_calibrated\":false},\"sources\":{\"animation_far\":"<<json_bytes(argv[1])<<",\"textures_far\":"<<json_bytes(argv[4])<<",\"skeleton_entry\":"<<json_bytes(argv[2])<<",\"skeleton_name\":"<<json_bytes(argv[3]);
    if(animation)out<<",\"animation_entry\":"<<json_bytes(anim_entry)<<",\"clip\":"<<json_bytes(clip)<<",\"cfp_entry\":"<<json_bytes(cfp_entry);
    out<<",\"parts\":[";for(std::size_t i=0;i<evidence.size();++i){if(i)out<<',';const auto& e=evidence[i];const auto& b=e.appearance.bindings[e.binding];out<<"{\"appearance_entry\":"<<json_bytes(e.source.entry)<<",\"appearance_name\":"<<json_bytes(e.source.name)<<",\"bmf_entry\":"<<json_bytes(e.source.bmf)<<",\"texture_entry\":"<<json_bytes(e.source.texture)<<",\"bmf_texture_name\":"<<json_bytes(e.default_texture)<<",\"skin_name\":"<<json_bytes(e.skin)<<",\"appearance_type\":"<<e.appearance.type<<",\"appearance_zero\":"<<e.appearance.zero<<",\"selected_binding\":"<<e.binding<<",\"binding\":{\"bone\":"<<json_bytes(b.bone)<<",\"mesh_name\":"<<json_bytes(b.mesh_name)<<",\"censor_flags\":"<<b.censor_flags<<",\"zero\":"<<b.zero<<"},\"vertices\":"<<e.vertices<<",\"triangles\":"<<e.triangles<<",\"clamped_binding_indices\":[";for(std::size_t j=0;j<e.clamped.size();++j){if(j)out<<',';out<<e.clamped[j];}out<<"]}";}
    out<<"]},\"pose\":{\"requested_frame\":"<<pose.requested_frame<<",\"frame\":"<<pose.frame<<",\"fraction\":"<<pose.fraction<<",\"completed\":"<<(pose.completed?"true":"false")<<",\"missing_motion_bones\":[";
    for(std::size_t j=0;j<pose.missing_motion_bones.size();++j){if(j)out<<',';out<<json_bytes(pose.missing_motion_bones[j]);}
    out<<"]},\"options\":{\"width\":"<<width<<",\"height\":"<<height<<",\"scale\":"<<scale<<",\"lighting\":"<<(options.lighting?"true":"false")<<",\"filter\":"<<json_bytes(options.filter==odh::TextureFilter::nearest?"nearest":"bilinear_wrap")<<"},\"vertices\":"<<vertex_count<<",\"triangles\":"<<triangles.size()<<",\"textures\":"<<textures.size()<<",\"tested_samples\":"<<image.tested_samples<<",\"shaded_samples\":"<<image.shaded_samples<<",\"colored_pixels\":"<<colored<<",\"covered_pixels\":"<<covered<<",\"coverage\":"<<static_cast<double>(covered)/image.pixels.size()<<",\"depth_range\":[";
    if(covered)out<<near<<','<<far;else out<<"null,null";
    out<<"],\"framebuffer_bytes\":"<<std::uint64_t{width}*height*8<<",\"texture_bytes\":"<<texture_pixels*4<<",\"output\":"<<json_bytes(output)<<"}\n";
    ppm(image,output);std::cout<<out.str();return 0;
}catch(const std::exception& e){std::cerr<<"odh-render: "<<e.what()<<'\n';return 2;}}
