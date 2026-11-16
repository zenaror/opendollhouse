/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __3DS__
#include "app/ui/OriginalSelectedCrystal.h"
#include "app/ui/OriginalCrystalProjection.h"
#include "app/ui/OriginalCrystalOverlay.h"
#include "app/Canvas.h"
#include "app/WorldPicking.h"
#include "content/ContentManager.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/model/VMMotive.h"
#include "world/World.h"
#include "world/Framebuffer.h"
#include "world/components/AvatarComponent.h"
#include "vitaboy/SimAvatar.h"
#include "selected_sim_crystal.hpp"
#include <cmath>
#include <cstdio>
#include <memory>

namespace simapp {
void DrawOriginalSelectedCrystal(Canvas& canvas, FSO::LotView::World& world,
        const FSO::LotView::Framebuffer& scene, FSO::SimAntics::VMAvatar* selected,
        double milliseconds) {
    if(!selected || selected->Dead || !selected->WorldUI)return;
    auto* component=dynamic_cast<FSO::LotView::Components::AvatarComponent*>(selected->WorldUI.get());
    if(!component || !component->Avatar || !component->Avatar->Skeleton ||
       !component->Visible || component->Level>world.State.Level() ||
       component->DisplayFlags==FSO::LotView::Components::AvatarDisplayFlags::ShowAsGhost)return;
    const auto position=component->DrawPosition();
    if(position.X < -2043 && position.Y < -2043)return;
    const auto* bone=component->Avatar->Skeleton->GetBone("HEAD");
    if(!bone)return;
    // Same animated skeleton and world transform used by DrawAvatar/DrawCensor.
    // No adult/child fixed height, bobbing, or synthetic replacement geometry.
    constexpr double pi=3.14159265358979323846;
    const auto transform=FSO::Vitaboy::Matrix::CreateRotationY(float(pi-component->RadianDirection))*
        FSO::Vitaboy::Matrix::CreateTranslation(FSO::Vitaboy::WorldCamera::GetWorldFromTile(position));
    const auto headWorld=FSO::Vitaboy::Transform(bone->AbsolutePosition,transform);
    const std::array<float,3> head{headWorld.X/3,headWorld.Z/3,headWorld.Y/3};
    const auto screen=MakeAvatarCamera(world).ProjectWorld(headWorld);
    if(!std::isfinite(screen.X)||!std::isfinite(screen.Y))return;
    // A bounded local viewport retains the old projection/rasterizer at every
    // desktop window size. Its subpixel anchor matches the engine's camera.
    constexpr int extent=OriginalCrystalViewport::Extent;
    if(screen.X < -extent || screen.Y < -extent || screen.X > canvas.Width+extent || screen.Y > canvas.Height+extent)return;
    const auto viewport=CrystalViewport(head,screen.X,screen.Y,uint8_t(world.State.Zoom()),uint8_t(world.State.Rotation()));
    const int originX=viewport.X,originY=viewport.Y;
    const auto& camera=viewport.Camera;
    const auto& center=camera.center_tile;
    const float depthOrigin=world.State.Camera()->Depth(st::Vector3(center[0]*3,center[2]*3,center[1]*3));
    static std::string loadedRoot;
    static bool attempted=false;
    static std::unique_ptr<odh_host::SelectedSimCrystal> crystal;
    auto* content=FSO::Content::Content::Get();if(!content)return;
    if(loadedRoot!=content->SimsCompleteDir){loadedRoot=content->SimsCompleteDir;attempted=false;crystal.reset();}
    if(!attempted){
        attempted=true;
        try { crystal=std::make_unique<odh_host::SelectedSimCrystal>(
            loadedRoot+"/GameData/Animation/Animation.far",loadedRoot+"/GameData/Textures/Textures.far"); }
        catch(const std::exception& error){std::fprintf(stderr,"[ui] Original selected crystal unavailable: %s\n",error.what());}
    }
    if(!crystal)return;
    // These are the preserved person.cpp rules: local ROOT Y=3 (one tile),
    // X basis pi/2, authored scale 1, mood index 3, one revolution per 2s.
    if(scene.Width!=canvas.Width || scene.Height!=canvas.Height || scene.Depth.size()!=size_t(scene.Width)*scene.Height || canvas.Px.size()<scene.Depth.size())return;
    std::array<uint32_t,2> origin{};
    const auto layer=crystal->render(camera,head,float(std::fmod(milliseconds*pi/1000.,2*pi)),
        selected->GetMotiveData(FSO::SimAntics::Model::VMMotive::Mood),1,{0,0,1},{float(pi/2),0,0},{},&origin);
    ComposeCrystal(canvas,scene.Depth,{originX,originY,extent,extent},layer,origin,depthOrigin);
    // Decorative pixels never alter the engine's depth or object picking data.
}
}
#endif
