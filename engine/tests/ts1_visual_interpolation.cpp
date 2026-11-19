// SPDX-License-Identifier: MPL-2.0
// Regression for absolute presentation samples between original owner passes.
// No display, renderer, commercial assets, game process, or audio initialization.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMThread.h"
#include "world/components/AvatarComponent.h"
#include "files/formats/iff/IffFile.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
using namespace FSO::SimAntics;
static void need(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
static void same(const st::Vector3& actual,const st::Vector3& expected,const char* message) {
    need(std::fabs(actual.X-expected.X)<.00001f && std::fabs(actual.Y-expected.Y)<.00001f &&
        std::fabs(actual.Z-expected.Z)<.00001f,message);
}
int main() { try {
    VM::UseWorld(false);
    VMContext context(nullptr); VM vm(&context,nullptr); vm.TS1=true;
    FSO::Files::Formats::IFF::IffFile iff;
    FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"visual-fixture",true);
    FSO::Files::Formats::IFF::Chunks::OBJD definition; definition.GUID=0x12345678;
    FSO::Content::GameObject object; object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
    VMAvatar actor(&object);
    actor.Thread=std::make_unique<FSO::SimAntics::Engine::VMThread>(&context,&actor,16);
    actor.Avatar=std::make_unique<FSO::Vitaboy::SimAvatar>(); // empty skeleton, actual ReloadSkeleton path
    actor.WorldUI=std::make_unique<FSO::LotView::Components::AvatarComponent>();
    // Enable actual WorldUI position access only after constructing the empty fixture.
    VM::UseWorld(true);
    const st::Vector3 base(10,20,30), velocity(2,-4,1);
    actor.VisualPosition(base);actor.Velocity=velocity;
    for(float fraction:{.2f,.4f,.6f,.8f}) actor.FractionalAnim(fraction);
    same(actor.VisualPosition(),base+.8f*velocity,"render partition accumulated absolute displacement");
    actor.FractionalAnim(.8f);
    same(actor.VisualPosition(),base+.8f*velocity,"repeated absolute fraction was not idempotent");
    actor.FractionalAnim(.3f);
    same(actor.VisualPosition(),base+.3f*velocity,"lower absolute fraction retained previous displacement");
    actor.VisualPosition(base);actor.FractionalAnim(.8f);
    same(actor.VisualPosition(),base+.8f*velocity,"single sample disagreed with partitioned samples");
    const st::Vector3 moved(50,60,70);
    actor.VisualPosition(moved);actor.FractionalAnim(.25f);
    same(actor.VisualPosition(),moved+.25f*velocity,"external visual movement retained stale anchor");
    const FSO::LotView::Model::LotTilePos placed(160,320,1);
    actor.Position(placed);actor.FractionalAnim(.5f);
    same(actor.VisualPosition(),st::Vector3(10,20,0)+.5f*velocity,"logical placement retained stale anchor");
    vm.TS1=false;actor.VisualPosition(base);
    actor.FractionalAnim(.5f);actor.FractionalAnim(.5f);
    same(actor.VisualPosition(),base+velocity,"legacy non-TS1 adapter changed");
    VM::UseWorld(false);
    std::puts("PASS actual WorldUI fractional samples, external movement, placement, legacy adapter");
    return 0;
} catch(const std::exception& e) { VM::UseWorld(false);std::fprintf(stderr,"FAIL %s\n",e.what());return 1; } }
