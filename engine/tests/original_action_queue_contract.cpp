// SPDX-License-Identifier: MPL-2.0
// Original DVD contracts through real selection, primitive and queue consumers.
// Synthetic resources need no commercial data; actual UI/save flows are separate.
#include "content/ContentManager.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/model/VMSetToNextCache.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/primitives/VMExpression.h"
#include "simantics/primitives/VMFindBestAction.h"
#include "simantics/primitives/VMGosubFoundAction.h"
#include "simantics/primitives/VMIdleForInput.h"
#include "simantics/primitives/VMSleep.h"
#include "simantics/primitives/VMPushInteraction.h"
#include "simantics/utils/VMTS1ActionIcon.h"
#include "app/ui/OriginalPCUI.h"
#include "files/formats/iff/chunks/TTAs.h"
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using Scope=Scopes::VMVariableScope;
static int checks=0,failures=0;
static void require(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
static void scenario(const char* name,const std::function<void()>& run){try{run();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
template<class T,typename T::Type M>struct Access{friend typename T::Type field(T){return M;}};
struct ObjectsField{using Type=std::unordered_map<int16_t,VMEntity*> VM::*;friend Type field(ObjectsField);};
template struct Access<ObjectsField,&VM::ObjectsById>;
struct NextField{using Type=void(VMThread::*)();friend Type field(NextField);};
template struct Access<NextField,&VMThread::NextInstruction>;
struct CacheLifetime{~CacheLifetime(){VM::ReleaseAssembled();}};
static BHAVInstruction expression(int attribute,int value,VMExpressionOperator op,uint8_t yes=254,uint8_t no=255){
 BHAVInstruction i;i.Opcode=2;i.TruePointer=yes;i.FalsePointer=no;
 VMExpressionOperand a;a.LhsOwner=Scope::MyObjectAttributes;a.LhsData=attribute;
 a.RhsOwner=Scope::Literal;a.RhsData=value;a.Operator=op;a.Write(i.Operand.data());return i;
}
struct Fixture{
 CacheLifetime lifetime;
 VMContext context{nullptr};VM vm{&context,nullptr};IffFile iff;
 FSO::Content::GameObjectResource resource{&iff,nullptr,nullptr,"original-queue-fixture",true};
 OBJD definition;FSO::Content::GameObject object;
 std::unique_ptr<VMAvatar> actor;std::unique_ptr<VMGameObject> target;
 Entities::VMMultitileGroup actorGroup,targetGroup;TTAB table;TTAs strings;
 Fixture(){
  vm.GlobalState.resize(64);definition.GUID=0x12345678;definition.NumAttributes=4;
  object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
  actor=std::make_unique<VMAvatar>(&object);target=std::make_unique<VMGameObject>(&object,nullptr);
  actor->ObjectID=1;target->ObjectID=2;actor->MultitileGroup=&actorGroup;target->MultitileGroup=&targetGroup;
  actorGroup.AddObject(actor.get());targetGroup.AddObject(target.get());
  actor->Position(FSO::LotView::Model::LotTilePos(64,64,1));target->Position(FSO::LotView::Model::LotTilePos(80,64,1));
  actor->Thread=std::make_unique<VMThread>(&context,actor.get(),16);target->Thread=std::make_unique<VMThread>(&context,target.get(),16);
  actor->SetPersonData(VMPersonDataVariable::PersonsAge,30);actor->SetPersonData(VMPersonDataVariable::AutonomyLevel,50);
  for(auto need:{VMMotive::Energy,VMMotive::Hunger,VMMotive::Bladder})actor->SetMotiveDataFloat(need,50);
  vm.Entities={actor.get(),target.get()};(vm.*field(ObjectsField{}))[1]=actor.get();(vm.*field(ObjectsField{}))[2]=target.get();
  // VM::AddEntity also registers this query cache. Direct fixture identity
  // binding must not omit the real consumer used by target use counting.
  context.SetToNextCache->NewObject(actor.get());context.SetToNextCache->NewObject(target.get());
  target->TreeTable=actor->TreeTable=&table;target->TreeTableStrings=actor->TreeTableStrings=&strings;
  tree(4096,{expression(1,7,VMExpressionOperator::Assign)});
  tree(4200,{expression(2,1,VMExpressionOperator::PlusEquals)});
  tree(4318,{expression(3,1,VMExpressionOperator::PlusEquals)});
  tree(4319,{expression(2,1,VMExpressionOperator::PlusEquals)});
  tree(4400,{expression(0,1,VMExpressionOperator::PlusEquals)});
  target->EntryPoints[4].ActionFunction=4400;
  auto add=[&](int id,int action,int check){auto row=std::make_unique<TTABInteraction>();row->TTAIndex=id;row->ActionFunction=action;row->TestFunction=check;
   row->AutonomyThreshold=id==0?50:0;row->MotiveEntries.resize(16);row->MotiveEntries[7].EffectRangeMinimum=id==0?9000:0;
   table.InteractionByIndex[id]=row.get();table.Interactions.push_back(std::move(row));};
  add(0,4096,4200);add(33,4318,4319);table.InitAutoInteractions();
  for(int i=0;i<=33;++i){STRItem name;name.Value=i==33?"Cancel Interaction":"Action";strings.AppendString(name);}
  auto global=FSO::Content::Content::Get()->WorldObjectGlobals.get();global->CurvesLoaded=true;
  global->InteractionScore.assign(9,FSO::Common::TS1::TS1Curve("(-100;-100) (100;100)"));global->InteractionScoreChild=global->InteractionScore;
  global->Autonomy.FamilyMinimum=global->Autonomy.VisitorMinimum=0.0000001f;global->Autonomy.RandomSelectionCount=1;
 }
 BHAV* tree(int id,std::initializer_list<BHAVInstruction> instructions){auto b=new BHAV();b->ChunkID=id;b->ChunkProcessed=true;b->Args=4;b->Instructions=instructions;iff.AddChunk(b);return b;}
 std::shared_ptr<VMStackFrame> parent(int opcode=30){
  BHAVInstruction i;i.Opcode=opcode;i.TruePointer=1;i.FalsePointer=2;
  if(opcode==17){VMIdleForInputOperand a;a.AllowPush=1;a.Write(i.Operand.data());}
  auto b=tree(4500,{i,expression(1,11,VMExpressionOperator::Assign),expression(1,22,VMExpressionOperator::Assign)});
  auto f=std::make_shared<VMStackFrame>();f->Caller=f->Callee=actor.get();f->CodeOwner=&object;f->Routine=vm.Assemble(b);f->Args.assign(4,0);actor->Thread->Push(f);return f;
 }
 std::shared_ptr<VMQueuedAction> action(int priority=50){auto a=target->GetAction(0,actor.get(),&context);a->Priority=priority;return a;}
 VMPrimitiveExitCode idle(VMStackFrame* f,uint16_t push=0){VMIdleForInputOperand o;o.AllowPush=push;return VMIdleForInput().Execute(f,&o);}
 void start(){auto p=parent(17);require(actor->Thread->EnqueueAction(action()),"first command accepted");require(idle(p.get(),1)==VMPrimitiveExitCode::CONTINUE,"Idle starts first command");}
};
int main(){try{VM::UseWorld(false);
 scenario("selection is distinct from pending commands",[]{Fixture f;auto p=f.parent();auto pending=f.action(100);f.actor->Thread->EnqueueAction(pending);
  require(VMFindBestAction().Execute(p.get(),nullptr)==VMPrimitiveExitCode::GOTO_TRUE,"chooser succeeds with a higher pending command");
  require(f.actor->Thread->Queue.size()==1&&f.actor->Thread->Queue[0]==pending,"chooser does not enqueue or consume pending input");
  require(p->StackObject==f.target.get()&&f.actor->GetValue(VMStackObjectVariable::TreeTableEntry)==0,"chooser stores original target and TTAB index");
  auto checked=f.actor->GetAttribute(2);require(VMGosubFoundAction().Execute(p.get(),nullptr)==VMPrimitiveExitCode::CONTINUE,"Gosub executes selected action");
  require(f.actor->GetAttribute(2)==checked,"Gosub never reruns chooser check BHAV");
  require(f.actor->Thread->Queue.size()==2&&f.actor->Thread->Queue[1]==pending&&f.actor->Thread->ActiveAction()->Priority==2,"selected action installs active without sorting pending commands");
  require(f.actor->Thread->Stack.back()->Args==std::vector<int16_t>(4,0),"Gosub action receives four zero parameters");
  require(f.target->GetValue(VMStackObjectVariable::UseCount)==1,"installed action owns exact target use");
  f.actor->Thread->Pop(VMPrimitiveExitCode::RETURN_TRUE);
  require(p->InstructionPointer==1&&f.actor->Thread->Queue.size()==1&&f.actor->Thread->Queue[0]==pending&&f.actor->Thread->ActiveQueueBlock==-1,"Gosub return takes true branch and leaves pending input intact");
  require(f.target->GetValue(VMStackObjectVariable::UseCount)==0,"return releases exact target use");
 });
 scenario("Gosub false return and invalid selection",[]{Fixture f;auto p=f.parent();p->StackObject=f.target.get();f.actor->SetValue(VMStackObjectVariable::TreeTableEntry,0);
  require(VMGosubFoundAction().Execute(p.get(),nullptr)==VMPrimitiveExitCode::CONTINUE,"selected action starts");f.actor->Thread->Pop(VMPrimitiveExitCode::RETURN_FALSE);require(p->InstructionPointer==2,"Gosub preserves false action result");
  p->StackObject=nullptr;p->StackObjectRawID=0;bool threw=false;try{VMGosubFoundAction().Execute(p.get(),nullptr);}catch(const VMSimanticsException&){threw=true;}require(threw,"zero selected object faults");
  p->StackObjectRawID=1234;require(VMGosubFoundAction().Execute(p.get(),nullptr)==VMPrimitiveExitCode::GOTO_FALSE,"missing selected instance returns false");
  p->StackObject=f.target.get();f.actor->SetValue(VMStackObjectVariable::TreeTableEntry,99);threw=false;try{VMGosubFoundAction().Execute(p.get(),nullptr);}catch(const VMSimanticsException&){threw=true;}require(threw,"missing selected TTAB entry faults");
  f.actor->SetValue(VMStackObjectVariable::TreeTableEntry,0);f.table.Interactions[0]->ActionFunction=4999;
  require(VMGosubFoundAction().Execute(p.get(),nullptr)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&p->InstructionPointer==2&&f.actor->Thread->Stack.size()==1,"missing action function stops this tick without taking false branch");
  f.tree(4998,{});f.table.Interactions[0]->ActionFunction=4998;
  require(VMGosubFoundAction().Execute(p.get(),nullptr)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,"empty action function retains the same original retry boundary");
 });
 scenario("direct Gosub honors child result even for an immediate-marked row",[]{Fixture f;auto p=f.parent();p->StackObject=f.target.get();f.actor->SetValue(VMStackObjectVariable::TreeTableEntry,0);f.table.Interactions[0]->Flags|=TTABFlags::RunImmediately;
  require(VMGosubFoundAction().Execute(p.get(),nullptr)==VMPrimitiveExitCode::CONTINUE,"direct selected invocation starts");f.actor->Thread->Pop(VMPrimitiveExitCode::RETURN_FALSE);require(p->InstructionPointer==2,"TTAB scheduling flag does not replace direct Gosub return policy");
 });
 scenario("Idle counter and notification are separate",[]{Fixture f;auto p=f.parent(17);p->Args[0]=1;
  require(f.idle(p.get())==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&p->Args[0]==0,"one decrements to zero and still waits this tick");
  require(f.idle(p.get())==VMPrimitiveExitCode::GOTO_TRUE&&!f.actor->GetFlag(VMEntityFlags::NotifiedByIdleForInput),"next zero counter exits without notification");
  p->Args[0]=-1;require(f.idle(p.get())==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&p->Args[0]==-2,"negative counter continues decrementing");
  f.actor->Thread->EnqueueAction(f.action());p->Args[0]=9;
  require(f.idle(p.get())==VMPrimitiveExitCode::GOTO_TRUE&&p->Args[0]==9&&f.actor->GetFlag(VMEntityFlags::NotifiedByIdleForInput),"input notification does not decrement counter");
  require(f.idle(p.get(),2)==VMPrimitiveExitCode::CONTINUE&&p->Args[0]==0,"any nonzero AllowPush starts a command and resets counter");
 });
 scenario("front record, noninterruptible and critical needs",[]{Fixture f;auto p=f.parent(17);f.actor->SetPersonData(VMPersonDataVariable::Priority,50);
  f.actor->Thread->Queue={f.action(50),f.action(100)};
  require(!f.actor->Thread->TS1ShouldNotifyIdle(),"later high priority does not bypass equal-priority front");
  f.actor->Thread->Queue[0]->Flags|=TTABFlags::FSOPushHead;require(f.actor->Thread->TS1ShouldNotifyIdle(),"forced front bypasses equal priority");
  f.actor->SetPersonData(VMPersonDataVariable::NonInterruptable,1);require(!f.actor->Thread->TS1ShouldNotifyIdle(),"noninterruptible gates forced notification");
  f.actor->Thread->Queue.clear();f.actor->SetPersonData(VMPersonDataVariable::NonInterruptable,0);f.actor->SetMotiveDataFloat(VMMotive::Hunger,-99);
  require(!f.actor->Thread->TS1ShouldNotifyIdle(),"critical boundary is strictly below minus99");f.actor->SetMotiveDataFloat(VMMotive::Hunger,-99.25f);require(f.actor->Thread->TS1ShouldNotifyIdle(),"fractional critical hunger wakes residential person");
  f.actor->SetPersonData(VMPersonDataVariable::PersonType,2);require(!f.actor->Thread->TS1ShouldNotifyIdle(),"service role excludes residential critical shortcut");
  f.actor->SetPersonData(VMPersonDataVariable::PersonType,0);f.actor->SetMotiveDataFloat(VMMotive::Hunger,50);f.actor->SetMotiveDataFloat(VMMotive::Energy,-100);f.actor->SetPersonData(VMPersonDataVariable::Gender,2);
  require(!f.actor->Thread->TS1ShouldNotifyIdle(),"pet energy alone does not use human critical shortcut");
 });
 scenario("first Idle handover differs from retry state",[]{Fixture f;f.start();auto& t=*f.actor->Thread;auto first=t.Stack.back();
  first->Routine->Instructions[0].Opcode=17;first->Routine->Instructions[0].TruePointer=254;first->Routine->Instructions[0].FalsePointer=255;
  require(first->ActionTree&&first->PrimitiveState==0,"first action-tree Idle starts with independent zero primitive state");
  auto next=f.action(100);t.EnqueueAction(next);
  require(f.idle(first.get(),1)==VMPrimitiveExitCode::CONTINUE&&first->PrimitiveState==1&&t.ActiveQueueBlock==1,"ordinary command may enter first Idle inside an action tree");
  t.Pop(VMPrimitiveExitCode::RETURN_TRUE);auto ordinary=f.action(100);t.EnqueueAction(ordinary);
  require(f.idle(first.get(),1)==VMPrimitiveExitCode::GOTO_TRUE&&t.ActiveQueueBlock==0&&t.Queue.back()==ordinary,"state1 blocks ordinary handover on resumed action-tree Idle");
  auto forced=f.action(1);forced->Flags|=TTABFlags::FSOPushHead;t.EnqueueAction(forced);
  require(f.idle(first.get(),1)==VMPrimitiveExitCode::CONTINUE&&t.ActiveAction()==forced,"forced head may hand over again from state1");
 });
 scenario("Idle return clears its state when the instruction completes",[]{Fixture f;f.start();auto& t=*f.actor->Thread;auto p=t.Stack.front();
  t.Pop(VMPrimitiveExitCode::RETURN_FALSE);require(p->PrimitiveState==1&&p->InstructionPointer==0,"pushed result is ignored while original parent state1 retries");
  (t.*field(NextField{}))();require(p->InstructionPointer==1&&p->PrimitiveState==0,"actual instruction consumer branches once and clears retry state");
 });
 for(bool childTrue:{false,true})scenario(childTrue?"completed Idle action reaches authored Sleep in the same owner":"failed Idle action reaches authored Sleep in the same owner",[=]{
  Fixture f;auto p=f.parent(17);auto& t=*f.actor->Thread;
  BHAVInstruction idle;idle.Opcode=17;idle.TruePointer=1;idle.FalsePointer=255;
  VMIdleForInputOperand idleOperand;idleOperand.AllowPush=1;idleOperand.Write(idle.Operand.data());
  auto counter=expression(0,1,VMExpressionOperator::Assign,2);VMExpressionOperand assign;
  assign.LhsOwner=Scope::Parameters;assign.LhsData=0;assign.RhsOwner=Scope::Literal;assign.RhsData=1;assign.Operator=VMExpressionOperator::Assign;assign.Write(counter.Operand.data());
  BHAVInstruction sleep;sleep.Opcode=0;sleep.TruePointer=3;sleep.FalsePointer=255;VMSleepOperand wait;wait.StackVarToDec=0;wait.Write(sleep.Operand.data());
  p->Routine=f.vm.Assemble(f.tree(4501,{idle,counter,sleep,expression(3,66,VMExpressionOperator::Assign)}));
  // Start the real queue handover; do not construct a synthetic retry state
  // or call Pop directly. The action's instruction actually returns here.
  auto action=f.action();action->ActionRoutine->Instructions[0].TruePointer=childTrue?254:255;
  require(t.EnqueueAction(action)&&f.idle(p.get(),1)==VMPrimitiveExitCode::CONTINUE,"actual Idle handover did not start the queued action");
  f.context.Clock->Ticks=232856;f.actor->TS1SetSchedulerDelay(-1);
  t.Tick(1000,false);
  require(t.ActiveQueueBlock==-1&&t.Queue.empty()&&t.Stack.size()==1&&t.Stack.back()==p,"completed child did not return to its original parent");
  require(p->InstructionPointer==2&&p->Args[0]==0&&p->PrimitiveState==0&&f.actor->GetAttribute(3)==0,
      "interaction return inserted a tick before the parent's authored Sleep1, or skipped its real yield");
  require(f.context.Clock->Ticks==232856,"interpreter fixture advanced the owner clock itself");
  ++f.context.Clock->Ticks;t.Tick(1000,false);
  require(f.actor->GetAttribute(3)==66&&t.Stack.empty(),"next visit did not complete Sleep0 and its continuation");
 });
 scenario("failed pending checks discard one record per tick",[]{Fixture f;auto p=f.parent(17);auto& t=*f.actor->Thread;
  auto bad=f.action();bad->CheckRoutine->Instructions[0].TruePointer=255;auto row=std::make_unique<TTABInteraction>();row->TTAIndex=1;row->ActionFunction=4096;f.table.InteractionByIndex[1]=row.get();f.table.Interactions.push_back(std::move(row));auto good=f.target->GetAction(1,f.actor.get(),&f.context);
  t.EnqueueAction(bad);t.EnqueueAction(good);p->Args[0]=7;
  require(f.idle(p.get(),1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&p->Args[0]==7&&t.Stack.size()==1&&t.Queue.size()==1&&t.Queue.front()==good,"failed head yields without consuming or starting its successor");
  require(f.target->GetAttribute(0)==1,"failed head runs target queue-skipped cleanup");
  require(f.idle(p.get(),1)==VMPrimitiveExitCode::CONTINUE&&t.ActiveAction()==good,"successor can start on following call");
 });
 for(bool missingCheck:{false,true})scenario(missingCheck?"missing nonzero pending test is rejected":"missing pending TTAB row is rejected",[=]{Fixture f;auto p=f.parent(17);auto& t=*f.actor->Thread;t.EnqueueAction(f.action());
  if(missingCheck)f.table.Interactions[0]->TestFunction=4999;else f.table.InteractionByIndex.erase(0);
  require(f.idle(p.get(),1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&t.Queue.empty()&&t.Stack.size()==1&&f.target->GetAttribute(0)==1,"unavailable pending authored entry is skipped and not executed");
 });
 scenario("pending target becoming burning fails the original permission gate",[]{Fixture f;auto p=f.parent(17);auto& t=*f.actor->Thread;t.EnqueueAction(f.action());f.target->SetFlag(VMEntityFlags::Burning,true);auto checks=f.actor->GetAttribute(2);
  require(f.idle(p.get(),1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&t.Queue.empty()&&f.actor->GetAttribute(2)==checks,"burning blocks pending action before authored test");
 });
 scenario("queue FIFO, priority purge and full ring",[]{Fixture f;auto& t=*f.actor->Thread;auto a=f.action(50),b=f.action(50);require(t.EnqueueAction(a)&&t.EnqueueAction(b)&&t.Queue[0]==a&&t.Queue[1]==b,"equal priorities preserve FIFO");
  auto low=f.action(2);low->UserQueued=true;t.Queue.push_back(low);auto c=f.action(50);require(t.EnqueueAction(c)&&t.Queue.size()==3&&t.Queue.back()==c&&f.target->GetAttribute(0)==1,"higher input purges lower suffix and runs target queue-skipped");
  while(t.Queue.size()<8)require(t.EnqueueAction(f.action(50)),"ring fills to eight pending records");
  require(!t.EnqueueAction(f.action(50))&&t.Queue.size()==8,"full equal-priority ring rejects incoming record");
  t.Queue.back()->Priority=2;auto high=f.action(100);require(t.EnqueueAction(high)&&t.Queue.size()==1&&t.Queue.front()==high,"full ring purges lower suffix before testing capacity");
  t.Queue.clear();for(int i=0;i<8;++i)t.EnqueueAction(f.action(50));auto old=t.Queue;auto forced=f.action(1);forced->Flags|=TTABFlags::FSOPushHead;auto skipped=f.target->GetAttribute(0);
  require(t.EnqueueAction(forced)&&t.Queue.size()==7&&t.Queue.front()==forced,"full forced continuation follows original seven-record result");
  for(size_t i=0;i<6;++i)require(t.Queue[i+1]==old[i],"forced full ring retains first six old records");require(f.target->GetAttribute(0)==skipped,"forced full drop emits no ordinary queue-skipped hook");
 });
 scenario("priority suffix callbacks observe the live ring before prefix restore",[]{Fixture f;auto& t=*f.actor->Thread;
  auto argument=expression(0,2,VMExpressionOperator::Assign,2);VMExpressionOperand parameter;parameter.LhsOwner=Scope::Parameters;parameter.LhsData=0;parameter.RhsOwner=Scope::Literal;parameter.RhsData=2;parameter.Operator=VMExpressionOperator::Assign;parameter.Write(argument.Operand.data());
  BHAVInstruction push;push.Opcode=13;push.TruePointer=push.FalsePointer=254;VMPushInteractionOperand op;op.Priority=VMPushPriority::Autonomous;op.Write(push.Operand.data());
  f.tree(4401,{expression(0,1,VMExpressionOperator::PlusEquals,1),argument,push});f.target->EntryPoints[4].ActionFunction=4401;
  auto kept=f.action(50),low=f.action(10),incoming=f.action(50);t.EnqueueAction(kept);t.EnqueueAction(low);
  require(t.EnqueueAction(incoming)&&t.Queue.size()==2&&t.Queue[0]==kept&&t.Queue[1]==incoming,"callback-added lower actions are overwritten by original prefix restore");
  require(f.target->GetAttribute(0)==7,"callback iteration reacquires live ring up to its eight-record capacity");
 });
 scenario("script push inherits priority and name without suppressing duplicates",[]{Fixture f;f.start();auto& t=*f.actor->Thread;auto active=t.ActiveAction();active->Name="Parent Action";
  f.actor->SetPersonData(VMPersonDataVariable::Priority,0);f.vm.SetGlobalValue(3,f.actor->ObjectID);
  auto p=t.Stack.front();p->StackObject=f.actor.get();p->Args[0]=f.target->ObjectID;
  VMPushInteractionOperand op;op.Priority=VMPushPriority::Inherited;op.Flags=8;
  require(VMPushInteraction().Execute(p.get(),&op)==VMPrimitiveExitCode::GOTO_TRUE&&t.Queue.size()==2,"inherited push enters original pending queue");
  require(t.Queue.back()->Priority==0&&t.Queue.back()->Name=="Action"&&(t.Queue.back()->Flags&TTABFlags::FSOSkipPermissions)==0,"ordinary script push inherits exact person priority without inventing an immediate-context name");
  require(VMPushInteraction().Execute(p.get(),&op)==VMPrimitiveExitCode::GOTO_TRUE&&t.Queue.size()==3,"name inheritance flag is not duplicate suppression");
  op.Flags=12;require(VMPushInteraction().Execute(p.get(),&op)==VMPrimitiveExitCode::GOTO_TRUE&&t.Queue[1]->Name=="Parent Action","forced continuation inherits the active name independently of selection");
  op.Priority=VMPushPriority::ParentIdle;bool threw=false;try{VMPushInteraction().Execute(p.get(),&op);}catch(const VMSimanticsException&){threw=true;}require(threw,"original opcode rejects non-original priority selectors");
 });
 scenario("pending cancellation and active cleanup request",[]{Fixture f;f.start();auto& t=*f.actor->Thread;auto active=t.ActiveAction();auto b=f.action(),c=f.action();t.EnqueueAction(b);t.EnqueueAction(c);
  require(!active->Cancelled,"higher pending command does not mark active record with red X");
  t.CancelAction(b->UID);require(t.Queue.size()==2&&t.ActiveAction()==active&&t.Queue.back()==c&&f.target->GetAttribute(0)==1,"cancel middle preserves active and following command and notifies target");
  auto priority=active->Priority;t.CancelAction(active->UID);require(active->Cancelled&&active->Priority==priority&&f.actor->GetPersonData(VMPersonDataVariable::Priority)==0,"active cancellation preserves record priority and lowers current priority");
  require(t.Queue.size()==2&&t.Queue.back()==c,"existing front wakes cancellation without redundant cleanup command");
 });
 scenario("lone active cancellation stays hidden but executable",[]{Fixture f;f.start();auto& t=*f.actor->Thread;auto active=t.ActiveAction();t.CancelAction(active->UID);
  require(t.Queue.size()==2&&t.Queue.back()->Callee==f.actor.get()&&t.Queue.back()->InteractionNumber==33&&t.Queue.back()->Priority==50,"lone cancel requests original self interaction33");
  auto cleanup=t.Queue.back();require(cleanup->HasHiddenIcon()&&!cleanup->IconOwner()&&!PersistedActionIcon(f.vm,*cleanup)&&cleanup->Mode==VMQueueMode::Normal,"cleanup is iconless without idle scheduling semantics");
  const auto tiles=simapp::OriginalPCUI::QueueTiles(f.actor.get(),800,600);require(tiles.size()==1&&tiles[0].action_index==0&&tiles[0].identity.uid==active->UID,"desktop omits hidden cleanup and retains original queue index");
  t.Pop(VMPrimitiveExitCode::RETURN_TRUE);auto p=t.Stack.back();require(f.idle(p.get(),1)==VMPrimitiveExitCode::GOTO_TRUE,"resumed Idle does not take ordinary cleanup before its own branch");
 });
 scenario("TSO Gosub remains a no-op",[]{Fixture f;f.vm.TS1=false;auto p=f.parent();require(VMGosubFoundAction().Execute(p.get(),nullptr)==VMPrimitiveExitCode::GOTO_TRUE&&f.actor->Thread->Queue.empty(),"TSO opcode30 retains syslog no-op behavior");});
 std::cout<<checks<<" checks, "<<failures<<" failed scenarios\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
