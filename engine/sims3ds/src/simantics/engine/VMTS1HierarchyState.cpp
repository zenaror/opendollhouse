// SPDX-License-Identifier: MPL-2.0
#include "simantics/VMContext.h"
#include "simantics/VM.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMEntity.h"
#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace FSO { namespace SimAntics {
using namespace Engine;
namespace {
VMContext& StateOwner(VMContext& c){return c.VM&&c.VM->Context?*c.VM->Context:c;}
const VMContext& StateOwner(const VMContext& c){return c.VM&&c.VM->Context?*c.VM->Context:c;}
void StateNeed(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool StateLive(const VMEntity* e){return e&&!e->Dead&&!e->GhostImage&&e->Object&&e->Object->OBJ;}
size_t StateCells(const VMContext& context){
 const auto* arch=context.Architecture();
 StateNeed(arch&&arch->Width>=3&&arch->Width<=127&&arch->Height>=3&&arch->Height<=127,"source hierarchy architecture is outside its grid domain");
 return size_t(arch->Width)*arch->Height;
}
}

VMTS1HierarchyState VMContext::TS1CaptureHierarchy() const {
 const auto& context=StateOwner(*this);if(&context!=this)return context.TS1CaptureHierarchy();
 StateNeed(VM&&VM->TS1,"source hierarchy capture requires its TS1 owner");
 VMTS1HierarchyState state;state.TileHeads=SpatialTileHeads;
 for(auto* entity:VM->Entities){
  if(!StateLive(entity))continue;
  StateNeed(entity->ObjectID>0&&entity->ObjectData.size()>67,"source hierarchy live identity/scalar bank is invalid");
  const auto node=TS1HierarchyNodes.find(entity->ObjectID);
  StateNeed(node!=TS1HierarchyNodes.end(),"source hierarchy capture lacks a live authoritative node");
  VMTS1HierarchyRecord record;record.ObjectID=entity->ObjectID;record.GUID=entity->Object->OBJ->GUID;record.Position=entity->Position();
  record.RawParent=entity->ObjectData[26];record.RawHierarchySlot=entity->ObjectData[14];
  record.RawContainer=entity->ObjectData[2];record.RawPhysicalSlot=entity->ObjectData[3];record.RawSlotCount=entity->ObjectData[67];
  record.Node=node->second;
  StateNeed(state.Records.emplace(record.ObjectID,std::move(record)).second,"source hierarchy live object identity is duplicated");
 }
 TS1ValidateHierarchy(state);return state;
}

void VMContext::TS1ValidateHierarchy(const VMTS1HierarchyState& state) const {
 const auto& context=StateOwner(*this);if(&context!=this){context.TS1ValidateHierarchy(state);return;}
 StateNeed(VM&&VM->TS1,"source hierarchy validation requires its TS1 owner");
 StateNeed(state.Records.size()<=32767,"source hierarchy object domain exceeded");
 const auto cells=StateCells(context);
 for(const auto& heads:state.TileHeads){StateNeed(heads.size()==cells,"source hierarchy tile grid extent changed");for(auto id:heads)StateNeed(id==0||(id>0&&state.Records.count(id)),"source hierarchy tile head has no saved identity");}
 size_t live=0,totalSlots=0;
 for(auto* entity:VM->Entities)if(StateLive(entity)){
  ++live;const auto found=state.Records.find(entity->ObjectID);
  StateNeed(found!=state.Records.end(),"source hierarchy omits a live identity");
  StateNeed(entity->ObjectData.size()>67,"source hierarchy live scalar bank is truncated");
 }
 StateNeed(live==state.Records.size(),"source hierarchy identity domain differs from the owner");
 for(const auto& entry:state.Records){
  const auto& record=entry.second;auto* entity=VM->GetObjectById(entry.first);
  StateNeed(entry.first>0&&record.ObjectID==entry.first&&StateLive(entity)&&entity->Object->OBJ->GUID==record.GUID,"source hierarchy object/GUID binding changed");
  StateNeed(!record.Node.Slots.empty()&&record.Node.Slots.size()<=32767,"source hierarchy vector extent exceeds its classic domain");
  totalSlots+=record.Node.Slots.size();
  // A bounded snapshot cannot expand into a quadratic object-by-slot matrix.
  // This is an explicit aggregate allocation guard, independent of raw67
  // and of the grow-only per-record extent. It is not an original rule.
  StateNeed(totalSlots<=16u*1024u*1024u,"source hierarchy aggregate slot storage exceeds its snapshot bound");
  for(auto child:record.Node.Slots)StateNeed(child==0||(child>0&&state.Records.count(child)),"source hierarchy child has no saved identity");
 }
 // Generic authored writes to14/26/67 do not modify the child vector. Validate
 // that actual vector, never invent parent reciprocity from the scalar bank.
 std::array<uint8_t,32768> color{};
 std::vector<std::pair<int16_t,size_t>> pending;pending.reserve(state.Records.size());
 for(const auto& entry:state.Records){
  if(color[uint16_t(entry.first)]==2)continue;
  color[uint16_t(entry.first)]=1;pending.emplace_back(entry.first,0);
  while(!pending.empty()){
   auto& frame=pending.back();const auto& slots=state.Records.at(frame.first).Node.Slots;
   if(frame.second==slots.size()){color[uint16_t(frame.first)]=2;pending.pop_back();continue;}
   const auto child=slots[frame.second++];if(!child)continue;
   StateNeed(color[uint16_t(child)]!=1,"source hierarchy child vector is cyclic");
   if(color[uint16_t(child)]==2)continue;
   color[uint16_t(child)]=1;pending.emplace_back(child,0);
  }
 }
}

void VMContext::TS1PublishHierarchy(VMTS1HierarchyState&& state,bool preservePhysicalViews){
 auto& context=StateOwner(*this);if(&context!=this){context.TS1PublishHierarchy(std::move(state),preservePhysicalViews);return;}
 TS1ValidateHierarchy(state);
 struct PhysicalView{VMEntity* Entity=nullptr;VMEntity* Container=nullptr;int16_t Slot=-1;std::vector<VMEntity*> Slots;};
 std::vector<PhysicalView> views;
 views.reserve(state.Records.size());
 for(const auto& entry:state.Records){
   PhysicalView view;view.Entity=VM->GetObjectById(entry.first);
   const auto& node=entry.second.Node;
   if(preservePhysicalViews){
    view.Container=view.Entity->Container;view.Slot=view.Entity->ContainerSlot;
    view.Slots=view.Entity->Contained;
    for(size_t i=node.Slots.size()-1;i<view.Slots.size();++i)
     StateNeed(view.Slots[i]==nullptr,"preserved physical occupant exceeds its source vector extent");
    view.Slots.resize(node.Slots.size()-1,nullptr);
   } else {
   view.Slots.reserve(node.Slots.size()-1);
   for(size_t i=1;i<node.Slots.size();++i)view.Slots.push_back(node.Slots[i]?VM->GetObjectById(node.Slots[i]):nullptr);
   auto* resolved=&entry.second;size_t remaining=state.Records.size()+1;
   while(resolved->RawHierarchySlot==0){
    StateNeed(remaining-->0,"source hierarchy physical projection has a cyclic raw parent chain");
    const auto parent=state.Records.find(resolved->RawParent);
    if(parent==state.Records.end())break; // Do not normalize the literal bank.
    resolved=&parent->second;
   }
   const auto parent=state.Records.find(resolved->RawParent);
   if(parent!=state.Records.end()){
    view.Container=VM->GetObjectById(parent->first);
    view.Slot=resolved->RawHierarchySlot==-1?int16_t(-1):int16_t(int(resolved->RawHierarchySlot)-1);
   }
   }
   views.push_back(std::move(view));
 }
 std::unordered_map<int16_t,VMTS1HierarchyNode> nodes;nodes.reserve(state.Records.size());
 for(auto& entry:state.Records){
  auto& node=entry.second.Node;
  node.Metadata=TS1BuildEntitySlotMetadata(*VM->GetObjectById(entry.first),node.Slots.size());
  StateNeed(node.Metadata.size()==node.Slots.size(),"restored metadata would shrink its constructor prefix");
  node.NeedsPlacement=false;nodes.emplace(entry.first,std::move(node));
 }
 // No allocations, position commits, callbacks, carry transitions or light
 // refreshes follow this point. Everything that can reject was staged above.
 TS1HierarchyNodes.swap(nodes);SpatialTileHeads.swap(state.TileHeads);
 for(const auto& entry:state.Records){auto* entity=VM->GetObjectById(entry.first);const auto& value=entry.second;
  entity->ObjectData[14]=value.RawHierarchySlot;entity->ObjectData[26]=value.RawParent;entity->ObjectData[67]=value.RawSlotCount;
  if(!preservePhysicalViews){entity->ObjectData[2]=value.RawContainer;entity->ObjectData[3]=value.RawPhysicalSlot;}
 }
 for(auto& view:views){
  view.Entity->Contained.swap(view.Slots);view.Entity->Container=view.Container;view.Entity->ContainerSlot=view.Slot;
  if(!preservePhysicalViews&&view.Entity->WorldUI){view.Entity->WorldUI->Container=view.Container?view.Container->WorldUI.get():nullptr;view.Entity->WorldUI->ContainerSlot=view.Container?view.Slot:0;}
 }
}

}} // FSO::SimAntics
