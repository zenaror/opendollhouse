// SPDX-License-Identifier: MPL-2.0
// Exercise the real animation sampler, not a duplicate of its implementation.
#include "vitaboy/Animator.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::Vitaboy;
namespace {
int checks=0;
void check(bool ok,const char* name) { if(!ok) throw std::runtime_error(name); ++checks; }
bool near(float a,float b) { return std::fabs(a-b)<0.000001f; }
}
int main() {
    try {
        Skeleton skeleton;
        auto bone=std::make_unique<Bone>(); bone->Name="ROOT"; bone->CanRotate=1;
        skeleton.RootBone=bone.get(); skeleton.Bones.push_back(std::move(bone));
        Avatar avatar(&skeleton);
        Animation clip; clip.Name="synthetic-quaternion"; clip.Duration=1000;
        AnimationMotion motion; motion.BoneName="ROOT"; motion.FrameCount=2; motion.HasRotation=true;
        clip.Motions.push_back(motion); clip.RotationCount=2;
        clip.Rotations={Quaternion(),Quaternion(0,0,1,0)};
        auto sampled=avatar.Skeleton->GetBone("ROOT");
        Animator::RenderFrameTS1(&avatar,&clip,0,0.75f,1.0f,false);
        check(near(sampled->Rotation.Z,0.948683298f)&&near(sampled->Rotation.W,0.316227766f),
            "TS1 key interpolation uses original normalized linear blend");
        sampled->Rotation=Quaternion();
        Animator::RenderFrame(&avatar,&clip,0,0.75f,1.0f);
        check(near(sampled->Rotation.Z,0.923879533f)&&near(sampled->Rotation.W,0.382683432f),
            "ordinary non-TS1 sampler retains spherical interpolation");
        Animator::RenderFrameTS1(&avatar,&clip,0,0.995f,1.0f,false);
        check(sampled->Rotation.Z==0&&sampled->Rotation.W==1,
            "TS1 one-percent key gate retains first key");
        sampled->Rotation=Quaternion();
        Animator::RenderFrameTS1(&avatar,&clip,1,-1,0.25f,false);
        check(near(sampled->Rotation.Z,0.316227766f)&&near(sampled->Rotation.W,0.948683298f),
            "TS1 layer weight belongs to sampled pose rather than existing pose");
        clip.Rotations={Quaternion(0,0,0,2),Quaternion(0,0,0,2)};
        Animator::RenderFrameTS1(&avatar,&clip,0,0.5f,1.0f,false);
        check(sampled->Rotation.W==2,"identical authored keys bypass normalization");
        sampled->Rotation=Quaternion(0,0,0,2);
        Animator::RenderFrameTS1(&avatar,&clip,0,0.5f,0.5f,false);
        check(sampled->Rotation.W==2,"identical layer and existing pose bypass normalization");
        sampled->Rotation=Quaternion(0,0,1,0);
        Animator::RenderFrameTS1(&avatar,&clip,0,0.5f,0.01f,false);
        check(sampled->Rotation.Z==1&&sampled->Rotation.W==0,
            "one-percent layer gate leaves existing pose untouched");
        Animator::RenderFrameTS1(&avatar,&clip,0,0.5f,1.1f,false);
        check(sampled->Rotation.W==2,"layer weight above one copies sampled pose");
        std::printf("PASS %d TS1 quaternion render controls\n",checks); return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
}
