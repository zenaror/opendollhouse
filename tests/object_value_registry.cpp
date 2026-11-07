// SPDX-License-Identifier: MPL-2.0
#include "odh/object_value_registry.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
namespace {
using namespace odh;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F operation){bool rejected=false;try{operation();}catch(const FormatError&){rejected=true;}check(rejected,"invalid value ledger rejected");}
SessionObjectValueMember member(World& world,std::int16_t id,std::uint32_t guid,std::int16_t x=0,
        std::uint16_t lead=0,std::uint16_t wear=0,std::optional<std::uint32_t> master={}){
    check(world.create_runtime_object(id,guid,{},std::vector<EntryPoint>(27),master),"typed constructor with real ObjectData41");
    SessionObjectValueMember value;value.object={id,world.find(id)->generation,guid,"Objects.far","part.iff",128,master};
    value.offset_x=x;value.lead_object=lead;value.wear_known=true;value.wear=wear;return value;
}
SessionObjectPriceOrigin price_origin(){return {SessionObjectPriceSource::purchase,0x900,"Objects.far","master.iff",150,{},{}};}
}
int main(){try{
    using namespace odh;
    World world;auto first=member(world,9,209,0,0,0,0x901);auto second=member(world,2,202,16,0,40);
    auto lead=member(world,7,207,32,1,100,0x900);auto unrelated=member(world,12,212);
    check(unrelated.object.id==12,"untracked object fixture has independent identity");
    SessionObjectTradeLedger ledger;ledger.version=2;ObjectValueRegistry values(world,ledger);
    SessionObjectValueGroup group;group.multitile=true;group.members={first,second,lead};group.base=object_value_source_base(group.members);
    group.members[0].disabled=0;group.members[1].disabled=3; // Explicit runtime facts independent of price origin.
    group.initial_price=100;group.price_origin=price_origin();
    check(group.base&&group.base->id==7&&values.admit(group)&&values.bind(),"source lead wins without choosing minimum ID, first member or common master");
    check(!world.set_current_value_read_resolver([](const World&,auto){return World::CurrentValueReadResult{};}),"second value owner cannot replace bound ledger");
    check(values.refund({2,second.object.generation}).amount==75,"refund uses actual base Wear100 and shared InitialPrice100");
    check(values.set_wear({7,lead.object.generation},true,{})&&values.refund({9,first.object.generation}).amount==80,
        "known absent base ObjectState selects source Wear80 fallback");
    check(values.set_wear({7,lead.object.generation},false,{})&&!values.refund({2,second.object.generation}).amount,
        "unknown Wear does not masquerade as absent state");
    check(values.set_wear({7,lead.object.generation},true,100),"restore exact source Wear");
    VmFrame frame;frame.context.caller=2;frame.context.stack_object=9;
    const auto before=ledger.revision;
    check(world.read(frame,3,41)==100&&world.read(frame,4,41)==100,"both live entity addressing scopes read group InitialPrice rather than sibling raw cache");
    check(world.write(frame,3,41,0)&&ledger.revision==before+1&&values.query({7,lead.object.generation})->initial_price==0,
        "writing same raw word0 still resets group price100 atomically and increments value revision");
    check(world.read(frame,4,41)==0&&world.find(9)->object_words.at(41)==0,"siblings share price without rewriting their raw word caches");
    check(ledger.groups[0].members[0].disabled==0&&ledger.groups[0].members[1].disabled==3&&
        !ledger.groups[0].members[2].disabled,"same-value CurrentValue write preserves known zero/nonzero/unknown runtime Disabled facts");
    check(world.write(frame,4,41,-123)&&world.read(frame,3,41)==-123&&ledger.groups[0].initial_price==-123&&
        ledger.groups[0].price_origin.kind==SessionObjectPriceSource::current_value_write&&ledger.groups[0].price_origin.writer->id==9,
        "CurrentValue sign extends authored short and records exact writing member");
    frame.context.caller=12;const auto unchanged=ledger.revision;
    check(world.write(frame,3,41,88)&&world.read(frame,3,41)==88&&ledger.revision==unchanged,
        "untracked legacy object preserves working raw reads/writes without inventing a price entry");
    check(world.declare_object_word(2,41,777)&&ledger.groups[0].initial_price==-123,
        "resource declaration bypasses price observer and never seeds a group from raw41");
    ledger.groups[0].initial_price=70000;
    frame.context.caller=2;check(world.read(frame,3,41)==4464,"shared CurrentValue read reproduces unchecked signed-short cast");
    ledger.groups[0].initial_price.reset();ledger.groups[0].price_origin={};
    check(!world.read(frame,3,41)&&!values.refund({2,second.object.generation}).amount,"admitted unknown price stays unknown despite nonzero raw41");
    check(world.write(frame,3,41,100)&&values.refund({2,second.object.generation}).amount==75,
        "authentic live write establishes formerly unknown shared value");
    check(values.retire_member({7,lead.object.generation})&&world.remove(7),"actual base member can retire without retiring surviving group");
    check(ledger.groups[0].members[0].object.id==9&&ledger.groups[0].base->id==9&&values.refund({2,second.object.generation}).amount==100,
        "removing lead recomputes source zero-offset base and selects its own Wear, preserving source order and InitialPrice");
    check(world.create_runtime_object(7,207,{},std::vector<EntryPoint>(27),0x900)&&!values.query({7,world.find(7)->generation}),
        "numeric ID reuse cannot inherit retired member valuation");
    auto stale=ledger;stale.groups[0].members[1].object.generation++;rejects([&]{validate_object_value_groups(stale,world.snapshot());});
    auto duplicate=ledger;duplicate.groups.push_back(duplicate.groups[0]);duplicate.groups.back().group_id++;
    rejects([&]{validate_object_value_groups(duplicate,world.snapshot());});
    auto wrong_base=ledger;wrong_base.groups[0].base=ObjectTradeIdentity{2,second.object.generation};
    rejects([&]{validate_object_value_groups(wrong_base,world.snapshot());});
    auto competing=ledger;competing.objects.push_back({second.object,2,{}});rejects([&]{validate_object_value_groups(competing,world.snapshot());});
    const auto saved=world.snapshot();World restored;check(restored.restore_snapshot(saved),"World snapshot restores raw fields with no price side effects");
    check(restored.find(2)->object_words.at(41)==100&&restored.read(frame,3,41)==100,"unbound restored World retains legacy raw cache only");
    check(values.retire_member({9,first.object.generation})&&world.remove(9)&&ledger.groups[0].base->id==2,
        "deleting zero-offset base falls through to first surviving source member");
    check(values.retire_member({2,second.object.generation})&&world.remove(2)&&ledger.groups.empty()&&ledger.version==2,
        "last member retirement empties only this group and preserves explicit v2 owner");
    {
        World legacy_world;const auto single=member(legacy_world,3,303);SessionObjectTradeLedger legacy{{{single.object,125,{}}}};
        ObjectValueRegistry registry(legacy_world,legacy);check(legacy.version==1&&legacy.groups.empty(),"construction/query never silently upgrades legacy bytes");
        check(registry.migrate_legacy_singletons()&&legacy.objects.empty()&&legacy.version==2&&registry.refund({3,single.object.generation}).amount==100,
            "explicit singleton migration preserves price and absent Wear fallback without reading raw41");
        check(!legacy.groups[0].members[0].disabled,"legacy singleton migration leaves unauthenticated runtime Disabled unknown");
    }
    {
        World purchase_world;auto base=member(purchase_world,1,101,0,1,100,0x900);
        auto part=member(purchase_world,2,102,16,0,40,0x900);base.disabled=0;part.disabled=2;
        SessionObjectTradeLedger data;data.version=2;ObjectValueRegistry registry(purchase_world,data);
        SessionObjectValueGroup bought;bought.multitile=true;bought.members={base,part};
        bought.base=ObjectTradeIdentity{1,base.object.generation};bought.initial_price=100;bought.price_origin=price_origin();
        check(registry.admit(bought)&&registry.bind(),"purchase group begins with catalog price before Init");
        VmFrame address;address.context.caller=1;address.context.stack_object=2;
        check(purchase_world.write(address,3,41,50)&&purchase_world.read(address,4,41)==50,
            "Init/placement script price is shared before UI purchase success");
        const auto revision=data.revision;
        check(registry.set_purchase_price({2,part.object.generation},100,price_origin())&&
            purchase_world.read(address,3,41)==100&&purchase_world.read(address,4,41)==100&&
            registry.refund({1,base.object.generation}).amount==75&&data.revision==revision+1,
            "successful UI purchase resets one shared catalog price for all pieces and refund");
        check(purchase_world.find(1)->object_words.at(41)==50&&purchase_world.find(2)->object_words.at(41)==0&&
            data.groups[0].members[0].wear==100&&data.groups[0].members[1].wear==40&&
            data.groups[0].members[0].disabled==0&&data.groups[0].members[1].disabled==2&&
            data.groups[0].price_origin.kind==SessionObjectPriceSource::purchase&&!data.groups[0].price_origin.writer,
            "final price assignment preserves raw caches, per-piece state and exact purchase provenance");
        const auto stable=data.revision;auto invalid=price_origin();invalid.source.clear();
        check(!registry.set_purchase_price({1,base.object.generation},7,invalid)&&
            !registry.set_purchase_price({1,base.object.generation+1},7,price_origin())&&
            data.revision==stable&&data.groups[0].initial_price==100,
            "invalid provenance and stale identity cannot publish a partial price assignment");
        data.revision=std::numeric_limits<std::uint64_t>::max();
        check(!registry.set_purchase_price({1,base.object.generation},7,price_origin())&&
            data.groups[0].initial_price==100&&purchase_world.find(1)->object_words.at(41)==50,
            "revision exhaustion preserves catalog price and raw cache");
    }
    {
        World atomic_world;auto single=member(atomic_world,1,101);SessionObjectTradeLedger data;data.version=2;
        ObjectValueRegistry registry(atomic_world,data);SessionObjectValueGroup one;one.members={single};one.base=ObjectTradeIdentity{1,single.object.generation};
        one.initial_price=5;one.price_origin=price_origin();check(registry.admit(one)&&registry.bind(),"atomic observer fixture");
        const auto revision=data.revision;data.revision=std::numeric_limits<std::uint64_t>::max();
        VmFrame address;address.context.caller=1;check(!atomic_world.write(address,3,41,7)&&atomic_world.find(1)->object_words.at(41)==0&&data.groups[0].initial_price==5,
            "revision failure leaves both raw field and shared price unchanged");data.revision=revision;
    }
    {
        World w;auto a=member(w,1,101,0,0,10);auto b=member(w,2,102,16,1,20,0x990);auto c=member(w,3,103,0,0,30);
        b.disabled=3;SessionObjectTradeLedger data;data.version=2;ObjectValueRegistry registry(w,data);
        SessionObjectValueGroup old;old.members={a,b};old.base=object_value_source_base(old.members);old.initial_price=1000;old.price_origin=price_origin();
        SessionObjectValueGroup receiver;receiver.members={c};receiver.base=ObjectTradeIdentity{3,c.object.generation};receiver.initial_price=20;
        receiver.price_origin={SessionObjectPriceSource::constructed_catalog,103,"Objects.far","part.iff",128,{},{}};
        check(registry.admit(old)&&registry.admit(receiver)&&registry.bind(),"dynamic value transition fixtures admitted");
        check(registry.split_member({2,b.object.generation},-16,32,1,1),"dynamic separation copies value to fresh ordered singleton");
        const auto* separated=registry.query({2,b.object.generation});const auto* remaining=registry.query({1,a.object.generation});
        check(data.groups.size()==3&&separated&&remaining&&separated->initial_price==1000&&remaining->initial_price==1000&&
            separated->price_origin.guid==remaining->price_origin.guid&&separated->members[0].object.master_guid==0x990&&
            separated->members[0].offset_x==-16&&separated->members[0].offset_y==32&&separated->members[0].offset_level==1&&
            separated->members[0].wear==20&&separated->members[0].disabled==3&&remaining->base->id==1,
            "split preserves both shared prices, exact member state/provenance and old source order/base");
        const auto rev=data.revision;check(registry.split_member({2,b.object.generation},99,99,2,0)&&data.revision==rev&&
            registry.query({2,b.object.generation})->members[0].offset_x==-16,"source singleton split is a no-op");
        ObjectGroupMember offset{2,b.object.generation,48,-32,-1};
        const auto saved=data;auto wrong=offset;++wrong.generation;
        check(!registry.combine_groups({3,c.object.generation},{2,b.object.generation},{wrong})&&data.revision==saved.revision&&data.groups.size()==saved.groups.size(),
            "stale combine identity is rejected without changing ledger");
        wrong=offset;wrong.origin=ObjectGroupMemberOrigin{ObjectGroupMemberSource::native_created,999,0x990,"Objects.far","part.iff",128};
        check(!registry.combine_groups({3,c.object.generation},{2,b.object.generation},{wrong})&&data.revision==saved.revision,
            "offset argument cannot forge donor resource provenance");
        check(registry.combine_groups({3,c.object.generation},{2,b.object.generation},{offset}),"combine appends actual donor with supplied transform");
        const auto* combined=registry.query({2,b.object.generation});check(combined&&combined==registry.query({3,c.object.generation})&&
            combined->initial_price==20&&combined->price_origin.kind==SessionObjectPriceSource::constructed_catalog&&
            combined->members.size()==2&&combined->members[0].object.id==3&&combined->members[1].object.id==2&&
            combined->members[1].offset_x==48&&combined->members[1].offset_y==-32&&combined->members[1].offset_level==-1&&
            combined->members[1].disabled==3&&combined->members[1].wear==20&&combined->base->id==2&&data.groups.size()==2,
            "combine conserves receiver price/origin rather than summing; donor state/order survive and base is recomputed");
        VmFrame frame;frame.context.caller=2;check(w.read(frame,3,41)==20&&w.find(2)->object_words.at(41)==0,
            "dynamic membership updates CurrentValue owner without rewriting raw field");
        const auto stable=data.revision;data.revision=std::numeric_limits<std::uint64_t>::max();
        check(!registry.split_member({2,b.object.generation},0,0,0,0)&&data.groups.size()==2&&
            registry.query({2,b.object.generation})->initial_price==20,"split revision exhaustion has no partial separation");
        check(!registry.combine_groups({1,a.object.generation},{2,b.object.generation},{{3,c.object.generation,0,0,0},{2,b.object.generation,0,0,0}})&&
            data.groups.size()==2,"combine revision exhaustion has no partial append/removal");data.revision=stable;
    }
    std::cout<<"object value registry source groups, CurrentValue and lifecycle checks passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
