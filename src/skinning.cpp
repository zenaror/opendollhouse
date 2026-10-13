#include "odh/skinning.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace odh {
namespace {
constexpr std::size_t max_bones=4096,max_vertices=65536,max_faces=131072,max_text=4194304;
constexpr auto unbound=std::numeric_limits<std::uint32_t>::max();
std::string folded(std::string name) {
    for(auto& c:name)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return name;
}
float real(std::uint32_t bits) {
    float value;static_assert(sizeof(value)==sizeof(bits));std::memcpy(&value,&bits,sizeof(value));
    if(!std::isfinite(value))throw FormatError("skinning vertex component is not finite");
    return value;
}
template<std::size_t N> void finite(const std::array<float,N>& values) {
    for(const auto value:values)if(!std::isfinite(value))throw FormatError("skinning transform is not finite or overflowed");
}
std::array<float,3> vector(const std::array<std::uint32_t,3>& bits) {
    return {-real(bits[0]),real(bits[1]),real(bits[2])};
}
std::array<float,3> transform(const std::array<float,12>& matrix,const std::array<float,3>& v,bool position) {
    std::array<float,3> result{};
    for(std::size_t row=0;row<3;++row){
        result[row]=matrix[row*4]*v[0]+matrix[row*4+1]*v[1]+matrix[row*4+2]*v[2];
        if(position)result[row]+=matrix[row*4+3];
    }
    finite(result);return result;
}
void assign(std::vector<std::uint32_t>& owners,std::int32_t first,std::int32_t count,std::uint32_t bone) {
    if(count<0)throw FormatError("skinning binding range count is negative");
    // Prepare never dereferences the first index of a zero-count range.
    if(count==0)return;
    if(first<0||static_cast<std::uint64_t>(first)+static_cast<std::uint32_t>(count)>owners.size())
        throw FormatError("skinning binding range is outside vertices");
    const auto end=static_cast<std::size_t>(first)+static_cast<std::size_t>(count);
    for(auto i=static_cast<std::size_t>(first);i<end;++i){
        if(owners[i]!=unbound)throw FormatError("skinning overlapping bindings are unsupported");
        owners[i]=bone;
    }
}
void covered(const std::vector<std::uint32_t>& owners) {
    for(const auto bone:owners)if(bone==unbound)throw FormatError("skinning bindings do not cover every vertex");
}
void validate_vertex(const MeshVertex& v) {
    for(const auto bits:v.position_bits)real(bits);
    for(const auto bits:v.normal_bits)real(bits);
    for(const auto bits:v.texcoord_bits)real(bits);
}
}
SkinnedMesh skin_mesh(const Mesh& mesh,const SkeletonPose& pose) {
    if(mesh.bones.empty()||mesh.bones.size()>max_bones||mesh.bindings.size()>max_bones||
       pose.bones.empty()||pose.bones.size()>max_bones||pose.missing_motion_bones.size()>max_bones||
       mesh.real_vertices.size()>max_vertices||mesh.blend_vertices.size()>max_vertices||
       mesh.blend_data.size()>max_vertices||mesh.faces.size()>max_faces)
        throw FormatError("skinning input budget exceeded");
    if(mesh.blend_vertices.size()!=mesh.blend_data.size())throw FormatError("skinning blend data count differs from blend vertices");
    std::size_t text=0;
    const auto charge=[&](const std::string& value){
        if(value.size()>max_text-text)throw FormatError("skinning text budget exceeded");
        text+=value.size();
    };
    charge(mesh.skin_name);charge(mesh.texture_name);charge(pose.skeleton);charge(pose.animation);
    for(const auto& name:mesh.bones)charge(name);
    for(const auto& bone:pose.bones)charge(bone.name);
    for(const auto& name:pose.missing_motion_bones)charge(name);
    if(!std::isfinite(pose.requested_frame)||!std::isfinite(pose.fraction))throw FormatError("skinning pose metadata is not finite");
    std::unordered_map<std::string,std::uint32_t> bone_indices;bone_indices.reserve(pose.bones.size());
    for(std::size_t i=0;i<pose.bones.size();++i){const auto& bone=pose.bones[i];
        if(bone.name.empty()||!bone_indices.emplace(folded(bone.name),static_cast<std::uint32_t>(i)).second)
            throw FormatError("skinning pose names are empty or ambiguous after ASCII folding");
        finite(bone.translation);finite(bone.rotation);finite(bone.absolute_matrix);finite(bone.absolute_position);
    }
    std::vector<std::uint32_t> mapped;mapped.reserve(mesh.bones.size());
    for(const auto& name:mesh.bones){const auto it=bone_indices.find(folded(name));
        if(it==bone_indices.end())throw FormatError("skinning mesh bone is missing from pose");
        mapped.push_back(it->second);
    }
    for(const auto& v:mesh.real_vertices)validate_vertex(v);
    for(const auto& v:mesh.blend_vertices)validate_vertex(v);
    for(const auto& face:mesh.faces)for(const auto index:face)
        if(index<0||static_cast<std::size_t>(index)>=mesh.real_vertices.size())throw FormatError("skinning face index is outside real vertices");
    for(const auto& blend:mesh.blend_data){
        if(blend.weight<0||blend.weight>32768)throw FormatError("skinning blend weight outside supported [0,32768] profile");
        if(blend.other_vertex<0||static_cast<std::size_t>(blend.other_vertex)>=mesh.real_vertices.size())
            throw FormatError("skinning blend OtherVertex is outside real vertices");
    }
    SkinnedMesh result{mesh.skin_name,mesh.texture_name,{},{},{}};
    std::vector<std::uint32_t> primary(mesh.real_vertices.size(),unbound),secondary(mesh.blend_vertices.size(),unbound);
    for(std::size_t i=0;i<mesh.bindings.size();++i){const auto& binding=mesh.bindings[i];
        if(binding.bone_index<0)throw FormatError("skinning binding bone index is negative");
        auto index=static_cast<std::size_t>(binding.bone_index);
        if(index>=mapped.size()){index=mapped.size()-1;result.clamped_binding_indices.push_back(i);}
        assign(primary,binding.first_real,binding.real_count,mapped[index]);
        assign(secondary,binding.first_blend,binding.blend_count,mapped[index]);
    }
    covered(primary);covered(secondary);
    // Prepare overwrites metadata for repeated OtherVertex records in order.
    // Preserve the winning blend vertex, including an explicitly zero weight.
    std::vector<std::size_t> blends(mesh.real_vertices.size(),mesh.blend_vertices.size());
    for(std::size_t i=0;i<mesh.blend_data.size();++i)blends[static_cast<std::size_t>(mesh.blend_data[i].other_vertex)]=i;
    result.vertices.reserve(mesh.real_vertices.size());result.faces.reserve(mesh.faces.size());
    for(std::size_t i=0;i<mesh.real_vertices.size();++i){const auto& v=mesh.real_vertices[i];
        const auto& matrix=pose.bones[primary[i]].absolute_matrix;
        auto position=transform(matrix,vector(v.position_bits),true);
        auto normal=vector(v.normal_bits);
        if(normal==std::array<float,3>{0,0,0})normal={0,1,0};
        normal=transform(matrix,normal,false);
        std::optional<std::uint32_t> blend_bone;float weight=0;
        if(blends[i]<mesh.blend_vertices.size()){
            const auto b=blends[i];blend_bone=secondary[b];weight=static_cast<float>(mesh.blend_data[b].weight)/32768.0f;
            const auto other=transform(pose.bones[*blend_bone].absolute_matrix,vector(mesh.blend_vertices[b].position_bits),true);
            for(std::size_t axis=0;axis<3;++axis)position[axis]=(1-weight)*position[axis]+weight*other[axis];
            finite(position);
        }
        result.vertices.push_back({position,normal,{real(v.texcoord_bits[0]),real(v.texcoord_bits[1])},primary[i],blend_bone,weight});
    }
    for(const auto& face:mesh.faces)result.faces.push_back({static_cast<std::uint32_t>(face[0]),static_cast<std::uint32_t>(face[1]),static_cast<std::uint32_t>(face[2])});
    return result;
}
}
