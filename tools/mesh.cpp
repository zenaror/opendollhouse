#include "odh/skinning.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
template<class T> void values(std::ostream& out,const T& list) {
    out<<'[';bool first=true;for(const auto& v:list){if(!first)out<<',';first=false;out<<v;}out<<']';
}
std::string folded(std::string s) {for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c+('a'-'A'));return s;}
double frame_number(const std::string& s) {
    if(s.empty()||std::isspace(static_cast<unsigned char>(s.front())))throw odh::FormatError("invalid frame number");
    std::size_t end=0;const double n=std::stod(s,&end);
    if(end!=s.size()||!std::isfinite(n))throw odh::FormatError("frame must be finite with no trailing characters");return n;
}
void raw_vertices(std::ostream& out,const std::vector<odh::MeshVertex>& vertices) {
    out<<'[';for(std::size_t i=0;i<vertices.size();++i){if(i)out<<',';const auto& v=vertices[i];
        out<<"{\"position_bits\":";values(out,v.position_bits);out<<",\"normal_bits\":";values(out,v.normal_bits);
        out<<",\"texcoord_bits\":";values(out,v.texcoord_bits);out<<'}';}out<<']';
}
void write_obj(const odh::SkinnedMesh& mesh,const std::string& path) {
    std::ostringstream out;out<<std::setprecision(std::numeric_limits<double>::max_digits10)
        <<"# OpenDollhouse CPU skinned mesh reference profile\n# Texture decoding and renderer are not implemented\n";
    for(const auto& v:mesh.vertices)out<<"v "<<v.position[0]<<' '<<v.position[1]<<' '<<v.position[2]<<'\n';
    for(const auto& v:mesh.vertices)out<<"vt "<<v.texcoord[0]<<' '<<v.texcoord[1]<<'\n';
    for(const auto& v:mesh.vertices)out<<"vn "<<v.normal[0]<<' '<<v.normal[1]<<' '<<v.normal[2]<<'\n';
    for(const auto& f:mesh.faces){out<<'f';for(const auto index:f){const auto n=std::uint64_t{index}+1;out<<' '<<n<<'/'<<n<<'/'<<n;}out<<'\n';}
    std::ofstream file(path,std::ios::binary|std::ios::trunc);if(!file)throw odh::FormatError("cannot open OBJ output");
    file<<out.str();file.close();if(!file)throw odh::FormatError("cannot write OBJ output");
}
}
int main(int argc,char** argv) {
    try {
        if(argc<7)throw odh::FormatError("Usage: odh-mesh FAR APPEARANCE_ENTRY APPEARANCE_NAME BMF_ENTRY SKELETON_ENTRY SKELETON_NAME [--animation BCF_ENTRY CLIP CFP_ENTRY] [--frame NUMBER] [--obj OUTPUT]");
        std::string bcf_entry,clip,cfp_entry,obj_path;bool has_animation=false,has_frame=false,has_obj=false;double frame=0;
        for(int i=7;i<argc;){const std::string option=argv[i++];
            if(option=="--animation"){if(has_animation||argc-i<3)throw odh::FormatError("duplicate or incomplete --animation");has_animation=true;bcf_entry=argv[i++];clip=argv[i++];cfp_entry=argv[i++];}
            else if(option=="--frame"){if(has_frame||i==argc)throw odh::FormatError("duplicate or incomplete --frame");has_frame=true;frame=frame_number(argv[i++]);}
            else if(option=="--obj"){if(has_obj||i==argc)throw odh::FormatError("duplicate or incomplete --obj");has_obj=true;obj_path=argv[i++];}
            else throw odh::FormatError("unknown option");}
        if(has_frame&&!has_animation)throw odh::FormatError("--frame requires --animation");
        std::optional<odh::AppearanceCollection> appearances;
        with_far_entry(argv[1],argv[2],[&](const auto& v){appearances=odh::read_bcf_appearances(v);});
        const odh::Appearance* appearance=nullptr;for(const auto& a:appearances->appearances)if(a.name==argv[3]){
            if(appearance)throw odh::FormatError("ambiguous appearance name");appearance=&a;}
        if(!appearance)throw odh::FormatError("exact appearance name not found");
        std::optional<std::size_t> selected_binding;
        for(std::size_t i=0;i<appearance->bindings.size();++i)if(folded(appearance->bindings[i].mesh_name+".bmf")==folded(argv[4])){
            if(selected_binding)throw odh::FormatError("ambiguous appearance mesh binding");selected_binding=i;}
        if(!selected_binding)throw odh::FormatError("appearance has no binding for selected BMF entry");
        std::optional<odh::Mesh> raw;with_far_entry(argv[1],argv[4],[&](const auto& v){raw=odh::read_bmf(v);});
        const auto& binding=appearance->bindings[*selected_binding];
        if(folded(raw->skin_name)!=folded(binding.mesh_name))throw odh::FormatError("BMF skin name does not match appearance binding");
        std::optional<odh::SkeletonCollection> skeletons;with_far_entry(argv[1],argv[5],[&](const auto& v){skeletons=odh::read_bcf_skeletons(v);});
        const odh::Skeleton* skeleton=nullptr;for(const auto& s:skeletons->skeletons)if(s.name==argv[6]){
            if(skeleton)throw odh::FormatError("ambiguous skeleton name");skeleton=&s;}
        if(!skeleton)throw odh::FormatError("exact skeleton name not found");
        std::optional<odh::AnimationCollection> animations;const odh::AnimationHeader* animation=nullptr;std::optional<odh::CfpPlanes> cfp;
        if(has_animation){with_far_entry(argv[1],bcf_entry,[&](const auto& v){animations=odh::read_bcf_animations(v);});
            for(const auto& a:animations->animations)if(a.name==clip){if(animation)throw odh::FormatError("ambiguous animation name");animation=&a;}
            if(!animation)throw odh::FormatError("exact animation name not found");
            if(folded(cfp_entry)!=folded(animation->xskill+".cfp"))throw odh::FormatError("CFP entry must match selected animation xskill");
            with_far_entry(argv[1],cfp_entry,[&](const auto& v){cfp=odh::read_cfp(v,animation->translations,animation->rotations);});}
        const auto pose=odh::sample_pose(*skeleton,animation,cfp?&*cfp:nullptr,frame);
        std::size_t binding_bones=0;for(const auto& b:pose.bones)if(folded(b.name)==folded(binding.bone))++binding_bones;
        if(binding_bones!=1)throw odh::FormatError("appearance binding bone must resolve uniquely in pose");
        const auto mesh=odh::skin_mesh(*raw,pose);
        std::ostringstream out;out<<std::setprecision(std::numeric_limits<double>::max_digits10)
            <<"{\"evidence\":\"ODH_CPU_skinned_mesh_reference_profile\",\"renderer\":\"none\",\"textures_decoded\":false,\"sources\":{\"far\":"<<json_bytes(argv[1])
            <<",\"appearance_entry\":"<<json_bytes(argv[2])<<",\"appearance_name\":"<<json_bytes(argv[3])<<",\"bmf_entry\":"<<json_bytes(argv[4])
            <<",\"skeleton_entry\":"<<json_bytes(argv[5])<<",\"skeleton_name\":"<<json_bytes(argv[6]);
        if(has_animation)out<<",\"animation_entry\":"<<json_bytes(bcf_entry)<<",\"clip\":"<<json_bytes(clip)<<",\"cfp_entry\":"<<json_bytes(cfp_entry);
        out<<"},\"appearance\":{\"name\":"<<json_bytes(appearance->name)<<",\"type\":"<<appearance->type<<",\"zero\":"<<appearance->zero<<",\"selected_binding\":"<<*selected_binding<<",\"bindings\":[";
        for(std::size_t i=0;i<appearance->bindings.size();++i){if(i)out<<',';const auto& b=appearance->bindings[i];out<<"{\"bone\":"<<json_bytes(b.bone)<<",\"mesh_name\":"<<json_bytes(b.mesh_name)<<",\"censor_flags\":"<<b.censor_flags<<",\"zero\":"<<b.zero<<'}';}
        out<<"]},\"pose\":{\"skeleton\":"<<json_bytes(pose.skeleton)<<",\"animation\":"<<json_bytes(pose.animation)<<",\"requested_frame\":"<<pose.requested_frame<<",\"frame\":"<<pose.frame<<",\"fraction\":"<<pose.fraction<<",\"completed\":"<<(pose.completed?"true":"false")<<",\"bones\":[";
        for(std::size_t i=0;i<pose.bones.size();++i){if(i)out<<',';out<<"{\"name\":"<<json_bytes(pose.bones[i].name)<<",\"absolute_matrix\":";values(out,pose.bones[i].absolute_matrix);out<<'}';}
        out<<"],\"missing_motion_bones\":[";for(std::size_t i=0;i<pose.missing_motion_bones.size();++i){if(i)out<<',';out<<json_bytes(pose.missing_motion_bones[i]);}
        out<<"]},\"raw_mesh\":{\"skin_name\":"<<json_bytes(raw->skin_name)<<",\"texture_name\":"<<json_bytes(raw->texture_name)<<",\"bones\":[";
        for(std::size_t i=0;i<raw->bones.size();++i){if(i)out<<',';out<<json_bytes(raw->bones[i]);}out<<"],\"faces\":[";
        for(std::size_t i=0;i<raw->faces.size();++i){if(i)out<<',';values(out,raw->faces[i]);}out<<"],\"bindings\":[";
        for(std::size_t i=0;i<raw->bindings.size();++i){if(i)out<<',';const auto& b=raw->bindings[i];out<<"{\"bone_index\":"<<b.bone_index<<",\"first_real\":"<<b.first_real<<",\"real_count\":"<<b.real_count<<",\"first_blend\":"<<b.first_blend<<",\"blend_count\":"<<b.blend_count<<'}';}
        out<<"],\"real_vertices\":";raw_vertices(out,raw->real_vertices);out<<",\"blend_vertices\":";raw_vertices(out,raw->blend_vertices);out<<",\"blend_data\":[";
        for(std::size_t i=0;i<raw->blend_data.size();++i){if(i)out<<',';out<<"{\"weight\":"<<raw->blend_data[i].weight<<",\"other_vertex\":"<<raw->blend_data[i].other_vertex<<'}';}
        out<<"],\"vertex_count2\":"<<raw->vertex_count2<<",\"remaining_bytes\":"<<raw->remaining_bytes<<"},\"skinned_mesh\":{\"vertices\":[";
        for(std::size_t i=0;i<mesh.vertices.size();++i){if(i)out<<',';const auto& v=mesh.vertices[i];out<<"{\"position\":";values(out,v.position);out<<",\"normal\":";values(out,v.normal);out<<",\"texcoord\":";values(out,v.texcoord);out<<",\"primary_bone\":"<<v.primary_bone<<",\"blend_bone\":";if(v.blend_bone)out<<*v.blend_bone;else out<<"null";out<<",\"blend_weight\":"<<v.blend_weight<<'}';}
        out<<"],\"faces\":[";for(std::size_t i=0;i<mesh.faces.size();++i){if(i)out<<',';values(out,mesh.faces[i]);}out<<"],\"clamped_binding_indices\":";values(out,mesh.clamped_binding_indices);out<<"}}\n";
        if(has_obj)write_obj(mesh,obj_path);std::cout<<out.str();return 0;
    }catch(const std::exception& e){std::cerr<<"odh-mesh: "<<e.what()<<'\n';return 2;}
}
