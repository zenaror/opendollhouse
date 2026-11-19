// SPDX-License-Identifier: MPL-2.0
#include "common/TS1/TS1PersonBirth.h"
#include "core/io.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMTS1MotiveDecay.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/primitives/VMExpression.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/NBRS.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Engine::Utils;
using namespace FSO::SimAntics::Primitives;
using FSO::SimAntics::Engine::Scopes::VMVariableScope;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace Birth=FSO::Common::TS1;
namespace {
int checks=0;void Need(bool condition,const char* text){if(!condition)throw std::runtime_error(text);++checks;}
struct ObservedAvatar:VMAvatar {
 explicit ObservedAvatar(FSO::Content::GameObject* object):VMAvatar(object){}
 int FirstTicks=0,FirstArg=-1;int16_t FirstStack=-1;int64_t FirstCounter=-1;int32_t ModelTimestamp=0;
 Entities::VMTS1MotiveDecay ObservedDecay;
 void Tick() override {
  ++FirstTicks;FirstArg=Thread->Stack.front()->Args[0];FirstStack=Thread->Stack.front()->StackObjectID();
  FirstCounter=TS1PersonTickCounter(Thread->Context->Clock->Ticks);ModelTimestamp=TS1AnimationTimestamp();
  ObservedDecay.Tick(this,Thread->Context);
  Thread->TempRegisters[4]=40;auto q=std::make_shared<VMQueuedAction>();q->Callee=this;Thread->Queue.push_back(q);
 }
};
struct Fixture {
 VMContext C{nullptr};VM V{&C,nullptr};IffFile Iff;OBJD Def;
 FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"birth-fixture",true};
 FSO::Content::GameObject Object;Entities::VMMultitileGroup Group;
 NBRS Registry;std::unique_ptr<FSO::Content::TS1::TS1NeighborhoodProvider> Old;
 std::unique_ptr<ObservedAvatar> Actor;Neighbour* Neighbor=nullptr;
 Fixture(){
  C.Clock->Ticks=236596;
  V.TS1=true;V.GlobalState.resize(38,0);
  auto* content=FSO::Content::Content::Get();Old=std::move(content->Neighborhood);
  content->Neighborhood=std::make_unique<FSO::Content::TS1::TS1NeighborhoodProvider>(content,false);content->Neighborhood->Neighbors=&Registry;
  Def.GUID=0xf001b124;Def.ObjectType=OBJDType::Person;Def.NumAttributes=8;Object.OBJ=&Def;Object.GUID=Def.GUID;Object.Resource=&Resource;
  auto n=std::make_unique<Neighbour>();Neighbor=n.get();n->NeighbourID=2;n->GUID=Def.GUID;n->PersonMode=10;n->PersonData.assign(88,100);Registry.NeighbourByID[2]=n.get();Registry.DefaultNeighbourByGUID[Def.GUID]=2;Registry.Entries.push_back(std::move(n));
  Actor=std::make_unique<ObservedAvatar>(&Object);Group.AddObject(Actor.get());Actor->MultitileGroup=&Group;V.AddEntity(Actor.get());
  for(int i=0;i<16;++i)Actor->SetMotiveDataFloat((Model::VMMotive)i,20.25f);
  Actor->SnapshotMotiveData();Actor->SetMotiveDataFloat(Model::VMMotive::Hunger,24.75f);
  Actor->SetPersonData((VMPersonDataVariable)32,1);Actor->SetPersonData((VMPersonDataVariable)34,3);Actor->SetPersonData((VMPersonDataVariable)35,9);
  auto* main=Add(4096,4);BHAVInstruction wait;wait.Opcode=0;wait.TruePointer=254;wait.FalsePointer=254;main->Instructions.push_back(wait);
  auto* init=Add(4097,5);
  Expr(init,VMVariableScope::MyObjectAttributes,0,VMExpressionOperator::PlusEquals,VMVariableScope::Literal,1);
  Expr(init,VMVariableScope::MyObjectAttributes,1,VMExpressionOperator::Assign,VMVariableScope::Parameters,4);
  Expr(init,VMVariableScope::Temps,4,VMExpressionOperator::PlusEquals,VMVariableScope::Literal,1);
  Expr(init,VMVariableScope::MyPersonData,46,VMExpressionOperator::Assign,VMVariableScope::Literal,99);End(init);
  auto* visitor=Add(4100,5);visitor->ChunkLabel="init visitor";
  Expr(visitor,VMVariableScope::MyObjectAttributes,3,VMExpressionOperator::PlusEquals,VMVariableScope::Literal,1);
  Expr(visitor,VMVariableScope::MyObjectAttributes,4,VMExpressionOperator::Assign,VMVariableScope::Parameters,4);
  Expr(visitor,VMVariableScope::MyObjectAttributes,5,VMExpressionOperator::Assign,VMVariableScope::StackObjectID,0);End(visitor);
  Resource.TreeByName.emplace("init visitor",FSO::Content::VMTreeByNameTableEntry(visitor));
  Actor->EntryPoints[0].ActionFunction=4097;Actor->EntryPoints[1].ActionFunction=4096;
 }
 ~Fixture(){FSO::Content::Content::Get()->Neighborhood=std::move(Old);}
 BHAV* Add(int id,int args){auto*b=new BHAV();b->ChunkID=id;b->ChunkProcessed=true;b->Args=args;Iff.AddChunk(b);return b;}
 void Expr(BHAV*b,VMVariableScope lhs,int data,VMExpressionOperator op,VMVariableScope rhs,int value){
  BHAVInstruction i;i.Opcode=2;i.TruePointer=uint8_t(b->Instructions.size()+1);i.FalsePointer=253;
  VMExpressionOperand e;e.LhsOwner=lhs;e.LhsData=data;e.RhsOwner=rhs;e.RhsData=value;e.Operator=op;e.Write(i.Operand.data());b->Instructions.push_back(i);
 }
 void End(BHAV*b){b->Instructions.back().TruePointer=254;}
};
void FiltersAndPets(){
 Fixture f;
 for(int mode=0;mode<=10;++mode){
  f.Neighbor->PersonMode=mode;for(int i=0;i<88;++i){f.Neighbor->PersonData[i]=int16_t(100+i);f.Actor->SetPersonData((VMPersonDataVariable)i,-9);}
  VMTS1Neighbourhood::ApplyBirthProfile(f.Actor.get());
  Need(f.Actor->GetPersonData((VMPersonDataVariable)31)==2,"broad profile repairs neighbor identity");
  Need(f.Actor->GetPersonData((VMPersonDataVariable)61)==161,"broad mode0 still copies family field");
  Need(f.Actor->GetPersonData((VMPersonDataVariable)32)==-9&&f.Actor->GetPersonData((VMPersonDataVariable)33)==-9,"broad source filter does not overwrite runtime type/priority");
  Need(f.Actor->GetPersonData((VMPersonDataVariable)65)==(mode>=3?165:-9),"gender filter uses source mode threshold3");
  Need(f.Actor->GetPersonData((VMPersonDataVariable)80)==(mode>=8?180:-9),"extension requires source mode8");
  f.Actor->SetPersonData((VMPersonDataVariable)2,-7);f.Actor->SetPersonData((VMPersonDataVariable)46,-7);f.Actor->SetPersonData((VMPersonDataVariable)31,-7);
  VMTS1Neighbourhood::RefreshBirthInterests(f.Actor.get());
  Need(f.Actor->GetPersonData((VMPersonDataVariable)31)==2,"narrow mode0 repairs identity too");
  Need(f.Actor->GetPersonData((VMPersonDataVariable)2)==-7,"narrow profile never copies broad-only fields");
  Need(f.Actor->GetPersonData((VMPersonDataVariable)46)==(mode>=7?146:-7),"narrow profile requires mode7");
 }
 f.Neighbor->PersonMode=3;
 for(int gender=2;gender<=5;++gender){f.Neighbor->PersonData[65]=gender;VMTS1Neighbourhood::ApplyBirthProfile(f.Actor.get());Need(f.Actor->GetPersonData((VMPersonDataVariable)65)==gender&&f.Actor->IsPet()&&f.Actor->IsDog()==(gender<4)&&f.Actor->IsCat()==(gender>=4),"raw TS1 pet species remains usable after profile import");}
}
void OrderedBirth(){
 Fixture f;const auto clock=f.C.Clock->Ticks;const auto seed=f.C.TS1SimulationRandomSeed;
 f.Actor->InitTS1FreshBirth(&f.C,true);
 Need(f.Actor->GetAttribute(0)==2&&f.Actor->FirstTicks==1,"two actual authored Init calls bracket one immediate virtual Tick");
 Need(f.Actor->FirstCounter==0&&f.Actor->ObservedDecay.LastTick==0&&f.Actor->GetMotiveDataFloat(Model::VMMotive::Hunger)==24.75f&&f.Actor->GetOldMotiveDataFloat(Model::VMMotive::Hunger)==20.25f,"first Tick0 preserves fractional motives, old bank and decay counter");
 Need(f.Actor->ModelTimestamp==int32_t(clock*50),"first Tick0 does not replace the independent animation clock");
 Need(f.Actor->FirstArg==0&&f.Actor->FirstStack==0,"Main initially has explicit zero args/StackOID before delayed creator patches");
 Need(f.Actor->GetAttribute(1)==40&&f.Actor->Thread->TempRegisters[4]==41,"second Init copies current fifth temporary on same interpreter");
 Need(f.Actor->Thread->Queue.size()==1,"second Init preserves first Tick queue effects instead of fresh-only rejection");
 Need(f.Actor->GetPersonData((VMPersonDataVariable)46)==100,"post-Init filtered profile restores saved interest value");
 Need(f.Actor->GetPersonData((VMPersonDataVariable)32)==1&&f.Actor->GetPersonData((VMPersonDataVariable)34)==3&&f.Actor->GetPersonData((VMPersonDataVariable)35)==9,"source reset preserves runtime type/greeting/schedule");
 auto* thread=f.Actor->Thread.get();f.Actor->RunTS1VisitorInit(&f.C);
 Need(f.Actor->Thread.get()==thread&&f.Actor->GetAttribute(3)==1&&f.Actor->GetAttribute(4)==41&&f.Actor->GetAttribute(5)==f.Actor->ObjectID,"named visitor runs on new actor interpreter with own StackOID and declared fifth argument");
 f.Actor->SetPersonData((VMPersonDataVariable)32,0);f.Actor->RunTS1VisitorInit(&f.C);Need(f.Actor->GetAttribute(3)==1,"resident type0 skips named visitor");
 Need(f.Actor->TS1PersonTickCounter(clock)==clock,"transient first-Tick argument restores on scope exit");
 Need(f.C.Clock->Ticks==clock&&f.C.TS1SimulationRandomSeed==seed,"synthetic authored callbacks do not advance clock or consume fabricated RNG");
 Fixture master;master.Def.MasterID=1;master.Actor->InitTS1FreshBirth(&master.C,true);Need(master.Actor->GetAttribute(0)==1&&master.Actor->FirstTicks==0,"MasterID excludes base Init/Tick but retains person override Init");
 Fixture noTick;noTick.Actor->InitTS1FreshBirth(&noTick.C,false);Need(noTick.Actor->GetAttribute(0)==2&&noTick.Actor->FirstTicks==0,"reset argument0 suppresses first Tick, not second Init");
}
void InitializationMigrationOrder(){
 Fixture f;
 // Both source profiles expose old 0..10 interests, enabling virtual3c
 //459a40 and then the separate post-twoInit reset459bde migration.
 f.Neighbor->PersonData.assign(88,5);
 auto& natural=Birth::NaturalPersonCRTStream();const auto saved=natural.State;
 struct Restore {Birth::PersonCRTStream& Stream;uint32_t State;~Restore(){Stream.State=State;}} restore{natural,saved};
 natural.State=1;Birth::PersonCRTStream expected(1);
 std::array<int16_t,101> first{},second{};
 Birth::MigratePersonInterests(first,expected);Birth::MigratePersonInterests(second,expected);
 const auto owner=f.C.TS1SimulationRandomSeed;
 f.Actor->InitTS1FreshBirth(&f.C,false);
 Need(natural.State==expected.State,"virtual3c and reset migrations consume one independent stream in source order");
 for(int index:{46,47,48,49,50,51,52,53,54,55,13,14,16,20,26})
  Need(f.Actor->GetPersonData((VMPersonDataVariable)index)==second[index],"post-reset fields derive from second source migration");
 Need(f.C.TS1SimulationRandomSeed==owner,"neither migration pads the lot owner RNG");
}
void LegacyHeaderArguments(){
 std::vector<uint8_t> raw(12,0);raw[0]=1;raw[1]=0x80;raw[5]=9;
 BHAV older;st::Reader oldReader(raw,st::Endian::Little);older.Read(nullptr,oldReader);
 Need(older.Args==4,"source8001 argument declaration is capped at four");
 raw[5]=2;BHAV small;st::Reader smallReader(raw,st::Endian::Little);small.Read(nullptr,smallReader);
 Need(small.Args==2,"source8001 smaller argument declaration is retained");
 for(int declared:{0,4,255}){
  raw[5]=uint8_t(declared);BHAV boundary;st::Reader input(raw,st::Endian::Little);boundary.Read(nullptr,input);
  Need(boundary.Args==std::min(declared,4),"source8001 zero and upper argument boundaries");
 }
 for(size_t size=4;size<12;++size){
  auto truncated=raw;truncated.resize(size);bool rejected=false;
  try{BHAV invalid;st::Reader input(truncated,st::Endian::Little);invalid.Read(nullptr,input);}
  catch(const std::exception&){rejected=true;}
  Need(rejected,"partial8001 metadata must not read beyond the header");
 }
 raw[0]=2;raw[5]=9;BHAV modern;st::Reader modernReader(raw,st::Endian::Little);modern.Read(nullptr,modernReader);
 Need(modern.Args==9,"source8002 preserves more than four declared arguments");
 raw[0]=0;BHAV oldest;st::Reader oldestReader(raw,st::Endian::Little);oldest.Read(nullptr,oldestReader);
 Need(oldest.Args==4,"source8000 argument bank defaults to four");
}
void Migration(){
 std::array<int16_t,101> bank;bank.fill(-123);Birth::PersonCRTStream stream(1);Birth::MigratePersonInterests(bank,stream);
 const int fields[]={46,47,48,49,50,51,52,53,54,55,13,14,16,20,26};
 const int values[]={1000,500,700,200,600,800,400,0,0,0,400,900,1000,300,600};
 for(int i=0;i<15;++i)Need(bank[fields[i]]==values[i],"fixed-seed source CRT migration field vector");
 Need(stream.State==1682158244u&&bank[32]==-123&&bank[65]==-123,"migration preserves unrelated bank and exact independent stream state");
 for(uint32_t seed=0;seed<256;++seed){Birth::PersonCRTStream rng(seed);Birth::MigratePersonInterests(bank,rng);int counts[3]={};for(int i=46;i<56;++i){Need(bank[i]>=0&&bank[i]<=1000&&bank[i]%100==0,"natural-domain migration bounds");++counts[bank[i]<400?0:bank[i]<700?1:2];}Need(counts[0]==4&&counts[1]==3&&counts[2]==3,"source first quota distribution");}
}
}
int main(){try{VM::UseWorld(false);FiltersAndPets();LegacyHeaderArguments();OrderedBirth();InitializationMigrationOrder();Migration();std::printf("PASS %d original person birth controls\n",checks);return 0;}catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
