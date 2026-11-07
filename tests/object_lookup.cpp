// SPDX-License-Identifier: MPL-2.0
#include "odh/object_lookup.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace odh;
void need(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
struct Fixture {
    World world;bool authority=true;unsigned table_reads=0;
    Ttab table{};ObjectLookupTableStatus table_status=ObjectLookupTableStatus::present;
    SessionObjectTradeLedger ledger;ObjectGroupSnapshot groups;
    std::unique_ptr<ObjectValueRegistry> values;std::unique_ptr<DynamicObjectGroups> dynamic;
    explicit Fixture(bool mt=false,bool known=true){
        for(const std::int16_t id:mt?std::vector<std::int16_t>{2,1}:std::vector<std::int16_t>{1})
            need(world.create_runtime_object(id,1000U+id,{},std::vector<EntryPoint>(27),mt?std::optional<std::uint32_t>{9000}:std::nullopt),"create real typed lookup body");
        world.set_definition_word_resolver([mt](const Entity& e,std::int16_t index)->std::optional<std::int16_t>{
            if(!e.definition)return {};if(index==10)return mt?77:0;if(index==8)return e.id==2?7:-7;return {};
        });
        table.status=TtabStatus::decoded;table.version=8;table.declared_count=1;table.interactions.push_back({});
        if(!mt)return;
        ObjectGroup group;group.kind=ObjectGroupKind::live_association;group.multitile=true;group.base_id=1;
        SessionObjectValueGroup price;price.group_id=price.revision=1;price.multitile=true;price.initial_price=300;price.base=ObjectTradeIdentity{1,ref(1).generation};
        price.price_origin={SessionObjectPriceSource::constructed_catalog,1002,"fixture","part.iff",128,{},{}};
        for(const auto id:{2,1}){
            ObjectGroupMember member{std::int16_t(id),ref(id).generation,std::int16_t(id==1?16:0),0,0};
            member.origin=ObjectGroupMemberOrigin{ObjectGroupMemberSource::original_import,1000U+id,9000,"fixture","part.iff",128};group.members.push_back(member);
            SessionObjectValueMember value;value.object={std::int16_t(id),ref(id).generation,1000U+id,"fixture","part.iff",128,9000};
            value.offset_x=member.offset_x;value.lead_object=std::uint16_t(id==1?1:0);price.members.push_back(value);
        }
        groups.groups={group};ledger.version=2;ledger.revision=1;ledger.groups={price};
        values=std::make_unique<ObjectValueRegistry>(world,ledger);need(values->bind(),"bind actual shared value owner");
        DynamicObjectGroupServices services;services.authority=[this]{return authority;};
        dynamic=std::make_unique<DynamicObjectGroups>(world,*values,groups,std::move(services));
        if(known)need(dynamic->bind_original_native_group({2,ref(2).generation},ref(2),
            {{ref(2),{0,0,0},{0,0,0}},{ref(1),{1,0,0},{1,0,0}}}),"bind actual native head/next owner");
    }
    WorldEntityReference ref(std::int16_t id)const{return {id,world.find(id)->generation};}
    void word(std::int16_t index,std::int16_t value,std::int16_t id=1){
        VmFrame address;address.context.caller=id;need(world.write(address,3,index,value),"write actual instance word");
    }
    ObjectLookupServices services(){
        ObjectLookupServices out;out.groups=dynamic.get();out.authority=[this]{return authority;};
        out.table_for=[this](auto){++table_reads;return ObjectLookupTable{table_status,table_status==ObjectLookupTableStatus::absent?nullptr:&table};};return out;
    }
};
void raw_predicate_matrix(){
    for(const std::int16_t independent:{std::int16_t(0),std::int16_t(1),std::int16_t(-1),std::int16_t(-32768)})
        for(const std::int16_t category:{std::int16_t(0),std::int16_t(17)})
            for(const auto threshold:{0U,99U,100U,0x7fffffffU,0x80000000U,0xffffffffU})for(const auto flags:{0U,0x80U}){
                Fixture f;f.word(60,independent);f.word(59,category);f.table.interactions.front().autonomy_threshold=threshold;f.table.interactions.front().flags=flags;
                const auto before=f.world.snapshot().spatial_revision;const auto result=object_lookup_eligibility(f.world,f.ref(1),f.services());
                const auto expected=std::uint16_t((independent?2:0)|(((threshold<100||threshold>=0x80000000U)&&!flags)?8:0));
                need(result.eligibility&&result.eligibility->reference==f.ref(1)&&result.eligibility->interaction_group==f.ref(1)&&result.eligibility->flags==expected,
                    "native masks use OD60 and signed32 TTAB threshold/bit80; Category59 and missing BHAVs add no predicate");
                need(f.world.snapshot().spatial_revision==before,"derived lookup never writes native masks or instance banks");
            }
    Fixture f;f.table.interactions.front().autonomy_threshold=100;auto row=f.table.interactions.front();row.autonomy_threshold=99;
    f.table.interactions.push_back(row);f.table.declared_count=2;
    need(object_lookup_eligibility(f.world,f.ref(1),f.services()).eligibility->flags==8,"any eligible physical TTAB row enables mask8");
}
void known_absence_and_unsupported(){
    for(const unsigned scenario:{0U,1U}){
        Fixture f;f.word(60,1);
        if(!scenario)f.table_status=ObjectLookupTableStatus::absent;
        else{f.table.status=TtabStatus::empty;f.table.declared_count=0;f.table.interactions.clear();}
        const auto result=object_lookup_eligibility(f.world,f.ref(1),f.services());
        need(result.eligibility&&result.eligibility->flags==2,"proven absent and present-empty TTAB are known false only for mask8");
    }
    for(const unsigned scenario:{0U,1U,2U,3U,4U,5U}){
        Fixture f;auto services=f.services();
        if(scenario==0)f.table_status=ObjectLookupTableStatus::unavailable;
        if(scenario==1)services.table_for=[](auto){return ObjectLookupTable{ObjectLookupTableStatus::present,nullptr};};
        if(scenario==2)f.table.status=TtabStatus::legacy_unsupported;
        if(scenario==3)f.table.declared_count=2;
        if(scenario==4)services.table_for=[&](auto){return ObjectLookupTable{ObjectLookupTableStatus::absent,&f.table};};
        if(scenario==5)f.world.set_definition_word_resolver({});
        const auto result=object_lookup_eligibility(f.world,f.ref(1),services);
        need(!result.eligibility&&!result.diagnostic.empty(),"unknown/unsupported resource data never becomes known-false lookup eligibility");
    }
}
void actual_interaction_owner(){
    Fixture f(true);f.word(60,1,2);auto services=f.services();services.table_for={};
    const auto follower=object_lookup_eligibility(f.world,f.ref(2),services);
    need(follower.eligibility&&follower.eligibility->interaction_group==f.ref(1)&&follower.eligibility->flags==2&&!f.table_reads,
        "actual native head2/base1 positive group resolves negative partner1 and returns false without asking for TTAB");
    const auto leader=object_lookup_eligibility(f.world,f.ref(1),f.services());
    need(leader.eligibility&&leader.eligibility->flags==8&&f.table_reads==1,"negative interaction-group owner uses its own exact resource TTAB");
    services.groups=nullptr;
    need(!object_lookup_eligibility(f.world,f.ref(1),services).eligibility,"MT source needs its actual native group owner, even for a trivial group word");
    Fixture unknown(true,false);
    need(!object_lookup_eligibility(unknown.world,unknown.ref(1),unknown.services()).eligibility,"unknown legacy MT is not promoted into native ownership");
}
void exact_lifetime_and_dispatch(){
    Fixture f;const auto original=f.ref(1);f.word(60,1);
    need(!object_lookup_eligibility(f.world,{1,original.generation+1},f.services()).eligibility,"stale reference rejected before resource lookup");
    need(f.world.remove_retaining_body(original),"actual deferred-dead body retained");
    const auto retained=object_lookup_eligibility(f.world,original,f.services());
    need(retained.eligibility&&retained.eligibility->flags==10,"native postLoad predicates read exact deferred-dead body, without invented Dead gate");
    need(f.world.create_runtime_object(1,1001,{},std::vector<EntryPoint>(27)),"reuse numeric ID while original exact body remains retained");
    need(object_lookup_eligibility(f.world,original,f.services()).eligibility->flags==10&&
        object_lookup_eligibility(f.world,f.ref(1),f.services()).eligibility->flags==8,"old exact fields never rebind to reused live ID");
    need(f.world.collect_retained_bodies({})&&!object_lookup_eligibility(f.world,original,f.services()).eligibility,"collected original body becomes unavailable");
    Fixture changed;auto services=changed.services();services.table_for=[&](auto){changed.word(60,1);return ObjectLookupTable{ObjectLookupTableStatus::present,&changed.table};};
    need(!object_lookup_eligibility(changed.world,changed.ref(1),services).eligibility,"provider-side source changes invalidate the derived result");
    Fixture dispatch;bool queried=false;Vm vm({{routine_key(4096),Bhav{0x8002,{{98,254,255,{}}},{},Bhav::Metadata{0,4,0,0}}}},dispatch.world.vm_memory(),{},
        [&](const auto&,const auto&,const auto&,const auto&){queried=object_lookup_eligibility(dispatch.world,dispatch.ref(1),dispatch.services()).eligibility.has_value();return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};});
    need(vm.start(routine_key(4096),{1,1,0})==VmStatus::running&&vm.run(4)==VmStatus::returned_true&&queried,
        "real dispatch can synchronously query the actual World/provider without another engine or thread reset");
    dispatch.authority=false;need(!object_lookup_eligibility(dispatch.world,dispatch.ref(1),dispatch.services()).eligibility,"expired source authority rejects lookup");
}
}
void publication(){
    Fixture f;const auto ref=f.ref(1);
    need(!f.world.lookup_selectors(ref,10),"unpublished selectors remain unknown");
    need(f.world.publish_lookup_selector(ref,0x40,true)&&f.world.publish_lookup_selector(ref,2,true)&&
        f.world.publish_lookup_selector(ref,8,false),"publish exact body selectors");
    need(f.world.lookup_selectors(ref,0x4a)==std::optional<std::uint16_t>{0x42},"masked writes preserve unrelated selectors");
    need(f.world.publish_lookup_selector(ref,2,false)&&f.world.publish_lookup_selector(ref,8,true)&&
        f.world.lookup_selectors(ref,0x4a)==std::optional<std::uint16_t>{0x48},"clear and set match original mask operation");
    auto stale=ref;++stale.generation;
    need(!f.world.publish_lookup_selector(stale,8,false)&&!f.world.lookup_selectors(stale,8),"stale identity cannot read or publish");
    need(f.world.remove(1)&&f.world.create_runtime_object(1,1001,{},std::vector<EntryPoint>(27))&&
        !f.world.lookup_selectors(f.ref(1),10)&&!f.world.publish_lookup_selector(ref,8,true),"reused IDs inherit no previous lookup state");
    Fixture retained;const auto old=retained.ref(1);
    need(retained.world.remove_retaining_body(old)&&retained.world.create_runtime_object(1,1001,{},std::vector<EntryPoint>(27))&&
        retained.world.publish_lookup_selector(old,8,true)&&retained.world.lookup_selectors(old,8)==std::optional<std::uint16_t>{8}&&
        !retained.world.lookup_selectors(retained.ref(1),8),"publishing exact retained body never touches reused live ID");
}
int main(){try{publication();raw_predicate_matrix();known_absence_and_unsupported();actual_interaction_owner();exact_lifetime_and_dispatch();std::cout<<"native object lookup eligibility passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
