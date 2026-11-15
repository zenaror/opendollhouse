// SPDX-License-Identifier: MPL-2.0
// Synthetic calls through real FindLocationFor and VMAvatar::Tick. No assets,
// world clock, authored NPC deletion or gameplay outcome is manufactured.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/primitives/VMFindLocationFor.h"
#include "files/formats/iff/IffFile.h"
#include "vitaboy/SimAvatar.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
using namespace FSO::LotView::Model;
using namespace FSO::Files::Formats::IFF;
static void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); std::printf("PASS %s\n",why); }
int main(){try{
 VM::UseWorld(false); VMContext ctx(nullptr); VM vm(&ctx,nullptr); vm.TS1=true; vm.GlobalState.assign(38,0);
 ctx.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&ctx));
 ctx.Architecture()->Rooms[0].Map.assign(64,0); ctx.RoomInfo.resize(1);
 IffFile iff; FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"offlot-fixture",true);
 Chunks::OBJD definition; definition.StackSize=8;
 FSO::Content::GameObject object; object.OBJ=&definition; object.Resource=&resource;
 VMAvatar actor(&object); actor.ObjectID=1;
 Entities::VMMultitileGroup actorGroup; actorGroup.AddObject(&actor); actor.MultitileGroup=&actorGroup;
 actor.Thread=std::make_unique<VMThread>(&ctx,&actor,8); actor.Thread->IsCheck=true;
 actor.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));
 // A valid recovery target makes the old unconditional mailbox branch fail this test.
 Chunks::OBJD mailboxDef; mailboxDef.GUID=0xEF121974;
 FSO::Content::GameObject mailboxObject; mailboxObject.OBJ=&mailboxDef; mailboxObject.Resource=&resource;
 VMGameObject mailbox(&mailboxObject,nullptr); mailbox.Position(LotTilePos(56,56,1));
 vm.Entities={&mailbox,&actor};
 VMStackFrame frame; frame.Caller=&actor; frame.Callee=&actor; frame.StackObject=&actor; frame.Thread=actor.Thread.get();
 Primitives::VMFindLocationFor primitive; Primitives::VMFindLocationForOperand out; out.Mode=1;
 for (int household:{0,1}) {
  actor.PersistID=household;
  actor.Visitor=household==0;
  actor.Position(LotTilePos(40,40,1));
  check(primitive.Execute(&frame,&out)==VMPrimitiveExitCode::GOTO_TRUE,"authored mode1 accepts off-lot transition");
  actor.Tick(); actor.Tick();
  check(actor.Position()==LotTilePos::OUT_OF_WORLD&&!actor.Dead,"TS1 off-lot service/visitor or resident remains outside and alive");
 }
 frame.StackObject=&actor;
 // The existing placement producer remains responsible for legitimate arrival.
 check(Primitives::VMFindLocationFor::FindLocationFor(&actor,&mailbox,&ctx),"explicit arrival producer still places avatar");
 auto arrived=actor.Position(); actor.Tick();
 check(actor.Position()==arrived,"TS1 tick preserves legitimate in-world arrival");
 primitive.Execute(&frame,&out); vm.TS1=false; actor.Tick();
 check(actor.Position()!=LotTilePos::OUT_OF_WORLD,"nonTS1 mailbox recovery retains its existing behavior");
 vm.Entities.clear(); std::puts("off-lot presence contract passed"); return 0;
}catch(const std::exception& e){std::fprintf(stderr,"off-lot presence contract: %s\n",e.what());return 1;}}
