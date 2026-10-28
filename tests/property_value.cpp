#include "odh/property_value.hpp"
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
    check(inspect_property_group_value(world,changed,true).reason==R::identity_mismatch,"unidentified multipart group");
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
    check(std::string(property_value_status_name(PropertyValueStatus::resolved))=="resolved" &&
        std::string(property_value_reason_name(R::missing_current_value))=="missing_current_value","audit names");
    std::cout<<"current group value, repair, identity and bounded input checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
