// SPDX-License-Identifier: MPL-2.0
#include "simantics/utils/VMTS1HierarchyPersistence.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "files/formats/iff/chunks/OBJMWriter.h"
#include "files/formats/iff/IffFieldEncodeWriter.h"
#include "files/formats/iff/IffFieldEncode.h"
#include "../sims3ds/src/simantics/utils/VMTS1InteractionCheckpoint.cpp"
#include <iostream>
#include <sstream>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Engine::Utils;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using H=VMTS1HierarchyPersistence;
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>static void Reject(F&& f,const char* why){try{f();}catch(const std::exception&){return;}throw std::runtime_error(why);}
static OBJTEntry Type(uint32_t guid){st::Writer bytes(st::Endian::Little);bytes.WriteUInt32(guid);for(int i=0;i<4;++i)bytes.WriteUInt16(0);bytes.WriteUInt16(1);bytes.WriteUInt16(uint16_t(OBJDType::Normal));bytes.WriteByte('x');bytes.WriteByte(0);auto raw=bytes.TakeArray();st::Reader reader(raw);return OBJTEntry(reader,2);}
static void WireExtent(){
 auto definition=Type(0x12345678);OBJMInstance instance;instance.Level=1;instance.OBJT=&definition;instance.ObjectData.resize(OBJMInstance::ObjectDataCount);instance.ObjectData[11]=1;
 auto roundtrip=[&](bool slot0){IffFieldEncodeWriter encoded;WriteOBJMInstance(instance,0x49,encoded,slot0);st::Reader bytes(encoded.Data);IffFieldEncode fields(&bytes);OBJMInstance result;result.Read(0x49,fields,[&](int16_t){OBJMResource r;r.OBJT=&definition;return r;});return result;};
 Need(H::SavedExtent(roundtrip(false))==0,"zero source records became an invented slot0 prefix");
 Need(H::SavedExtent(roundtrip(true))==1,"literal source slot0 extent lost");
 instance.LinkedSlot.ObjectID=7;instance.Slots.resize(2);instance.Slots[0].ObjectID=8;instance.Slots[1].ObjectID=9;
 const auto result=roundtrip(true);Need(H::SavedExtent(result)==3&&result.LinkedSlot.ObjectID==7&&result.Slots[0].ObjectID==8&&result.Slots[1].ObjectID==9,"classic wire reordered the independent full source vector");
}
static std::string Value(const VMTS1HierarchyState& state){std::ostringstream out;std::map<int,VMTS1HierarchyRecord> sorted(state.Records.begin(),state.Records.end());for(const auto& p:sorted){const auto&r=p.second;out<<p.first<<':'<<r.GUID<<','<<r.RawParent<<','<<r.RawHierarchySlot<<','<<r.RawContainer<<','<<r.RawPhysicalSlot<<','<<r.RawSlotCount<<';';for(auto id:r.Node.Slots)out<<id<<',';}for(const auto& grid:state.TileHeads)for(auto id:grid)out<<id<<',';return out.str();}
static void Staging(){
 VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&context));
 context.SpatialTileHeads[0].assign(64,0);context.SpatialTileHeads[1].assign(64,0);
 IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"hierarchy-persistence",true);OBJD definition;definition.GUID=0x12345678;FSO::Content::GameObject object;object.GUID=definition.GUID;object.OBJ=&definition;object.Resource=&resource;
 VMGameObject a(&object,nullptr),b(&object,nullptr),c(&object,nullptr),d(&object,nullptr);Entities::VMMultitileGroup group;
 for(auto* e:{&a,&b,&c,&d}){e->MultitileGroup=&group;group.AddObject(e);vm.AddEntity(e);e->Position(FSO::LotView::Model::LotTilePos(24,24,1));context.TS1EnsureHierarchy(e);e->ObjectData[26]=0;e->ObjectData[14]=-1;e->ObjectData[2]=0;e->ObjectData[3]=-1;}
 context.TS1HierarchyNodes[a.ObjectID].Slots={b.ObjectID,c.ObjectID,0};context.TS1HierarchyNodes[b.ObjectID].Slots={0};context.TS1HierarchyNodes[c.ObjectID].Slots={d.ObjectID};context.TS1HierarchyNodes[d.ObjectID].Slots={0};
 b.ObjectData[26]=a.ObjectID;b.ObjectData[14]=0;c.ObjectData[26]=a.ObjectID;c.ObjectData[14]=1;d.ObjectData[26]=c.ObjectID;d.ObjectData[14]=0;a.ObjectData[67]=7; // Literal stored count is not vector extent.
 context.SpatialTileHeads[0][9]=a.ObjectID;
 const auto before=context.TS1CaptureHierarchy();context.TS1ValidateHierarchy(before);const auto untouched=Value(before);
 auto type=Type(definition.GUID);OBJM classic;classic.Decoded=true;
 for(const auto& p:before.Records){auto instance=std::make_shared<OBJMInstance>();instance->OBJT=&type;instance->X=p.second.Position.x;instance->Y=p.second.Position.y;instance->Level=p.second.Position.Level;instance->ObjectData.assign(80,0);instance->ObjectData[11]=p.first;instance->ObjectData[26]=p.second.RawParent;instance->ObjectData[14]=p.second.RawHierarchySlot;instance->ObjectData[2]=p.second.RawContainer;instance->ObjectData[3]=p.second.RawPhysicalSlot;instance->ObjectData[67]=p.second.RawSlotCount;instance->SavedSlotRecordCount=int32_t(p.second.Node.Slots.size());instance->LinkedSlot.ObjectID=p.second.Node.Slots[0];instance->Slots.resize(p.second.Node.Slots.size()-1);for(size_t i=1;i<p.second.Node.Slots.size();++i)instance->Slots[i-1].ObjectID=p.second.Node.Slots[i];classic.ObjectData[p.first].Instance=instance;}
 H::VerifyBoundClassic(vm,classic);
 classic.ObjectData.at(a.ObjectID).Instance->ObjectData[67]=6;
 Reject([&]{H::VerifyBoundClassic(vm,classic);},"capture accepted a stale classic literal count");
 classic.ObjectData.at(a.ObjectID).Instance->ObjectData[67]=7;
 auto staged=H::Stage(vm,classic,false);Need(Value(staged)==untouched,"exact source snapshot changed during classic staging");
 // Publication is a data transaction. Native cached physical views and raw
 // cache words can legitimately be retained separately from the source graph.
 a.PublishTS1PhysicalSlots({&b});b.Container=&a;b.ContainerSlot=0;b.ObjectData[2]=123;b.ObjectData[3]=-3;
 auto preserved=staged;preserved.Records.at(b.ObjectID).RawContainer=7;preserved.Records.at(b.ObjectID).RawPhysicalSlot=8;
 context.TS1PublishHierarchy(std::move(preserved),true);
 Need(a.GetSlot(0)==&b&&b.Container==&a&&b.ContainerSlot==0&&b.ObjectData[2]==123&&b.ObjectData[3]==-3,"native hierarchy publication overwrote retained physical cache views/raw words");
 const auto originalPosition=d.Position();auto physical=staged;
 context.TS1PublishHierarchy(std::move(physical),false);
 Need(a.GetSlot(0)==&c&&c.Container==&a&&c.ContainerSlot==0&&d.Container==&a&&d.ContainerSlot==0&&b.Container==nullptr,
      "source0 siblings did not derive the independent physical source1 cache");
 Need(d.Position()==originalPosition&&c.ObjectData[2]==staged.Records.at(c.ObjectID).RawContainer,
      "hierarchy publication moved a source sibling or recomputed a literal raw cache word");
 // Restore the original raw data after testing preserve-only semantics.
 Need(Value(context.TS1CaptureHierarchy())==untouched,"false publication failed to restore all staged literal hierarchy words");
 auto invalid=staged;invalid.TileHeads[0].pop_back();Reject([&]{context.TS1PublishHierarchy(std::move(invalid));},"truncated source head grid published");
 Need(Value(context.TS1CaptureHierarchy())==untouched&&a.GetSlot(0)==&c,"failed publication changed live source or physical views");
 auto removedNode=std::move(context.TS1HierarchyNodes.at(c.ObjectID));context.TS1HierarchyNodes.erase(c.ObjectID);
 Reject([&]{context.TS1CaptureHierarchy();},"capture invented a missing authoritative node from physical slots");context.TS1HierarchyNodes.emplace(c.ObjectID,std::move(removedNode));
 Need(H::PhysicalMember(staged,a.ObjectID,0,c.ObjectID)&&H::PhysicalMember(staged,a.ObjectID,0,d.ObjectID)&&!H::PhysicalMember(staged,a.ObjectID,0,b.ObjectID),"physical-source1 membership confused tile/source0 siblings");
 auto& record=*classic.ObjectData.at(a.ObjectID).Instance;record.SavedSlotRecordCount=1;record.Slots.clear();
 staged=H::Stage(vm,classic,false);Need(staged.Records.at(a.ObjectID).Node.Slots==before.Records.at(a.ObjectID).Node.Slots,"short classic prefix shrank constructor/current source extent");Need(staged.Records.at(a.ObjectID).RawSlotCount==7,"stored OD67 was replaced with vector extent");
 record.SavedSlotRecordCount=0;record.LinkedSlot.ObjectID=0;staged=H::Stage(vm,classic,false);Need(staged.Records.at(a.ObjectID).Node.Slots[0]==b.ObjectID,"zero classic records overwrote source slot0 instead of preserving its constructor prefix");
 record.SavedSlotRecordCount=1;record.LinkedSlot.ObjectID=32767;Reject([&]{H::Stage(vm,classic,false);},"dangling source reference accepted");Need(Value(context.TS1CaptureHierarchy())==untouched,"failed staging mutated live hierarchy/raw fields/heads");
 record.LinkedSlot.ObjectID=a.ObjectID;Reject([&]{H::Stage(vm,classic,false);},"source self-cycle accepted");Need(Value(context.TS1CaptureHierarchy())==untouched,"cycle rejection mutated live graph");
 record.LinkedSlot.ObjectID=b.ObjectID;type.GUID^=1;Reject([&]{H::Stage(vm,classic,false);},"foreign classic hierarchy GUID accepted");Need(Value(context.TS1CaptureHierarchy())==untouched,"foreign identity rejection mutated live graph");
}
static void NativeScalarAuthority(){
 VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;
 context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&context));
 context.SpatialTileHeads[0].assign(64,0);context.SpatialTileHeads[1].assign(64,0);
 IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"native scalar authority",true);
 OBJD definition;definition.GUID=0x12345679;FSO::Content::GameObject object;
 object.GUID=definition.GUID;object.OBJ=&definition;object.Resource=&resource;
 VMGameObject entity(&object,nullptr);vm.AddEntity(&entity);
 entity.Thread=std::make_unique<VMThread>(&context,&entity,16);
 entity.Position(FSO::LotView::Model::LotTilePos::OUT_OF_WORLD);context.TS1EnsureHierarchy(&entity);
 entity.ObjectData[2]=0;entity.ObjectData[3]=-1;entity.ObjectData[14]=-1;entity.ObjectData[26]=0;entity.ObjectData[67]=7;
 entity.TS1NumericFootprintKnown=true;entity.TS1NumericFootprint={1,2,3,4};
 auto type=Type(definition.GUID);OBJM classic;classic.Decoded=true;
 auto saved=std::make_shared<OBJMInstance>();saved->OBJT=&type;
 saved->X=entity.Position().x;saved->Y=entity.Position().y;saved->Level=entity.Position().Level;
 saved->ObjectData=entity.ObjectData;
 const auto source=context.TS1CaptureHierarchy();const auto& slots=source.Records.at(entity.ObjectID).Node.Slots;
 saved->SavedSlotRecordCount=int32_t(slots.size());
 if(!slots.empty()){saved->LinkedSlot.ObjectID=slots[0];saved->Slots.resize(slots.size()-1);
  for(size_t i=1;i<slots.size();++i)saved->Slots[i-1].ObjectID=slots[i];}
 classic.ObjectData[entity.ObjectID].Instance=saved;
 H::VerifyBoundClassic(vm,classic);
 auto stage=[&](){std::map<int16_t,Record> records;auto& r=records[entity.ObjectID];r.entity=&entity;
  r.preparedThread=std::make_unique<VMThread>(&context,&entity,16);r.position=entity.Position();
  r.objectData=entity.ObjectData;r.numericFootprintKnown=true;r.numericFootprint=entity.TS1NumericFootprint;
  for(int i=0;i<entity.TotalSlots();++i)r.slots.push_back(entity.GetSlot(i));return records;};
 const auto raw=entity.ObjectData;const auto hierarchy=Value(source);auto* thread=entity.Thread.get();
 const auto physical=entity.TotalSlots();
 auto unchanged=[&](){Need(entity.ObjectData==raw&&Value(context.TS1CaptureHierarchy())==hierarchy&&
  entity.Thread.get()==thread&&entity.Container==nullptr&&entity.TotalSlots()==physical&&
  entity.TS1NumericFootprint==std::array<int32_t,4>{1,2,3,4},"native scalar preflight changed live state");
  for(int i=0;i<physical;++i)Need(!entity.GetSlot(i),"native scalar preflight changed physical slots");};
 st::Writer unused;Archive fields(unused);
 {InteractionCheckpointCodec codec(vm,fields,22);auto records=stage();codec.publish(records,classic,true);
  Need(codec.HierarchyLegacyFieldsReconciled==0,"matching Native22 scalar bank invoked legacy migration");unchanged();}
 for(int index:{2,3,14,26,67}){
  auto records=stage();records.at(entity.ObjectID).objectData[index]^=1;
  InteractionCheckpointCodec modern(vm,fields,22);bool rejected=false;
  try{modern.publish(records,classic);}catch(const std::exception& e){
   rejected=std::string(e.what()).find("native source hierarchy disagrees with its bound classic scalar bank")!=std::string::npos;}
  Need(rejected,"Native22 mismatched literal scalar was accepted or failed for an unrelated reason");unchanged();
  // The same hash-bound <=21 pair remains a supported migration. Exercise
  // the complete publication preflight, rather than copying the reconcile loop.
  auto legacyRecords=stage();legacyRecords.at(entity.ObjectID).objectData[index]^=1;
  InteractionCheckpointCodec legacy(vm,fields,21);legacy.publish(legacyRecords,classic,true);
  Need(legacy.HierarchyLegacyFieldsReconciled==1&&legacyRecords.at(entity.ObjectID).objectData==raw,
       "Native21 bound-classic scalar migration was lost or normalized unrelated words");unchanged();
 }
 vm.RemoveEntity(&entity);
}
// 4333c0 -> 432a70(commit=false), destination has a physical parent.
// These are source numeric rejection codes, independent of misleading legacy
// placement-error enum labels. Query must not publish or invoke callbacks.
static void ContainedAdmission() {
 VMContext context(nullptr);VM vm(&context,nullptr);vm.GlobalState.assign(38,0);IffFile iff;
 FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"contained-admission",true);
 OBJD definition;definition.GUID=0x7814;FSO::Content::GameObject object;
 object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
 context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&context));
 auto* arch=context.Architecture();arch->TS1WaterTiles.assign(64,-1);arch->TS1PoolTiles.assign(64,0);
 arch->TS1FloorPresent.assign(2,std::vector<uint8_t>(64,1));
 arch->TS1TileFlags.assign(2,std::vector<uint8_t>(64,255));
 for(auto& heads:context.SpatialTileHeads)heads.assign(64,0);
 auto* moved=vm.OwnEntity(new VMGameObject(&object,nullptr));
 auto* parent=vm.OwnEntity(new VMGameObject(&object,nullptr));
 auto* other=vm.OwnEntity(new VMGameObject(&object,nullptr));
 OBJD personDefinition;personDefinition.GUID=0x7815;personDefinition.ObjectType=OBJDType::Person;
 FSO::Content::GameObject personObject;personObject.OBJ=&personDefinition;personObject.GUID=personDefinition.GUID;personObject.Resource=&resource;
 auto* person=vm.OwnEntity(new VMAvatar(&personObject));
 for(auto* e:{moved,parent,other,person}) {
  vm.AddEntity(e);e->Position(FSO::LotView::Model::LotTilePos(24,24,1));context.TS1EnsureHierarchy(e);
  e->ObjectData[4]=1;e->ObjectData[27]=10;e->ObjectData[28]=10;e->ObjectData[8]=0;e->ObjectData[42]=0;
 }
 auto& node=context.TS1HierarchyNodes.at(parent->ObjectID);node.Slots={0,0,0};node.Metadata.resize(3);
 node.Metadata[1].Height=1;node.Metadata[2].Height=2;
 auto request=context.TS1PhysicalRequest(moved,parent,0);
 // Some negative cases intentionally supply an invalid edge. Snapshot raw
 // channels without invoking a separate graph-validation gate before the query.
 const auto snapshot=[&](){std::ostringstream out;
  for(auto* e:vm.Entities){out<<e->ObjectID<<':'<<e->Container<<':'<<e->ContainerSlot<<';';
   for(auto value:e->ObjectData)out<<value<<',';
   const auto& n=context.TS1HierarchyNodes.at(e->ObjectID);out<<n.NeedsPlacement<<';';
   for(auto value:n.Slots)out<<value<<',';
   for(const auto& m:n.Metadata)out<<m.Offset.X<<','<<m.Offset.Y<<','<<m.Offset.Z<<','<<m.Height<<','<<m.Capacity<<','<<m.Flags<<','<<m.ResourceRow<<','<<m.BoneName<<';';
  }return out.str();};
 const auto query=[&](int expected,const char* why) {
  auto raw=moved->ObjectData;auto parentRaw=parent->ObjectData;auto graph=snapshot();
  const auto result=context.TS1QueryContainedPlacement(moved,request);
  Need(int(result.Status)==expected-1,why);
  Need(moved->ObjectData==raw && parent->ObjectData==parentRaw && snapshot()==graph,
       "contained query changed source state");
 };
 query(0,"equal signed weight must be admitted");
 arch->TS1TileFlags[0][9]=0;query(0,"ARRY8 flags must not be mistaken for ARRY9 pool coverage");
 node.Metadata[1].Capacity=-99;node.Metadata[1].Flags=-1;query(0,"unread capacity/flags must not invent gates");
 request.Position.x=4112;query(0,"source signed-byte tile narrowing lost");request.Position.x=24;
 request.Position.x=0;query(1,"contained interior bounds gate missing");request.Position.x=24;
 request.Position.Level=0;query(2,"contained level gate missing");request.Position.Level=2;
 moved->ObjectData[42]=0x1000;query(31,"upper pool-only gate missing");moved->ObjectData[42]=0;
 arch->TS1FloorPresent[1][9]=0;query(41,"upper floor coverage gate missing");
 moved->ObjectData[42]=0x200;query(0,"in-air flag must bypass upper floor coverage");
 moved->ObjectData[42]=0;arch->TS1FloorPresent[1][9]=1;request.Position.Level=1;
 moved->ObjectData[42]=0x2000;query(47,"water-only flag on dry tile admitted");moved->ObjectData[42]=0;
 arch->TS1WaterTiles[9]=0;query(29,"signed water tile gate missing");
 moved->ObjectData[42]=4;query(0,"water admission flag ignored");moved->ObjectData[42]=0;
 context.SpatialTileHeads[0][9]=parent->ObjectID;node.Slots[2]=other->ObjectID;other->ObjectData[8]=0x1000;
 query(0,"water bridge in source DFS not found");node.Slots[2]=0;other->ObjectData[8]=0;
 arch->TS1WaterTiles[9]=-1;arch->TS1PoolTiles[9]=1;query(29,"pool gate missing");
 moved->ObjectData[42]=0x800;query(0,"pool flag ignored");moved->ObjectData[42]=0;arch->TS1PoolTiles[9]=0;
 moved->ObjectData[42]=0x1000;query(30,"pool-required gate missing");moved->ObjectData[42]=0;
 moved->ObjectData[8]=4;query(12,"moved raw zero-extent flag ignored");moved->ObjectData[8]=0;
 parent->ObjectData[8]=4;query(12,"parent raw zero-extent flag ignored");parent->ObjectData[8]=0;
 request.PhysicalSlot=2;query(13,"physical index source extent gate missing");request.PhysicalSlot=0;
 node.Slots[1]=other->ObjectID;query(11,"occupied slot accepted");node.Slots[1]=moved->ObjectID;
 query(0,"self occupant rejected");node.Slots[1]=0;
 vm.GlobalState[19]=2;node.Metadata[1].Offset.X=1;query(3,"off-center multi-slot mode2 gate missing");
 moved->ObjectData[8]=0x2000;query(0,"raw mode2 exemption ignored");moved->ObjectData[8]=0;
 vm.GlobalState[19]=0;node.Metadata[1].Offset.X=0;
 request.ParentID=person->ObjectID;vm.GlobalState[19]=1;query(13,"person support mode restriction missing");
 request.ParentID=parent->ObjectID;vm.GlobalState[19]=0;
 node.Slots[2]=other->ObjectID;node.Metadata[2].Height=1;
 query(11,"same-height centered sibling admitted");other->ObjectData[8]=0x2000;
 query(11,"requested centered slot needs separate exemption");moved->ObjectData[8]=0x2000;
 query(0,"both centered exemptions ignored");node.Metadata[2].Offset.X=1;other->ObjectData[8]=0;
 query(0,"noncentered other needs no exemption");moved->ObjectData[8]=0;
 query(11,"requested center gate ignored");node.Metadata[1].Offset.Y=1;query(0,"noncentered pair incorrectly rejected");
 node.Metadata[1].Offset.Y=0;node.Slots[2]=0;
 moved->ObjectData[27]=11;query(14,"signed support strength gate missing");moved->ObjectData[27]=-1;
 parent->ObjectData[28]=-1;query(0,"signed equal weight rejected");parent->ObjectData[28]=10;moved->ObjectData[27]=10;
 parent->ObjectData[26]=moved->ObjectID;parent->ObjectData[14]=1;query(11,"ancestor self containment admitted");
 parent->ObjectData[26]=0;parent->ObjectData[14]=-1;
 node.Metadata[1].Height=0;query(24,"zero height accepted");node.Metadata[1].Height=33;
 query(0,"x86 height shift must mask count to five bits");node.Metadata[1].Height=2;query(24,"height mask ignored");
 node.Metadata[1].Height=16;moved->ObjectData[4]=int16_t(0x8000);query(0,"signed height flags must preserve sign extension");
 node.Metadata[1].Height=32;query(0,"sign extended allowed heights lost upper bits");
 definition.MasterID=1;query(16,"factory-selected MT class must reject containment");definition.MasterID=0;
}
int main(){try{VM::UseWorld(false);WireExtent();Staging();NativeScalarAuthority();ContainedAdmission();std::cout<<"TS1 hierarchy persistence contract passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
