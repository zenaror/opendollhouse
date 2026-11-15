/* SPDX-License-Identifier: MPL-2.0 */
// Resource-free producer -> existing presentation event -> real camera service.
// No VM updates, commercial assets, game saves or invented preference/follow mode.
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include "world/World.h"
#include "world/utils/TriangleRaster.h"
#include "world/utils/_2DWorldBatch.h"
#include "world/components/AvatarComponent.h"
#include "world/components/ObjectComponent.h"
#include "vitaboy/SimAvatar.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMTS1Travel.h"
#include "simantics/primitives/VMSpecialEffect.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "files/formats/iff/IffFile.h"
using namespace FSO::LotView;
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using FSO::SimAntics::Engine::Utils::VMTS1Travel;
static void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void near(float a,float b,const char* message){need(std::abs(a-b)<.001f,message);}
int main(){try{
    const bool previousUseWorld=VM::UseWorld();
    struct Restore{bool value;~Restore(){VM::UseWorld(value);}}restore{previousUseWorld};
    World world(1024,768);
    VMContext context(&world);VM vm(&context,nullptr);vm.TS1=true;
    FSO::Files::Formats::IFF::Chunks::OBJD definition;definition.GUID=0x12345678;
    FSO::Files::Formats::IFF::IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"camera-focus-contract",true);
    FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
    VMGameObject target(&object,new Components::ObjectComponent(&object));
    auto component=target.WorldUI.get();component->Room=1;component->Level=2;component->Position={18,24,2.95f};
    vm.AddEntity(&target);
    VMThread thread(&context,&target,8);VMStackFrame frame;frame.Thread=&thread;frame.StackObject=&target;
    // The host resolves each event synchronously, without retaining a VM pointer
    // in a renderer or storing a deferred focus for a later/changed object.
    int requests=0;
    vm.OnTS1Event=[&](int32_t event,int32_t value){
        need(event==VMTS1Travel::EventCameraFocus&&value==target.ObjectID,"focus carries actual stack object identity");
        ++requests;auto entity=vm.GetObjectById(int16_t(value));if(entity)world.CenterTo(entity->WorldUI.get());
    };
    Primitives::VMSpecialEffect primitive;Primitives::VMSpecialEffectOperand operand;
    VM::UseWorld(true);
    for(unsigned flags=0;flags<256;++flags){
        operand.Flags=int8_t(flags);const int before=requests;
        need(primitive.Execute(&frame,&operand)==VMPrimitiveExitCode::GOTO_TRUE,"special effect keeps normal true result");
        need(requests-before==int((flags&8)!=0),"only original bit3 requests a focus");
    }
    operand.Flags=8;vm.TS1=false;const int before=requests;primitive.Execute(&frame,&operand);
    vm.TS1=true;VM::UseWorld(false);primitive.Execute(&frame,&operand);VM::UseWorld(true);
    frame.StackObject=nullptr;primitive.Execute(&frame,&operand);frame.StackObject=&target;
    need(requests==before,"nonTS1/headless/missing target do not request presentation");
    VMTS1Travel::FocusCamera(&vm,&target);
    need(requests==before+1,"same normal focus boundary used by Downtown arrival");
    near(world.State.Level(),2,"event focus selects the actual target level");
    // Target elevation and pose must be centered in the real 2D rendering
    // projection, not raw VM tile XY. Compare against the existing draw origin.
    FSO::LotView::Model::Blueprint blueprint(64,64);std::vector<int16_t> terrain(64*64,40);
    blueprint.Altitude=&terrain;blueprint.AltitudeCenters=&terrain;component->AltitudeSource=&blueprint;
    int projections=0;
    for(int rotation=0;rotation<4;++rotation)for(int zoom=1;zoom<=3;++zoom){
        world.State.Rotation(WorldRotation(rotation));world.State.Zoom(WorldZoom(zoom));
        need(world.CenterTo(component),"placed target can focus at each zoom/rotation");
        const auto offset=world.State.WorldSpace.GetScreenOffset();world.State._2D->SetScroll({-offset.X,-offset.Y});
        const auto pixel=world.State.WorldSpace.GetScreenFromTile(component->DrawPosition())+FSO::LotView::Utils::TileProjectionOrigin(world.State);
        need(std::abs(pixel.X-512)<=.51f&&std::abs(pixel.Y-384)<=.51f,"terrain and upper-floor target reaches viewport center");++projections;
    }
    // Pose follows the same bone transform as native avatar drawing. The bone
    // carries horizontal displacement as well as height, preventing feet-only
    // focus from satisfying the assertion.
    FSO::Vitaboy::Skeleton skeleton;auto bone=std::make_unique<FSO::Vitaboy::Bone>();bone->Name="PELVIS";bone->ParentName="NULL";bone->AbsolutePosition={.6f,2.1f,.9f};skeleton.RootBone=bone.get();skeleton.Bones.push_back(std::move(bone));
    FSO::Vitaboy::SimAvatar model(&skeleton);model.Skeleton->GetBone("PELVIS")->AbsolutePosition={.6f,2.1f,.9f};Components::AvatarComponent avatar;
    avatar.Avatar=&model;avatar.Room=1;avatar.Level=1;avatar.Position={22,23,0};avatar.RadianDirection=.75f;avatar.AltitudeSource=&blueprint;
    need(world.CenterTo(&avatar),"avatar pose can focus");
    const auto offset=world.State.WorldSpace.GetScreenOffset();world.State._2D->SetScroll({-offset.X,-offset.Y});
    // Independently use the reference Z rotation in tile space, rather than the
    // renderer's Y rotation implementation used by World.CenterTo.
    const float angle=avatar.RadianDirection+float(M_PI),c=std::cos(angle),s=std::sin(angle);
    const auto pelvis=avatar.DrawPosition()+st::Vector3((.6f*c-.9f*s)/3,(.6f*s+.9f*c)/3,2.1f/3);
    const auto pixel=world.State.WorldSpace.GetScreenFromTile(pelvis)+FSO::LotView::Utils::TileProjectionOrigin(world.State);
    need(std::abs(pixel.X-512)<=.51f&&std::abs(pixel.Y-384)<=.51f,"animated pelvis, not feet, reaches viewport center");
    const auto fixed=world.State.CenterTile();const auto level=world.State.Level();
    for(uint16_t room:{uint16_t(0),uint16_t(65531)}){component->Room=room;need(!world.CenterTo(component),"invalid room refuses focus");}
    component->Room=1;component->Position.X=std::numeric_limits<float>::quiet_NaN();need(!world.CenterTo(component),"nonfinite target refuses focus");
    need(!world.CenterTo(nullptr)&&world.State.CenterTile()==fixed&&world.State.Level()==level,"refusal does not move camera or change level");
    const auto manual=world.State.CenterTile();world.Scroll({.25f,0});need(world.State.CenterTile()!=manual,"manual camera still responds after event focus");
    component->Position={10,11,0};world.Update();need(world.State.CenterTile()!=world.State.WorldSpace.GetTileFromScreen(world.State.WorldSpace.GetScreenFromTile(component->DrawPosition())),"one-shot focus does not begin persistent following");
    vm.RemoveEntity(&target);std::cout<<"PASS original event gates, Downtown boundary and "<<projections<<" native focus projections/pose/level/terrain/refusal/manual cases\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
