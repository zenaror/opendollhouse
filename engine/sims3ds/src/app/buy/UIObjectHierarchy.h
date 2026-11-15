/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
// Buy-mode-only snapshot/preview. Scripted moves and global Direction setters
// retain their existing semantics; source containment is never edited by ghosts.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "world/components/ObjectComponent.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
namespace simapp {
class UIObjectHierarchy {
    using Entity=FSO::SimAntics::VMEntity;
    using Group=FSO::SimAntics::Entities::VMMultitileGroup;
    using Context=FSO::SimAntics::VMContext;
    using VM=FSO::SimAntics::VM;
    using Direction=FSO::LotView::Model::Direction;
    using Pos=FSO::LotView::Model::LotTilePos;
    struct Part {
        Entity* source=nullptr;int16_t id=0;Group* group=nullptr;
        Entity* parent=nullptr;int16_t parentID=0,slot=-1;
        Pos position;Direction direction=Direction::NORTH;
        Entity* preview=nullptr;
        FSO::LotView::Components::EntityComponent* world=nullptr;bool visible=false;
    };
    std::vector<Part> parts_;
    std::unordered_map<Entity*,size_t> index_;
    std::vector<Group*> groups_,previews_; // root first; owned preview children only
    std::unordered_map<Group*,size_t> sizes_;
    Group* root_=nullptr;
    Pos origin_;Direction facing_=Direction::NORTH;
    static int Notch(Direction d){unsigned n=unsigned(d);for(int i=0;i<8;++i)if(n==(1u<<i))return i;return -1;}
    static Direction Turn(Direction d,int delta){unsigned n=unsigned(d);delta=(delta+8)%8;return Direction(((n<<delta)|(n>>(8-delta)))&255);}
    Pos Transformed(const Pos& p,const Pos& origin,int delta)const{
        if(p==Pos::OUT_OF_WORLD||origin==Pos::OUT_OF_WORLD)return Pos::OUT_OF_WORLD;
        const float r=delta*float(M_PI)/4,c=std::cos(r),s=std::sin(r);const int x=p.x-origin_.x,y=p.y-origin_.y;
        return Pos(int16_t(std::lround(origin.x+x*c-y*s)),int16_t(std::lround(origin.y+x*s+y*c)),int8_t(origin.Level+p.Level-origin_.Level));
    }
    Group* CopyCompleteGroup(Group* group,Context* ctx){
        auto* copy=ctx->GhostCopyGroup(group);
        bool exact=copy&&copy->Objects.size()==group->Objects.size()&&copy->Offsets==group->Offsets;
        if(exact)for(size_t i=0;i<copy->Objects.size();++i)if(copy->Objects[i]->Object->GUID!=group->Objects[i]->Object->GUID){exact=false;break;}
        if(exact)return copy;
        if(copy)copy->Delete(ctx);
        // Dynamic multitile groups can contain more parts than their base GUID
        // recreates. Reuse object construction/ghost state per physical part.
        auto* result=ctx->VM->OwnGroup(new Group());result->MultiTile=group->MultiTile;result->Price=group->Price;
        for(size_t i=0;i<group->Objects.size();++i){
            auto* original=group->Objects[i];
            auto* one=ctx->CreateObjectInstance(original->Object->OBJ->GUID,Pos::OUT_OF_WORLD,original->Direction(),true,false);
            if(!one||one->Objects.size()!=1){if(one)one->Delete(ctx);result->Delete(ctx);return nullptr;}
            auto* part=one->Objects[0];one->RemoveObject(part);result->AddDynamicObject(part,group->Offsets[i]);part->MultitileGroup=result;
            part->MasterDefinition=original->MasterDefinition;part->GhostOriginal=group;
            part->SetValue(FSO::SimAntics::Model::VMStackObjectVariable::Graphic,original->GetValue(FSO::SimAntics::Model::VMStackObjectVariable::Graphic));
            part->DynamicSpriteFlags=original->DynamicSpriteFlags;part->DynamicSpriteFlags2=original->DynamicSpriteFlags2;
            part->SetDynamicSpriteFlag(0,original->IsDynamicSpriteFlagSet(0));part->PlatformState=original->PlatformState;
            if(auto* go=dynamic_cast<FSO::SimAntics::VMGameObject*>(part))go->RefreshGraphic();
        }
        return result;
    }
public:
    UIObjectHierarchy()=default;
    UIObjectHierarchy(const UIObjectHierarchy&)=delete;
    UIObjectHierarchy& operator=(const UIObjectHierarchy&)=delete;
    Entity* PreviewFor(Entity* source)const{auto it=index_.find(source);return it==index_.end()?nullptr:parts_[it->second].preview;}
    Group* Root()const{return root_;}
    size_t GroupCount()const{return groups_.size();}
    size_t PartCount()const{return parts_.size();}
    // Validate the whole group/slot graph before any IsUserMovable recursion.
    bool Capture(Group* root,VM* vm){
        if(!parts_.empty()||!root||!root->BaseObject()||!vm)return false;
        root_=root;origin_=root->BaseObject()->Position();facing_=root->BaseObject()->Direction();if(Notch(facing_)<0)return false;
        std::unordered_set<Group*> seen;groups_.push_back(root);seen.insert(root);
        for(size_t g=0;g<groups_.size();++g){
            auto* group=groups_[g];if(group->Objects.empty()||group->Objects.size()!=group->Offsets.size())return false;
            sizes_[group]=group->Objects.size();
            for(auto* e:group->Objects){
                if(!e||e->Dead||e->GhostImage||!dynamic_cast<FSO::SimAntics::VMGameObject*>(e)||e->MultitileGroup!=group||vm->GetObjectById(e->ObjectID)!=e||Notch(e->Direction())<0||index_.count(e))return false;
                index_[e]=parts_.size();parts_.push_back({e,e->ObjectID,group,e->Container,int16_t(e->Container?e->Container->ObjectID:0),e->ContainerSlot,e->Position(),e->Direction(),nullptr,e->WorldUI.get(),e->WorldUI&&e->WorldUI->Visible});
                for(int slot=0;slot<e->TotalSlots();++slot){auto* child=e->GetSlot(slot);if(!child)continue;if(child->Container!=e||child->ContainerSlot!=slot||!child->MultitileGroup)return false;
                    if(seen.insert(child->MultitileGroup).second)groups_.push_back(child->MultitileGroup);}
                if(parts_.size()>vm->Entities.size())return false;
            }
        }
        for(const auto& p:parts_){
            // A picked root may itself be on an external support. A child
            // group's other parts cannot drag an unrelated external support.
            if(p.parent&&p.group!=root_&&!index_.count(p.parent))return false;
            std::unordered_set<Entity*> chain;Entity* e=p.source;
            while(e&&index_.count(e)){if(!chain.insert(e).second)return false;e=parts_[index_.at(e)].parent;}
            if(p.parent&&(vm->GetObjectById(p.parentID)!=p.parent||p.parent->GetSlot(p.slot)!=p.source))return false;
        }
        return true;
    }
    bool SourcesCurrent(VM* vm)const{
        if(!root_||parts_.empty())return false;
        for(auto* group:groups_){
            if(group->Objects.size()!=sizes_.at(group))return false;
            for(auto* e:group->Objects){auto it=index_.find(e);if(it==index_.end()||parts_[it->second].group!=group)return false;}
        }
        for(const auto& p:parts_){
            // Resolve identity before dereferencing the retained source pointer.
            if(vm->GetObjectById(p.id)!=p.source)return false;
            auto* e=p.source;if(e->Dead||e->WorldUI.get()!=p.world||e->MultitileGroup!=p.group||e->Position()!=p.position||e->Direction()!=p.direction||e->Container!=p.parent||(p.parent&&e->ContainerSlot!=p.slot))return false;
            if(p.parent&&(vm->GetObjectById(p.parentID)!=p.parent||p.parent->GetSlot(p.slot)!=e))return false;
            for(int slot=0;slot<e->TotalSlots();++slot)if(auto* child=e->GetSlot(slot)){auto it=index_.find(child);if(it==index_.end()||parts_[it->second].parent!=e||parts_[it->second].slot!=slot)return false;}
        }
        return true;
    }
    void HideSources(VM* vm){
        if(!SourcesCurrent(vm))return;
        for(const auto& p:parts_)if(p.world)p.world->Visible=false;
    }
    void RestoreVisibility(VM* vm){
        for(const auto& p:parts_)if(vm->GetObjectById(p.id)==p.source&&!p.source->Dead&&p.source->WorldUI.get()==p.world&&p.world)p.world->Visible=p.visible;
    }
    Group* CreateRootPreview(Context* ctx){return root_?CopyCompleteGroup(root_,ctx):nullptr;}
    bool CreatePreviews(Group* rootPreview,Context* ctx){
        if(!rootPreview||!previews_.empty()||rootPreview->Objects.size()!=root_->Objects.size())return false;
        for(size_t g=0;g<groups_.size();++g){
            auto* copy=g?CopyCompleteGroup(groups_[g],ctx):rootPreview;
            if(g&&copy)previews_.push_back(copy);
            if(!copy||copy->Objects.size()!=groups_[g]->Objects.size()){ClearPreviews(ctx);return false;}
            for(size_t i=0;i<copy->Objects.size();++i){auto& p=parts_[index_.at(groups_[g]->Objects[i])];p.preview=copy->Objects[i];p.preview->SetRoom(65534);
                if(p.preview->WorldUI)p.preview->WorldUI->Visible=p.visible;
                if(auto* c=dynamic_cast<FSO::LotView::Components::ObjectComponent*>(p.preview->WorldUI.get()))c->ForceDynamic=true;}
        }
        UpdatePreviews(rootPreview,ctx);return true;
    }
    void UpdatePreviews(Group* rootPreview,Context* ctx){
        const int delta=(Notch(rootPreview->BaseObject()->Direction())-Notch(facing_)+8)%8;
        const auto origin=rootPreview->BaseObject()->Position();
        for(auto& p:parts_)if(p.group!=root_&&p.preview){
            Entity* parent=nullptr;auto it=index_.find(p.parent);if(it!=index_.end())parent=parts_[it->second].preview;
            // Ghosts deliberately have no logical containment. Reuse the real
            // renderer SLOT chain only, and publish location for dirty tiles.
            const auto pos=parent?parent->Position():Transformed(p.position,origin,delta);
            p.preview->SetIndivPosition(pos,Turn(p.direction,delta),ctx,{});
            if(p.preview->WorldUI){p.preview->WorldUI->Container=parent?parent->WorldUI.get():nullptr;p.preview->WorldUI->ContainerSlot=p.slot;}
        }
    }
    void SetPlacementRejected(bool rejected){
        for(const auto& p:parts_)if(p.preview)
            if(auto* c=dynamic_cast<FSO::LotView::Components::ObjectComponent*>(p.preview->WorldUI.get()))
                c->SetPlacementRejected(rejected);
    }
    void ClearPreviews(Context* ctx){
        // Detach all visual links before deleting children in reverse order;
        // no source Container/GetSlot/Direction or world link is modified.
        for(auto& p:parts_)if(p.group!=root_&&p.preview&&p.preview->WorldUI)p.preview->WorldUI->Container=nullptr;
        for(auto it=previews_.rbegin();it!=previews_.rend();++it)(*it)->Delete(ctx);
        previews_.clear();for(auto& p:parts_)p.preview=nullptr;
    }
    // The user-placement hook runs after validation/SetIndivPosition of all
    // root parts but BEFORE their one normal PositionChange/BHAV publication.
    void PrepareCommitted(Direction direction,Context* ctx){
        const int delta=(Notch(direction)-Notch(facing_)+8)%8;
        const auto origin=root_->BaseObject()->Position();
        for(const auto& p:parts_)if(p.group!=root_&&ctx->VM->GetObjectById(p.id)==p.source&&!p.source->Dead){
            auto* e=p.source;ctx->UnregisterObjectPos(e);
            const auto pos=e->Container?e->Container->Position():Transformed(p.position,origin,delta);
            e->SetIndivPosition(pos,Turn(p.direction,delta),ctx,{});
            // A contained object is represented by its support, not a second
            // solid footprint (normal PlaceInSlot/PositionChange leaves none).
            if(e->Container)e->Footprint.reset();
            else e->Footprint=e->GetObstacle(pos,e->Direction());
        }
    }
    void FinishCommitted(Context* ctx){
        // Non-contained parts of a contained multi-tile group are not visited
        // through SLOT recursion. Publish each such subtree once, separately.
        for(const auto& p:parts_)if(p.group!=root_&&ctx->VM->GetObjectById(p.id)==p.source&&!p.source->Dead&&!p.source->Container)p.source->PositionChange(ctx,false);
    }
};
}
