// SPDX-License-Identifier: MPL-2.0
// Cohesive original hierarchy lifecycle controls, with real VM/cache APIs.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMAvatar.h"
#include "files/formats/iff/chunks/SLOT.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMPrimitiveRegistration.h"
#include "files/formats/iff/chunks/BHAV.h"
#include <functional>
#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using Pos=FSO::LotView::Model::LotTilePos;
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>static void Reject(F&& f,const char* why){try{f();}catch(const std::exception&){return;}throw std::runtime_error(why);}
struct Probe:VMGameObject{
 std::vector<int>* Commits=nullptr;int PreCalls=0;
 explicit Probe(FSO::Content::GameObject* o):VMGameObject(o,nullptr){}
 void PrePositionChange(VMContext* c)override{++PreCalls;VMGameObject::PrePositionChange(c);}
 void PositionChange(VMContext* c,bool noEntry)override{if(Commits)Commits->push_back(ObjectID);VMGameObject::PositionChange(c,noEntry);}
};
struct Fixture{
 VMContext Context{nullptr};VM Machine{&Context,nullptr};IffFile Iff;
 FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"hierarchy-runtime",true};
 OBJD Definition;FSO::Content::GameObject Object;Entities::VMMultitileGroup Group;
 std::vector<std::unique_ptr<Probe>> Objects;
 Fixture(){Machine.TS1=true;Definition.GUID=0xf0360001;Object.GUID=Definition.GUID;Object.OBJ=&Definition;Object.Resource=&Resource;
  Context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&Context));Context.Architecture()->Rooms[0].Map.assign(64,0);Context.RoomInfo.resize(1);
  Context.SpatialTileHeads[0].assign(64,0);Context.SpatialTileHeads[1].assign(64,0);}
 Probe* Make(int physical=0){auto owned=std::make_unique<Probe>(&Object);auto* e=owned.get();e->PublishTS1PhysicalSlots(std::vector<VMEntity*>(size_t(physical),nullptr));e->Position(Pos(24,24,1));e->MultitileGroup=&Group;Group.AddObject(e);Machine.AddEntity(e);e->Thread=std::make_unique<VMThread>(&Context,e,16);Objects.push_back(std::move(owned));return e;}
 void Put(Probe* e,VMEntity* owner=nullptr,int slot=-1){Context.TS1CommitPhysicalPlacement(e,Context.TS1PhysicalRequest(e,owner,slot));}
 int16_t Head()const{return Context.SpatialTileHeads[0][9];}
 std::string Snapshot(){const auto state=Context.TS1CaptureHierarchy();std::map<int16_t,const VMTS1HierarchyRecord*> sorted;for(const auto& p:state.Records)sorted[p.first]=&p.second;std::ostringstream out;
  for(const auto& p:sorted){const auto& r=*p.second;auto* e=Machine.GetObjectById(p.first);out<<p.first<<':'<<r.Position.x<<','<<r.Position.y<<','<<int(r.Position.Level)<<';'<<r.RawParent<<','<<r.RawHierarchySlot<<','<<r.RawContainer<<','<<r.RawPhysicalSlot<<','<<r.RawSlotCount<<','<<r.Node.NeedsPlacement<<';';for(auto id:r.Node.Slots)out<<id<<',';out<<'/'<<(e->Container?e->Container->ObjectID:0)<<','<<e->ContainerSlot<<';';for(auto* child:e->Contained)out<<(child?child->ObjectID:0)<<',';}
  for(const auto& layer:state.TileHeads)for(auto id:layer)out<<id<<',';return out.str();}
};
static void TilePromotion(){
 Fixture f;auto* a=f.Make();auto* b=f.Make();auto* c=f.Make();f.Put(a);f.Put(b);f.Put(c);
 Need(f.Head()==c->ObjectID&&f.Context.TS1SourceSlot(c,0)==b&&f.Context.TS1SourceSlot(b,0)==a,"tile insertion did not prepend source0 chain");
 Need(!a->Container&&!b->Container&&!c->Container&&b->ObjectData[26]==c->ObjectID&&b->ObjectData[14]==0,"tile successor became a physical contained object");
 const auto sameDestination=f.Snapshot();f.Put(c);Need(f.Snapshot()==sameDestination,"same destination detached/relinked its source0 successor");
 f.Context.TS1RemovePlacement(b);
 Need(f.Head()==c->ObjectID&&f.Context.TS1SourceSlot(c,0)==a&&!f.Context.TS1SourceSlot(b,0)&&a->ObjectData[26]==c->ObjectID&&a->ObjectData[14]==0,"middle removal did not promote its source0 successor");
 const auto detached=f.Snapshot();f.Context.TS1RemovePlacement(b);Need(f.Snapshot()==detached,"repeated removal changed an already detached source transaction");
 f.Context.TS1RemovePlacement(c);Need(f.Head()==a->ObjectID&&a->ObjectData[26]==0&&a->ObjectData[14]==-1,"head removal lost promoted tile root");
 f.Context.TS1RemovePlacement(a);Need(f.Head()==0,"last tile removal retained a stale head");
}
static void InvalidRequestRemovesOnly(){
 Fixture f;auto* a=f.Make();auto* b=f.Make();auto* c=f.Make();f.Put(a);f.Put(b);f.Put(c);
 auto request=f.Context.TS1PhysicalRequest(b,nullptr,-1);request.Valid=false;
 f.Context.TS1CommitPhysicalPlacement(b,request);
 Need(f.Head()==c->ObjectID&&f.Context.TS1SourceSlot(c,0)==a&&f.Context.TS1SourceSlot(b,0)==nullptr,
      "invalid committed request republished self and erased its promoted successor");
}
static void DetachVersusLift(){
 Fixture f;auto* a=f.Make();auto* b=f.Make();auto* c=f.Make();f.Put(a);f.Put(b);f.Put(c);
 f.Context.TS1DetachRawLink(b);
 Need(f.Head()==c->ObjectID&&!f.Context.TS1SourceSlot(c,0)&&f.Context.TS1SourceSlot(b,0)==a,
      "raw detach was confused with removal/lift and promoted a successor");
 Need(b->ObjectData[26]==0&&b->ObjectData[14]==-1&&a->ObjectData[26]==b->ObjectID,
      "raw detach rewrote its child's independent parent history");
}
static void PhysicalAndRawIndependence(){
 Fixture f;auto* owner=f.Make(1);auto* a=f.Make();auto* b=f.Make();f.Put(owner);f.Put(a,owner,0);f.Put(b,owner,0);
 Need(f.Context.TS1SourceSlot(owner,1)==b&&f.Context.TS1SourceSlot(b,0)==a&&owner->GetSlot(0)==b,
      "physical0/source1 normalization or occupied-slot splice changed");
 Need(b->ObjectData[26]==owner->ObjectID&&b->ObjectData[14]==1&&a->ObjectData[26]==b->ObjectID&&a->ObjectData[14]==0,
      "spliced raw source coordinates were double-normalized");
 const auto resolved=f.Context.TS1ResolvePhysicalPlacement(a,false);
 Need(resolved.ParentID==owner->ObjectID&&resolved.PhysicalSlot==0&&resolved.Position==a->Position(),"source0 sibling physical resolution changed its original XY or slot coordinate");
 // A scalar write does not mutate source children, even if later room/light
 // cache unregistration/re-registration occurs without a placement transaction.
 a->SetValue(Model::VMStackObjectVariable::ParentId,0);a->SetValue(Model::VMStackObjectVariable::SlotHierNumber,-1);
 const auto before=f.Snapshot();f.Context.UnregisterObjectPos(a);f.Context.RegisterObjectPos(a);
 Need(f.Snapshot()==before&&f.Context.TS1SourceSlot(b,0)==a&&a->GetValue(Model::VMStackObjectVariable::ParentId)==0,
      "cache registration autorelinked literal raw parent writes");
}
static void InvalidDestinationAtomicity(){
 Fixture f;auto* a=f.Make();auto* b=f.Make();auto* owner=f.Make(1);f.Put(a);f.Put(b);f.Put(owner);
 const auto before=f.Snapshot();auto invalid=f.Context.TS1PhysicalRequest(b,owner,1); // source2 is beyond N=1.
 Reject([&]{f.Context.TS1CommitPhysicalPlacement(b,invalid);},"out-of-vector destination was accepted");
 Need(f.Snapshot()==before,"destination preflight detached an existing source object before rejecting");
 auto raw=VMTS1RawPlacementLink{true,b->Position(),owner->ObjectID,32767};
 Reject([&]{f.Context.TS1PublishRawLink(b,raw);},"out-of-vector raw destination was accepted");Need(f.Snapshot()==before,"raw destination failure partially published source/cache state");
 invalid.PhysicalSlot=-2;Reject([&]{f.Context.TS1CommitPhysicalPlacement(b,invalid);},"invalid signed physical request was accepted");Need(f.Snapshot()==before,"invalid signed request changed the graph");
 auto* occupant=f.Make();f.Put(occupant,owner,0);occupant->SetValue(Model::VMStackObjectVariable::SlotHierNumber,32767);
 const auto malformed=f.Snapshot();const auto preCalls=b->PreCalls;
 Reject([&]{owner->PlaceInSlot(b,0,true,&f.Context);},"public physical placement accepted a malformed destination occupant link");
 Need(f.Snapshot()==malformed&&b->PreCalls==preCalls,"PlaceInSlot(cleanOld) invoked caller PrePositionChange before destination preflight rejected");
}
static void FailedGroupMovePreservesChain(){
 Fixture f;auto* a=f.Make();auto* b=f.Make();auto* c=f.Make();f.Put(a);f.Put(b);f.Put(c);
 const auto before=f.Snapshot();const int calls=a->PreCalls+b->PreCalls+c->PreCalls;
 const auto result=f.Group.ChangePosition(Pos(-32,24,1),FSO::LotView::Model::Direction::NORTH,&f.Context);
 Need(result.Status!=Model::VMPlacementError::Success,"failed-group fixture unexpectedly accepted an outside-lot destination");
 Need(f.Snapshot()==before&&f.Head()==c->ObjectID&&f.Context.TS1SourceSlot(c,0)==b&&f.Context.TS1SourceSlot(b,0)==a,
      "failed group move rolled back by reinserting a different source head/order");
 Need(a->PreCalls+b->PreCalls+c->PreCalls==calls,"failed group move detached source links before validating the destination");
}
static void SandboxIdentityIsolation(){
 Fixture f;auto* a=f.Make();auto* b=f.Make();f.Put(a);f.Put(b);const auto before=f.Snapshot();const auto first=a->ObjectID;
 auto sandbox=f.Machine.Sandbox();Need(f.Context.TS1HierarchyNodes.empty()&&f.Head()==0,"sandbox retained outer source graph/heads");
 auto* local=f.Make();Need(local->ObjectID==first&&f.Machine.GetObjectById(first)==local,"sandbox did not reuse its isolated identity domain");f.Put(local);
 Need(f.Context.TS1SourceSlot(local,0)==nullptr&&f.Head()==local->ObjectID,"sandbox source graph linked an outer same-OID entity");
 f.Machine.SandboxRestore(std::move(sandbox));Need(f.Machine.GetObjectById(first)==a&&f.Snapshot()==before,"sandbox restore lost outer source graph/heads/raw state");
}
static void SourceDFSAndNumericScope(){
 Fixture f;auto* root=f.Make(1);auto* neighbor=f.Make();auto* held=f.Make(1);auto* sibling=f.Make();auto* nested=f.Make();
 auto state=f.Context.TS1CaptureHierarchy();auto set=[&](Probe* e,std::vector<int16_t> slots,int16_t parent,int16_t index){auto& r=state.Records.at(e->ObjectID);r.Node.Slots=std::move(slots);r.RawParent=parent;r.RawHierarchySlot=index;};
 set(root,{neighbor->ObjectID,held->ObjectID},0,-1);set(neighbor,{0},root->ObjectID,0);
 set(held,{sibling->ObjectID,nested->ObjectID},root->ObjectID,1);set(sibling,{0},held->ObjectID,0);set(nested,{0},held->ObjectID,1);
 state.TileHeads[0][9]=root->ObjectID;f.Context.TS1PublishHierarchy(std::move(state));
 std::vector<int> full;std::vector<VMEntity*> pending{root};while(!pending.empty()){auto* e=pending.back();pending.pop_back();full.push_back(e->ObjectID);for(int i=int(f.Context.TS1SourceVectorExtent(e))-1;i>=0;--i)if(auto* child=f.Context.TS1SourceSlot(e,i))pending.push_back(child);}
 Need(full==std::vector<int>({root->ObjectID,neighbor->ObjectID,held->ObjectID,sibling->ObjectID,nested->ObjectID}),"full source preorder omitted slot0 or reordered physical descendants");
 std::vector<int> commits;for(auto* e:{root,neighbor,held,sibling,nested})e->Commits=&commits;
 root->PositionChange(&f.Context,true);
 Need(commits==std::vector<int>({root->ObjectID,held->ObjectID,nested->ObjectID}),"numeric position commit incorrectly propagated through own/child source0 siblings");
}
static void PublicUnplaceAndDelete(){
 Fixture f;auto* neighbor=f.Make();auto* owner=f.Make(1);auto* child=f.Make();
 // A physical child's Delete(true) is group-aware. Keep the unrelated tile
 // neighbor and parent in their own genuine groups for this control.
 Entities::VMMultitileGroup neighborGroup,ownerGroup,childGroup;f.Group.Objects.clear();
 neighbor->MultitileGroup=&neighborGroup;neighborGroup.AddObject(neighbor);owner->MultitileGroup=&ownerGroup;ownerGroup.AddObject(owner);child->MultitileGroup=&childGroup;childGroup.AddObject(child);
 f.Put(neighbor);f.Put(owner);f.Put(child,owner,0);
 owner->UnplaceTS1(&f.Context);
 Need(f.Head()==neighbor->ObjectID&&!neighbor->Dead&&neighbor->Position()==Pos(24,24,1),"public unplace lost or moved its source0 neighbor");
 Need(owner->Position()==Pos(-16,-16,1)&&child->Position()==Pos(-16,-16,1)&&f.Context.TS1SourceSlot(owner,1)==child,
      "public unplace did not preserve physical source1 ownership while propagating its OOW numeric pose");
 owner->Position(Pos(24,24,1));child->Position(Pos(24,24,1));f.Put(owner);
 struct Cleanup:VMPrimitiveHandler{std::function<VMPrimitiveExitCode(VMStackFrame*)> Body;VMPrimitiveExitCode Execute(VMStackFrame* frame,VMPrimitiveOperand*)override{return Body(frame);}};
 int parentCallbacks=0,childCallbacks=0;auto* handler=new Cleanup;
 handler->Body=[&](VMStackFrame* frame){Need(f.Machine.GetObjectById(owner->ObjectID)==owner&&!owner->Dead,"cleanup callback observed parent manager identity removed too early");
  Need(f.Machine.GetObjectById(frame->Caller->ObjectID)==frame->Caller&&!frame->Caller->Dead,"cleanup callback lost its own live manager identity");
  if(frame->Caller==owner)++parentCallbacks;if(frame->Caller==child)++childCallbacks;frame->Caller->UnplaceTS1(&f.Context);return VMPrimitiveExitCode::GOTO_TRUE;};
 auto* registration=new VMPrimitiveRegistration(handler);registration->Opcode=250;f.Context.AddPrimitive(registration);
 auto* cleanup=new BHAV;cleanup->ChunkProcessed=true;cleanup->ChunkID=4096;cleanup->Args=4;BHAVInstruction instruction;instruction.Opcode=250;instruction.TruePointer=254;instruction.FalsePointer=255;cleanup->Instructions.push_back(instruction);f.Iff.AddChunk(cleanup);
 owner->EntryPoints[3].ActionFunction=4096;child->EntryPoints[3].ActionFunction=4096;
 const auto ownerID=owner->ObjectID,childID=child->ObjectID;owner->Delete(false,&f.Context);
 Need(parentCallbacks==1&&childCallbacks==1,"generic Delete(false) missed authored parent/physical-child cleanup callbacks");
 Need(owner->Dead&&child->Dead&&!f.Machine.GetObjectById(ownerID)&&!f.Machine.GetObjectById(childID),"generic delete did not remove source1 child and parent identities");
 Need(!neighbor->Dead&&f.Machine.GetObjectById(neighbor->ObjectID)==neighbor&&f.Head()==neighbor->ObjectID&&neighbor->Position()==Pos(24,24,1),"generic delete recursed into the unrelated source0 neighbor");
}
static void OwnWallReservationQuery(){
 Fixture f;auto* object=f.Make();object->Direction(FSO::LotView::Model::Direction::NORTH);f.Put(object);
 object->SetValue(Model::VMStackObjectVariable::AllowedHeightFlags,1);
 object->SetValue(Model::VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));
 object->SetValue(Model::VMStackObjectVariable::WallPlacementFlags,int16_t(WallPlacementFlags::WallRequiredInFront));
 auto* arch=f.Context.Architecture();auto wall=arch->GetWall(1,1,1);wall.Segments=FSO::LotView::Model::WallSegments(15);wall.TopLeftStyle=wall.TopRightStyle=1;arch->SetWall(1,1,1,wall);
 object->SetWallUse(arch,true);const auto own=arch->GetWall(1,1,1);Need(int(own.OccupiedWalls)!=0,"own-wall reservation fixture was not published");
 auto foreign=own;arch->SetWall(2,1,1,foreign);
 auto walls=[&](){std::ostringstream out;for(const auto& story:arch->Walls)for(const auto& w:story)out<<int(w.Segments)<<','<<int(w.OccupiedWalls)<<','<<w.TopLeftStyle<<','<<w.TopRightStyle<<','<<w.TopLeftPattern<<','<<w.TopRightPattern<<','<<w.BottomLeftPattern<<','<<w.BottomRightPattern<<','<<w.ObjSetTLStyle<<','<<w.ObjSetTRStyle<<','<<w.TopLeftDoor<<','<<w.TopRightDoor<<';';return out.str();};
 const auto graph=f.Snapshot(),architecture=walls();
 Need(object->PositionValid(object->Position(),object->Direction(),&f.Context).Status==Model::VMPlacementError::Success,"same-position query treated its own published wall reservation as foreign");
 Need(object->PositionValid(Pos(40,24,1),object->Direction(),&f.Context).Status!=Model::VMPlacementError::Success,"foreign occupied wall reservation was ignored");
 Need(f.Snapshot()==graph&&walls()==architecture,"wall placement queries mutated hierarchy or live architecture");
}
static void PreservedViewExtent(){
 Fixture f;auto* owner=f.Make(2);auto* child=f.Make();f.Put(owner);f.Put(child,owner,0);const auto source=f.Context.TS1CaptureHierarchy();
 owner->PublishTS1PhysicalSlots({child});auto copy=source;f.Context.TS1PublishHierarchy(std::move(copy),true);
 Need(owner->Contained.size()==2&&owner->GetSlot(0)==child&&owner->GetSlot(1)==nullptr,"preserved physical view did not grow to source vector extent");
 owner->PublishTS1PhysicalSlots({child,nullptr,child});const auto rawParent=child->Container;const auto rawSlot=child->ContainerSlot;copy=source;
 Reject([&]{f.Context.TS1PublishHierarchy(std::move(copy),true);},"publication discarded a cached occupant beyond source extent");
 Need(owner->Contained.size()==3&&owner->GetSlot(2)==child&&child->Container==rawParent&&child->ContainerSlot==rawSlot,"rejected physical view normalization changed cached pointers");
}
static void SourceMetadataConsumers(){
 Fixture f;
 auto* source=new SLOT();source->ChunkID=0;source->ChunkProcessed=true;
 SLOTItem low;low.Type=0;low.SourceRowIndex=1;low.RawHeight=2;low.Height=9;low.PhysicalCapacity=100;low.PhysicalFlags=0;
 SLOTItem high=low;high.SourceRowIndex=3;high.RawHeight=4;high.PhysicalCapacity=200;high.PhysicalFlags=16;
 source->Slots[0]={low,high};f.Iff.AddChunk(source);
 auto* table=f.Make();
 Need(table->GetSlotHeight(0)==2&&table->GetSlotHeight(1)==4,"real height consumer reused the first resource row or legacy routing Height");
 Need(table->GetSlotHeight(-1)==-1&&table->GetSlotHeight(2)==-1,"source height consumer admitted an invalid physical coordinate");
 const auto* metadata=f.Context.TS1SourceSlotMetadata(table,2);
 Need(metadata&&metadata->ResourceRow==3&&metadata->Capacity==200&&metadata->Flags==16&&metadata->Offset.Z==4.05f,"resource physical metadata lost global ordinal/capacity/flags/source height normalization");
 table->SetValue(Model::VMStackObjectVariable::SlotCount,99);
 Need(f.Context.TS1SourceVectorExtent(table)==3&&table->GetSlotHeight(1)==4,"literal raw slot-count write changed the constructor metadata domain");
 auto state=f.Context.TS1CaptureHierarchy();state.Records.at(table->ObjectID).Node.Slots.resize(5,0);f.Context.TS1PublishHierarchy(std::move(state),true);
 const auto* added=f.Context.TS1SourceSlotMetadata(table,4);
 Need(added&&added->Height==0&&added->Capacity==50&&added->Flags==0&&added->ResourceRow==-1,"saved suffix did not grow with untouched default metadata");
 VMAvatar person(&f.Object);Entities::VMMultitileGroup personGroup;person.MultitileGroup=&personGroup;personGroup.AddObject(&person);f.Machine.AddEntity(&person);person.Thread=std::make_unique<VMThread>(&f.Context,&person,16);
 Need(person.GetSlotHeight(0)==2&&person.GetSlotHeight(1)==4&&person.GetSlotHeight(2)==6&&person.GetSlotHeight(3)==0,"person metadata was hardcoded to five slots or shifted existing resource slots");
 const auto* source2=f.Context.TS1SourceSlotMetadata(&person,2);const auto* head=f.Context.TS1SourceSlotMetadata(&person,4);
 Need(source2&&!(source2->Flags&2)&&head&&(head->Flags&2)&&head->BoneName=="HEAD","resource rows were ignored when resolving the appended head slot");
 f.Machine.RemoveEntity(&person);
}
int main(){try{for(auto test:{TilePromotion,InvalidRequestRemovesOnly,DetachVersusLift,PhysicalAndRawIndependence,InvalidDestinationAtomicity,FailedGroupMovePreservesChain,SandboxIdentityIsolation,SourceDFSAndNumericScope,PublicUnplaceAndDelete,OwnWallReservationQuery,PreservedViewExtent,SourceMetadataConsumers})test();std::cout<<"TS1 hierarchy runtime contract passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
