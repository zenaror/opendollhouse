// SPDX-License-Identifier: MPL-2.0
#include "simantics/VMContext.h"
#include "simantics/VM.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "files/formats/iff/chunks/SLOT.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace FSO { namespace SimAntics {
using namespace Engine;
using namespace Model;
namespace {
using Pos = FSO::LotView::Model::LotTilePos;
constexpr int ParentWord = int(VMStackObjectVariable::ParentId);
constexpr int HierarchyWord = int(VMStackObjectVariable::SlotHierNumber);
constexpr int ContainerWord = int(VMStackObjectVariable::ContainerId);
constexpr int PhysicalWord = int(VMStackObjectVariable::SlotNumber);
constexpr int CountWord = int(VMStackObjectVariable::SlotCount);
VMContext& Owner(VMContext& c) { return c.VM && c.VM->Context ? *c.VM->Context : c; }
const VMContext& Owner(const VMContext& c) { return c.VM && c.VM->Context ? *c.VM->Context : c; }
void Need(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool Interior(const VMContext& c, const Pos& p) {
    const auto* a=c.Architecture();
    return a && p.TileX()>=1 && p.TileY()>=1 && p.TileX()<a->Width-1 && p.TileY()<a->Height-1;
}
bool GridCell(const VMContext& c,const Pos& p,size_t& cell) {
    const auto* a=c.Architecture();
    if (!a || p.Level<1 || p.Level>2 || p.TileX()<0 || p.TileY()<0 ||
        p.TileX()>=a->Width || p.TileY()>=a->Height) return false;
    cell=size_t(p.TileY())*a->Width+p.TileX();
    return cell<c.SpatialTileHeads[size_t(p.Level-1)].size();
}
void PublishPhysicalRoot(VMContext& c,VMEntity& parent,int sourceSlot,int16_t child) {
    if (sourceSlot<=0) return;
    parent.Contained[size_t(sourceSlot-1)]=child?c.VM->GetObjectById(child):nullptr;
}
void PreparePhysicalRoot(VMContext& c,VMEntity& parent,int sourceSlot) {
    if(sourceSlot<=0)return;
    // Constructor/load publication stages the complete physical view. Runtime
    // edge changes must never allocate after an earlier edge was detached.
    const auto n=c.TS1SourceVectorExtent(&parent);
    Need(parent.Contained.size()+1==n,"Original physical view extent differs from its source vector");
}
void PreflightRawLink(VMContext& c,const VMTS1RawPlacementLink& link) {
    auto* parent=c.VM->GetObjectById(link.ParentID);
    if(!parent)return;
    Need(link.SourceSlot>=0 && size_t(link.SourceSlot)<c.TS1SourceVectorExtent(parent),
        "Original source slot is outside its vector");
    PreparePhysicalRoot(c,*parent,link.SourceSlot);
}

}

void VMContext::TS1EnsureHierarchy(VMEntity* e) {
    auto& c=Owner(*this);if(&c!=this){c.TS1EnsureHierarchy(e);return;}
    if (!VM || !VM->TS1 || !e || e->GhostImage) return;
    Need(e->ObjectID>0 && VM->GetObjectById(e->ObjectID)==e,"Original hierarchy requires a live manager identity");
    if (TS1HierarchyNodes.count(e->ObjectID)) return;
    size_t physical=0;
    if(e->Slots) {
        auto found=e->Slots->Slots.find(0);
        if(found!=e->Slots->Slots.end()) physical=found->second.size();
    }
    if(dynamic_cast<VMAvatar*>(e)) physical+=5; //459797, after resource slots.
    physical=std::max(physical,size_t(std::max(0,e->TotalSlots())));
    Need(physical<32767,"Original hierarchy constructor exceeds the classic slot domain");
    VMTS1HierarchyNode node;node.Slots.assign(physical+1,0);node.NeedsPlacement=true;
    node.Metadata=TS1BuildEntitySlotMetadata(*e,physical+1);
    Need(node.Metadata.size()==node.Slots.size(),"Original metadata constructor extent changed");
    std::vector<VMEntity*> view(physical,nullptr);
    for(int i=0;i<e->TotalSlots();++i) {
        auto* child=e->GetSlot(i);view[size_t(i)]=child;
        node.Slots[size_t(i)+1]=child?child->ObjectID:0;
    }
    TS1HierarchyNodes.emplace(e->ObjectID,std::move(node));
    e->Contained.swap(view);
    // Fresh constructor publication, not a read-side scalar normalization.
    e->ObjectData[ParentWord]=0;e->ObjectData[HierarchyWord]=-1;
    e->ObjectData[ContainerWord]=e->Container?e->Container->ObjectID:0;
    e->ObjectData[PhysicalWord]=e->Container?e->ContainerSlot:-1;
    e->ObjectData[CountWord]=int16_t(physical);
}

VMEntity* VMContext::TS1SourceSlot(const VMEntity* e,int index) const {
    const auto& c=Owner(*this);if(&c!=this)return c.TS1SourceSlot(e,index);
    if(!e || index<0)return nullptr;
    auto node=TS1HierarchyNodes.find(e->ObjectID);
    Need(node!=TS1HierarchyNodes.end(),"Original hierarchy lacks its source slot vector");
    if(size_t(index)>=node->second.Slots.size())return nullptr;
    const auto id=node->second.Slots[size_t(index)];
    return id && VM ? VM->GetObjectById(id):nullptr;
}

const TS1SlotMetadata* VMContext::TS1SourceSlotMetadata(const VMEntity* e,int index) const {
    const auto& c=Owner(*this);if(&c!=this)return c.TS1SourceSlotMetadata(e,index);
    if(!e || index<0)return nullptr;
    const auto node=TS1HierarchyNodes.find(e->ObjectID);
    Need(node!=TS1HierarchyNodes.end(),"Original metadata lacks its source node");
    if(size_t(index)>=node->second.Slots.size())return nullptr;
    Need(node->second.Metadata.size()==node->second.Slots.size(),"Original metadata/source vector extents differ");
    return &node->second.Metadata[size_t(index)];
}

size_t VMContext::TS1SourceVectorExtent(const VMEntity* e) const {
    const auto& c=Owner(*this);if(&c!=this)return c.TS1SourceVectorExtent(e);
    if(!e)return 0;
    auto node=TS1HierarchyNodes.find(e->ObjectID);
    Need(node!=TS1HierarchyNodes.end(),"Original hierarchy lacks its source slot extent");
    return node->second.Slots.size();
}

VMEntity* VMContext::TS1SourceParent(const VMEntity* e) const {
    const auto& c=Owner(*this);if(&c!=this)return c.TS1SourceParent(e);
    return e && VM ? VM->GetObjectById(e->ObjectData[ParentWord]):nullptr;
}

VMTS1RawPlacementLink VMContext::TS1CaptureRawLink(const VMEntity* e) const {
    const auto& c=Owner(*this);if(&c!=this)return c.TS1CaptureRawLink(e);
    Need(e!=nullptr,"Cannot capture a missing original placement");
    VMTS1RawPlacementLink link;link.Position=e->Position();link.Valid=Interior(c,link.Position);
    auto* parent=TS1SourceParent(e);link.ParentID=parent?parent->ObjectID:0;
    link.SourceSlot=e->ObjectData[HierarchyWord];return link;
}

VMTS1PhysicalPlacement VMContext::TS1PhysicalRequest(const VMEntity* e,VMEntity* parent,int slot) const {
    Need(e!=nullptr,"Cannot construct a missing original placement");
    Need(!parent || (slot>=0 && slot<32766),"Invalid original physical slot request");
    VMTS1PhysicalPlacement request;request.Valid=true;
    request.Position=parent?parent->Position():e->Position();
    request.ParentID=parent?parent->ObjectID:0;request.PhysicalSlot=parent?int16_t(slot):int16_t(-1);
    return request;
}

VMTS1RawPlacementLink VMContext::TS1NormalizePhysicalRequest(const VMTS1PhysicalPlacement& p) {
    Need(p.PhysicalSlot>=-1 && p.PhysicalSlot<32766,"Invalid original physical slot normalization");
    return {p.Valid,p.Position,p.ParentID,p.PhysicalSlot==-1?int16_t(-1):int16_t(p.PhysicalSlot+1)};
}

void VMContext::TS1PublishRawLink(VMEntity* e,const VMTS1RawPlacementLink& link) {
    auto& c=Owner(*this);if(&c!=this){c.TS1PublishRawLink(e,link);return;}
    Need(e && VM && VM->TS1,"Original raw publication requires a TS1 object");
    TS1EnsureHierarchy(e);
    auto* parent=VM->GetObjectById(link.ParentID);
    if(parent) {
        auto found=TS1HierarchyNodes.find(parent->ObjectID);
        Need(found!=TS1HierarchyNodes.end() && link.SourceSlot>=0 &&
            size_t(link.SourceSlot)<found->second.Slots.size(),"Original parent source slot is outside its vector");
        PreparePhysicalRoot(c,*parent,link.SourceSlot);
    }
    // Validate both locations before DetachRawLink can mutate either edge.
    if(!link.Valid)PreflightRawLink(c,TS1CaptureRawLink(e));
    if(!link.Valid)TS1DetachRawLink(e);
    if(parent) {
        TS1HierarchyNodes.at(parent->ObjectID).Slots[size_t(link.SourceSlot)]=e->ObjectID;
        e->ObjectData[ParentWord]=parent->ObjectID;e->ObjectData[HierarchyWord]=link.SourceSlot;
        PublishPhysicalRoot(c,*parent,link.SourceSlot,e->ObjectID);
    } else if(link.Valid) {
        size_t cell=0;if(GridCell(c,link.Position,cell)) SpatialTileHeads[size_t(link.Position.Level-1)][cell]=e->ObjectID;
        e->ObjectData[ParentWord]=0;e->ObjectData[HierarchyWord]=-1;
    } else return;
    auto pos=e->Position();pos.Level=link.Position.Level;e->Position(pos); // no raw XY commit
    TS1HierarchyNodes.at(e->ObjectID).NeedsPlacement=false;
    SpatialSnapshotDirty=true;
}

void VMContext::TS1DetachRawLink(VMEntity* e) {
    auto& c=Owner(*this);if(&c!=this){c.TS1DetachRawLink(e);return;}
    auto link=TS1CaptureRawLink(e);auto* parent=VM->GetObjectById(link.ParentID);
    if(parent) {
        auto found=TS1HierarchyNodes.find(parent->ObjectID);
        Need(found!=TS1HierarchyNodes.end() && link.SourceSlot>=0 && size_t(link.SourceSlot)<found->second.Slots.size(),
            "Original detach source slot is outside its vector");
        PreparePhysicalRoot(c,*parent,link.SourceSlot);
        found->second.Slots[size_t(link.SourceSlot)]=0;
        PublishPhysicalRoot(c,*parent,link.SourceSlot,0);
    } else if(link.Valid) {
        size_t cell=0;if(GridCell(c,link.Position,cell)) SpatialTileHeads[size_t(link.Position.Level-1)][cell]=0;
    }
    e->ObjectData[ParentWord]=0;e->ObjectData[HierarchyWord]=-1;SpatialSnapshotDirty=true;
}

void VMContext::TS1RemovePlacement(VMEntity* e) {
    auto& c=Owner(*this);if(&c!=this){c.TS1RemovePlacement(e);return;}
    TS1EnsureHierarchy(e);
    if(TS1HierarchyNodes.at(e->ObjectID).NeedsPlacement)return;
    const auto old=TS1CaptureRawLink(e);auto* next=TS1SourceSlot(e,0);
    PreflightRawLink(c,old);
    // Preflight the second detach before making either source write.
    if(next) {
        const auto child=TS1CaptureRawLink(next);auto* parent=VM->GetObjectById(child.ParentID);
        if(parent) Need(child.SourceSlot>=0 && size_t(child.SourceSlot)<TS1SourceVectorExtent(parent),
            "Original successor detach is outside its source vector");
        if(parent)PreparePhysicalRoot(c,*parent,child.SourceSlot);
    }
    if(auto* parent=VM->GetObjectById(old.ParentID))PreparePhysicalRoot(c,*parent,old.SourceSlot);
    TS1DetachRawLink(e);
    if(next){TS1DetachRawLink(next);TS1PublishRawLink(next,old);}
    TS1HierarchyNodes.at(e->ObjectID).NeedsPlacement=true;
}

void VMContext::TS1PreflightPhysicalPlacement(VMEntity* e,const VMTS1PhysicalPlacement& request) {
    auto& c=Owner(*this);if(&c!=this){c.TS1PreflightPhysicalPlacement(e,request);return;}
    Need(e && VM && VM->TS1,"Original placement preflight requires its TS1 owner");
    const auto node=TS1HierarchyNodes.find(e->ObjectID);
    Need(node!=TS1HierarchyNodes.end() && !node->second.Slots.empty(),"Original placement lacks its source node");
    if(!node->second.NeedsPlacement) {
        PreflightRawLink(c,TS1CaptureRawLink(e));
        if(auto* next=TS1SourceSlot(e,0))PreflightRawLink(c,TS1CaptureRawLink(next));
    }
    if(!request.Valid)return;
    const auto link=TS1NormalizePhysicalRequest(request);
    auto* parent=VM->GetObjectById(link.ParentID);
    PreflightRawLink(c,link);
    VMEntity* previous=nullptr;
    if(parent)previous=TS1SourceSlot(parent,link.SourceSlot);
    else {size_t cell=0;if(GridCell(c,link.Position,cell))previous=VM->GetObjectById(SpatialTileHeads[size_t(link.Position.Level-1)][cell]);}
    if(previous && previous!=e)PreflightRawLink(c,TS1CaptureRawLink(previous));
}

void VMContext::TS1CommitPhysicalPlacement(VMEntity* e,const VMTS1PhysicalPlacement& request) {
    auto& c=Owner(*this);if(&c!=this){c.TS1CommitPhysicalPlacement(e,request);return;}
    TS1EnsureHierarchy(e);TS1PreflightPhysicalPlacement(e,request);
    const auto link=TS1NormalizePhysicalRequest(request);
    // 432a70 handles an invalid committed request as removal only. It does
    // not publish self again, which would erase the promoted tile successor.
    if(!request.Valid) {
        TS1RemovePlacement(e);
        e->ClearTS1PhysicalPlacement(this);
        return;
    }
    // Validate the destination before touching the object's current edges.
    auto* parent=VM->GetObjectById(link.ParentID);
    if(parent) {
        Need(link.SourceSlot>=0 && size_t(link.SourceSlot)<TS1SourceVectorExtent(parent),
            "Original destination is outside its source vector");
        PreparePhysicalRoot(c,*parent,link.SourceSlot);
    }
    // The old destination occupant may require another raw-parent detach.
    // Preflight it now, before removal promotes a source0 successor there.
    VMEntity* destination=nullptr;
    if(parent)destination=TS1SourceSlot(parent,link.SourceSlot);
    else {size_t cell=0;if(GridCell(c,link.Position,cell))destination=VM->GetObjectById(SpatialTileHeads[size_t(link.Position.Level-1)][cell]);}
    if(destination && destination!=e)PreflightRawLink(c,TS1CaptureRawLink(destination));
    if(destination==e) {
        // 43308e: an already occupied destination is not detached/relinked.
        // The outer physical adapter may still publish its distinct cache.
        e->ObjectData[ContainerWord]=request.ParentID;e->ObjectData[PhysicalWord]=request.PhysicalSlot;
        e->Container=parent;e->ContainerSlot=parent?request.PhysicalSlot:int16_t(-1);
        if(e->WorldUI){e->WorldUI->Container=parent?parent->WorldUI.get():nullptr;e->WorldUI->ContainerSlot=parent?request.PhysicalSlot:0;}
        return;
    }
    if(!TS1HierarchyNodes.at(e->ObjectID).NeedsPlacement)TS1RemovePlacement(e);
    VMEntity* previous=nullptr;
    if(parent) {
        Need(link.SourceSlot>=0 && size_t(link.SourceSlot)<TS1SourceVectorExtent(parent),
            "Original destination is outside its source vector");
        PreparePhysicalRoot(c,*parent,link.SourceSlot);
        if(link.Valid)previous=TS1SourceSlot(parent,link.SourceSlot);
    } else {
        size_t cell=0;if(link.Valid && GridCell(c,link.Position,cell)) previous=VM->GetObjectById(SpatialTileHeads[size_t(link.Position.Level-1)][cell]);
    }
    if(previous==e)previous=nullptr;
    if(previous) TS1DetachRawLink(previous);
    TS1PublishRawLink(e,link);
    if(previous) TS1PublishRawLink(previous,{true,previous->Position(),e->ObjectID,0});
    TS1HierarchyNodes.at(e->ObjectID).NeedsPlacement=false;
    //4333ef stores the physical request independently from raw26/14.
    e->ObjectData[ContainerWord]=request.ParentID;e->ObjectData[PhysicalWord]=request.PhysicalSlot;
    e->Container=parent;e->ContainerSlot=parent?request.PhysicalSlot:int16_t(-1);
    if(e->WorldUI){e->WorldUI->Container=parent?parent->WorldUI.get():nullptr;e->WorldUI->ContainerSlot=parent?request.PhysicalSlot:0;}
}

VMTS1PhysicalPlacement VMContext::TS1ResolvePhysicalPlacement(VMEntity* e,bool normalize) const {
    const auto& c=Owner(*this);if(&c!=this)return c.TS1ResolvePhysicalPlacement(e,normalize);
    Need(e && VM,"Original physical resolution requires a live object");
    VMTS1PhysicalPlacement result;result.Position=e->Position();
    result.Valid=Interior(c,result.Position);
    if(!result.Valid){result.Position.x=-16;result.Position.y=-16;}
    auto* current=e;size_t remaining=VM->Entities.size()+1;
    while(current->ObjectData[HierarchyWord]==0) {
        Need(remaining-->0,"Original physical resolution has a cyclic parent chain");
        auto* parent=TS1SourceParent(current);
        if(!parent){if(normalize)current->ObjectData[HierarchyWord]=-1;break;}
        current=parent;
    }
    auto* parent=TS1SourceParent(current);result.ParentID=parent?parent->ObjectID:0;
    const auto slot=current->ObjectData[HierarchyWord];result.PhysicalSlot=slot==-1?int16_t(-1):int16_t(slot-1);
    return result;
}

}} // FSO::SimAntics
