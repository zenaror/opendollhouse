#include "odh/property_value.hpp"
#include "odh/object_value_registry.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void object(odh::World& world,std::int16_t id,std::optional<std::uint32_t> master,
    std::optional<std::int16_t> value,std::optional<std::int16_t> repair) {
    check(world.create(id,odh::EntityKind::object) && world.define_type(id,100U+static_cast<unsigned>(id),master),"fixture identity");
    if(value)check(world.declare_object_word(id,41,*value),"fixture current value");
    if(repair)check(world.declare_object_word(id,15,*repair),"fixture repair state");
}
void write(odh::World& world,std::int16_t id,std::int16_t index,std::int16_t value) {
    const odh::VmFrame frame{odh::routine_key(4096),0,{id,id,id,0},{},{}};
    check(world.write(frame,3,index,value),"live word write");
}
}
int main(){try{
    using namespace odh;using R=PropertyValueReason;
    World world;object(world,1,900,101,0);object(world,2,900,32000,0);
    const SavedMultitileGroup group{1,900,{2,1}};
    auto result=inspect_property_group_value(world,group,true);
    check(result.status==PropertyValueStatus::resolved && result.value==101 && !result.repair_discount,"leader value counted once");
    write(world,2,15,-1);
    result=inspect_property_group_value(world,group,true);
    check(result.value==50 && result.leader_current_value==101 && result.repair_discount,"nonleader repair halves odd value toward zero");
    for(const auto pair:{std::pair<std::int16_t,std::int16_t>{-101,-50},{-1,0},{-32768,-16384},{32767,16383},{0,0}}) {
        write(world,1,41,pair.first);
        check(inspect_property_group_value(world,group,true).value==pair.second,"signed half matches comparison assembly");
    }
    write(world,2,15,0);write(world,1,41,-101);
    check(inspect_property_group_value(world,group,true).value==-101,"no repair preserves signed value");
    write(world,1,15,1);
    check(inspect_property_group_value(world,group,true).value==-50,"leader repair also halves");
    check(inspect_property_group_value(world,group,false).reason==R::incomplete_group,"complete inventory is explicit precondition");
    check(inspect_property_group_value(world,group,true,1).reason==R::member_budget,"caller member budget");
    auto changed=group;changed.members.assign(4097,1);
    check(inspect_property_group_value(world,changed,true,9999).reason==R::member_budget,"hard cap precedes allocation/duplicate checks");
    changed=group;changed.members.clear();
    check(inspect_property_group_value(world,changed,true).reason==R::empty_group,"empty group");
    changed=group;changed.members={1,1};
    check(inspect_property_group_value(world,changed,true).reason==R::duplicate_member,"duplicate member");
    for(const auto bad:{std::int16_t{0},std::int16_t{-1}}) {
        changed=group;changed.members={1,bad};
        check(inspect_property_group_value(world,changed,true).reason==R::invalid_id,"nonpositive member rejected");
        changed=group;changed.leader=bad;
        check(inspect_property_group_value(world,changed,true).reason==R::invalid_id,"nonpositive leader rejected");
    }
    changed=group;changed.leader=3;
    check(inspect_property_group_value(world,changed,true).reason==R::leader_absent,"leader must belong to group");
    changed=group;changed.members={1,3};
    check(inspect_property_group_value(world,changed,true).reason==R::missing_entity,"missing live member");
    changed=group;changed.master_guid=901;
    check(inspect_property_group_value(world,changed,true).reason==R::identity_mismatch,"master mismatch");
    changed=group;changed.master_guid.reset();
    check(inspect_property_group_value(world,changed,true).value==-50,"absent optional master summary does not reject authenticated complete group");
    World mixed;object(mixed,1,900,101,0);object(mixed,2,901,32000,0);object(mixed,3,{}, {},0);
    const SavedMultitileGroup mixed_group{1,{}, {2,3,1}};
    check(inspect_property_group_value(mixed,mixed_group,true).value==101,"different live masters and nonmultitile members preserve root value only");
    for(const auto id:{std::int16_t{1},std::int16_t{2},std::int16_t{3}}){
        write(mixed,id,15,1);
        check(inspect_property_group_value(mixed,mixed_group,true).value==50,"repair on any heterogeneous chain member halves root value");
        write(mixed,id,15,0);
    }
    auto mixed_summary=mixed_group;mixed_summary.master_guid=900;
    check(inspect_property_group_value(mixed,mixed_summary,true).reason==R::identity_mismatch,"provided master summary remains a checked assertion");
    World zero_master;object(zero_master,1,0,101,0);
    check(inspect_property_group_value(zero_master,{1,{}, {1}},true).reason==R::identity_mismatch,"optional group summary does not authorize a zero live master GUID");
    World standalone_root;object(standalone_root,1,{},101,0);object(standalone_root,2,901,32000,1);object(standalone_root,3,902,{},{});
    const SavedMultitileGroup standalone_chain{1,{}, {2,3,1}};
    auto standalone_value=inspect_property_group_value(standalone_root,standalone_chain,true);
    check(standalone_value.status==PropertyValueStatus::resolved&&standalone_value.value==101&&!standalone_value.repair_discount,
        "non-MT root ignores foreign broken and missing RepairState words");
    write(standalone_root,1,15,1);
    standalone_value=inspect_property_group_value(standalone_root,standalone_chain,true);
    check(standalone_value.value==50&&standalone_value.repair_discount,"non-MT root discounts only its own repair state");
    auto duplicate_standalone=standalone_chain;duplicate_standalone.members.push_back(2);
    check(inspect_property_group_value(standalone_root,duplicate_standalone,true).reason==R::duplicate_member,
        "nonconsumed foreign repair does not bypass complete chain identity checks");
    World standalone_missing;object(standalone_missing,1,{},101,{});object(standalone_missing,2,901,32000,0);
    check(inspect_property_group_value(standalone_missing,{1,{}, {2,1}},true).reason==R::missing_repair_state,
        "non-MT root still requires its actual own RepairState");
    World missing;object(missing,1,{},101,{});
    check(inspect_property_group_value(missing,{1,{}, {1}},true).reason==R::missing_repair_state,"missing repair is not zero");
    World missing_value;object(missing_value,1,{}, {},0);
    check(inspect_property_group_value(missing_value,{1,{}, {1}},true).reason==R::missing_current_value,"missing current value is not zero");
    World missing_member_repair;object(missing_member_repair,1,900,101,0);object(missing_member_repair,2,900,99,{});
    check(inspect_property_group_value(missing_member_repair,group,true).reason==R::missing_repair_state,"every member needs current repair state");
    World missing_identity;check(missing_identity.create(1,EntityKind::object),"untyped fixture");
    check(inspect_property_group_value(missing_identity,{1,{}, {1}},true).reason==R::missing_identity,"missing identity rejected");

    World imported;std::array<std::int16_t,68> data{};std::array<std::int16_t,5> extras{};
    data[41]=1000;data[15]=0;
    check(imported.create_saved_entity(1,EntityKind::object,101,{},{},{},data,extras,0),"saved fixture");
    write(imported,1,41,99);write(imported,1,15,1);
    result=inspect_property_group_value(imported,{1,{}, {1}},true);
    check(result.value==49 && imported.find(1)->saved_object_banks->object_data[41]==1000 &&
        imported.find(1)->saved_object_banks->object_data[15]==0,"live values override immutable saved banks without mutation");
    {
        World owned;
        check(owned.create_runtime_object(1,101,{},std::vector<EntryPoint>(27),900)&&
            owned.create_runtime_object(2,102,{},std::vector<EntryPoint>(27),900),
            "price fixture uses actual typed multipart construction");
        SessionObjectTradeLedger ledger;ledger.version=2;ObjectValueRegistry values(owned,ledger);
        SessionObjectValueGroup price;price.multitile=true;
        for(std::int16_t id:{1,2}){SessionObjectValueMember member;
            member.object={id,owned.find(id)->generation,static_cast<std::uint32_t>(100+id),
                "Objects.far","part.iff",128,900};
            member.offset_x=static_cast<std::int16_t>((id-1)*16);member.lead_object=id==1?1:0;
            member.wear_known=true;member.wear=0;price.members.push_back(member);}
        price.base=ObjectTradeIdentity{1,owned.find(1)->generation};
        price.initial_price=1;price.price_origin={SessionObjectPriceSource::purchase,900,"Objects.far","master.iff",128,{},{}};
        check(values.admit(price)&&values.bind()&&values.set_purchase_price(*price.base,150,price.price_origin),
            "purchase establishes actual shared value owner without rewriting raw fields");
        const auto ledger_revision=ledger.revision;
        const auto raw=owned.find(1)->object_words;
        const SavedMultitileGroup actual{1,900,{1,2}};
        auto measured=inspect_property_group_value(owned,actual,true);
        check(measured.status==PropertyValueStatus::resolved&&measured.leader_current_value==150&&measured.value==150&&
            owned.find(1)->object_words.at(41)==0&&owned.find(1)->object_words==raw&&ledger.revision==ledger_revision,
            "property getter consumes shared InitialPrice150 while raw41 stays0 and quote mutates neither owner");
        ledger.groups[0].initial_price.reset();ledger.groups[0].price_origin={};
        check(inspect_property_group_value(owned,actual,true).reason==R::missing_current_value,
            "handled unknown shared value never falls back to known raw cache0");
        check(values.set_purchase_price(*price.base,150,price.price_origin)&&
            owned.declare_object_word(1,41,999)&&owned.remove(2)&&
            owned.create_runtime_object(2,102,{},std::vector<EntryPoint>(27),900),
            "source owner becomes stale after real numeric identity reuse");
        check(inspect_property_group_value(owned,actual,true).reason==R::missing_current_value&&
            owned.find(1)->object_words.at(41)==999,
            "stale group ownership remains unknown despite a tempting nonzero raw root cache");
    }
    check(std::string(property_value_status_name(PropertyValueStatus::resolved))=="resolved" &&
        std::string(property_value_reason_name(R::missing_current_value))=="missing_current_value","audit names");
    std::cout<<"current group value, repair, identity and bounded input checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
