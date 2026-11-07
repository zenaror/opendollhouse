// SPDX-License-Identifier: MPL-2.0
// Primary static evidence: reports/ghidra/native-seating-20261107/{004336c1,
// 0043f3a4,0043f3b9,004325af,004328d2}.{c,asm}.txt and constants.txt;
// direct iterator ancestry327ad is in native-light-cache-20261107/.
#include "native_seating.hpp"
#include "odh/avatar_containment.hpp"
#include <array>
#include <exception>
#include <set>
#include <vector>

namespace odh_host {
namespace {
using Ref=odh::WorldEntityReference;
constexpr std::array<std::int32_t,8> dx{-1,-1,0,1,1,1,0,-1};
constexpr std::array<std::int32_t,8> dy{0,1,1,1,0,-1,-1,-1};
constexpr std::size_t budget=4096;
std::int32_t byte(std::int64_t value){const auto low=static_cast<std::uint64_t>(value)&255U;return low>=128?std::int32_t(low)-256:std::int32_t(low);}
std::int32_t tile(std::int32_t value){return byte(value>=0?std::int64_t(value)/16:-((15-std::int64_t(value))/16));}
NativeSeatingTile normalized(NativeSeatingTile value){return {byte(value.x),byte(value.y),static_cast<std::int16_t>(byte(value.level))};}
bool fail(std::string& why,const char* text){why=text;return false;}
bool current(const NativeSeatingContext& c){return c.world&&c.source_still_matches&&c.source_still_matches();}
bool inside(const NativeSeatingContext& c,NativeSeatingTile at){return at.x>=1&&at.y>=1&&at.x<std::int64_t(c.width)-1&&at.y<std::int64_t(c.height)-1&&at.level>=1&&at.level<=2;}
const odh::Entity* body(const NativeSeatingContext& c,Ref ref){
    if(!current(c)||ref.id<=0||!ref.generation)return nullptr;
    const auto* e=c.world->resolve_reference(ref);return e&&!e->ui_ghost?e:nullptr;
}
std::optional<std::int16_t> word(const odh::Entity& e,std::int16_t n){const auto p=e.object_words.find(n);return p==e.object_words.end()?std::nullopt:std::optional<std::int16_t>{p->second};}
std::optional<Ref> head(const NativeSeatingContext& c,NativeSeatingTile at,std::string& why){
    if(!current(c)||!c.floor_head){fail(why,"native seating actual floor-head owner unavailable");return {};}
    const auto found=c.floor_head({at.x*16+8,at.y*16+8,at.level,0});
    if(!current(c)||!found||found->id<0||(!found->id&&found->generation)||
       (found->id&&(!c.world->is_live_reference(*found)||!body(c,*found)||!c.world->is_live_reference(*found)))){
        fail(why,"native seating floor head is unknown or expired");return {};
    }return found;
}
// Raw native vector slots: zero is dedicated native storage; positive slots
// are the same physical slots represented by World at nativeIndex-1.
bool child(const NativeSeatingContext& c,Ref owner,std::size_t raw,std::optional<Ref>& result,std::string& why){
    const auto* e=body(c,owner);if(!e)return fail(why,"native seating child-vector owner expired");
    result.reset();
    if(raw==0){const auto stored=c.world->native_slot0(owner);if(!stored)return fail(why,"native seating slot-zero backing is unknown");
        if(!stored->id)return !stored->generation||fail(why,"native empty slot-zero generation invalid");
        const auto* value=body(c,*stored);if(!value||value->attachment||word(*value,14)!=0||word(*value,26)!=owner.id||c.world->native_slot0(owner)!=stored)
            return fail(why,"native seating slot-zero reciprocity unavailable");
        result=*stored;return true;
    }
    const auto stored=odh::containment_slot_value(*e,raw-1);if(!stored)return fail(why,"native physical vector slot unavailable");
    if(!*stored)return true;
    const auto* value=c.world->find(*stored);
    if(!value||value->ui_ghost||!value->attachment||value->attachment->slot!=raw-1||
       word(*value,14)!=static_cast<std::int16_t>(raw)||word(*value,26)!=owner.id||
       c.world->attachment_parent({value->id,value->generation})!=std::optional<Ref>{owner})
        return fail(why,"native seating physical child reciprocity unavailable");
    result=Ref{value->id,value->generation};return true;
}
bool parent(const NativeSeatingContext& c,Ref value,Ref& owner,std::size_t& raw,std::string& why){
    const auto* e=body(c,value);if(!e)return fail(why,"native seating iterator current identity expired");
    const auto slot=word(*e,14),id=word(*e,26);
    if(!slot||!id||*slot<0||*id<=0)return fail(why,"native seating iterator lost current parent before its root");
    const auto* p=c.world->find(*id);if(!p)return fail(why,"native seating iterator parent is missing");
    owner={p->id,p->generation};raw=static_cast<std::size_t>(*slot);std::optional<Ref> actual;
    return child(c,owner,raw,actual,why)&&actual==std::optional<Ref>{value}||fail(why,"native seating iterator parent no longer owns current exact body");
}
bool next(const NativeSeatingContext& c,Ref root,Ref value,std::optional<Ref>& result,std::string& why){
    std::size_t start=0;std::set<Ref> ascending;
    for(;;){
        const auto* e=body(c,value);if(!e)return fail(why,"native seating iterator current body unavailable");
        const auto count=odh::logical_containment_slots(*e)+1;
        if(count>32768)return fail(why,"native seating native child-vector exceeds source word profile");
        for(auto raw=start;raw<count;++raw){if(!child(c,value,raw,result,why))return false;if(result)return true;}
        if(value==root){result.reset();return true;}
        if(!ascending.insert(value).second||ascending.size()>budget)return fail(why,"native seating iterator parent cycle");
        Ref owner;std::size_t raw=0;if(!parent(c,value,owner,raw,why))return false;
        value=owner;start=raw+1;
    }
}
struct Plan {Ref root{};bool matching=false;};
bool lookup(const NativeSeatingContext& c,NativeSeatingTile at,Plan& plan,std::string& why){
    if(!current(c)||c.width<3||c.height<3||c.width!=c.height)return fail(why,"native seating source geometry/authority unavailable");
    if(!inside(c,at))return true; // Native signed-byte guard /325af levels.
    const auto center=head(c,at,why);if(!center)return false;
    if(!center->id)return true;plan.root=*center;
    for(unsigned direction=0;direction<8;direction+=2){
        const auto point=normalized({at.x+dx[direction],at.y+dy[direction],at.level});if(!inside(c,point))continue;
        auto current_head=head(c,point,why);if(!current_head)return false;
        std::set<Ref> seen;
        while(current_head->id){
            if(!seen.insert(*current_head).second||seen.size()>budget)return fail(why,"native seating hidden-neighbor chain cycles");
            const auto* e=body(c,*current_head);const auto flags=e?word(*e,8):std::nullopt;
            if(!flags)return fail(why,"native seating neighbor Flags8 unavailable");
            if(!(std::uint16_t(*flags)&4U))break;
            std::optional<Ref> below;if(!child(c,*current_head,0,below,why))return false;
            current_head=below?below:std::optional<Ref>{Ref{}};
        }
        if(!current_head->id)continue;
        const auto* e=body(c,*current_head);if(!e||!e->entry_points)return fail(why,"native seating neighbor function table unavailable");
        const auto action26=e->entry_points->size()>26?e->entry_points->at(26).action:0;
        if(!action26)continue;
        const auto facing=word(*e,1);if(!facing)return fail(why,"native seating neighbor direction unavailable");
        if(std::uint16_t(*facing)==((direction-4U)&7U)){plan.matching=true;break;}
    }return true;
}
bool admit_entry(const NativeSeatingContext& c,Ref ref,std::int16_t before,std::uint16_t ep25,std::string& why){
    if(!c.preflight_entry||!c.native_entry)return fail(why,"native seating changed state requires actual EP9 owner");
    if(!current(c))return fail(why,"native seating source expired before entry admission");const auto revision=c.world->spatial_revision();
    if(!c.preflight_entry(ref,9,why))return false;
    const auto* e=body(c,ref);
    return e&&c.world->spatial_revision()==revision&&word(*e,8)==before&&e->entry_points&&e->entry_points->size()>25&&e->entry_points->at(25).action==ep25||
        fail(why,"native seating read-only EP9 preflight changed World or receiver");
}
bool inspect(const NativeSeatingContext& c,Ref ref,bool matching,bool apply,NativeSeatingResult& result,std::string& why){
    const auto* e=body(c,ref);const auto before=e?word(*e,8):std::nullopt;
    if(!before)return fail(why,"native seating current Flags8 unavailable");
    const bool changed=bool(std::uint16_t(*before)&0x100U)!=matching;
    std::uint16_t action25=0;
    if(changed){
        if(!e->entry_points)return fail(why,"native seating changed receiver EP25 table unavailable");
        action25=e->entry_points->size()>25?e->entry_points->at(25).action:0;
        if(action25&&!admit_entry(c,ref,*before,action25,why))return false;
    }
    if(!apply)return true;
    e=body(c,ref);if(!e||word(*e,8)!=before)return fail(why,"native seating flags changed before publication");
    const auto after=static_cast<std::int16_t>(matching?std::uint16_t(*before)|0x100U:std::uint16_t(*before)&0xfeffU);
    odh::VmFrame frame;frame.context.caller=ref.id;
    if(!c.world->is_live_reference(ref)||!c.world->bind_context_live(frame.context)||!c.world->write(frame,3,8,after))
        return fail(why,"native seating actual Flags8 publication unavailable");
    ++result.visited;if(changed){++result.changed;result.effects_started=true;}
    if(changed&&action25){
        ++result.entry_calls;
        if(!c.native_entry(ref,9,why)){if(why.empty())fail(why,"native seating EP9 did not complete");return false;}
        if(!current(c)||!c.world->resolve_reference(ref))return fail(why,"native seating EP9 expired source or current exact body");
    }return true;
}
NativeSeatingResult run(const NativeSeatingContext& c,NativeSeatingTile requested,bool apply){
    NativeSeatingResult result;
    try {
    std::string& why=result.diagnostic;const auto at=normalized(requested);Plan plan;
    if(!lookup(c,at,plan,why))return result;
    if(!plan.root.id){result.completed=true;return result;}
    result.matching_neighbor=plan.matching;
    // Admit the complete current graph before writes; actual callbacks may
    // change later children, so execution also admits each newly current node.
    std::optional<Ref> cursor=plan.root;std::set<Ref> preview;
    while(cursor){if(preview.size()>=budget||!preview.insert(*cursor).second){fail(why,"native seating preview tree repeats/exceeds budget");return result;}
        if(!inspect(c,*cursor,plan.matching,false,result,why))return result;
        if(!next(c,plan.root,*cursor,cursor,why))return result;
    }
    if(!apply){result.completed=true;return result;}
    cursor=plan.root;std::set<Ref> visited;
    while(cursor){if(visited.size()>=budget||!visited.insert(*cursor).second){fail(why,"native seating live tree repeats/exceeds budget");return result;}
        const auto ref=*cursor;if(!inspect(c,ref,plan.matching,true,result,why))return result;
        if(!next(c,plan.root,ref,cursor,why))return result;
    }
    result.completed=true;return result;
    }catch(const std::exception& error){result.diagnostic=std::string("native seating owner exception: ")+error.what();return result;}
    catch(...){result.diagnostic="native seating owner exception";return result;}
}
}
bool preflight_native_seating(const NativeSeatingContext& c,NativeSeatingTile at,std::string& why){const auto result=run(c,at,false);why=result.diagnostic;return result.completed;}
NativeSeatingResult update_native_seating(const NativeSeatingContext& c,NativeSeatingTile at){return run(c,at,true);}
NativeSeatingTile native_seating_center_tile(const odh::EntityPosition& pose){return {tile(pose.x),tile(pose.y),static_cast<std::int16_t>(byte(pose.level))};}
std::optional<NativeSeatingTile> native_seating_front_tile(const odh::EntityPosition& pose,std::int16_t direction,std::string& why){
    if(direction<0||direction>7){fail(why,"native seating captured direction is outside source profile");return {};}
    const auto center=native_seating_center_tile(pose);why.clear();return normalized({center.x+dx[direction],center.y+dy[direction],center.level});
}
}
