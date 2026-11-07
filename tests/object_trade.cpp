// SPDX-License-Identifier: MPL-2.0
#include "odh/object_trade.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace odh;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void reason(ObjectTradeDecision value,ObjectTradeStatus status,ObjectTradeReason why){
    check(value.status==status&&value.reason==why,"trade decision/status reason");
}
void word(World& world,std::int16_t id,std::int16_t index,std::int16_t value){
    check(world.declare_object_word(id,index,value),"fixture object word");
}
ObjectTradeIdentity identity(const World& world,std::int16_t id){return {id,world.find(id)->generation};}
void value_rules(){
    using R=ObjectTradeReason;using S=ObjectTradeStatus;
    reason(ts1_object_purchase_value(42,{}).decision,S::unresolved,R::unknown_catalog);
    const auto missing=ts1_object_purchase_value(42,{ObjectTradeCatalogState::missing,42,0});
    check(missing.amount==0&&missing.decision.status==S::allowed,"known missing catalog entry source zero price");
    const auto item=ObjectTradeCatalogItem{ObjectTradeCatalogState::present,42,101};
    check(ts1_object_purchase_value(42,item).amount==101,"TS1 full catalog price, no TSO discount");
    reason(ts1_object_purchase_value(43,item).decision,S::unresolved,R::invalid_catalog);
    check(ts1_object_purchase_value(42,{ObjectTradeCatalogState::present,42,UINT32_MAX}).amount==-1,"source uint cast preserves signed price bits");
    check(ts1_object_purchase_value(42,{ObjectTradeCatalogState::present,42,INT32_MAX}).amount==-1,"source catalog multiplication wraps before division");
    for(const auto wear:{0,1,80,399,400,401,65535}){
        const auto expected=std::max(0,std::min(101,101*(400-wear)/400));
        check(ts1_object_refund_value(101,true,static_cast<std::uint16_t>(wear)).amount==expected,
            "refund uses source Wear formula, integer truncation and both clamps");
    }
    check(ts1_object_refund_value(101,true,{}).amount==80,"known absent ObjectState fallback80");
    check(ts1_object_refund_value(-1,true,0).amount==0,"negative InitialPrice clamps refund to zero");
    reason(ts1_object_refund_value(101,false,{}).decision,S::unresolved,R::unknown_price);
    reason(ts1_object_refund_value({},true,0).decision,S::unresolved,R::unknown_price);
    check(ts1_object_refund_value(INT32_MAX,true,0).amount==0,"overflowed refund product follows source clamp");
    reason(ts1_object_trade_balance({},1,false).decision,S::unresolved,R::unknown_funds);
    const ObjectTradeFunds funds{true,101};
    check(ts1_object_trade_balance(funds,101,false).budget_after==0,"exact affordable debit");
    reason(ts1_object_trade_balance(funds,102,false).decision,S::rejected,R::insufficient_funds);
    check(ts1_object_trade_balance(funds,80,true).budget_after==181&&funds.family_budget==101,"refund quote does not mutate funds");
    const auto no_family=ts1_object_trade_balance({true,{}},101,false);
    check(no_family.decision.status==S::allowed&&!no_family.budget_after,"known missing CurrentFamily budget is source no-op");
    reason(ts1_object_trade_balance({true,INT32_MAX},1,true).decision,S::rejected,R::insufficient_funds);
    check(ts1_object_trade_balance(funds,-1,false).budget_after==102,"negative source quote debits negative amount exactly");
    check(ts1_object_trade_balance({true,INT32_MIN},INT32_MIN,false).budget_after==0,"source min-int negation and addition use defined bit wrapping");
}
void permission_rules(){
    using R=ObjectTradeReason;using S=ObjectTradeStatus;
    World world;
    check(world.create(1,EntityKind::object,{0,0,1})&&world.create(2,EntityKind::object)&&
        world.create(3,EntityKind::object)&&world.create(10,EntityKind::avatar),"fixture entities");
    for(const auto id:{1,2,3,10}){
        word(world,id,8,0);word(world,id,43,10);check(world.set_position(id,{32,32,1,0}),"fixture position");
    }
    const Bhav tree{0x8002,{{99,254,255,{}}},{},Bhav::Metadata{0,4,0,0}};
    Vm avatar({{routine_key(4096),tree}});
    std::vector<ObjectTradeAvatarThread> threads{{identity(world,10),&avatar}};
    std::vector<ObjectTradeGroup> groups{{true,{identity(world,1),identity(world,2)}},{true,{identity(world,3)}},{true,{identity(world,10)}}};
    ObjectTradePermissionInput input{world,identity(world,1),groups,threads};
    input.avatar_threads_complete=true;
    input.buildable=ObjectTradeBuildableArea{8,8,2,1,1,7,7,{}};
    input.selected_disabled=0;
    const auto decide=[&]{
        const auto prior=world.snapshot();const auto serial=world.room_registration_revision();
        const auto result=ts1_object_trade_permission(input);const auto after=world.snapshot();
        check(prior.word_count==after.word_count&&prior.generation_sequence==after.generation_sequence&&
            prior.spatial_revision==after.spatial_revision&&prior.entities.size()==after.entities.size()&&
            serial==world.room_registration_revision(),"trade decision preserves World accounting and spatial history");
        for(std::size_t n=0;n<prior.entities.size();++n){const auto& x=prior.entities[n];const auto& y=after.entities[n];
            check(x.id==y.id&&x.generation==y.generation&&x.object_words==y.object_words&&x.slots==y.slots&&
                x.position==y.position&&x.attributes==y.attributes&&x.person_data==y.person_data,
                "trade decision preserves actual positions, flags, containment and banks");}
        return result;
    };
    reason(decide(),S::allowed,R::none);
    input.deleting=true;reason(decide(),S::allowed,R::none);
    input.selected_disabled={};reason(decide(),S::unresolved,R::unknown_transaction_state);
    input.selected_disabled=1;reason(decide(),S::rejected,R::transaction_incomplete);
    input.verify_delete_transaction=false;reason(decide(),S::allowed,R::none);
    input.selected_disabled={};reason(decide(),S::allowed,R::none);
    input.verify_delete_transaction=true;reason(decide(),S::unresolved,R::unknown_transaction_state);
    input.selected_disabled=2;reason(decide(),S::allowed,R::none); // ForSale is not a TS1 ownership ban.
    word(world,2,43,2);reason(decide(),S::rejected,R::cannot_delete);
    input.deleting=false;reason(decide(),S::allowed,R::none);
    word(world,2,43,8);reason(decide(),S::rejected,R::cannot_move);word(world,2,43,10);
    word(world,2,8,32);reason(decide(),S::rejected,R::in_use);word(world,2,8,0);
    check(avatar.start(routine_key(4096),{10,2,10,0})==VmStatus::running,"actual avatar stack");
    reason(decide(),S::rejected,R::in_use);
    check(avatar.start(routine_key(4096),{10,10,2,0})==VmStatus::running,"actual unrelated callee");
    reason(decide(),S::allowed,R::none); // StackObject alone does not block.
    input.avatar_threads_complete=false;reason(decide(),S::unresolved,R::unknown_threads);
    input.avatar_threads_complete=true;
    threads.clear();reason(decide(),S::unresolved,R::unknown_threads);
    threads.push_back({identity(world,10),&avatar});
    input.buildable={};reason(decide(),S::unresolved,R::unknown_buildable_area);
    input.buildable=ObjectTradeBuildableArea{8,8,2,3,3,7,7,{}};
    reason(decide(),S::rejected,R::out_of_bounds);
    input.buildable->fine=std::vector<bool>(64,true);
    reason(decide(),S::allowed,R::none); // Fine map supersedes rectangle.
    (*input.buildable->fine)[18]=false;reason(decide(),S::rejected,R::out_of_bounds);
    (*input.buildable->fine)[18]=true;
    check(world.set_position(2,{160,32,1,0}),"outside architectural fine map");
    reason(decide(),S::allowed,R::none); // Source FineBuildableArea outside => false.
    check(world.set_position(2,{32,32,1,0}),"restore fixture member");
    input.buildable->fine->pop_back();reason(decide(),S::unresolved,R::unknown_buildable_area);
    input.buildable=ObjectTradeBuildableArea{8,8,2,1,1,7,7,{}};
    check(world.attach(1,0,3),"fixture contained object");
    reason(decide(),S::allowed,R::none);
    word(world,3,43,0);reason(decide(),S::rejected,R::contained_item);word(world,3,43,10);
    input.deleting=true;reason(decide(),S::rejected,R::contained_item);
    check(world.detach(1,0),"remove contained fixture");input.deleting=false;
    check(world.attach(1,0,10),"fixture carried avatar");reason(decide(),S::rejected,R::contained_item);
    check(world.detach(1,0),"remove avatar slot");
    groups[0].complete=false;reason(decide(),S::unresolved,R::incomplete_group);groups[0].complete=true;
    groups.push_back(groups[0]);reason(decide(),S::unresolved,R::invalid_topology);groups.pop_back();
    ++input.target.generation;reason(decide(),S::unresolved,R::invalid_identity);--input.target.generation;
    groups[0].members[0].generation++;reason(decide(),S::unresolved,R::invalid_identity);groups[0].members[0].generation--;
    check(world.create(4,EntityKind::object),"incomplete source facts fixture");
    groups.push_back({true,{identity(world,4)}});input.target=identity(world,4);
    reason(decide(),S::unresolved,R::unknown_flags);
    groups.pop_back();input.target=identity(world,1);
    input.target=identity(world,10);reason(decide(),S::rejected,R::avatar);
    input.move_objects=true;input.deleting=true;input.selected_disabled=1;input.buildable={};
    reason(decide(),S::allowed,R::none); // Cheat bypass includes avatars and in-flight disable.
    input.target={99,1};reason(decide(),S::unresolved,R::invalid_identity);
    check(world.find(1)&&world.find(2)&&world.find(3)&&world.find(4)&&world.find(10),
        "all original and explicitly constructed fixture entities remain");
}
}
int main(){try{value_rules();permission_rules();std::cout<<"TS1 object trade rules passed (no trade integration)\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
