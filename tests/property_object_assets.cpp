#include "odh/property_object_assets.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
odh::DefinitionCandidate definition(std::uint32_t guid,std::uint16_t type,
    std::optional<std::uint16_t> build,std::uint16_t master=0,std::int16_t sub=0) {
    odh::ObjectDefinition d{};d.version=138;d.guid=guid;d.object_type=type;
    d.master_id=master;d.sub_index=sub;d.is_multitile=master!=0;d.build_mode_type=build;
    return {"objects.far","objects.iff",static_cast<std::uint16_t>(guid),d};
}
void object(odh::World& world,std::int16_t id,std::uint32_t guid,
    std::optional<std::uint32_t> master={},std::optional<std::int16_t> flags=0,
    std::optional<std::int16_t> value=101,std::optional<std::int16_t> repair=0,
    odh::EntityKind kind=odh::EntityKind::object,std::size_t slots=0) {
    check(world.create(id,kind,{0,0,slots,0}) && world.define_type(id,guid,master),"fixture entity");
    if(flags)check(world.declare_object_word(id,43,*flags),"fixture flags");
    if(value)check(world.declare_object_word(id,41,*value),"fixture value");
    if(repair)check(world.declare_object_word(id,15,*repair),"fixture repair");
}
void at(odh::World& world,std::int16_t id,std::int32_t x=16,std::int32_t y=16,std::int16_t level=1) {
    check(world.set_position(id,{x,y,level,0}),"fixture position");
}
void write(odh::World& world,std::int16_t id,std::int16_t word,std::int16_t value) {
    const odh::VmFrame frame{odh::routine_key(4096),0,{id,id,id,0},{},{}};
    check(world.write(frame,3,word,value),"fixture live write");
}
void unresolved(const odh::PropertyObjectAssets& result,odh::PropertyObjectAssetsReason reason) {
    if(result.status!=odh::PropertyObjectAssetsStatus::unresolved || result.reason!=reason ||
        result.primary || result.architecture_objects)
        throw std::runtime_error(std::string("unresolved result: expected ")+odh::property_object_assets_reason_name(reason)+
            ", got "+odh::property_object_assets_reason_name(result.reason)+
            ", totals_present="+(result.primary || result.architecture_objects?"true":"false"));
}
}
int main(){try{
    using namespace odh;using R=PropertyObjectAssetsReason;using B=PropertyObjectAssetBucket;
    const DefinitionIndex definitions({definition(1,4,0),definition(2,4,3),definition(3,4,4),
        definition(4,2,0),definition(5,7,{}),definition(6,4,{}),definition(7,2,3)});
    World world;
    object(world,1,1);at(world,1);
    object(world,2,1,{},16,{},{});at(world,2); // Retained buy root excludes without reading price.
    object(world,3,2,{},0,33);at(world,3);
    object(world,4,3,{},0,{},{});at(world,4); // Build4 never asks for value.
    object(world,5,4,{},0,17,0,EntityKind::object);at(world,5); // OBJD actor despite EntityKind.
    object(world,6,4,{},16,{},{});at(world,6);
    object(world,7,5,{},{},{},{}); // Type7 needs neither flags, build category nor value.
    object(world,8,1,{}, {},{},{}); // Out-of-world buy excludes without flags/value.
    object(world,9,7,{},16,9);at(world,9);
    std::vector<SavedMultitileGroup> groups;
    for(std::int16_t id=1;id<=9;++id)groups.push_back({id,{}, {id}});
    auto result=inspect_property_object_assets(world,definitions,groups,true,64);
    check(result.status==PropertyObjectAssetsStatus::resolved && result.primary==118 &&
        result.architecture_objects==42,"exact primary/architecture branch ordering");
    check(result.groups[3].reason==R::excluded_build4 && result.groups[6].reason==R::own_type7 &&
        result.groups[5].reason==R::excluded_build0 && result.groups[8].bucket==B::architecture_objects,
        "retained actors use effective category and type7 skips metadata");
    const auto before=world.snapshot();
    (void)inspect_property_object_assets(world,definitions,groups,true,64);
    check(world.snapshot().word_count==before.word_count && world.find(1)->object_words==before.entities[0].object_words,
        "inspection does not mutate World words");
    unresolved(inspect_property_object_assets(world,definitions,groups,false,64),R::incomplete_inventory);
    for(const auto dimension:{0U,65U})unresolved(inspect_property_object_assets(world,definitions,groups,true,dimension),R::invalid_dimension);
    auto changed=groups;changed.pop_back();unresolved(inspect_property_object_assets(world,definitions,changed,true,64),R::incomplete_inventory);
    changed=groups;changed.push_back(groups[0]);unresolved(inspect_property_object_assets(world,definitions,changed,true,64),R::invalid_partition);
    changed=groups;changed[0].members={1,1};unresolved(inspect_property_object_assets(world,definitions,changed,true,64),R::invalid_partition);
    changed=groups;changed[0].leader=10;unresolved(inspect_property_object_assets(world,definitions,changed,true,64),R::invalid_partition);
    changed=groups;changed[0].members={0};unresolved(inspect_property_object_assets(world,definitions,changed,true,64),R::invalid_partition);
    changed=groups;changed[0].members={10};unresolved(inspect_property_object_assets(world,definitions,changed,true,64),R::invalid_partition);
    World missing_flags;object(missing_flags,1,1,{},{},101,0);at(missing_flags,1);
    unresolved(inspect_property_object_assets(missing_flags,definitions,{{1,{}, {1}}},true,64),R::missing_flags);
    World missing_category;object(missing_category,1,6);at(missing_category,1);
    unresolved(inspect_property_object_assets(missing_category,definitions,{{1,{}, {1}}},true,64),R::missing_build_mode);
    World missing_value;object(missing_value,1,1,{},0,{},0);at(missing_value,1);
    unresolved(inspect_property_object_assets(missing_value,definitions,{{1,{}, {1}}},true,64),R::group_value_unresolved);
    check(missing_value.declare_definition_word(1,9,2),"contradictory current OBJD metadata");
    unresolved(inspect_property_object_assets(missing_value,definitions,{{1,{}, {1}}},true,64),R::definition_type_mismatch);
    const DefinitionIndex ambiguous({definition(1,4,0),definition(1,4,3)});
    unresolved(inspect_property_object_assets(world,ambiguous,groups,true,64),R::ambiguous_definition);

    const DefinitionIndex multipart({definition(10,4,0,7,0),definition(11,2,0,7,1),definition(12,4,3,7,-1)});
    World multi;object(multi,1,10,12,0,101,0);object(multi,2,11,12,16,32000,1);at(multi,1);
    const std::vector<SavedMultitileGroup> partition{{1,12,{2,1}}};
    result=inspect_property_object_assets(multi,multipart,partition,true,64);
    check(result.primary==0 && result.architecture_objects==50 && result.groups.size()==1,
        "root once with master build category; secondary actor/retention cannot choose bucket; secondary repair halves");
    write(multi,1,43,16);write(multi,1,41,-101);
    result=inspect_property_object_assets(multi,multipart,partition,true,64);
    check(result.architecture_objects==-50,"signed group value from live root");
    auto bad_partition=partition;bad_partition[0].master_guid=13;
    unresolved(inspect_property_object_assets(multi,multipart,bad_partition,true,64),R::identity_mismatch);
    const DefinitionIndex no_master({definition(10,4,0,7,0),definition(11,2,0,7,1)});
    unresolved(inspect_property_object_assets(multi,no_master,partition,true,64),R::missing_master);
    auto other=definition(11,2,0,7,1);other.asset="other.iff";
    const DefinitionIndex crossed({definition(10,4,0,7,0),other,definition(12,4,3,7,-1)});
    unresolved(inspect_property_object_assets(multi,crossed,partition,true,64),R::missing_master);

    const auto in=[](std::int32_t x,std::int32_t y,std::int16_t level=1,unsigned dimension=64){
        return property_object_position_in_world(EntityPosition{x,y,level,0},dimension);
    };
    check(in(16,16) && in(62*16,62*16) && !in(0,16) && !in(63*16,16),"spatial interior/borders");
    check(!in(-1,16) && !in(-16,16) && !in(-17,16) && in(-4080,16),"negative arithmetic floor then signed-byte wrap");
    check(in(4096+16,16) && !in(128*16,16) && !in(std::numeric_limits<std::int32_t>::min(),16),"large signed-byte wrap");
    check(in(16,16,-123) && in(16,16,30000) && !in(16,16,1,2) && !in(16,16,1,65),"level ignored and dimensions bounded");
    check(!property_object_position_in_world({},64),"absent position is out-of-world");
    World attached;object(attached,1,1,{},0,5,0,EntityKind::object,1);object(attached,2,1);
    at(attached,1,16,16,30000);at(attached,2,0,16);
    check(attached.attach(1,0,2),"attach preserves child own position");
    result=inspect_property_object_assets(attached,definitions,{{1,{}, {1}},{2,{}, {2}}},true,64);
    check(result.primary==5 && result.groups[1].reason==R::excluded_build0,"attached root uses own coordinates, not parent");
    object(attached,3,1);check(attached.set_ui_ghost(3,true),"ghost fixture");
    check(inspect_property_object_assets(attached,definitions,{{1,{}, {1}},{2,{}, {2}}},true,64).primary==5,
        "UI ghosts are outside collector inventory");

    // The4096-entity/int16 contribution bound makes int32 overflow unreachable
    // for valid inventories. Exercise the maximal positive/negative sums and
    // reject oversize inventories before they could evade that arithmetic bound.
    World maximum;std::vector<SavedMultitileGroup> maximum_groups;
    for(std::int16_t id=1;id<=4096;++id){object(maximum,id,4,{},0,32767,0);at(maximum,id);maximum_groups.push_back({id,{}, {id}});}
    result=inspect_property_object_assets(maximum,definitions,maximum_groups,true,64);
    check(result.primary==std::int32_t{4096}*32767,"maximum bounded sum does not overflow");
    for(std::int16_t id=1;id<=4096;++id)write(maximum,id,41,-32768);
    check(inspect_property_object_assets(maximum,definitions,maximum_groups,true,64).primary==std::int32_t{4096}*-32768,
        "minimum bounded sum does not overflow");
    maximum_groups.push_back({1,{}, {1}});
    unresolved(inspect_property_object_assets(maximum,definitions,maximum_groups,true,64),R::inventory_budget);
    check(std::string(property_object_asset_bucket_name(B::primary))=="primary" &&
        std::string(property_object_assets_reason_name(R::missing_flags))=="missing_flags","diagnostic names");
    std::cout<<"object asset bucket order, current identity, complete partition, spatial and signed bounds passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
