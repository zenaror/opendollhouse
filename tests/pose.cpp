#include "odh/pose.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void close(float actual,float expected,const char* why){require(std::abs(actual-expected)<0.00002f,why);}
template<class F> void reject(F&& f,const char* why){bool rejected=false;try{f();}catch(const odh::FormatError&){rejected=true;}require(rejected,why);}
std::uint32_t bits(float f){std::uint32_t n;std::memcpy(&n,&f,sizeof(n));return n;}
odh::SkeletonBone bone(const std::string& name,const std::string& parent,std::array<float,3> t={0,0,0},std::array<float,4> q={0,0,0,-1}){
    odh::SkeletonBone result{};result.name=name;result.parent=parent;
    for(std::size_t i=0;i<3;++i)result.translation_bits[i]=bits(t[i]);
    for(std::size_t i=0;i<4;++i)result.rotation_bits[i]=bits(q[i]);
    return result;
}
odh::Skeleton skeleton(){return {"fixture",{bone("ROOT","NULL"),bone("CHILD","ROOT",{1,2,3})}};}
struct Clip {
    odh::AnimationHeader animation{};odh::CfpPlanes cfp{};
    Clip(){animation.name="clip";}
    void translations(std::initializer_list<std::array<float,3>> values){
        animation.translations=static_cast<std::uint32_t>(values.size());
        for(const auto& v:values)for(std::size_t i=0;i<3;++i)cfp.bits[i].push_back(bits(v[i]));
    }
    void rotations(std::initializer_list<std::array<float,4>> values){
        animation.rotations=static_cast<std::uint32_t>(values.size());
        for(const auto& v:values)for(std::size_t i=0;i<4;++i)cfp.bits[i+3].push_back(bits(v[i]));
    }
    void motion(const std::string& name,std::uint32_t frames,std::int32_t t=-1,std::int32_t q=-1){
        odh::BoneMotion m{};m.bone=name;m.frames=frames;m.first_translation=t;m.first_rotation=q;
        m.has_translation=t>=0;m.has_rotation=q>=0;animation.motions.push_back(m);
    }
    odh::SkeletonPose sample(const odh::Skeleton& s,double frame=0){return odh::sample_pose(s,&animation,&cfp,frame);}
};
}
int main(){try{
    const auto s=skeleton();
    {const auto p=odh::sample_pose(s);
     require(p.skeleton=="fixture"&&p.animation.empty()&&p.frame==0&&p.fraction==0&&!p.completed,"bind pose metadata");
     require(p.bones.size()==2&&p.bones[0].parent_index==-1&&p.bones[1].parent_index==0,"parent indices in source order");
     close(p.bones[1].translation[0],-1,"translation X handedness conversion");
     close(p.bones[1].absolute_position[1],2,"rest Y translation");close(p.bones[1].absolute_position[2],3,"rest Z translation");
     close(p.bones[0].rotation[3],1,"quaternion W handedness conversion");}
    {auto h=skeleton();const auto v=std::sqrt(0.5f);
     h.bones={bone("TIP","CHILD",{0,2,0}),bone("CHILD","ROOT",{-1,0,0}),bone("ROOT","NULL",{-10,0,0},{0,0,-v,-v})};
     const auto p=odh::sample_pose(h);
     require(p.bones[0].parent_index==1&&p.bones[1].parent_index==2,"unsorted hierarchy retains file order");
     close(p.bones[1].absolute_position[0],10,"child translation rotates around root");
     close(p.bones[1].absolute_position[1],1,"parent rotation applies to child translation");
     close(p.bones[0].absolute_position[0],8,"grandchild parent matrix order");close(p.bones[0].absolute_position[1],1,"grandchild composed Y");
     close(p.bones[2].absolute_position[0],10,"local translation does not rotate itself");
     close(p.bones[0].absolute_matrix[1],-1,"column-vector matrix rotation convention");
     close(p.bones[0].absolute_matrix[3],p.bones[0].absolute_position[0],"3x4 row-major position slot");}
    {auto h=skeleton();h.bones[0].rotation_bits={bits(1),bits(2),bits(3),bits(4)};const auto p=odh::sample_pose(h);
     require(p.bones[0].rotation==std::array<float,4>{1,-2,-3,-4},"all quaternion signs and raw non-unit components preserved");
     close(p.bones[0].absolute_matrix[0],-25,"rest quaternion is not normalized");}
    {Clip c;c.translations({{0,0,0},{4,2,6}});c.motion("ROOT",2,0);const auto p=c.sample(s,0.5);
     require(p.animation=="clip"&&p.requested_frame==0.5&&p.frame==0&&p.fraction==0.5f&&!p.completed,"continuous frame split");
     close(p.bones[0].translation[0],-2,"linear translation between samples");close(p.bones[0].translation[1],1,"linear translation Y");
     require(p.bones[1].translation==std::array<float,3>{-1,2,3},"unmentioned rest channels preserved");
     close(p.bones[1].absolute_position[0],-3,"sample propagates into hierarchy");
     require(s.bones[0].translation_bits[0]==bits(0)&&c.cfp.bits[0][1]==bits(4),"pure sampling leaves source bits unchanged");}
    {Clip c;c.rotations({{0,0,0,-1},{0,0,-1,0}});c.motion("ROOT",2,-1,0);const auto p=c.sample(s,0.5);const auto v=std::sqrt(0.5f);
     close(p.bones[0].rotation[2],v,"slerp midpoint Z");close(p.bones[0].rotation[3],v,"slerp midpoint W");
     close(p.bones[1].absolute_position[0],-2,"interpolated rotation transforms child");close(p.bones[1].absolute_position[1],-1,"interpolated orientation Y");}
    {Clip c;c.rotations({{0,0,0,-1},{0,0,0,1}});c.motion("ROOT",2,-1,0);const auto p=c.sample(s,0.5);
     close(p.bones[0].rotation[3],1,"antipodal quaternion takes shortest arc without zero midpoint");close(p.bones[0].absolute_matrix[0],1,"antipodal midpoint finite identity");}
    {Clip c;c.rotations({{0,0,0,-1},{0,0,-0.0001f,-1}});c.motion("ROOT",2,-1,0);const auto p=c.sample(s,0.5);
     close(p.bones[0].rotation[2],0.00005f,"nearlinear quaternion interpolation");close(p.bones[0].rotation[3],1,"nearlinear branch retains raw W");}
    {Clip c;c.rotations({{1,2,3,4},{2,4,6,8}});c.motion("ROOT",2,-1,0);const auto p=c.sample(s);
     require(p.bones[0].rotation==std::array<float,4>{1,-2,-3,-4},"integer frame preserves raw non-unit rotation");
     const auto middle=c.sample(s,0.5);close(middle.bones[0].rotation[0],1.5f,"non-unit nearlinear interpolation stays unnormalized");}
    {Clip c;c.translations({{2,0,0},{7,0,0},{9,0,0}});c.motion("ROOT",3,0);
     const auto last=c.sample(s,2.5);close(last.bones[0].translation[0],-9,"last sample repeats next endpoint");
     require(last.frame==2&&last.fraction==0.5f&&!last.completed,"last available sample precedes clip completion count");
     const auto end=c.sample(s,3);require(end.frame==3&&end.completed,"NumFrames clamp count completes");close(end.bones[0].translation[0],-9,"completion uses last motion sample");
     const auto beyond=c.sample(s,9.25);require(beyond.frame==3&&beyond.fraction==0&&beyond.completed,"beyond clip clamps metadata");
     const auto negative=c.sample(s,-0.25);require(negative.frame==0&&negative.fraction==0&&negative.completed,"negative continuous frame clamps and completes");close(negative.bones[0].translation[0],-2,"negative frame uses first sample");}
    {Clip c;c.translations({{1,0,0},{4,0,0},{8,0,0}});c.motion("CHILD",1,0);c.motion("ROOT",2,1);
     const auto p=c.sample(s,1.5);close(p.bones[1].translation[0],-1,"shorter motion holds final sample");close(p.bones[0].translation[0],-8,"longer motion reaches own final sample");}
    {Clip c;c.translations({{3,0,0},{6,0,0}});c.motion("root",1,0);c.motion("ROOT",1,0);c.motion("ROOT",1,1);c.motion("root",1,0);
     const auto p=c.sample(s);require(p.missing_motion_bones==std::vector<std::string>{"root","root"},"exact case and missing-bone motion order");
     close(p.bones[0].translation[0],-6,"duplicate matching motions apply in order despite CanTranslate0");
     require(p.bones[0].rotation==std::array<float,4>{0,0,0,1},"translation-only motion preserves rest rotation");}
    {Clip c;c.motion("ROOT",0);const auto p=c.sample(s);require(p.completed,"empty inactive clip has zero frame count");}
    reject([&]{odh::sample_pose(s,nullptr,nullptr,std::numeric_limits<double>::quiet_NaN());},"NaN frame rejected");
    reject([&]{odh::sample_pose(s,nullptr,nullptr,std::numeric_limits<double>::infinity());},"infinite frame rejected");
    reject([&]{odh::sample_pose(s,nullptr,nullptr,2147483648.0);},"oversized double rejects before integer conversion");
    reject([&]{odh::sample_pose(s,nullptr,nullptr,-2147483649.0);},"undersized double rejects before integer conversion");
    {Clip c;reject([&]{odh::sample_pose(s,&c.animation);},"animation without CFP rejected");reject([&]{odh::sample_pose(s,nullptr,&c.cfp);},"CFP without animation rejected");}
    {auto h=skeleton();h.bones[1].name="ROOT";reject([&]{odh::sample_pose(h);},"duplicate bone names rejected");}
    {auto h=skeleton();h.bones[1].name="";reject([&]{odh::sample_pose(h);},"empty bone name rejected");}
    {auto h=skeleton();h.bones[0].name="NULL";reject([&]{odh::sample_pose(h);},"reserved NULL bone name rejected");}
    {auto h=skeleton();h.bones[1].parent="missing";reject([&]{odh::sample_pose(h);},"missing parent rejected");}
    {auto h=skeleton();h.bones[1].parent="NULL";reject([&]{odh::sample_pose(h);},"multiple roots rejected");}
    {auto h=skeleton();h.bones[0].parent="CHILD";reject([&]{odh::sample_pose(h);},"no root rejected");}
    {auto h=skeleton();h.bones.push_back(bone("OTHER","CHILD"));h.bones[1].parent="OTHER";reject([&]{odh::sample_pose(h);},"disconnected cycle rejected");}
    {auto h=skeleton();h.bones[1].parent="CHILD";reject([&]{odh::sample_pose(h);},"self-parent cycle rejected");}
    {odh::Skeleton h{"empty",{}};reject([&]{odh::sample_pose(h);},"empty skeleton rejected");}
    {auto h=skeleton();h.bones.resize(4097);reject([&]{odh::sample_pose(h);},"bone allocation bound checked first");}
    {odh::Skeleton h{"deep",{bone("0","NULL")}};
     for(int i=1;i<4096;++i)h.bones.push_back(bone(std::to_string(i),std::to_string(i-1),{-1,0,0}));
     const auto p=odh::sample_pose(h);close(p.bones.back().absolute_position[0],4095,"maximum depth handled iteratively");}
    {auto h=skeleton();h.bones[0].translation_bits[0]=bits(std::numeric_limits<float>::quiet_NaN());reject([&]{odh::sample_pose(h);},"nonfinite rest transform rejected");}
    {auto h=skeleton();h.bones[0].rotation_bits[0]=bits(std::numeric_limits<float>::max());reject([&]{odh::sample_pose(h);},"finite quaternion matrix overflow rejected");}
    {auto h=skeleton();h.bones[0].translation_bits[0]=bits(std::numeric_limits<float>::max());h.bones[1].translation_bits[0]=bits(std::numeric_limits<float>::max());
     reject([&]{odh::sample_pose(h);},"absolute hierarchy translation overflow rejected");}
    {Clip c;c.translations({{1,2,3}});c.motion("ROOT",1,0);c.cfp.bits[1].clear();reject([&]{c.sample(s);},"short declared plane rejected");}
    {Clip c;c.translations({{1,2,3}});c.cfp.bits[1].push_back(bits(0));reject([&]{c.sample(s);},"extra undeclared sample rejected");}
    {Clip c;c.translations({{1,2,3}});c.cfp.bits[0][0]=bits(std::numeric_limits<float>::quiet_NaN());reject([&]{c.sample(s);},"unused nonfinite plane rejected");}
    {Clip c;c.translations({{1,2,3}});c.motion("ROOT",2,0);reject([&]{c.sample(s);},"motion range beyond declared samples rejected");}
    {Clip c;c.translations({{1,2,3}});c.motion("ROOT",1,0);c.animation.motions[0].first_translation=-1;reject([&]{c.sample(s);},"active negative sample index rejected");}
    {Clip c;c.translations({{1,2,3}});c.motion("ROOT",0,0);reject([&]{c.sample(s);},"active zero-frame motion rejects underflow");}
    {Clip c;c.motion("ROOT",1);c.animation.motions[0].has_rotation=2;reject([&]{c.sample(s);},"invalid channel flag rejected");}
    {Clip c;c.motion("ROOT",0xffffffffU);reject([&]{c.sample(s);},"motion count prevents signed overflow");}
    {Clip c;c.motion("ROOT",0x7fffffffU);const auto p=c.sample(s,2147483647.0);require(p.completed&&p.frame==0x7fffffffU,"INTMAX completion addition safely widened");}
    {Clip c;c.translations({{1,2,3}});c.motion("ROOT",0x7fffffffU,0);c.animation.motions[0].first_translation=0x7fffffff;
     reject([&]{c.sample(s);},"sample index addition safely widened");}
    {Clip c;c.translations({{std::numeric_limits<float>::max(),0,0},{-std::numeric_limits<float>::max(),0,0}});c.motion("ROOT",2,0);
     reject([&]{c.sample(s,0.5);},"linear arithmetic overflow rejected");}
    {Clip c;c.animation.motions.resize(4097);reject([&]{c.sample(s);},"motion allocation bound checked first");}
    {Clip c;c.animation.translations=1048577;reject([&]{c.sample(s);},"declared sample budget rejected");}
    {Clip c;c.animation.translations=200000;c.animation.rotations=200000;reject([&]{c.sample(s);},"combined translation and rotation plane budget rejected before allocation");}
    {auto h=skeleton();h.name.resize(4194305,'x');reject([&]{odh::sample_pose(h);},"copied text budget checked");}
    {auto h=skeleton();h.bones[1].parent.resize(4194305,'x');reject([&]{odh::sample_pose(h);},"manual parent text budget checked before hierarchy lookup");}
    {auto rig=skeleton();rig.bones.push_back(bone("OTHER","ROOT",{2,4,6}));
     Clip ordinary;ordinary.translations({{2,3,4}});ordinary.rotations({{0,0,0,-1}});
     ordinary.motion("ROOT",1,0);ordinary.motion("CHILD",1,-1,0);
     Clip carry;carry.translations({{8,9,10},{80,90,100}});carry.motion("CHILD",2,0);
     const auto before=ordinary.sample(rig);
     const auto combined=odh::sample_pose(rig,&ordinary.animation,&ordinary.cfp,0,&carry.animation,&carry.cfp);
     require(combined.bones[0].translation==before.bones[0].translation,"carry does not reset ordinary root");
     require(combined.bones[1].translation==std::array<float,3>{-8,9,10},"carry samples frame0 at weight1");
     require(combined.bones[1].rotation==before.bones[1].rotation,"carry translation leaves ordinary rotation");
     require(combined.bones[2].absolute_matrix==before.bones[2].absolute_matrix,"unmentioned branch unaffected by carry");
     close(combined.bones[1].absolute_position[0],-10,"global matrix rebuilt after local carry overlay");
     carry.animation.motions[0].has_translation=0;carry.animation.motions[0].first_translation=-1;
     const auto inactive=odh::sample_pose(rig,&ordinary.animation,&ordinary.cfp,0,&carry.animation,&carry.cfp);
     require(inactive.bones[1].translation==before.bones[1].translation,"inactive carry channel preserves ordinary pose");
     reject([&]{odh::sample_pose(rig,nullptr,nullptr,0,&carry.animation,nullptr);},"carry requires paired CFP");
     carry.cfp.bits[0].clear();reject([&]{odh::sample_pose(rig,nullptr,nullptr,0,&carry.animation,&carry.cfp);},"carry plane bounds validated");}
    {Clip idle,full,half,carry;
     idle.translations({{0,0,0}});idle.rotations({{0,0,0,-1}});idle.motion("ROOT",1,0,0);
     full.translations({{30,0,0}});full.motion("ROOT",1,0);
     half.translations({{60,0,0}});half.motion("ROOT",1,0);
     std::vector<odh::PoseClipSample> clips={{&idle.animation,&idle.cfp,0,0.5f},
         {&full.animation,&full.cfp,0,0.25f},{&half.animation,&half.cfp,0,0.25f}};
     const auto mixed=odh::sample_pose_collection(s,clips);
     close(mixed.bones[0].translation[0],-22.5f,"ordered cumulative weights reproduce source collection blend");
     close(mixed.bones[0].rotation[3],1,"later clips without rotation preserve preceding rotation channel");
     close(mixed.bones[1].absolute_position[0],-23.5f,"hierarchy rebuild follows combined primary channels");
     clips[0].weight=0;clips[1].weight=0.66f;clips[2].weight=0.33f;
     const auto walking=odh::sample_pose_collection(s,clips);
     close(walking.bones[0].translation[0],-40,"zero idle contributes nothing; source full/half weights normalize by prefix");
     carry.translations({{7,0,0}});carry.motion("ROOT",1,0);
     const auto held=odh::sample_pose_collection(s,clips,&carry.animation,&carry.cfp);
     close(held.bones[0].translation[0],-7,"carry overrides after all three locomotion clips");
     clips[1].weight=-1;reject([&]{odh::sample_pose_collection(s,clips);},"negative collection weight rejected");
     clips[1].weight=std::numeric_limits<float>::quiet_NaN();
     reject([&]{odh::sample_pose_collection(s,clips);},"nonfinite collection weight rejected");
     clips[1].weight=1;clips.push_back(clips[1]);
     reject([&]{odh::sample_pose_collection(s,clips);},"collection budget preserves bounded three-clip profile");}
    {Clip first,second;first.rotations({{0,0,0,-1}});first.motion("ROOT",1,-1,0);
     second.rotations({{0,0,-1,0}});second.motion("ROOT",1,-1,0);
     const auto mixed=odh::sample_pose_collection(s,{{&first.animation,&first.cfp,0,0.5f},
         {&second.animation,&second.cfp,0,0.5f}});
     close(mixed.bones[0].rotation[2],std::sqrt(0.5f),"collection rotations use shortest-arc Slerp");
     close(mixed.bones[0].rotation[3],std::sqrt(0.5f),"collection rotation midpoint stays finite");}
    {Clip carry;carry.translations({{0,0,0},{10,0,0},{20,0,0}});carry.motion("ROOT",3,0);
     const auto old=odh::sample_pose(s,nullptr,nullptr,0,&carry.animation,&carry.cfp);
     const auto imported=odh::sample_pose(s,nullptr,nullptr,0,&carry.animation,&carry.cfp,1.5);
     const auto collection=odh::sample_pose_collection(s,{},&carry.animation,&carry.cfp,1);
     require(old.bones[0].translation!=imported.bones[0].translation&&
         collection.bones[0].translation==imported.bones[0].translation,
         "nonzero original carry frame reaches both real pose consumers and overrides at full source weight");}
    {const std::array<float,12> neck{1,0,0,0,0,1,0,0,0,0,1,0};
     const auto forward=odh::calculate_head_seek(neck,{0,0,1},float(3.14159265358979323846));
     close(forward[0],0,"source forward head target horizontal angle");close(forward[1],0,"source forward vertical angle");close(forward[3],1,"source forward head quaternion");
     const auto limited=odh::calculate_head_seek(neck,{0,-100,1},float(3.14159265358979323846));
     close(limited[0],std::sin(float(3.14159265358979323846)*65/360),"source horizontal head clamp65 degrees");
     auto rig=s;rig.bones[1].name="HEAD";odh::AvatarHeadSeekSnapshot state;
     state.actor=1;state.actor_generation=1;state.vm_generation=1;state.weight=15;state.current=limited;state.target=limited;
     const auto before=odh::sample_pose(rig);const auto after=odh::apply_head_seek(before,state);
     require(after.bones[1].rotation==limited&&before.bones[1].rotation!=limited,"head overlay uses full source quaternion after animation/carry without mutating sampled input");
     state.weight=0;require(odh::apply_head_seek(before,state).bones[1].rotation==before.bones[1].rotation,"zero head weight preserves sampled pose");
     reject([&]{odh::calculate_head_seek({}, {0,0,1},0);},"singular actual neck matrix cannot create fake head pose");}
    std::cout<<checks<<" pose checks passed (synthetic CPU transforms only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Pose test failed: "<<e.what()<<'\n';return 1;}}
