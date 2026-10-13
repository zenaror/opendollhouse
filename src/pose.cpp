#include "odh/pose.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace odh {
namespace {
constexpr std::size_t max_bones=4096,max_motions=4096,max_samples=1048576,max_text=4194304;
float real(std::uint32_t bits) {
    float value;static_assert(sizeof(value)==sizeof(bits));std::memcpy(&value,&bits,sizeof(value));
    if(!std::isfinite(value))throw FormatError("pose transform is not finite");
    return value;
}
template<std::size_t N> void finite(const std::array<float,N>& values) {
    for(const auto value:values)if(!std::isfinite(value))throw FormatError("pose transform arithmetic overflow");
}
std::array<float,3> translation(const std::array<std::uint32_t,3>& bits) {
    return {-real(bits[0]),real(bits[1]),real(bits[2])};
}
std::array<float,4> rotation(const std::array<std::uint32_t,4>& bits) {
    return {real(bits[0]),-real(bits[1]),-real(bits[2]),-real(bits[3])};
}
std::array<float,3> translation(const CfpPlanes& c,std::size_t i) {
    return translation({c.bits[0][i],c.bits[1][i],c.bits[2][i]});
}
std::array<float,4> rotation(const CfpPlanes& c,std::size_t i) {
    return rotation({c.bits[3][i],c.bits[4][i],c.bits[5][i],c.bits[6][i]});
}
std::array<float,4> slerp(const std::array<float,4>& a,const std::array<float,4>& b,float fraction) {
    // XNA-compatible shortest-arc weights, with no normalization. Preserve the
    // sampled raw endpoint at an integer frame even for non-unit quaternions.
    if(fraction==0)return a;
    auto dot=((a[0]*b[0]+a[1]*b[1])+a[2]*b[2])+a[3]*b[3];
    if(!std::isfinite(dot))throw FormatError("pose quaternion dot product overflow");
    const bool opposite=dot<0;if(opposite)dot=-dot;
    float wa,wb;
    if(dot>0.999999f){wa=1-fraction;wb=fraction;}
    else {
        const auto angle=std::acos(dot);
        const auto reciprocal=static_cast<float>(1.0/std::sin(static_cast<double>(angle)));
        wa=std::sin((1-fraction)*angle)*reciprocal;
        wb=std::sin(fraction*angle)*reciprocal;
    }
    if(opposite)wb=-wb;
    std::array<float,4> result{};
    for(std::size_t i=0;i<4;++i)result[i]=wa*a[i]+wb*b[i];
    finite(result);return result;
}
std::array<float,12> local_matrix(const PoseBone& bone) {
    const auto& q=bone.rotation;
    const auto xx=q[0]*q[0],yy=q[1]*q[1],zz=q[2]*q[2];
    const auto xy=q[0]*q[1],xz=q[0]*q[2],yz=q[1]*q[2];
    const auto xw=q[0]*q[3],yw=q[1]*q[3],zw=q[2]*q[3];
    std::array<float,12> result{
        1-2*(yy+zz),2*(xy-zw),2*(xz+yw),bone.translation[0],
        2*(xy+zw),1-2*(zz+xx),2*(yz-xw),bone.translation[1],
        2*(xz-yw),2*(yz+xw),1-2*(yy+xx),bone.translation[2]};
    finite(result);return result;
}
std::array<float,12> compose(const std::array<float,12>& parent,const std::array<float,12>& local) {
    std::array<float,12> result{};
    for(std::size_t row=0;row<3;++row)for(std::size_t col=0;col<4;++col){
        result[row*4+col]=parent[row*4]*local[col]+parent[row*4+1]*local[4+col]+parent[row*4+2]*local[8+col];
        if(col==3)result[row*4+col]+=parent[row*4+3];
    }
    finite(result);return result;
}
void motion_range(std::uint32_t active,std::int32_t start,std::uint32_t frames,std::uint32_t count) {
    if(active>1)throw FormatError("pose motion channel flag must be0 or1");
    if(!active)return;
    if(!frames)throw FormatError("pose active motion has zero frames");
    if(start<0||static_cast<std::uint64_t>(start)+frames>count)throw FormatError("pose motion sample range outside CFP");
}
}
SkeletonPose sample_pose(const Skeleton& skeleton,const AnimationHeader* animation,const CfpPlanes* cfp,double requested) {
    if(!std::isfinite(requested)||requested<std::numeric_limits<std::int32_t>::min()||requested>std::numeric_limits<std::int32_t>::max())
        throw FormatError("pose requested frame outside finite signed range");
    if(bool(animation)!=bool(cfp))throw FormatError("pose animation and CFP must be supplied together");
    if(skeleton.bones.empty()||skeleton.bones.size()>max_bones)throw FormatError("pose skeleton bone budget exceeded");
    std::size_t text=0;
    const auto budget_text=[&](const std::string& value){
        if(value.size()>max_text-text)throw FormatError("pose text budget exceeded");
        text+=value.size();
    };
    budget_text(skeleton.name);
    std::unordered_map<std::string_view,std::size_t> indices;
    indices.reserve(skeleton.bones.size());
    for(std::size_t i=0;i<skeleton.bones.size();++i){const auto& name=skeleton.bones[i].name;budget_text(name);budget_text(skeleton.bones[i].parent);
        if(name.empty()||name=="NULL"||!indices.emplace(name,i).second)throw FormatError("pose bone names must be unique and nonempty, with NULL reserved");
    }
    SkeletonPose pose{skeleton.name,{},requested,0,0,false,{},{}};pose.bones.reserve(skeleton.bones.size());
    std::size_t roots=0;
    for(const auto& bone:skeleton.bones){
        std::int32_t parent=-1;
        if(bone.parent=="NULL")++roots;
        else {const auto it=indices.find(bone.parent);
            if(it==indices.end())throw FormatError("pose bone parent does not exist");
            parent=static_cast<std::int32_t>(it->second);}
        pose.bones.push_back({bone.name,parent,translation(bone.translation_bits),rotation(bone.rotation_bits),{}, {}});
    }
    if(roots!=1)throw FormatError("pose skeleton requires one NULL root");
    if(animation){
        if(animation->motions.size()>max_motions||3ULL*animation->translations+4ULL*animation->rotations>max_samples)
            throw FormatError("pose animation budget exceeded");
        budget_text(animation->name);pose.animation=animation->name;
        for(std::size_t p=0;p<cfp->bits.size();++p){
            if(cfp->bits[p].size()!=(p<3?animation->translations:animation->rotations))throw FormatError("pose CFP plane size differs from animation");
            for(const auto bits:cfp->bits[p])real(bits);
        }
        std::uint32_t frames=0;
        for(const auto& motion:animation->motions){
            if(motion.frames>static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))throw FormatError("pose motion frame range exceeded");
            motion_range(motion.has_translation,motion.first_translation,motion.frames,animation->translations);
            motion_range(motion.has_rotation,motion.first_rotation,motion.frames,animation->rotations);
            frames=std::max(frames,motion.frames);
        }
        if(requested<0){pose.completed=true;pose.frame=0;pose.fraction=0;}
        else {const auto whole=static_cast<std::uint32_t>(std::floor(requested));
            pose.frame=std::min(whole,frames);pose.fraction=static_cast<float>(requested-std::floor(requested));
            pose.completed=whole>frames||static_cast<std::uint64_t>(pose.frame)+1>frames;
            if(whole>frames)pose.fraction=0;
        }
        for(const auto& motion:animation->motions){
            const auto it=indices.find(motion.bone);
            if(it==indices.end()){budget_text(motion.bone);pose.missing_motion_bones.push_back(motion.bone);continue;}
            if(!motion.has_translation&&!motion.has_rotation)continue;
            auto& bone=pose.bones[it->second];
            const auto current=std::min(pose.frame,motion.frames-1);
            const auto next=static_cast<std::uint64_t>(pose.frame)+1>=motion.frames?current:current+1;
            if(motion.has_translation){
                const auto start=static_cast<std::size_t>(motion.first_translation);
                auto a=translation(*cfp,start+current);const auto b=translation(*cfp,start+next);
                if(pose.fraction!=0)for(std::size_t j=0;j<a.size();++j)a[j]=a[j]+(b[j]-a[j])*pose.fraction;
                finite(a);bone.translation=a;
            }
            if(motion.has_rotation){
                const auto start=static_cast<std::size_t>(motion.first_rotation);
                bone.rotation=slerp(rotation(*cfp,start+current),rotation(*cfp,start+next),pose.fraction);
            }
        }
    }
    // Walk parent chains iteratively so file order and deep trees cannot cause
    // stack recursion. Every chain must reach the unique root without a cycle.
    std::vector<std::uint8_t> state(pose.bones.size(),0);std::vector<std::size_t> path;path.reserve(pose.bones.size());
    for(std::size_t i=0;i<pose.bones.size();++i){
        auto index=static_cast<std::int32_t>(i);path.clear();
        while(index>=0&&state[static_cast<std::size_t>(index)]!=2){
            const auto current=static_cast<std::size_t>(index);
            if(state[current]==1)throw FormatError("pose skeleton parent cycle");
            state[current]=1;path.push_back(current);index=pose.bones[current].parent_index;
        }
        while(!path.empty()){
            const auto current=path.back();path.pop_back();auto& bone=pose.bones[current];
            const auto local=local_matrix(bone);
            bone.absolute_matrix=bone.parent_index<0?local:compose(pose.bones[static_cast<std::size_t>(bone.parent_index)].absolute_matrix,local);
            bone.absolute_position={bone.absolute_matrix[3],bone.absolute_matrix[7],bone.absolute_matrix[11]};state[current]=2;
        }
    }
    return pose;
}
}
