#include "odh/skinning.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void close(float actual,float expected,const char* why){require(std::abs(actual-expected)<0.00002f,why);}
void close3(const std::array<float,3>& actual,const std::array<float,3>& expected,const char* why){for(std::size_t i=0;i<3;++i)close(actual[i],expected[i],why);}
template<class F> void reject(F&& f,const char* why){bool rejected=false;try{f();}catch(const odh::FormatError&){rejected=true;}require(rejected,why);}
std::uint32_t bits(float f){std::uint32_t result;std::memcpy(&result,&f,sizeof(result));return result;}
odh::MeshVertex vertex(std::array<float,3> position,std::array<float,3> normal={2,3,4},std::array<float,2> uv={0.25f,0.75f}){
    odh::MeshVertex result{};
    for(std::size_t i=0;i<3;++i){result.position_bits[i]=bits(position[i]);result.normal_bits[i]=bits(normal[i]);}
    for(std::size_t i=0;i<2;++i)result.texcoord_bits[i]=bits(uv[i]);return result;
}
odh::SkeletonPose pose(){
    odh::SkeletonPose result{};result.skeleton="synthetic";
    result.bones={{"PRIMARY",-1,{10,20,30},{0,0,0,1},{0,-1,0,10,1,0,0,20,0,0,1,30},{10,20,30}},
                  {"SECONDARY",0,{100,200,300},{0,0,0,1},{1,0,0,100,0,1,0,200,0,0,1,300},{100,200,300}}};
    return result;
}
odh::Mesh mesh(){
    odh::Mesh result{};result.skin_name="skin";result.texture_name="texture";result.bones={"primary","SeCoNdArY"};
    result.real_vertices={vertex({1,2,3})};result.faces={{{0,0,0}}};
    result.bindings={{0,0,1,-1,0}};result.vertex_count2=999;return result;
}
odh::Mesh blended(std::int32_t weight=16384){
    auto result=mesh();result.blend_vertices={vertex({4,5,6},{99,98,97})};result.blend_data={{weight,0}};
    result.bindings.push_back({1,-1,0,0,1});return result;
}
}
int main(){try{
    const auto p=pose();const auto m=mesh();
    {const auto s=odh::skin_mesh(m,p);
     require(s.skin_name=="skin"&&s.texture_name=="texture","mesh names retained");
     require(s.vertices.size()==1&&s.faces==std::vector<std::array<std::uint32_t,3>>{{{0,0,0}}},"real vertices and faces retained");
     close3(s.vertices[0].position,{8,19,33},"bone-local handedness then rotation and translation");
     close3(s.vertices[0].normal,{-3,-2,4},"normal handedness and rotation without translation or normalization");
     require(s.vertices[0].texcoord==std::array<float,2>{0.25f,0.75f},"UV retained");
     require(s.vertices[0].primary_bone==0&&!s.vertices[0].blend_bone&&s.vertices[0].blend_weight==0,"unblended metadata");
     require(s.clamped_binding_indices.empty(),"valid bone table indices do not report clamping");}
    {auto h=mesh();h.real_vertices[0]=vertex({1,2,3},{0,0,0});const auto s=odh::skin_mesh(h,p);
     close3(s.vertices[0].normal,{-1,0,0},"zero primary normal defaults upward before transform");}
    for(const auto weight:{0,16384,32768}){
        const auto s=odh::skin_mesh(blended(weight),p);const auto fraction=static_cast<float>(weight)/32768;
        close3(s.vertices[0].position,{8+(96-8)*fraction,19+(205-19)*fraction,33+(306-33)*fraction},"two bone blend endpoints and midpoint");
        close3(s.vertices[0].normal,{-3,-2,4},"normal remains primary only at every weight");
        require(s.vertices[0].blend_bone==1&&s.vertices[0].blend_weight==fraction,"blend bone metadata and fixed point weight");
    }
    {auto h=blended();h.blend_vertices.push_back(vertex({7,8,9}));h.blend_data.push_back({32768,0});h.bindings[1].blend_count=2;
     const auto s=odh::skin_mesh(h,p);close3(s.vertices[0].position,{93,208,309},"duplicate OtherVertex keeps last source record");
     h.blend_data[1].weight=0;const auto zero=odh::skin_mesh(h,p);close3(zero.vertices[0].position,{8,19,33},"last zero weight overwrites earlier blend");
     require(zero.vertices[0].blend_bone==1&&zero.vertices[0].blend_weight==0,"explicit zero blend retains blend bone");}
    {auto h=mesh();h.bindings[0].bone_index=2147483647;const auto s=odh::skin_mesh(h,p);
     close3(s.vertices[0].position,{99,202,303},"large nonnegative bone index clamps to final mesh bone");
     require(s.vertices[0].primary_bone==1&&s.clamped_binding_indices==std::vector<std::size_t>{0},"clamp reports binding index");}
    {auto h=blended();h.bindings[1].bone_index=2;const auto s=odh::skin_mesh(h,p);
     require(s.clamped_binding_indices==std::vector<std::size_t>{1},"blend-only binding clamp is reported");}
    {auto h=mesh();auto q=p;std::swap(q.bones[0],q.bones[1]);const auto s=odh::skin_mesh(h,q);
     close3(s.vertices[0].position,{8,19,33},"name resolution follows pose order rather than mesh index");require(s.vertices[0].primary_bone==1,"primary index addresses pose table");}
    {auto h=mesh();auto q=p;h.bones[0]="\xc3\x84";q.bones[0].name="\xc3\x84";
     require(odh::skin_mesh(h,q).vertices[0].primary_bone==0,"non-ASCII name bytes preserved");
     q.bones[0].name="\xc3\xa4";reject([&]{odh::skin_mesh(h,q);},"ASCII folding does not guess non-ASCII case");}
    {auto h=mesh();h.real_vertices[0].texcoord_bits={bits(-2),bits(3)};
     require(odh::skin_mesh(h,p).vertices[0].texcoord==std::array<float,2>{-2,3},"finite UV is not clamped");}
    {auto h=mesh();auto q=p;q.bones[0].absolute_matrix[0]=2;q.bones[0].absolute_matrix[1]=0;q.bones[0].absolute_matrix[4]=0;q.bones[0].absolute_matrix[5]=3;
     close3(odh::skin_mesh(h,q).vertices[0].normal,{-4,9,4},"normal uses matrix directly without inverse transpose");}
    {auto h=mesh();h.bindings.push_back({0,1,0,-1,0});require(odh::skin_mesh(h,p).vertices.size()==1,"empty one-past-end real range allowed");}
    {auto h=mesh();h.faces.clear();h.real_vertices.clear();h.bindings={{0,-1,0,-1,0}};
     require(odh::skin_mesh(h,p).vertices.empty(),"empty mesh with zero binding ranges supported");}
    {auto h=blended();const auto before=h.real_vertices[0].position_bits;const auto matrix=p.bones[0].absolute_matrix;
     odh::skin_mesh(h,p);require(h.real_vertices[0].position_bits==before&&p.bones[0].absolute_matrix==matrix,"skinning leaves inputs unchanged");
     h.blend_data[0].weight=-1;reject([&]{odh::skin_mesh(h,p);},"invalid weight rejects");
     require(h.real_vertices[0].position_bits==before&&p.bones[0].absolute_matrix==matrix,"rejection leaves inputs unchanged");}
    {auto h=mesh();h.bones[0]="missing";reject([&]{odh::skin_mesh(h,p);},"missing mesh bone rejected");}
    {auto q=p;q.bones[1].name="primary";reject([&]{odh::skin_mesh(m,q);},"ASCII-fold ambiguous pose names rejected");}
    {auto q=p;q.bones[1].name="";reject([&]{odh::skin_mesh(m,q);},"empty pose name rejected");}
    {auto h=mesh();h.bones.clear();reject([&]{odh::skin_mesh(h,p);},"empty mesh bone table rejected");}
    {auto q=p;q.bones.clear();reject([&]{odh::skin_mesh(m,q);},"empty pose rejected");}
    {auto h=mesh();h.bindings[0].bone_index=-1;reject([&]{odh::skin_mesh(h,p);},"negative bone index rejected");}
    {auto h=mesh();h.bindings[0].first_real=-1;reject([&]{odh::skin_mesh(h,p);},"active negative range start rejected");}
    {auto h=mesh();h.bindings[0].first_real=1;reject([&]{odh::skin_mesh(h,p);},"active real range out of bounds rejected");}
    {auto h=blended();h.bindings[1].blend_count=2;reject([&]{odh::skin_mesh(h,p);},"active blend range out of bounds rejected");}
    {auto h=mesh();h.bindings[0].real_count=-1;reject([&]{odh::skin_mesh(h,p);},"negative range count rejected");}
    {auto h=mesh();h.bindings[0].first_real=2147483647;h.bindings[0].real_count=2147483647;reject([&]{odh::skin_mesh(h,p);},"range sum safely widened");}
    {auto h=mesh();h.bindings[0].first_blend=-2;require(odh::skin_mesh(h,p).vertices.size()==1,"empty range ignores arbitrary negative first index");}
    {auto h=mesh();h.bindings[0].first_blend=2147483647;require(odh::skin_mesh(h,p).vertices.size()==1,"empty range ignores arbitrarily large first index");}
    {auto h=mesh();h.bindings.push_back(h.bindings[0]);reject([&]{odh::skin_mesh(h,p);},"overlapping real ownership explicitly unsupported");}
    {auto h=blended();h.bindings.push_back(h.bindings[1]);reject([&]{odh::skin_mesh(h,p);},"overlapping blend ownership explicitly unsupported");}
    {auto h=mesh();h.bindings.clear();reject([&]{odh::skin_mesh(h,p);},"uncovered real vertex rejected");}
    {auto h=blended();h.bindings.pop_back();reject([&]{odh::skin_mesh(h,p);},"uncovered blend vertex rejected");}
    {auto h=blended();h.blend_data.clear();reject([&]{odh::skin_mesh(h,p);},"blend vertices and data require equal count");}
    for(const auto index:{-1,1}){
        auto h=mesh();h.faces[0][1]=index;reject([&]{odh::skin_mesh(h,p);},"face outside real vertex table rejected");
        h=blended();h.blend_data[0].other_vertex=index;reject([&]{odh::skin_mesh(h,p);},"OtherVertex outside real table rejected");
    }
    {auto h=blended(32769);reject([&]{odh::skin_mesh(h,p);},"weight above profile range rejected");}
    const auto nan=bits(std::numeric_limits<float>::quiet_NaN());
    {auto h=mesh();h.real_vertices[0].position_bits[0]=nan;reject([&]{odh::skin_mesh(h,p);},"nonfinite real position rejected");}
    {auto h=mesh();h.real_vertices[0].normal_bits[1]=nan;reject([&]{odh::skin_mesh(h,p);},"nonfinite primary normal rejected");}
    {auto h=mesh();h.real_vertices[0].texcoord_bits[1]=nan;reject([&]{odh::skin_mesh(h,p);},"nonfinite UV rejected");}
    {auto h=blended();h.blend_vertices[0].position_bits[2]=nan;reject([&]{odh::skin_mesh(h,p);},"nonfinite blend position rejected");}
    {auto h=blended();h.blend_vertices[0].normal_bits[2]=nan;reject([&]{odh::skin_mesh(h,p);},"unused blend normal still structurally finite");}
    {auto q=p;q.bones[1].absolute_matrix[4]=std::numeric_limits<float>::infinity();reject([&]{odh::skin_mesh(m,q);},"unused pose matrix must be finite");}
    {auto q=p;q.bones[1].translation[1]=std::numeric_limits<float>::quiet_NaN();reject([&]{odh::skin_mesh(m,q);},"unused pose local transform must be finite");}
    {auto q=p;q.bones[1].rotation[2]=std::numeric_limits<float>::quiet_NaN();reject([&]{odh::skin_mesh(m,q);},"unused pose quaternion must be finite");}
    {auto q=p;q.bones[1].absolute_position[1]=std::numeric_limits<float>::quiet_NaN();reject([&]{odh::skin_mesh(m,q);},"unused pose position must be finite");}
    {auto q=p;q.requested_frame=std::numeric_limits<double>::quiet_NaN();reject([&]{odh::skin_mesh(m,q);},"nonfinite pose frame rejected");}
    {auto q=p;q.fraction=std::numeric_limits<float>::infinity();reject([&]{odh::skin_mesh(m,q);},"nonfinite pose fraction rejected");}
    {auto q=p;q.bones[0].absolute_matrix[1]=std::numeric_limits<float>::max();reject([&]{odh::skin_mesh(m,q);},"finite matrix and vertex multiplication overflow rejected");}
    {auto h=mesh();h.bones.resize(4097);reject([&]{odh::skin_mesh(h,p);},"mesh bone budget rejected before lookup");}
    {auto h=mesh();h.bindings.resize(4097);reject([&]{odh::skin_mesh(h,p);},"binding budget rejected");}
    {auto q=p;q.bones.resize(4097);reject([&]{odh::skin_mesh(m,q);},"pose bone budget rejected");}
    {auto h=mesh();h.real_vertices.resize(65537);reject([&]{odh::skin_mesh(h,p);},"real vertex budget rejected");}
    {auto h=mesh();h.blend_vertices.resize(65537);reject([&]{odh::skin_mesh(h,p);},"blend vertex budget rejected");}
    {auto h=mesh();h.blend_data.resize(65537);reject([&]{odh::skin_mesh(h,p);},"blend data budget rejected");}
    {auto h=mesh();h.faces.resize(131073);reject([&]{odh::skin_mesh(h,p);},"face budget rejected");}
    {auto h=mesh();h.skin_name.resize(4194305,'x');reject([&]{odh::skin_mesh(h,p);},"text budget rejected");}
    {auto h=mesh();auto q=p;h.skin_name.resize(2097152,'x');q.bones[0].name.resize(2097152,'x');reject([&]{odh::skin_mesh(h,q);},"aggregate text budget covers mesh and pose");}
    std::cout<<checks<<" skinning checks passed (synthetic CPU profile only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Skinning test failed: "<<e.what()<<'\n';return 1;}}
