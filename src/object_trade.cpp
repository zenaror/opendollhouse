// SPDX-License-Identifier: MPL-2.0
#include "odh/object_trade.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <functional>

namespace odh {
namespace {
ObjectTradeDecision unknown(ObjectTradeReason why,std::int16_t id=0){return {ObjectTradeStatus::unresolved,why,id};}
ObjectTradeDecision reject(ObjectTradeReason why,std::int16_t id=0){return {ObjectTradeStatus::rejected,why,id};}
ObjectTradeDecision yes(){return {ObjectTradeStatus::allowed,ObjectTradeReason::none,0};}
std::int32_t signed_bits(std::uint32_t n){
    return n<=INT32_MAX?static_cast<std::int32_t>(n):-1-static_cast<std::int32_t>(UINT32_MAX-n);
}
std::int32_t multiply(std::int32_t a,std::int32_t b){return signed_bits(static_cast<std::uint32_t>(a)*static_cast<std::uint32_t>(b));}
std::int32_t add(std::int32_t a,std::int32_t b){return signed_bits(static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b));}
std::int32_t negate(std::int32_t n){return signed_bits(0U-static_cast<std::uint32_t>(n));}
}
ObjectTradeValue ts1_object_purchase_value(std::uint32_t guid,const ObjectTradeCatalogItem& item){
    if(item.state==ObjectTradeCatalogState::unknown)return {unknown(ObjectTradeReason::unknown_catalog),{}};
    if(item.guid!=guid)return {unknown(ObjectTradeReason::invalid_catalog),{}};
    if(item.state==ObjectTradeCatalogState::missing)return {yes(),0};
    if(item.state!=ObjectTradeCatalogState::present)return {unknown(ObjectTradeReason::invalid_catalog),{}};
    // C# uint->int cast and unchecked int product, then signed truncation.
    return {yes(),multiply(signed_bits(item.price),100)/100};
}
ObjectTradeValue ts1_object_refund_value(std::optional<std::int32_t> initial,bool known,std::optional<std::uint16_t> wear){
    if(!initial||!known)return {unknown(ObjectTradeReason::unknown_price),{}};
    const auto product=multiply(*initial,400-static_cast<std::int32_t>(wear.value_or(80)));
    return {yes(),std::max<std::int32_t>(0,std::min(*initial,product/400))};
}
ObjectTradeBalance ts1_object_trade_balance(const ObjectTradeFunds& funds,std::int32_t amount,bool refund){
    if(!funds.known)return {unknown(ObjectTradeReason::unknown_funds),{}};
    if(!funds.family_budget)return {yes(),{}};
    const auto next=add(*funds.family_budget,refund?amount:negate(amount));
    if(next<0)return {reject(ObjectTradeReason::insufficient_funds),{}};
    return {yes(),next};
}
ObjectTradeDecision ts1_object_trade_permission(const ObjectTradePermissionInput& in){
    const auto* target=in.world.find(in.target.id);
    if(!target||!in.target.generation||target->generation!=in.target.generation)
        return unknown(ObjectTradeReason::invalid_identity,in.target.id);
    if(in.move_objects)return yes(); // Source TS1 cheat bypasses group/type/Disabled checks.
    if(in.groups.size()>4096)return unknown(ObjectTradeReason::topology_budget);
    std::map<std::int16_t,std::size_t> group_for;
    std::size_t members=0;
    for(std::size_t n=0;n<in.groups.size();++n){
        const auto& group=in.groups[n];
        if(!group.complete||group.members.empty())return unknown(ObjectTradeReason::incomplete_group);
        for(const auto identity:group.members){
            if(++members>4096)return unknown(ObjectTradeReason::topology_budget);
            const auto* entity=in.world.find(identity.id);
            if(!entity||!identity.generation||entity->generation!=identity.generation)
                return unknown(ObjectTradeReason::invalid_identity,identity.id);
            if(!group_for.emplace(identity.id,n).second)return unknown(ObjectTradeReason::invalid_topology,identity.id);
        }
    }
    const auto word=[](const Entity& e,std::int16_t index)->std::optional<std::uint16_t>{
        const auto i=e.object_words.find(index);return i==e.object_words.end()?std::nullopt:
            std::optional<std::uint16_t>{static_cast<std::uint16_t>(i->second)};};
    const auto in_use=[&](const ObjectTradeGroup& group)->std::optional<bool>{
        for(const auto identity:group.members){const auto flags=word(*in.world.find(identity.id),8);
            if(!flags)return {};if(*flags&32U)return true;}
        if(!in.avatar_threads_complete||in.avatar_threads.size()>4096)return {};
        std::set<std::int16_t> supplied;
        for(const auto& thread:in.avatar_threads){
            const auto* actor=in.world.find(thread.avatar.id);
            if(!actor||actor->kind!=EntityKind::avatar||actor->generation!=thread.avatar.generation||
               !thread.thread||!supplied.insert(actor->id).second)return {};
        }
        for(std::int16_t cursor=0;;){const auto next=in.world.next_entity(cursor);if(!next)break;cursor=*next;
            const auto* e=in.world.find(cursor);if(e->kind==EntityKind::avatar&&!supplied.count(cursor))return {};}
        for(const auto& thread:in.avatar_threads)for(const auto& frame:thread.thread->stack())
            for(const auto identity:group.members)if(frame.context.callee==identity.id)return true;
        return false; // StackObject and merely queued actions are not IsInUse(false).
    };
    const auto outside=[&](const Entity& entity)->std::optional<bool>{
        if(!in.buildable)return {};
        const auto& a=*in.buildable;
        if(!a.width||!a.height||a.width>4096||a.height>4096||a.floors<1)return {};
        // C# integer division truncates toward zero, including OUT_OF_WORLD.
        const auto x=entity.position?entity.position->x/16:-32768/16;
        const auto y=entity.position?entity.position->y/16:-32768/16;
        const auto level=entity.position?entity.position->level:1;
        if(a.fine){
            if(a.fine->size()!=static_cast<std::size_t>(a.width)*a.height)return {};
            if(x<0||y<0||x>=static_cast<std::int64_t>(a.width)||y>=static_cast<std::int64_t>(a.height)||level<1||level>a.floors)return false;
            return !(*a.fine)[static_cast<std::size_t>(x)+static_cast<std::size_t>(y)*a.width];
        }
        if(a.right<a.x||a.bottom<a.y)return {};
        return x<a.x||y<a.y||x>=a.right||y>=a.bottom||level<1||level>a.floors;
    };
    std::set<std::size_t> visiting;
    std::function<ObjectTradeDecision(std::int16_t)> visit;
    visit=[&](std::int16_t id)->ObjectTradeDecision{
        const auto found=group_for.find(id);if(found==group_for.end())return unknown(ObjectTradeReason::incomplete_group,id);
        if(visiting.size()>=256)return unknown(ObjectTradeReason::topology_budget,id);
        if(!visiting.insert(found->second).second)return unknown(ObjectTradeReason::invalid_topology,id);
        struct Erase {std::set<std::size_t>& set;std::size_t key;~Erase(){set.erase(key);}} erase{visiting,found->second};
        const auto& group=in.groups[found->second];
        for(const auto identity:group.members){
            const auto& entity=*in.world.find(identity.id);
            if(entity.kind==EntityKind::avatar)return reject(ObjectTradeReason::avatar,entity.id);
            const auto movement=word(entity,43);if(!movement)return unknown(ObjectTradeReason::unknown_flags,entity.id);
            if(!(*movement&2U))return reject(ObjectTradeReason::cannot_move,entity.id);
            if(in.deleting&&!(*movement&8U))return reject(ObjectTradeReason::cannot_delete,entity.id);
            const auto oob=outside(entity);if(!oob)return unknown(ObjectTradeReason::unknown_buildable_area,entity.id);
            if(*oob)return reject(ObjectTradeReason::out_of_bounds,entity.id);
            const auto used=in_use(group);if(!used)return unknown(ObjectTradeReason::unknown_threads,entity.id);
            if(*used)return reject(ObjectTradeReason::in_use,entity.id);
            for(const auto child:entity.slots)if(child){
                const auto* contained=in.world.find(child);if(!contained)return unknown(ObjectTradeReason::invalid_topology,child);
                if(in.deleting||contained->kind==EntityKind::avatar)return reject(ObjectTradeReason::contained_item,entity.id);
                const auto result=visit(child);
                if(result.status==ObjectTradeStatus::unresolved)return result;
                if(result.status!=ObjectTradeStatus::allowed)return reject(ObjectTradeReason::contained_item,entity.id);
            }
        }
        return yes();
    };
    auto result=visit(in.target.id);if(result.status!=ObjectTradeStatus::allowed)return result;
    if(in.deleting&&in.verify_delete_transaction){
        if(!in.selected_disabled)return unknown(ObjectTradeReason::unknown_transaction_state,in.target.id);
        if(*in.selected_disabled&1U)return reject(ObjectTradeReason::transaction_incomplete,in.target.id);
    }
    return result;
}
} // namespace odh
