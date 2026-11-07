// SPDX-License-Identifier: MPL-2.0
// Sources: native22665,325af,32888,328f3,32984,327d9.
#include "native_floor_heads.hpp"
#include <algorithm>
#include <sstream>

namespace odh_host {
namespace {
bool fail(std::string& error,const char* why){error=why;return false;}
std::int16_t native_tile(std::int32_t coordinate){
    const auto shifted=coordinate>=0?std::int64_t(coordinate)/16:-((15-std::int64_t(coordinate))/16);
    const auto byte=static_cast<unsigned>(shifted)&255U;
    return static_cast<std::int16_t>(byte>=128?int(byte)-256:int(byte));
}
bool same_xy(const odh::EntityPosition& a,const odh::EntityPosition& b){return a.x==b.x&&a.y==b.y;}
}
NativeFloorHeads::NativeFloorHeads(odh::World& world,std::uint32_t width,std::uint32_t height,std::function<bool()> current):
    world_(&world),width_(width),height_(height),authority_(std::move(current)){}
std::shared_ptr<NativeFloorHeads> NativeFloorHeads::for_current_world(odh::World& world,std::uint32_t width,std::uint32_t height,
        std::function<bool()> authority,PendingUnlinkedSource pending){
    auto owner=std::make_shared<NativeFloorHeads>(world,width,height,std::move(authority));
    owner->current_world_mode_=true;owner->pending_unlinked_source_=std::move(pending);owner->seeded_=owner->knowledge_=true;return owner;
}
bool NativeFloorHeads::mark_source_unlinked(Ref ref,std::string& error){
    if(!authority()||!world_->is_live_reference(ref))return fail(error,"native unlink receipt identity/source unavailable");
    const auto* e=world_->resolve_reference(ref);const auto pose=world_->native_position(ref);
    if(!e||!pose||e->attachment||!e->object_words.count(14)||!e->object_words.count(26)||
       e->object_words.at(14)!=-1||e->object_words.at(26)!=0||e->native_slot0!=std::optional<Ref>{Ref{}})
        return fail(error,"native unlink receipt lacks actual detached backing");
    for(const auto& [where,entry]:heads_)if(entry.head==ref)return fail(error,"native unlink receipt still owns a published head");
    for(std::int16_t cursor=0;;){const auto next=world_->next_entity(cursor,{},true);if(!next)break;cursor=*next;
        const auto* body=world_->find(cursor);if(body&&!body->ui_ghost&&body->native_slot0==std::optional<Ref>{ref})return fail(error,"native unlink receipt still has incoming storage");}
    removed_pending_[ref]=*pose;error.clear();return true;
}
std::optional<NativeFloorHeads::Ref> NativeFloorHeads::current_cell_head(const odh::EntityPosition& point,std::string& error){
    const auto where=tile(point);if(!where||!authority())return {};
    if(unknown_.count(*where))return {};
    std::set<Ref> excluded;if(pending_retired_child_)excluded.insert(pending_retired_child_->second);
    const auto provided=pending_unlinked_source_?pending_unlinked_source_():std::vector<Ref>{};
    for(const auto ref:provided){
        if(!world_->is_live_reference(ref))continue; // Actual retired bodies do not enter the cell inventory.
        const auto* e=world_->find(ref.id);const auto pose=world_->native_position(ref);
        if(!e||!pose||e->attachment||e->native_slot0!=std::optional<Ref>{Ref{}}||!e->object_words.count(14)||!e->object_words.count(26)||
           e->object_words.at(14)!=-1||e->object_words.at(26)!=0){fail(error,"pending Delete source receipt is no longer detached");return {};}
        excluded.insert(ref);
    }
    for(const auto& [ref,pose]:removed_pending_)if(world_->is_live_reference(ref)){
        const auto actual=world_->native_position(ref);
        if(actual&&actual->x==pose.x&&actual->y==pose.y&&actual->level==pose.level)excluded.insert(ref);
    }
    std::map<Ref,View> rows;if(!collect(rows,error))return {};
    Ref result{};
    for(const auto& [ref,row]:rows){
        if(excluded.count(ref)){
            // A source receipt must not excuse a newly incoming chain.
            for(const auto& [other,value]:rows)if(value.child==std::optional<Ref>{ref}&&(!pending_retired_child_||pending_retired_child_->second!=ref)){fail(error,"pending Delete receipt gained an incoming chain");return {};}
            continue;
        }
        const auto at=row.pose?tile(*row.pose):std::optional<Tile>{};
        if(row.pose&&at!=where)continue; // Unknown backing elsewhere does not choose this cell's head.
        if(row.physical){
            const auto* e=world_->resolve_reference(ref);const auto* parent=world_->resolve_reference(*row.physical);
            if(!e||!e->attachment||!parent||row.parent!=row.physical->id||row.slot!=std::int32_t(e->attachment->slot)+1||
               e->attachment->slot>=parent->slots.size()||parent->slots[e->attachment->slot]!=ref.id||
               (!world_->is_live_reference(*row.physical)&&!retained_physical_parent(*row.physical,ref,e->attachment->slot))){fail(error,"current cell physical owner unavailable");return {};}
            continue;
        }
        if(!row.pose||!row.slot||!row.parent||!row.child){fail(error,"current cell native ownership cannot be located/proved");return {};}
        if(*row.parent){
            const auto* parent=world_->find(*row.parent);
            if(*row.slot!=0||!parent||parent->native_slot0!=std::optional<Ref>{ref}){fail(error,"current cell raw-parent storage is not reciprocal");return {};}
            continue;
        }
        if(*row.slot!=-1){fail(error,"current cell root slot is unknown");return {};}
        std::set<Ref> chain;auto cursor=ref;
        while(cursor.id){
            if(!chain.insert(cursor).second||chain.size()>4096){fail(error,"current cell native chain cycles");return {};}
            const auto node=rows.find(cursor);if(node==rows.end()||!node->second.child){fail(error,"current cell chain backing unavailable");return {};}
            const auto child=*node->second.child;if(!child.id){if(child.generation){fail(error,"current cell empty link generation invalid");return {};}break;}
            const auto next=rows.find(child);
            if(next==rows.end()||next->second.physical||next->second.slot!=0||next->second.parent!=cursor.id){fail(error,"current cell child storage is not reciprocal");return {};}
            cursor=child;
        }
        if(result.id){fail(error,"current cell contains ambiguous unparented owners");return {};}
        result=ref;
    }
    if(!authority()){fail(error,"current cell source expired");return {};}
    error.clear();return result;
}
bool NativeFloorHeads::publish_current_cell(const odh::EntityPosition& point,Ref expected,Ref next,std::string& error){
    const auto where=tile(point);if(!where||expected.id<0||next.id<0||(!expected.id&&expected.generation)||(!next.id&&next.generation))return fail(error,"current cell publication has invalid references");
    const auto actual=head(point);if(!actual||*actual!=expected)return fail(error,"current cell compare-and-publish mismatch");
    if(next.id){const auto* e=world_->resolve_reference(next);if(!world_->is_live_reference(next)||!e||e->attachment||!e->object_words.count(14)||!e->object_words.count(26)||e->object_words.at(26)||
        (e->object_words.at(14)!=-1&&e->object_words.at(14)!=0))return fail(error,"current cell next owner is not actually unlinked");}
    if(expected.id){const auto pose=world_->native_position(expected);if(!pose)return fail(error,"current cell previous owner scalar unavailable");removed_pending_[expected]=*pose;}
    heads_[*where]=Entry{next,next.id?world_->native_position(next):std::optional<odh::EntityPosition>{}};
    if(next.id)removed_pending_.erase(next);error.clear();return true;
}
bool NativeFloorHeads::settled_current_cells(std::string& error){
    if(!authority()||pending_retired_child_||!unknown_.empty())return fail(error,"current cell publications are unknown/pending");
    for(auto& [where,entry]:heads_){
        const odh::EntityPosition point{std::int32_t(std::get<0>(where))*16+8,std::int32_t(std::get<1>(where))*16+8,std::get<2>(where),0};
        const auto actual=current_cell_head(point,error);if(!actual||*actual!=entry.head)return fail(error,"current cell published head did not settle to actual native graph");
        entry.pending_pose.reset();
    }
    error.clear();return true;
}
bool NativeFloorHeads::authority()const{return authority_&&authority_();}
std::optional<NativeFloorHeads::Tile> NativeFloorHeads::tile(const odh::EntityPosition& p)const{
    const auto x=native_tile(p.x),y=native_tile(p.y);
    if(x<1||y<1||std::int32_t(x)>=std::int64_t(width_)-1||std::int32_t(y)>=std::int64_t(height_)-1||p.level<1||p.level>3)return {};
    return Tile{x,y,p.level};
}
bool NativeFloorHeads::collect(std::map<Ref,View>& result,std::string& error)const{
    if(!authority())return fail(error,"native floor-head source authority unavailable");
    std::map<Ref,View> candidate;
    for(std::int16_t cursor=0;;){
        const auto next=world_->next_entity(cursor,{},true);if(!next)break;cursor=*next;
        const auto* e=world_->find(cursor);if(!e)return fail(error,"native floor-head cohort disappeared");
        if(e->ui_ghost)continue;
        const Ref ref{e->id,e->generation};View view;view.ref=ref;
        view.pose=world_->native_position(ref);view.child=e->native_slot0;
        const auto slot=e->object_words.find(14),parent=e->object_words.find(26);
        if(slot!=e->object_words.end())view.slot=slot->second;
        if(parent!=e->object_words.end())view.parent=parent->second;
        if(e->attachment)view.physical=world_->attachment_parent(ref);
        candidate.emplace(ref,view);
    }
    if(!authority())return fail(error,"native floor-head source authority expired during cohort collection");
    // No borrowed pointers survive the final authority callback. Recheck owned
    // snapshots so a mutation inside that callback cannot authenticate old data.
    for(const auto& [ref,saved]:candidate){
        const auto* e=world_->find(ref.id);
        if(!e||e->generation!=ref.generation||e->ui_ghost||!(world_->native_position(ref)==saved.pose)||e->native_slot0!=saved.child)
            return fail(error,"native floor-head identity/scalar changed during collection");
        const auto slot=e->object_words.find(14),parent=e->object_words.find(26);
        const auto a=slot==e->object_words.end()?std::optional<std::int16_t>{}:slot->second;
        const auto b=parent==e->object_words.end()?std::optional<std::int16_t>{}:parent->second;
        if(a!=saved.slot||b!=saved.parent||(e->attachment?world_->attachment_parent(ref):std::optional<Ref>{})!=saved.physical)
            return fail(error,"native floor-head hierarchy changed during collection");
    }
    std::size_t count=0;for(std::int16_t cursor=0;;){const auto next=world_->next_entity(cursor,{},true);if(!next)break;cursor=*next;const auto* e=world_->find(cursor);if(e&&!e->ui_ghost)++count;}
    if(count!=candidate.size())return fail(error,"native floor-head membership changed during collection");
    result=std::move(candidate);return true;
}
bool NativeFloorHeads::seed(const std::vector<Ref>& whole,std::string& error){
    if(seeded_)return fail(error,"native floor-head index cannot be reseeded during mutation");
    // Native House geometry uses one square dimension for both signed-byte axes.
    if(width_<3||width_!=height_||width_>256)return fail(error,"native floor-head geometry is outside the proven House profile");
    std::map<Ref,View> rows;if(!collect(rows,error))return false;
    std::set<Ref> requested(whole.begin(),whole.end());
    if(requested.size()!=whole.size()||requested.size()!=rows.size())return fail(error,"native floor-head seed needs the entire unique live cohort");
    for(const auto& [ref,row]:rows)if(!requested.count(ref))return fail(error,"native floor-head seed omitted/replaced a live body");
    std::map<Tile,Entry> heads;std::set<Tile> unknown;bool knowledge=true;
    for(const auto& [ref,row]:rows){
        if(!row.pose||!row.slot||!row.parent||!row.child){knowledge=false;continue;}
        const auto at=tile(*row.pose);
        if(row.child->id){const auto child=rows.find(*row.child);
            if(child==rows.end()||child->second.slot!=0||child->second.parent!=ref.id||child->second.physical)
                return fail(error,"native floor-head zero child lacks exact reciprocal storage");
        }else if(row.child->generation)return fail(error,"native empty zero-slot generation is not canonical");
        if(row.physical){const auto* e=world_->find(ref.id);const auto* p=world_->find(row.physical->id);
            if(!e||!e->attachment||!p||p->generation!=row.physical->generation||*row.parent!=row.physical->id||
               *row.slot!=std::int32_t(e->attachment->slot)+1||e->attachment->slot>=p->slots.size()||p->slots[e->attachment->slot]!=ref.id)
                return fail(error,"native floor-head physical parent is not reciprocal");
            continue;
        }
        if(*row.parent){
            const auto* p=world_->find(*row.parent);const auto parent=p?rows.find({p->id,p->generation}):rows.end();
            if(*row.slot!=0||parent==rows.end()||parent->second.child!=std::optional<Ref>{ref})
                return fail(error,"native floor-head raw zero parent is not reciprocal");
            std::set<Ref> seen{ref};auto current=parent;
            while(current!=rows.end()&&current->second.parent&&*current->second.parent&&current->second.slot==0){
                if(!seen.insert(current->first).second)return fail(error,"native floor-head zero chain cycles");
                const auto* next=world_->find(*current->second.parent);if(!next)return fail(error,"native floor-head ancestor unavailable");
                current=rows.find({next->id,next->generation});
            }
            continue;
        }
        if(!at)continue; // Proven native sentinel/boundary location has no floor head.
        if(*row.slot!=-1){unknown.insert(*at);continue;}
        const auto inserted=heads.emplace(*at,Entry{ref,{}});
        if(!inserted.second)unknown.insert(*at); // Never choose by ID/order.
    }
    cohort_=std::move(requested);heads_=std::move(heads);unknown_=std::move(unknown);knowledge_=knowledge;seeded_=true;
    error.clear();return true;
}
bool NativeFloorHeads::refresh(std::map<Ref,View>& rows,std::string& error){
    if(!seeded_||!knowledge_)return fail(error,"native floor-head index has unknown source state");
    if(!collect(rows,error))return false;
    if(rows.size()!=cohort_.size())return fail(error,"native floor-head live cohort changed without its constructor/removal owner");
    for(const auto& [ref,row]:rows){
        if(!cohort_.count(ref)||!row.pose||!row.slot||!row.parent||!row.child)
            return fail(error,"native floor-head current body/knowledge differs from seeded cohort");
        if(row.child->id){const auto child=rows.find(*row.child);
            if(child==rows.end()||child->second.slot!=0||child->second.parent!=ref.id||child->second.physical)
                return fail(error,"native floor-head current child lost reciprocity");
        }
        if(row.physical){const auto* e=world_->find(ref.id);const auto* p=world_->resolve_reference(*row.physical);
            if(!e||!e->attachment||!p||p->generation!=row.physical->generation||*row.parent!=row.physical->id||
               *row.slot!=std::int32_t(e->attachment->slot)+1||e->attachment->slot>=p->slots.size()||p->slots[e->attachment->slot]!=ref.id||
               (!world_->is_live_reference(*row.physical)&&!retained_physical_parent(*row.physical,ref,e->attachment->slot)))
                return fail(error,"native floor-head current physical parent lost reciprocity");
        }else if(*row.parent){const auto* p=world_->find(*row.parent);const auto owner=p?rows.find({p->id,p->generation}):rows.end();
            if(*row.slot!=0||owner==rows.end()||owner->second.child!=std::optional<Ref>{ref})
                return fail(error,"native floor-head current raw parent lost reciprocal storage");
        }
        const auto at=tile(*row.pose);
        if(row.physical||*row.parent||!at)continue;
        const auto entry=heads_.find(*at);
        if(*row.slot==-1&&entry!=heads_.end()&&entry->second.head==ref)continue;
        bool pending=false;
        for(const auto& [where,value]:heads_)if(value.head==ref&&value.pending_pose&&
            (*row.slot==0||*row.slot==-1)&&same_xy(*row.pose,*value.pending_pose)&&
            (row.pose->level==value.pending_pose->level||row.pose->level==std::get<2>(where))){pending=true;break;}
        const auto removed=removed_pending_.find(ref);
        if(!pending&&removed!=removed_pending_.end()&&same_xy(*row.pose,removed->second)&&row.pose->level==removed->second.level)pending=true;
        if(pending_retired_child_&&pending_retired_child_->second==ref)pending=true; // Explicit active child source publication.
        if(publication_next_==std::optional<Ref>{ref})pending=true; // Explicit native publication owner, not a read-side guess.
        if(!pending)unknown_.insert(*at); // An unexplained scalar move is not an empty-cell proof.
    }
    for(auto& [where,entry]:heads_)if(entry.head.id){
        const auto found=rows.find(entry.head);
        if(found==rows.end()){unknown_.insert(where);continue;}
        const auto& row=found->second;
        if(row.physical||*row.parent||(*row.slot!=-1&&!(entry.pending_pose&&*row.slot==0))){unknown_.insert(where);continue;}
        const auto at=tile(*row.pose);
        if(at&&*at==where&&*row.slot==-1){entry.pending_pose.reset();continue;}
        if(!entry.pending_pose||!same_xy(*row.pose,*entry.pending_pose)||
           (row.pose->level!=entry.pending_pose->level&&row.pose->level!=std::get<2>(where)))unknown_.insert(where);
    }
    for(auto i=removed_pending_.begin();i!=removed_pending_.end();){const auto found=rows.find(i->first);
        if(found!=rows.end()&&(found->second.physical||*found->second.parent||!tile(*found->second.pose)))i=removed_pending_.erase(i);else ++i;}
    return true;
}
std::string NativeFloorHeads::describe_cell(const odh::EntityPosition& point)const{
    std::ostringstream out;
    out<<"query_position=["<<point.x<<','<<point.y<<','<<point.level<<','<<unsigned(point.direction)
        <<"] query_tile=["<<native_tile(point.x)<<','<<native_tile(point.y)<<','<<point.level<<"] candidates=[";
    bool first=true;std::vector<Ref> unlocated;
    for(std::int16_t cursor=0;;){const auto next=world_->next_entity(cursor,{},true);if(!next)break;cursor=*next;
        const auto* body=world_->find(cursor);if(!body)continue;const Ref ref{body->id,body->generation};
        const auto pose=world_->native_position(ref);
        if(!pose){unlocated.push_back(ref);continue;}
        if(native_tile(pose->x)!=native_tile(point.x)||native_tile(pose->y)!=native_tile(point.y)||pose->level!=point.level)continue;
        if(!first)out<<';';first=false;
        out<<"{ref="<<ref.id<<':'<<ref.generation<<" position=["<<pose->x<<','<<pose->y<<','<<pose->level<<','<<unsigned(pose->direction)
            <<"] kind="<<(body->kind==odh::EntityKind::avatar?"avatar":"object")<<" ui_ghost="<<(body->ui_ghost?"true":"false")<<" dead="<<(body->dead?"true":"false");
        if(body->definition)out<<" guid="<<body->definition->guid;
        for(const auto word:{14,26,8}){out<<" raw"<<word<<'=';const auto found=body->object_words.find(std::int16_t(word));if(found==body->object_words.end())out<<"unknown";else out<<found->second;}
        out<<" native0=";if(body->native_slot0)out<<body->native_slot0->id<<':'<<body->native_slot0->generation;else out<<"unknown";
        out<<" physical_parent=";const auto parent=world_->attachment_parent(ref);if(parent)out<<parent->id<<':'<<parent->generation;else out<<"none";
        if(body->attachment)out<<" physical_slot="<<body->attachment->slot<<" positioned="<<(body->attachment->positioned?"true":"false");
        out<<" native_oow_level=";if(body->native_out_of_world_level)out<<*body->native_out_of_world_level;else out<<"unknown";
        out<<'}';
    }
    out<<"] unlocated_native_scalars=[";first=true;
    for(const auto ref:unlocated){if(!first)out<<',';first=false;out<<ref.id<<':'<<ref.generation;}
    out<<']';return out.str();
}
std::optional<NativeFloorHeads::Ref> NativeFloorHeads::head(const odh::EntityPosition& point){
    std::string error;return checked_head(point,error);
}
std::optional<NativeFloorHeads::Ref> NativeFloorHeads::checked_head(const odh::EntityPosition& p,std::string& error){
    error.clear();const auto at=tile(p);
    const auto reject=[&](std::string reason)->std::optional<Ref>{error=std::move(reason)+"; "+describe_cell(p);return {};};
    if(!at)return reject("native floor query is outside the proved cell profile");
    if(current_world_mode_){
        if(!authority())return reject("current cell source authority unavailable");
        if(unknown_.count(*at)){const auto previous=unknown_reasons_.find(*at);return reject(previous==unknown_reasons_.end()?"current cell was already marked unknown":previous->second);}
        const auto published=heads_.find(*at);if(published!=heads_.end())return published->second.head;
        std::string reason;const auto current=current_cell_head(p,reason);
        if(!current){unknown_.insert(*at);if(reason.empty())reason="current cell resolver returned unknown without a diagnostic";unknown_reasons_[*at]=reason;return reject(std::move(reason));}
        return current;
    }
    std::map<Ref,View> rows;std::string reason;
    if(!refresh(rows,reason))return reject(reason.empty()?"native floor refresh unavailable":reason);
    if(unknown_.count(*at))return reject("native floor cell is marked unknown after source refresh");
    const auto found=heads_.find(*at);return found==heads_.end()?Ref{}:found->second.head;
}
bool NativeFloorHeads::publish(const odh::EntityPosition& p,Ref expected,Ref next,std::string& error){
    if(current_world_mode_)return publish_current_cell(p,expected,next,error);
    const auto at=tile(p);if(!at||(!next.id&&next.generation)||(!expected.id&&expected.generation)||next.id<0||expected.id<0)
        return fail(error,"native floor-head publication has invalid tile/reference");
    struct Publishing {std::optional<Ref>& value;std::optional<Ref> previous;~Publishing(){value=previous;}}publishing{publication_next_,publication_next_};
    publication_next_=next.id?std::optional<Ref>{next}:std::nullopt;
    const auto current=head(p);if(!current||*current!=expected)return fail(error,"native floor-head compare-and-publish mismatch/unknown");
    if(expected==next){error.clear();return true;}
    std::map<Ref,View> rows;if(!refresh(rows,error))return false;
    const auto latest=heads_.find(*at);const auto latest_head=latest==heads_.end()?Ref{}:latest->second.head;
    if(unknown_.count(*at)||latest_head!=expected)return fail(error,"native floor-head expected ownership changed during publication admission");
    if(next.id){const auto found=rows.find(next);
        if(found==rows.end()||found->second.physical||*found->second.parent||(*found->second.slot!=-1&&*found->second.slot!=0))
            return fail(error,"native floor-head next body is not a proven unlinked receiver");
        for(const auto& [where,value]:heads_)if(where!=*at&&value.head==next)return fail(error,"native floor-head receiver still owns another tile");
    }
    if(expected.id){const auto found=rows.find(expected);if(found==rows.end())return fail(error,"native floor-head old generation expired");removed_pending_[expected]=*found->second.pose;}
    auto& entry=heads_[*at];entry.head=next;entry.pending_pose=next.id?rows.at(next).pose:std::optional<odh::EntityPosition>{};
    if(next.id)removed_pending_.erase(next);
    error.clear();return true;
}
bool NativeFloorHeads::admit_out_of_world(Ref ref,std::string& error){
    if(!seeded_||!knowledge_)return fail(error,"native floor-head cohort is not ready for construction");
    std::map<Ref,View> rows;if(!collect(rows,error))return false;
    const auto found=rows.find(ref);
    if(found==rows.end()||cohort_.count(ref)||rows.size()!=cohort_.size()+1)return fail(error,"native floor-head constructor membership is not exact");
    for(const auto known:cohort_)if(!rows.count(known))return fail(error,"native floor-head constructor replaced another generation");
    const auto& row=found->second;
    if(!row.pose||row.pose->x!=-16||row.pose->y!=-16||row.physical||!row.parent||*row.parent||!row.slot||(*row.slot!=-1&&*row.slot!=0)||row.child!=std::optional<Ref>{Ref{}})
        return fail(error,"native floor-head constructor is not known unlinked/OOW/empty");
    cohort_.insert(ref);error.clear();return true;
}
bool NativeFloorHeads::forget_unlinked(Ref ref,std::string& error){
    if(!authority()||(current_world_mode_?forgotten_refs_.count(ref)!=0:!cohort_.count(ref)))return fail(error,"native floor-head removal source unavailable");
    for(const auto& [where,entry]:heads_)if(entry.head==ref)return fail(error,"native floor-head removal still owns a tile");
    if(world_->is_live_reference(ref))return fail(error,"native floor-head forget must follow actual body removal");
    cohort_.erase(ref);removed_pending_.erase(ref);if(current_world_mode_)forgotten_refs_.insert(ref);error.clear();return true;
}
bool NativeFloorHeads::retained_physical_parent(Ref parent,Ref child,std::size_t slot)const{
    const auto owner=retired_containers_.find(parent);
    return owner!=retired_containers_.end()&&world_->retired_container_active(owner->second)&&
        owner->second.parent()==parent&&world_->attachment_parent(child)==std::optional<Ref>{parent}&&
        world_->retired_slot(owner->second,slot)==std::optional<Ref>{child};
}
bool NativeFloorHeads::begin_retired_container(const odh::World::RetiredContainer& token,std::string& error){
    const auto parent=token.parent();
    if(!authority()||!world_->retired_container_active(token)||world_->is_live_reference(parent)||
       retired_containers_.count(parent)||pending_retired_child_)
        return fail(error,"native floor retained-container boundary unavailable or repeated");
    if(!forget_unlinked(parent,error))return false;
    retired_containers_.emplace(parent,token);error.clear();return true;
}
bool NativeFloorHeads::begin_retired_child_position(const odh::World::RetiredContainer& token,Ref child,std::string& error){
    const auto owner=retired_containers_.find(token.parent());
    if(!authority()||pending_retired_child_||owner==retired_containers_.end()||
       !world_->retired_container_active(token)||!world_->retired_container_active(owner->second)||
       (!current_world_mode_&&!cohort_.count(child))||!world_->retired_child_authorized(token,child)||!world_->is_live_reference(child))
        return fail(error,"native floor child publication requires its exact active retirement owner");
    pending_retired_child_=std::make_pair(token.parent(),child);error.clear();return true;
}
bool NativeFloorHeads::end_retired_child_position(const odh::World::RetiredContainer& token,Ref child,std::string& error){
    if(!authority()||!world_->retired_container_active(token)||!pending_retired_child_||
       *pending_retired_child_!=std::make_pair(token.parent(),child))
        return fail(error,"native floor child publication token/identity expired");
    const auto* body=world_->find(child.id);
    if(!body||body->generation!=child.generation||!body->object_words.count(14)||!body->object_words.count(26))
        return fail(error,"native floor child publication lost its actual body/backing");
    const auto pose=world_->native_position(child);if(!pose)return fail(error,"native floor child publication scalar unavailable");
    if(current_world_mode_){
        if(tile(*pose)&&!body->attachment&&!body->object_words.at(26)){
            if(body->object_words.at(14)!=-1)return fail(error,"native floor placed child has no source root slot");
            const auto previous=head(*pose);if(!previous)return fail(error,"native floor placed child destination head unknown");
            if(*previous!=child){
                if(previous->id)return fail(error,"native floor child placement did not publish a reciprocal occupied-cell chain");
                if(!publish(*pose,{},child,error))return false;
            }
        }
    }else{
        std::map<Ref,View> rows;if(!refresh(rows,error))return false;
        const auto current=rows.find(child);
        if(current==rows.end()||!(current->second.pose==pose))return fail(error,"native floor child scalar changed during publication admission");
        if(tile(*pose)&&!current->second.physical&&current->second.parent==0){
            if(current->second.slot!=-1)return fail(error,"native floor placed child has no source root slot");
            const auto previous=head(*pose);if(!previous)return fail(error,"native floor placed child destination head unknown");
            if(*previous!=child){
                if(previous->id)return fail(error,"native floor child placement did not publish a reciprocal occupied-cell chain");
                if(!publish(*pose,{},child,error))return false;
            }
        }
    }
    pending_retired_child_.reset();
    if(current_world_mode_){if(!settled_current_cells(error))return false;}
    else {std::map<Ref,View> rows;if(!refresh(rows,error))return false;}
    error.clear();return true;
}
bool NativeFloorHeads::end_retired_container(const odh::World::RetiredContainer& token,std::string& error){
    const auto parent=token.parent();const auto owner=retired_containers_.find(parent);
    if(!authority()||owner==retired_containers_.end()||!world_->retired_container_active(token)||pending_retired_child_)
        return fail(error,"native floor retained-container completion unavailable");
    const auto count=world_->retired_slot_count(token);if(!count)return fail(error,"native floor retained slots unavailable");
    for(std::size_t slot=0;slot<*count;++slot){const auto child=world_->retired_slot(token,slot);if(!child||child->id||child->generation)return fail(error,"native floor retained container still holds children");}
    if(!settled(error))return false;
    retired_containers_.erase(owner);error.clear();return true;
}
void NativeFloorHeads::abandon_retired_container(const odh::World::RetiredContainer& token){
    if(pending_retired_child_&&pending_retired_child_->first==token.parent())pending_retired_child_.reset();
    retired_containers_.erase(token.parent());
}
bool NativeFloorHeads::settled(std::string& error){
    if(current_world_mode_)return settled_current_cells(error);
    std::map<Ref,View> rows;if(!refresh(rows,error))return false;
    if(pending_retired_child_||!unknown_.empty()||!removed_pending_.empty()||std::any_of(heads_.begin(),heads_.end(),[](const auto& row){return row.second.pending_pose.has_value();}))
        return fail(error,"native floor-head index still has ambiguous/pending native publications");
    error.clear();return true;
}
} // namespace odh_host
