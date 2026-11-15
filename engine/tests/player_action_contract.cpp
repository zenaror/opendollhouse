// SPDX-License-Identifier: MPL-2.0
// Data-free contract through the real LotSession -> VMEntity -> VMThread path.
// Fixture construction follows interaction_checkpoint_route_contract; private
// access supplies the session's existing context without adding a product API.
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/model/VMSetToNextCache.h"
#include "simantics/model/VMStackObjectVariable.h"
#include "simantics/primitives/VMExpression.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMQueuedAction.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/BHAV.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "files/formats/iff/chunks/TTAB.h"
#include "world/World.h"
#include "app/live/UIHeadlineRenderer.h"
#define private public
#include "app/LotSession.h"
#undef private
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);std::printf("PASS %s\n",message);}
// Original DVD expression scope4/word62 and the exact target of its object-use
// push/pop were traced statically. This data-free fixture tests that contract,
// not the unobserved physical completion of making half of a commercial bed.
static void instanceUseContract(){
 using namespace FSO::SimAntics::Model;
 using namespace FSO::SimAntics::Entities;
 using namespace FSO::SimAntics::Primitives;
 VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;
 IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"instance-use-fixture",true);
 std::array<OBJD,4> defs;std::array<FSO::Content::GameObject,4> objects;
 for(int i=0;i<4;++i){defs[i].GUID=0x12340000+i;defs[i].InteractionGroupID=i==0?-1:i==1?1:i==2?-2:2;
  objects[i].OBJ=&defs[i];objects[i].GUID=defs[i].GUID;objects[i].Resource=&resource;}
 VMGameObject left(&objects[0],nullptr),leftPart(&objects[1],nullptr),right(&objects[2],nullptr),rightPart(&objects[3],nullptr);
 VMMultitileGroup group;group.Objects={&left,&leftPart,&right,&rightPart};
 for(auto e:group.Objects){e->MultitileGroup=&group;e->Thread=std::make_unique<VMThread>(&context,e,8);}
 VMAvatar sleeper(&objects[0]),awake(&objects[0]);sleeper.ObjectID=10;awake.ObjectID=11;
 sleeper.Thread=std::make_unique<VMThread>(&context,&sleeper,8);awake.Thread=std::make_unique<VMThread>(&context,&awake,8);
 context.SetToNextCache->Avatars={&sleeper,&awake};vm.Entities={&left,&leftPart,&right,&rightPart,&sleeper,&awake};
 auto use=std::make_shared<VMQueuedAction>();use->Callee=&right;sleeper.Thread->Queue={use};sleeper.Thread->ActiveQueueBlock=0;
 require(group.GetInteractionGroupLeader(&leftPart)==&left&&group.GetInteractionGroupLeader(&rightPart)==&right,
  "two interaction groups retain distinct exact leaders");
 require(left.GetValue(VMStackObjectVariable::UseCount)==0&&right.GetValue(VMStackObjectVariable::UseCount)==1,
  "TS1 empty leader stays unused while opposite leader has an active user");
 // Authored generic availability tree: StackObject UseCount equals zero.
 auto check=new BHAV();check->ChunkID=4097;check->ChunkProcessed=true;
 BHAVInstruction test;test.Opcode=2;test.TruePointer=254;test.FalsePointer=255;
 VMExpressionOperand operand;operand.Operator=VMExpressionOperator::Equals;operand.LhsOwner=Scopes::VMVariableScope::StackObject;
 operand.LhsData=int16_t(VMStackObjectVariable::UseCount);operand.RhsOwner=Scopes::VMVariableScope::Literal;operand.RhsData=0;
 operand.Write(test.Operand.data());check->Instructions.push_back(test);iff.AddChunk(check);
 auto available=[&](VMEntity* target){auto action=std::make_shared<VMQueuedAction>();action->Callee=action->StackObject=target;
  action->CodeOwner=target->Object;action->CheckRoutine=vm.Assemble(check);return bool(awake.Thread->CheckTS1Action(action,false));};
 require(available(&left)&&!available(&right),"real TS1 action check admits free leader and rejects occupied leader");
 auto frame=std::make_shared<VMStackFrame>();frame->Callee=&right;sleeper.Thread->Stack.push_back(frame);
 require(left.IsInUse(&context,true)&&right.IsInUse(&context,false)&&!left.IsInUse(&context,false),
  "group-wide reservation still sees the opposite part's live callee");
 sleeper.Thread->ActiveQueueBlock=-1;
 require(left.UseCount()==0&&right.UseCount()==0,"pending/inactive queue entries do not reserve either leader");
 sleeper.Thread->ActiveQueueBlock=1;sleeper.Thread->Queue.push_back(use);
 require(right.UseCount()==1,"repeated active entries for one avatar retain deduplication");
 auto own=std::make_shared<VMQueuedAction>();own->Callee=&left;awake.Thread->Queue={own};awake.Thread->ActiveQueueBlock=0;
 require(left.UseCount()==1&&right.UseCount()==1&&!available(&left),"own active user still blocks the same leader");
 own->Callee=&right;
 require(left.UseCount()==0&&right.UseCount()==2,"two active users of one exact instance are both retained");
 vm.TS1=false;
 require(left.UseCount()==2&&right.UseCount()==2,"TSO group aggregation retains both active users");
 left.ObjectData.at(size_t(VMStackObjectVariable::UseCount))=123;
 require(left.GetValue(VMStackObjectVariable::UseCount)==123,"TSO word62 remains the stored field");
 left.MultitileGroup=nullptr;vm.TS1=true;own->Callee=&left;
 require(left.UseCount()==1,"single-instance active use is unchanged");
 sleeper.Thread.reset();awake.Thread.reset();context.SetToNextCache->Avatars.clear();vm.Entities.clear();
}
int main(){try{
 VMEntity::UseWorld=false;instanceUseContract();
 IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"player-action-fixture",true);
 OBJD definition;definition.GUID=0x12345678;definition.ChunkLabel="fixture";
 FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
 VMAvatar actor(&object);actor.ObjectID=1;VMGameObject target(&object,nullptr);target.ObjectID=2;
 simapp::LotSession session;session._Ctx=std::make_unique<VMContext>(nullptr);session._VM=std::make_unique<VM>(session._Ctx.get(),nullptr);session._VM->TS1=true;
 actor.Thread=std::make_unique<VMThread>(session._Ctx.get(),&actor,8);session.Family={&actor};session.Selected=0;
 auto existing=std::make_shared<VMQueuedAction>();existing->UID=41;existing->Priority=100;existing->Callee=&target;actor.Thread->Queue.push_back(existing);
 simapp::PieChoice choice;choice.ID=7;choice.Param0=19;choice.Name="fixture action";
 auto rejected=[&](const char* why){const auto before=actor.Thread->Queue;require(!session.PushInteraction(&target,false,choice)&&actor.Thread->Queue==before&&existing->UID==41&&!existing->UserQueued,why);};
 rejected("no interaction table reports false and preserves queue/UID");
 TTAB table;target.TreeTable=&table;rejected("missing interaction reports false and preserves queue/UID");
 auto interaction=std::make_shared<TTABInteraction>();interaction->ActionFunction=4096;table.InteractionByIndex[7]=interaction.get();
 rejected("missing action BHAV reports false and preserves queue/UID");
 BHAV routine;routine.ChunkID=4096;routine.ChunkProcessed=true;routine.Instructions.emplace_back();routine.Instructions[0].TruePointer=254;routine.Instructions[0].FalsePointer=255;iff.AddChunk(&routine);
 interaction->TestFunction=4097;rejected("missing nonzero check BHAV reports false and preserves queue/UID");interaction->TestFunction=0;
 require(session.PushInteraction(&target,false,choice),"available action reports true through actual enqueue");
 require(actor.Thread->Queue.size()==2&&actor.Thread->Queue[0]==existing&&!existing->UserQueued,"existing queued work retains identity and provenance");
 auto added=actor.Thread->Queue[1];require(added->UID==0&&added->UserQueued&&added->Callee==&target&&added->ActionRoutine&&added->ActionRoutine->ID==4096&&added->InteractionNumber==7&&added->Args==std::vector<int16_t>({19,0,0,0}),"new player action carries actual UID/callee/routine/arguments");
 require(!session.PushInteraction(nullptr,false,choice),"null target rejected");session.Selected=-1;require(!session.PushInteraction(&target,true,choice),"missing selected actor rejects goto before allocation");
 session.Family.clear();actor.Thread.reset();std::puts("player action contract complete; no game assets, simulation updates or saves");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
