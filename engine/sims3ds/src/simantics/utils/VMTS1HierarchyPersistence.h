// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMEntity.h"
#include "files/formats/iff/chunks/OBJM.h"
#include "files/formats/iff/chunks/ARRY.h"
#include "files/formats/iff/IffRawFile.h"
#include <stdexcept>
#include <unordered_set>
namespace FSO { namespace SimAntics { namespace Engine { namespace Utils {
// The classic ObjM vector is an independent, already serialized channel.
// No child graph is inferred by reversing raw Parent26/Hierarchy14.
struct VMTS1HierarchyPersistence {
    using State=VMTS1HierarchyState;
    using OBJM=Files::Formats::IFF::Chunks::OBJM;
    using OBJMInstance=Files::Formats::IFF::Chunks::OBJMInstance;
    static void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
    struct PublicationScope {
        VMContext& Context; bool Previous;
        explicit PublicationScope(VMContext& context):Context(context),Previous(context.TS1HierarchyPublication){context.TS1HierarchyPublication=true;}
        ~PublicationScope(){Context.TS1HierarchyPublication=Previous;}
    };
    static size_t SavedExtent(const OBJMInstance& instance) {
        const int32_t n=instance.SavedSlotRecordCount;
        Need(n>=-1 && n<=32767,"ObjM source slot extent exceeds domain");
        const size_t result=n<0?instance.Slots.size()+1:size_t(n);
        Need(result==0 || result==instance.Slots.size()+1,"ObjM source slot extent disagrees with decoded prefix");
        return result;
    }
    static State Stage(VM& vm,const OBJM& objects,bool readHeads=true) {
        auto state=vm.Context->TS1CaptureHierarchy();
        for(const auto& saved:objects.ObjectData) {
            const auto& instance=saved.second.Instance;
            if(!instance)continue;
            auto* entity=vm.GetObjectById(int16_t(saved.first));
            if(!entity||entity->Dead||entity->GhostImage)continue;
            Need(instance->OBJT&&entity->Object&&entity->Object->OBJ&&
                instance->OBJT->GUID==entity->Object->OBJ->GUID,"ObjM source hierarchy identity changed");
            auto found=state.Records.find(entity->ObjectID);
            Need(found!=state.Records.end(),"constructed source hierarchy record missing");
            auto& record=found->second;
            Need(instance->ObjectData.size()>67,"ObjM source hierarchy scalar bank truncated");
            record.Position=LotView::Model::LotTilePos(int16_t(instance->X),int16_t(instance->Y),int8_t(instance->Level));
            record.RawParent=instance->ObjectData[26];record.RawHierarchySlot=instance->ObjectData[14];
            record.RawContainer=instance->ObjectData[2];record.RawPhysicalSlot=instance->ObjectData[3];
            record.RawSlotCount=instance->ObjectData[67];
            const auto n=SavedExtent(*instance);
            if(record.Node.Slots.size()<n)record.Node.Slots.resize(n,0);
            if(n){record.Node.Slots[0]=instance->LinkedSlot.ObjectID;
                for(size_t i=1;i<n;++i)record.Node.Slots[i]=instance->Slots[i-1].ObjectID;}
        }
        if(readHeads&&objects.ChunkParent) {
            const auto* arch=vm.Context->Architecture();Need(arch,"source hierarchy has no architecture");
            for(size_t story=0;story<state.TileHeads.size();++story) {
                auto* layer=objects.ChunkParent->Get<Files::Formats::IFF::Chunks::ARRY>(uint16_t(3+100*story));
                if(!layer)continue;
                const auto data=layer->TransposeData();
                Need(layer->Width>0&&layer->Height>0&&data.size()>=size_t(layer->Width)*layer->Height*2,"ObjM source object grid truncated");
                auto& heads=state.TileHeads[story];heads.assign(size_t(arch->Width)*arch->Height,0);
                for(int y=0;y<arch->Height&&y<layer->Height;++y)for(int x=0;x<arch->Width&&x<layer->Width;++x){
                    const size_t offset=(size_t(y)*layer->Width+x)*2;
                    heads[size_t(y)*arch->Width+x]=int16_t(uint16_t(data[offset])|(uint16_t(data[offset+1])<<8));
                }
                // Saved arrays do not authorize dropping a newly created
                // bootstrap root. OOW bootstrap records are retained without
                // inventing tile placement; ambiguous occupied cells fail.
                for(const auto& record:state.Records){
                    if(objects.ObjectData.count(record.first))continue;
                    const auto position=record.second.Position;
                    if(position.Level!=int(story)+1||position.x<0||position.y<0||
                       position.TileX()>=arch->Width||position.TileY()>=arch->Height)continue;
                    const auto cell=size_t(position.TileY())*arch->Width+position.TileX();
                    const auto liveHead=vm.Context->SpatialTileHeads[story].at(cell);
                    Need(!heads[cell],"bootstrap hierarchy overlaps an imported tile root");
                    Need(liveHead&&objects.ObjectData.count(liveHead)==0,"bootstrap hierarchy lacks an independent tile root");
                    heads[cell]=liveHead;
                }
            }
        }
        vm.Context->TS1ValidateHierarchy(state);return state;
    }
    // A source0 sibling can share a physical owner/logical slot without being
    // the slot's first cached occupant. The bound classic vector proves it.
    static bool PhysicalMember(const State& state,int16_t owner,int slot,int16_t child) {
        const auto found=state.Records.find(owner);
        if(slot<0||found==state.Records.end()||size_t(slot)+1>=found->second.Node.Slots.size())return false;
        int16_t current=found->second.Node.Slots[size_t(slot)+1];
        size_t remaining=state.Records.size()+1;
        while(current&&remaining--){if(current==child)return true;
            const auto next=state.Records.find(current);if(next==state.Records.end()||next->second.Node.Slots.empty())return false;
            current=next->second.Node.Slots[0];}
        return false;
    }
    static void VerifyBoundClassic(VM& vm,const OBJM& objects,Files::Formats::IFF::IffRawFile* house=nullptr) {
        const auto live=vm.Context->TS1CaptureHierarchy();
        for(auto* entity:vm.Entities)if(entity&&!entity->Dead&&!entity->GhostImage&&entity->Object&&entity->Object->OBJ)
            Need(live.Records.count(entity->ObjectID)!=0,"source hierarchy capture lacks a live authoritative node");
        for(const auto& item:live.Records){const auto saved=objects.ObjectData.find(item.first);
            Need(saved!=objects.ObjectData.end()&&saved->second.Instance,"synchronized ObjM omits source hierarchy record");
            const auto& instance=*saved->second.Instance;const auto& record=item.second;
            Need(instance.OBJT&&instance.OBJT->GUID==record.GUID,"synchronized source hierarchy GUID changed");
            Need(instance.ObjectData.size()>67&&instance.ObjectData[26]==record.RawParent&&
                 instance.ObjectData[14]==record.RawHierarchySlot&&instance.ObjectData[67]==record.RawSlotCount&&
                 instance.ObjectData[2]==record.RawContainer&&instance.ObjectData[3]==record.RawPhysicalSlot,
                 "synchronized source hierarchy scalar changed");
            Need(SavedExtent(instance)==record.Node.Slots.size(),"synchronized source hierarchy extent changed");
            if(!record.Node.Slots.empty())Need(instance.LinkedSlot.ObjectID==record.Node.Slots[0],"synchronized source slot0 changed");
            for(size_t i=1;i<record.Node.Slots.size();++i)Need(instance.Slots[i-1].ObjectID==record.Node.Slots[i],"synchronized source child changed");
        }
        if(house){
            const auto* arch=vm.Context->Architecture();Need(arch,"source hierarchy has no architecture");
            for(size_t story=0;story<live.TileHeads.size();++story){
                const auto* raw=house->Find("Arry",uint16_t(3+100*story));
                Need(raw,"synchronized source tile grid missing");
                Files::Formats::IFF::Chunks::ARRY layer;st::Reader input(raw->Data);layer.Read(nullptr,input);
                const auto data=layer.TransposeData();
                Need(layer.Width>=arch->Width&&layer.Height>=arch->Height&&data.size()>=size_t(layer.Width)*layer.Height*2,
                     "synchronized source tile grid truncated");
                Need(live.TileHeads[story].size()==size_t(arch->Width)*arch->Height,"live source tile grid extent changed");
                for(int y=0;y<arch->Height;++y)for(int x=0;x<arch->Width;++x){
                    const size_t offset=(size_t(y)*layer.Width+x)*2;
                    const auto id=int16_t(uint16_t(data[offset])|(uint16_t(data[offset+1])<<8));
                    Need(live.TileHeads[story][size_t(y)*arch->Width+x]==id,"synchronized source tile head changed");
                }
            }
        }
    }
};
}}}}
