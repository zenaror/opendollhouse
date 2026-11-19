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
int main(){try{VM::UseWorld(false);WireExtent();Staging();NativeScalarAuthority();std::cout<<"TS1 hierarchy persistence contract passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
