// SPDX-License-Identifier: MPL-2.0
// Synthetic source-contract checks; these do not replace a real-lot continuation.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/primitives/VMGenericTS1Call.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "content/ContentManager.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::LotView::Model;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() { try {
    VM::UseWorld(false); VMContext context(nullptr); VM vm(&context,nullptr);
    IffFile iff; FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"footprint",true);
    OBJD definition; FSO::Content::GameObject object; object.OBJ=&definition; object.Resource=&resource;
    VMAvatar avatar(&object); avatar.Thread=std::make_unique<VMThread>(&context,&avatar,16);
    avatar.Position(LotTilePos(100,200,1));
    for (int d=0;d<8;++d) {
        auto direction=static_cast<Direction>(1<<d);
        avatar.SetPersonData(static_cast<VMPersonDataVariable>(73),8);
        auto f=avatar.GetObstacle(avatar.Position(),direction);
        int q=((8-d)/2)%4, e[4]={-8,0,0,0};
        if (d&1) e[0]=0;
        require(f->x1==97+e[(q+3)%4] && f->y1==197+e[q] &&
                f->x2==103-e[(q+1)%4] && f->y2==203-e[(q+2)%4],"direction geometry");
    }
    definition.FootprintMask=0x1020; avatar.SetPersonData(static_cast<VMPersonDataVariable>(73),0x0031);
    auto f=avatar.GetObstacle(avatar.Position(),Direction::NORTH);
    require(f->x1==98 && f->y1==196 && f->x2==104 && f->y2==203,"independent signed nibbles");
    definition.FootprintMask=0; avatar.SetPersonData(static_cast<VMPersonDataVariable>(73),0); avatar.SetPersonData(static_cast<VMPersonDataVariable>(68),1);
    f=avatar.GetObstacle(avatar.Position(),Direction::NORTH);
    require(f->x1==100 && f->x2==100 && f->y1==200 && f->y2==200,"zero width retains numeric rectangle");
    avatar.SetPersonData(static_cast<VMPersonDataVariable>(68),0); avatar.Direction(Direction::NORTH); avatar.RefreshTS1Footprint();
    VMStackFrame frame; frame.Caller=&avatar; frame.Thread=avatar.Thread.get();
    avatar.Thread->TempRegisters[0]=8;
    VMGenericTS1Call::Execute(&frame,VMGenericTS1CallMode::MyRoutingFootprintEqualsTemp0);
    require(avatar.GetPersonData(static_cast<VMPersonDataVariable>(72))==8 && avatar.GetPersonData(static_cast<VMPersonDataVariable>(73))==0 && avatar.Footprint->y1==197,"nonzero request retains cache");
    avatar.RestoreTS1Footprint(89,189,111,211); avatar.Thread->TempRegisters[0]=0;
    VMGenericTS1Call::Execute(&frame,VMGenericTS1CallMode::MyRoutingFootprintEqualsTemp0);
    require(avatar.GetPersonData(static_cast<VMPersonDataVariable>(72))==0 && avatar.Footprint->x1==97 && avatar.Footprint->y1==197,"zero request refreshes cache");
    vm.TS1=false; avatar.SetPersonData(static_cast<VMPersonDataVariable>(68),1); avatar.SetPersonData(static_cast<VMPersonDataVariable>(73),8);
    f=avatar.GetObstacle(avatar.Position(),Direction::NORTH);
    require(f->x1==97 && f->y1==197 && f->x2==103 && f->y2==203,"TSO geometry unchanged");
    avatar.Position(LotTilePos::OUT_OF_WORLD); avatar.RefreshTS1Footprint(); require(!avatar.Footprint,"out of world has no footprint");
    std::puts("PASS routing footprint contract"); return 0;
} catch (const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; } }
