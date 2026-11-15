// SPDX-License-Identifier: MPL-2.0
// Synthetic real lifecycle/restore consumers; no game files, Content::Init or world ticks.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/model/VMSetToNextCache.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/primitives/VMExpression.h"
#include "simantics/utils/VMWorldActivator.h"
#include "simantics/utils/VMTS1ObjectNames.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/OBJM.h"
#include "files/formats/iff/chunks/OBJT.h"
#include "files/formats/iff/chunks/BHAV.h"
#include "files/formats/iff/chunks/SLOT.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Engine;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using Scope=FSO::SimAntics::Engine::Scopes::VMVariableScope;
static int checks=0;
static void check(bool ok,const char* s){if(!ok)throw std::runtime_error(s);++checks;printf("PASS %s\n",s);}
// Access the real saved-field and containment consumer without a production hook.
template<class T,typename T::Type M>struct Access{friend typename T::Type field(T){return M;}};
struct RestoreField{using Type=void(VMWorldActivator::*)(OBJM*);friend Type field(RestoreField);};
template struct Access<RestoreField,&VMWorldActivator::RestoreSavedState>;
using Op=Primitives::VMExpressionOperator;
static BHAVInstruction expr(int index,int value,Op op,int yes,int no=255){BHAVInstruction i;i.Opcode=2;i.TruePointer=yes;i.FalsePointer=no;Primitives::VMExpressionOperand a;a.LhsOwner=Scope::MyObjectAttributes;a.LhsData=index;a.RhsOwner=Scope::Literal;a.RhsData=value;a.Operator=op;a.Write(i.Operand.data());return i;}
static void tree(IffFile& iff,int id,std::initializer_list<BHAVInstruction> code){auto b=new BHAV();b->ChunkProcessed=true;b->ChunkID=id;b->Args=4;b->Instructions=code;iff.AddChunk(b);}
struct ObjectsField{using Type=std::unordered_map<int16_t,VMEntity*> VM::*;friend Type field(ObjectsField);};
template struct Access<ObjectsField,&VM::ObjectsById>;
static OBJTEntry type(uint32_t guid){st::Writer w(st::Endian::Little);w.WriteUInt32(guid);for(int i=0;i<4;++i)w.WriteUInt16(0);w.WriteUInt16(1);w.WriteUInt16(uint16_t(OBJDType::Normal));w.WriteByte('x');w.WriteByte(0);auto b=w.TakeArray();st::Reader in(b);return OBJTEntry(in,2);}
int main(){try{
 VM::UseWorld(false);VMContext ctx(nullptr);VM vm(&ctx,nullptr);vm.TS1=false;
 ctx.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&ctx));ctx.Architecture()->Rooms[0].Map.assign(64,0);ctx.RoomInfo.resize(1);
 IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"saved-lifecycle-fixture",true);
 tree(iff,0x1000,{expr(0,13,Op::Assign,1),expr(8,0,Op::Assign,254)});
 tree(iff,0x1001,{expr(2,9,Op::Assign,254)});
 tree(iff,0x1003,{expr(6,77,Op::Assign,254)});
 auto slotRead=expr(5,1,Op::Assign,254);{Primitives::VMExpressionOperand a;a.LhsOwner=Scope::MyObjectAttributes;a.LhsData=5;a.RhsOwner=Scope::MySlot;a.RhsData=1;a.Operator=Op::Assign;a.Write(slotRead.Operand.data());}tree(iff,0x1008,{slotRead});
 BHAVInstruction remove;remove.Opcode=18;remove.TruePointer=254;remove.FalsePointer=254;
 tree(iff,0x100b,{expr(8,0,Op::Equals,1,254),remove});
 auto slots=new SLOT();slots->ChunkProcessed=true;slots->ChunkID=128;slots->Slots[0].resize(2);iff.AddChunk(slots);
 OBJD def;def.GUID=0x12345678;def.NumAttributes=9;def.StackSize=32;def.SlotID=128;def.BHAV_Init=0x1000;def.BHAV_MainID=0x1001;def.BHAV_UserPlace=0x100b;def.BHAV_Cleanup=0x1003;def.BHAV_DynamicMultiTileUpdate=0x1008;
 FSO::Content::GameObject object;object.OBJ=&def;object.GUID=def.GUID;object.Resource=&resource;
 OBJD childDef=def;childDef.GUID=0x23456789;childDef.BHAV_DynamicMultiTileUpdate=0;FSO::Content::GameObject childObject;childObject.OBJ=&childDef;childObject.GUID=childDef.GUID;childObject.Resource=&resource;
 auto make=[&](int id){auto e=static_cast<VMGameObject*>(vm.OwnEntity(new VMGameObject(id==11?&childObject:&object,nullptr)));auto g=vm.OwnGroup(new Entities::VMMultitileGroup());e->ObjectID=id;e->MultitileGroup=g;g->AddObject(e);vm.Entities.push_back(e);(vm.*field(ObjectsField{}))[id]=e;ctx.SetToNextCache->NewObject(e);return e;};
 auto saved=make(10);saved->PrepareForRestore(&ctx);
 check(saved->Thread&&saved->Thread->Stack.empty()&&saved->TreeByName==&resource.TreeByName,"saved setup creates thread/name map without Main");
 check(saved->GetAttribute(0)==0&&saved->Contained.size()==2&&saved->PersistID==10,"saved setup retains slots/identity without authored Init");
 auto child=make(11);child->PrepareForRestore(&ctx);saved->Position(FSO::LotView::Model::LotTilePos(64,64,1));child->Position(saved->Position());auto typ=type(def.GUID);auto childType=type(childDef.GUID);OBJM snapshot;snapshot.Decoded=true;
 auto record=[&](int id,int parent,int slot){auto& m=snapshot.ObjectData[id];m.Instance=std::make_shared<OBJMInstance>();auto& s=*m.Instance;s.OBJT=id==11?&childType:&typ;s.Attributes.assign(9,0);s.Attributes[0]=42;s.Attributes[8]=1;s.ObjectData.resize(OBJMInstance::ObjectDataCount);s.ObjectData[int(VMStackObjectVariable::ObjectId)]=id;s.ObjectData[int(VMStackObjectVariable::Flags)]=int16_t(VMEntityFlags::HasZeroExtent);s.ObjectData[int(VMStackObjectVariable::PlacementFlags)]=int16_t(VMPlacementFlags::OnTerrain);s.ObjectData[int(VMStackObjectVariable::MovementFlags)]=5;s.ObjectData[int(VMStackObjectVariable::FlagField2)]=64;s.ObjectData[int(VMStackObjectVariable::SlotCount)]=2;s.ObjectData[int(VMStackObjectVariable::Category)]=7;m.ContainerID=parent;m.ContainerSlot=slot;};
 record(10,0,0);record(11,10,1);VMWorldActivator activator(&vm,nullptr);(activator.*field(RestoreField{}))(&snapshot);
 check(saved->GetAttribute(0)==42&&saved->GetAttribute(8)==1,"real restore consumer preserves saved attributes");
 check(saved->GetValue(VMStackObjectVariable::Flags)==int16_t(VMEntityFlags::HasZeroExtent)&&saved->GetValue(VMStackObjectVariable::PlacementFlags)==int16_t(VMPlacementFlags::OnTerrain)&&saved->GetValue(VMStackObjectVariable::MovementFlags)==5&&saved->GetValue(VMStackObjectVariable::FlagField2)==64&&saved->GetValue(VMStackObjectVariable::SlotCount)==2,"saved collision/placement/movement/slot policies precede publication");
 check(child->Container==saved&&child->ContainerSlot==1&&saved->GetSlot(1)==child,"actual restore binds saved child slot");
 check(saved->GetAttribute(5)==11,"actual in-world dynamic entry8 sees saved child in its slot on first publication");
 auto category=ctx.SetToNextCache->GetObjectsByCategory(7);check(category&&std::find(category->begin(),category->end(),saved)!=category->end(),"restored category reaches actual SetToNext category consumer");
 saved->MultitileGroup->Name="persistent instance";auto names=FSO::SimAntics::Utils::VMTS1ObjectNames::Capture({saved,child},17);saved->MultitileGroup->Name.clear();FSO::SimAntics::Utils::VMTS1ObjectNames::Restore(&vm,names,17);check(saved->Name()=="persistent instance"&&!saved->Dead,"strict names restore sees persistent identity");
 bool refused=false;(vm.*field(ObjectsField{})).erase(10);try{FSO::SimAntics::Utils::VMTS1ObjectNames::Restore(&vm,names,17);}catch(const std::runtime_error&){refused=true;}(vm.*field(ObjectsField{}))[10]=saved;check(refused,"missing named identity remains rejected");
 check(saved->Thread->Stack.empty(),"saved state does not start Main before continuation decision");saved->StartMainAfterRestore(&ctx);check(!saved->Thread->Stack.empty()&&saved->GetAttribute(2)==0,"classic Main queues after state without executing a tick");
 auto actor=static_cast<VMAvatar*>(vm.OwnEntity(new VMAvatar(&object)));auto actorGroup=vm.OwnGroup(new Entities::VMMultitileGroup());actor->ObjectID=40;actor->MultitileGroup=actorGroup;actorGroup->AddObject(actor);vm.Entities.push_back(actor);(vm.*field(ObjectsField{}))[40]=actor;actor->PrepareForRestore(&ctx);
 IffFile globalsIff;FSO::Content::GameGlobalResource globalsResource(&globalsIff,nullptr);FSO::Content::GameGlobal globals;globals.Resource=&globalsResource;ctx.Globals=&globals;
 record(40,0,0);auto& person=snapshot.ObjectData.at(40).Instance->PersonData;snapshot.ObjectData.at(40).Instance->HasPersonData=true;person.PersonData.resize(101);person.PersonData[int(VMPersonDataVariable::Priority)]=100;person.PersonData[int(VMPersonDataVariable::NonInterruptable)]=1;person.PersonData[int(VMPersonDataVariable::Posture)]=1;person.PersonData[int(VMPersonDataVariable::BodySkill)]=321;person.PersonData[int(VMPersonDataVariable::RenderDisplayFlags)]=7;person.PersonData[int(VMPersonDataVariable::MoneyAmmountOverHead)]=999;person.MotiveData[int(VMMotive::Hunger)]=73;person.MotiveData[int(VMMotive::SleepState)]=1;
 (activator.*field(RestoreField{}))(&snapshot);
 check(actor->GetPersonData(VMPersonDataVariable::Priority)==100&&actor->GetPersonData(VMPersonDataVariable::NonInterruptable)==1&&actor->GetMotiveData(VMMotive::SleepState)==1,"full snapshot state remains intact before an explicit C2 restart");
 check(actor->GetPersonData(VMPersonDataVariable::RenderDisplayFlags)==7&&actor->GetPersonData(VMPersonDataVariable::MoneyAmmountOverHead)==999&&!actor->Headline,"bulk saved-person installation preserves display word and emits no money headline");
 // Real classic format sizes differ from the 101-word runtime/ODHI bank.
 auto extended=person.PersonData;extended.resize(256,0);extended[100]=37;extended[255]=1234;
 actor->RestorePersonData(extended,person.MotiveData);bool prefix=true;
 for(int i=0;i<=100;++i)prefix=prefix&&actor->GetPersonData((VMPersonDataVariable)i)==extended[size_t(i)];
 check(prefix,"ObjM256 installs all 101 supported words without treating reserved tail as runtime overflow");
 auto classic=person.PersonData;classic.resize(80);actor->RestorePersonData(classic,person.MotiveData);
 check(actor->GetPersonData(VMPersonDataVariable::BodySkill)==321&&actor->GetPersonData(VMPersonDataVariable::RenderDisplayFlags)==7&&actor->GetPersonData((VMPersonDataVariable)80)==0&&actor->GetPersonData((VMPersonDataVariable)100)==0,"ObjM80 installs classic prefix and clears absent runtime extension");
 actor->RestorePersonData(person.PersonData,person.MotiveData);
 auto action=std::make_shared<VMQueuedAction>();action->Callee=saved;action->StackObject=saved;action->CodeOwner=&object;action->ActionRoutine=vm.Assemble(resource.Get<BHAV>(0x1001));action->Priority=50;action->Flags=TTABFlags::MustRun;actor->Thread->Queue.push_back(action);
 check(!actor->Thread->AttemptPush(),"saved high priority without old continuation blocks a new action before C2 cleanup");
 actor->RestartAfterRestore(&ctx);check(actor->GetPersonData(VMPersonDataVariable::Priority)==0&&actor->GetPersonData(VMPersonDataVariable::NonInterruptable)==0&&actor->GetPersonData(VMPersonDataVariable::Posture)==0&&actor->GetMotiveData(VMMotive::SleepState)==0,"classic C2 restart releases priority/noninterruptable and standing/sleep state");
 check(actor->GetPersonData(VMPersonDataVariable::BodySkill)==321&&actor->GetMotiveData(VMMotive::Hunger)==73,"classic C2 cleanup preserves skill and hunger");actor->Thread->Queue.push_back(action);check(actor->Thread->AttemptPush(),"same actual queue consumer accepts action after classic C2 cleanup");check(actor->GetAttribute(6)==0,"human classic restart does not execute object Reset3 as in both pins");
 saved->SetFlag(VMEntityFlags::Occupied,true);saved->RestartAfterRestore(&ctx);check(!saved->GetFlag(VMEntityFlags::Occupied)&&saved->GetFlag(VMEntityFlags::HasZeroExtent),"classic abandoned occupancy clears without erasing durable collision flags");check(saved->GetAttribute(6)==77,"ordinary object classic restart retains authored Reset3");
 auto fresh=make(20);fresh->Init(&ctx);check(fresh->GetAttribute(0)==13&&fresh->GetAttribute(8)==0&&!fresh->Thread->Stack.empty(),"new instance runs authored Init and starts Main");fresh->ExecuteEntryPoint(11,&ctx,true);check(fresh->Dead,"user placement still executes authored removal on default state");
 auto existing=make(30);existing->PrepareForRestore(&ctx);existing->SetAttribute(8,1);existing->ExecuteEntryPoint(11,&ctx,true);check(!existing->Dead,"same placement consumer distinguishes persisted state");
 printf("saved lifecycle checks=%d failures=0\n",checks);return 0;
}catch(const std::exception&e){fprintf(stderr,"saved lifecycle: %s\n",e.what());return 1;}}
