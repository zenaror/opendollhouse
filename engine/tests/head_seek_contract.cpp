// SPDX-License-Identifier: MPL-2.0
// Real native primitive -> person data -> head controller -> HEAD/absolute pose.
// Synthetic resources only. Private access isolates these update boundaries;
// no product debug API, previous VM, renderer substitute or game data.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "simantics/VMContext.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/entities/VMGameObject.h"
#include "world/components/AvatarComponent.h"
#include "world/components/ObjectComponent.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "files/formats/iff/chunks/DGRP.h"
#include "vitaboy/Animator.h"
#include "vitaboy/SimAvatar.h"
#include "simantics/primitives/VMLookTowards.h"
#define private public
#include "simantics/VM.h"
#include "simantics/entities/VMAvatar.h"
#undef private
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
namespace VB=FSO::Vitaboy;
namespace LV=FSO::LotView;
namespace IFF=FSO::Files::Formats::IFF;
static int checks=0;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);++checks;std::printf("PASS %s\n",why);}
static bool close(float a,float b){return std::abs(a-b)<0.0001f;}
static bool same(VB::Quaternion a,VB::Quaternion b){return close(a.X,b.X)&&close(a.Y,b.Y)&&close(a.Z,b.Z)&&close(a.W,b.W);}
static bool sameMatrix(const VB::Matrix& a,const VB::Matrix& b){
 const std::array<float,16> x{a.M11,a.M12,a.M13,a.M14,a.M21,a.M22,a.M23,a.M24,a.M31,a.M32,a.M33,a.M34,a.M41,a.M42,a.M43,a.M44};
 const std::array<float,16> y{b.M11,b.M12,b.M13,b.M14,b.M21,b.M22,b.M23,b.M24,b.M31,b.M32,b.M33,b.M34,b.M41,b.M42,b.M43,b.M44};
 for(size_t i=0;i<x.size();++i)if(!close(x[i],y[i]))return false;return true;
}
static VB::Matrix posedHead(VB::SimAvatar& avatar,const VB::Quaternion& pose){
 auto h=avatar.Skeleton->GetBone("HEAD");auto neck=avatar.Skeleton->GetBone(h->ParentName);
 return (VB::Matrix::CreateFromQuaternion(pose)*VB::Matrix::CreateTranslation(h->Translation))*neck->AbsoluteMatrix;
}
static std::unique_ptr<VB::Skeleton> rig(float height=3,bool withHead=true){
 auto s=std::make_unique<VB::Skeleton>();auto n=std::make_unique<VB::Bone>();n->Name="NECK";n->ParentName="NULL";n->Index=0;
 auto h=std::make_unique<VB::Bone>();h->Name=withHead?"HEAD":"OTHER";h->ParentName="NECK";h->Index=1;h->Translation=st::Vector3(.6f,height,-.9f);
 n->Children.push_back(h.get());s->RootBone=n.get();s->Bones.push_back(std::move(n));s->Bones.push_back(std::move(h));s->ComputeBonePositions(s->RootBone,VB::Matrix::Identity());return s;
}
int main(){try{
 const bool priorWorld=VM::UseWorld();struct Restore{bool value;~Restore(){VM::UseWorld(value);}}restore{priorWorld};
 VM::UseWorld(false);
 IFF::IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"head-seek-fixture",true);
 IFF::Chunks::OBJD definition;FSO::Content::GameObject obj;obj.OBJ=&definition;obj.Resource=&resource;
 VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;
 VMAvatar actor(&obj);actor.ObjectID=1;actor.Thread=std::make_unique<VMThread>(&context,&actor,8);
 VMGameObject target(&obj,nullptr);target.ObjectID=2;
 // Authenticate by the real VM's lookup, no fake target callback.
 vm.ObjectsById[1]=&actor;vm.ObjectsById[2]=&target;
 auto skeleton=rig();actor.Avatar=std::make_unique<VB::SimAvatar>(skeleton.get());
 actor.WorldUI=std::make_unique<LV::Components::AvatarComponent>();
 static_cast<LV::Components::AvatarComponent*>(actor.WorldUI.get())->Avatar=actor.Avatar.get();
 actor.WorldUI->Position=st::Vector3(10,10,0);
 target.WorldUI=std::make_unique<LV::Components::ObjectComponent>(nullptr);target.WorldUI->Position=st::Vector3(12,9,0);
 VMStackFrame frame;frame.Caller=&actor;frame.StackObject=&target;frame.Thread=actor.Thread.get();
 Primitives::VMLookTowards primitive;Primitives::VMLookTowardsOperand operand;
 frame.Caller=&target;bool invalidCaller=false;
 try{primitive.Execute(&frame,&operand);}catch(const std::exception&){invalidCaller=true;}
 require(invalidCaller,"non-avatar primitive caller takes source invalid-cast failure rather than unsafe downcast");frame.Caller=&actor;
 actor.SetPersonData(VMPersonDataVariable::HeadSeekFinishAction,9);actor.SetPersonData(VMPersonDataVariable::HeadSeekTimeout,99);
 require(primitive.Execute(&frame,&operand)==VMPrimitiveExitCode::GOTO_TRUE&&actor.GetPersonData(VMPersonDataVariable::HeadSeekObject)==2&&
   actor.GetPersonData(VMPersonDataVariable::HeadSeekState)==1&&actor.GetPersonData(VMPersonDataVariable::HeadSeekLimitAction)==1&&
   actor.GetPersonData(VMPersonDataVariable::HeadSeekFinishAction)==0&&actor.GetPersonData(VMPersonDataVariable::HeadSeekTimeout)==0,
   "real mode0 writes all five source person entries");
 VM::UseWorld(true);actor.UpdateHeadSeek();
 require(actor.Avatar->HeadSeek.weight==1&&actor.Avatar->HeadSeekPoseValid,"actual live object target reaches controller and calculation");
 const auto timeout=actor.GetPersonData(VMPersonDataVariable::HeadSeekTimeout);actor.FractionalAnim(.5f);
 auto head=actor.Avatar->Skeleton->GetBone("HEAD");
 require(!same(head->Rotation,VB::Quaternion::Identity())&&actor.Avatar->HeadSeek.last_fraction==.5f&&
   actor.GetPersonData(VMPersonDataVariable::HeadSeekTimeout)==timeout,"fractional actual pose applies HEAD without advancing person-data time");
 require(!same(head->Rotation,actor.Avatar->BaseSkeleton->GetBone("HEAD")->Rotation),"simulation overlay never mutates bind pose");
 VB::SimAvatar portraitClone(*actor.Avatar);
 require(portraitClone.HeadSeek.weight==0&&!portraitClone.HeadSeekPoseValid&&same(portraitClone.Skeleton->GetBone("HEAD")->Rotation,VB::Quaternion::Identity()),
   "source clone starts neutral despite world head pose; portrait skeleton remains separate");
 // The real fractional consumer must apply head seeking after carry, not
 // silently lose it when another channel poses HEAD at full source weight.
 VB::Animation carry;carry.RotationCount=1;carry.Rotations.push_back(VB::Quaternion(0,0,0.5f,std::sqrt(.75f)));
 VB::AnimationMotion motion;motion.BoneName="HEAD";motion.FrameCount=1;motion.HasRotation=true;carry.Motions.push_back(motion);
 actor.CarryAnimationState=std::make_shared<VMAnimationState>(&carry,false);
 const auto& controller=actor.Avatar->HeadSeek;
 const VB::Quaternion desired(controller.current[0],controller.current[1],controller.current[2],controller.current[3]);
 const auto expected=VB::Quaternion::Slerp(carry.Rotations[0],desired,(controller.weight+.5f)/15.f);
 actor.FractionalAnim(.5f);
 require(same(head->Rotation,expected)&&sameMatrix(actor.Avatar->SkelBones()[head->Index],posedHead(*actor.Avatar,expected)),
   "fractional entry publishes head-after-carry in renderer SkelBones");
 // Real Tick entry with no BHAV thread: isolates final publication from the
 // unrelated lot scheduler/motive service. Controller advancement is covered
 // by the direct UpdateHeadSeek checks; this case fails if Tick drops overlay.
 auto heldThread=std::move(actor.Thread);actor.Avatar->HeadSeek.last_fraction=0;
 const auto fullExpected=VB::Quaternion::Slerp(carry.Rotations[0],desired,controller.weight/15.f);
 const auto tickTimeout=actor.GetPersonData(VMPersonDataVariable::HeadSeekTimeout);
 const auto tickWeight=controller.weight;const auto beforePosition=actor.VisualPosition();
 actor.Tick();
 require(same(head->Rotation,fullExpected)&&sameMatrix(actor.Avatar->SkelBones()[head->Index],posedHead(*actor.Avatar,fullExpected)),
   "full Tick entry publishes head-after-carry in renderer SkelBones");
 require(actor.Avatar->HeadSeek.last_fraction==0&&controller.weight==tickWeight&&
   actor.GetPersonData(VMPersonDataVariable::HeadSeekTimeout)==tickTimeout&&close(actor.VisualPosition().X,beforePosition.X)&&close(actor.VisualPosition().Y,beforePosition.Y)&&close(actor.VisualPosition().Z,beforePosition.Z),
   "full pose publication alone does not resample controller time or interpolate position");
 actor.FractionalAnim(.5f);
 require(same(head->Rotation,expected)&&sameMatrix(actor.Avatar->SkelBones()[head->Index],posedHead(*actor.Avatar,expected)),
   "following half frame recomposes once over carry and republishes renderer matrices");
 actor.Thread=std::move(heldThread);actor.CarryAnimationState.reset();
 actor.SetPersonData(VMPersonDataVariable::HeadSeekTimeout,2);actor.UpdateHeadSeek();
 require(actor.Avatar->HeadSeek.weight==2&&actor.GetPersonData(VMPersonDataVariable::HeadSeekTimeout)==1&&actor.Avatar->HeadSeek.last_fraction==0,
   "next full update consumes remaining fractional slide once and decrements timeout");
 actor.UpdateHeadSeek();require(actor.GetPersonData(VMPersonDataVariable::HeadSeekState)==4&&actor.Avatar->HeadSeek.weight==3,"timeout enters source fade state");
 for(int i=0;i<3;i++)actor.UpdateHeadSeek();
 require(actor.GetPersonData(VMPersonDataVariable::HeadSeekState)==8&&actor.Avatar->HeadSeek.weight==0&&actor.GetPersonData(VMPersonDataVariable::HeadSeekFinishAction)==0,
   "fade ends at zero and clears finish flag");
 primitive.Execute(&frame,&operand);actor.UpdateHeadSeek();actor.UpdateHeadSeek();vm.ObjectsById.erase(2);actor.UpdateHeadSeek();
 require(actor.Avatar->HeadSeek.weight==1,"removed target lookup fades rather than retaining a dangling pointer");actor.UpdateHeadSeek();
 require(actor.Avatar->HeadSeek.weight==0&&actor.GetPersonData(VMPersonDataVariable::HeadSeekState)==8,"missing target reaches completion");
 vm.ObjectsById[2]=&target;primitive.Execute(&frame,&operand);target.Dead=true;actor.UpdateHeadSeek();
 require(actor.Avatar->HeadSeek.weight==1,"registered dead flag alone adds no eligibility policy absent in either source pin");target.Dead=false;
 target.WorldUI->Position=st::Vector3(-2048,-2048,0);actor.UpdateHeadSeek();
 require(actor.Avatar->HeadSeek.weight==2,"registered out-of-world target follows source lookup instead of invented rejection");
 VM::UseWorld(false);actor.SetPersonData(VMPersonDataVariable::HeadSeekTimeout,1);actor.UpdateHeadSeek();
 require(actor.GetPersonData(VMPersonDataVariable::HeadSeekState)==4,"headless path still advances the actual source timeout/state");
 const auto limited=VB::Animator::CalculateHeadSeek(actor.Avatar.get(),st::Vector3(0,-100,1),3.14159265358979323846f);
 require(limited&&close(limited->X,std::sin(3.14159265358979323846f*65/360)),"reused neck calculation preserves reference65degree clamp");
 actor.Avatar=std::make_unique<VB::SimAvatar>();
 require(!VB::Animator::CalculateHeadSeek(actor.Avatar.get(),st::Vector3(1,2,3),0)&&!VB::Animator::ApplyHeadSeek(actor.Avatar.get(),VB::Quaternion::Identity(),1),
   "missing native bones skip the visual overlay safely");
 auto missingNeck=rig();missingNeck->Bones[1]->ParentName="ABSENT";
 actor.Avatar=std::make_unique<VB::SimAvatar>(missingNeck.get());
 static_cast<LV::Components::AvatarComponent*>(actor.WorldUI.get())->Avatar=actor.Avatar.get();
 VM::UseWorld(true);primitive.Execute(&frame,&operand);actor.UpdateHeadSeek();
 const auto unchanged=actor.Avatar->Skeleton->GetBone("HEAD")->Rotation;actor.FractionalAnim(.5f);
 require(!actor.Avatar->HeadSeekPoseValid&&same(actor.Avatar->Skeleton->GetBone("HEAD")->Rotation,unchanged),
   "missing neck does not manufacture an identity head overlay while person state remains live");
 // Valid observer must also reject an orphan HEAD on its target, whose
 // AbsolutePosition is not computed by ConstructTree from the real root.
 actor.Avatar=std::make_unique<VB::SimAvatar>(skeleton.get());
 static_cast<LV::Components::AvatarComponent*>(actor.WorldUI.get())->Avatar=actor.Avatar.get();
 VB::SimAvatar orphanTarget(missingNeck.get());target.WorldUI=std::make_unique<LV::Components::AvatarComponent>();
 static_cast<LV::Components::AvatarComponent*>(target.WorldUI.get())->Avatar=&orphanTarget;
 primitive.Execute(&frame,&operand);actor.UpdateHeadSeek();
 require(!target.WorldUI->GetLookTarget()&&!actor.Avatar->HeadSeekPoseValid&&actor.Avatar->HeadSeek.weight==1,
   "valid observer does not manufacture pivot target from orphan HEAD");
 for(float height:{1.f,2.f,4.f}){
  auto r=rig(height);VB::SimAvatar a(r.get());LV::Components::AvatarComponent visual;visual.Avatar=&a;visual.Position=st::Vector3(10,20,2);
  visual.RadianDirection=.37f;const float angle=.37f+3.14159265358979323846f;
  const float x=.6f/3,z=-.9f/3;auto look=visual.GetLookTarget();
  require(look&&close(look->Y,2+height/3)&&close(look->X,10+x*std::cos(angle)-z*std::sin(angle))&&
    close(look->Z,20+x*std::sin(angle)+z*std::cos(angle)),
    "Y-up adult/child/pet-sized HEAD swizzles and rotates asymmetric horizontal offsets");
 }
 LV::Components::ObjectComponent object(nullptr);object.Position=st::Vector3(1,2,3);auto low=object.GetLookTarget();
 IFF::Chunks::DGRP group;group.Images.emplace_back(&group);group.Images[0].Direction=4;group.Images[0].Zoom=3;
 group.Images[0].Sprites.emplace_back(&group);group.Images[0].Sprites[0].SpriteOffset.Y=-90;object.DGRP=&group;auto high=object.GetLookTarget();
 require(low&&high&&close(low->Y,3.4f)&&close(high->Y,4.5f),"object look target uses canonical sprite top rather than camera height");
 vm.ObjectsById.clear();actor.Thread.reset();std::printf("%d native head seek contracts; no game assets, simulation clock or saves\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
