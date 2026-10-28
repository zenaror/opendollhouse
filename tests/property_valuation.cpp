#include "odh/property_valuation.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
odh::DefinitionCandidate definition(std::uint32_t guid,std::uint16_t type,std::uint16_t build) {
    odh::ObjectDefinition d{};d.version=138;d.guid=guid;d.object_type=type;d.build_mode_type=build;
    return {"fixture.far","fixture.iff",static_cast<std::uint16_t>(guid),d};
}
void object(odh::World& world,std::int16_t id,std::uint32_t guid,std::int16_t value,std::int16_t flags=0) {
    check(world.create(id,odh::EntityKind::object) && world.define_type(id,guid) &&
        world.declare_object_word(id,41,value) && world.declare_object_word(id,43,flags) &&
        world.declare_object_word(id,15,0) && world.set_position(id,{16,16,1,0}),"native fixture object");
}
void write(odh::World& world,std::int16_t id,std::int16_t word,std::int16_t value) {
    const odh::VmFrame frame{odh::routine_key(4096),0,{id,id,id,0},{},{}};
    check(world.write(frame,3,word,value),"native fixture live write");
}
odh::SavedArchitecture architecture() {
    odh::SavedArchitecture a;a.width=a.height=4;
    for(auto& level:a.levels){level.floors.resize(16);level.walls.resize(16);level.flags=std::vector<std::uint8_t>(16,0);}
    a.pools=std::vector<std::uint8_t>(16,255);a.levels[0].floors[5]=1;return a;
}
void rejected(const odh::PropertyValuation& result,odh::PropertyValuationReason reason) {
    check(result.status==odh::PropertyValuationStatus::unresolved && result.reason==reason &&
        !result.combined_architecture && !result.scaled_architecture && !result.property_value,"rejection exposes no financial fields");
}
}
int main(){try{
    using namespace odh;using R=PropertyValuationReason;
    const DefinitionIndex definitions({definition(1,4,0),definition(2,4,3),definition(3,7,0)});
    World world;object(world,1,1,100);object(world,2,2,7);object(world,3,1,777,16);object(world,4,3,888);
    std::vector<SavedMultitileGroup> groups{{1,{}, {1}},{2,{}, {2}},{3,{}, {3}},{4,{}, {4}}};
    auto a=architecture();PropertyArchitecturePrices p;p.floors[1]=3;
    const auto inspect=[&](std::int32_t base=1000,std::uint32_t multiplier=0x3f333333U,bool complete=true,
        PropertyValuationProfile profile=PropertyValuationProfile::comparison_pc24) {
        return inspect_property_valuation({world,definitions,groups,complete,a,p,base,multiplier,profile});
    };
    auto result=inspect();
    check(result.status==PropertyValuationStatus::resolved && result.objects.primary==100 &&
        result.objects.architecture_objects==7 && result.geometry.total==3 && result.combined_architecture==10 &&
        result.scaled_architecture==7 && result.property_value==1107,"native components combined before PC24 scale then lot+primary");
    check(result.scale_reason==PropertyValueScaleReason::none,"explicit scale diagnostic");
    write(world,2,41,0);p.floors[1]=5;
    check(inspect().scaled_architecture==3,"positive fractional truncation");
    p.floors[1]=-5;result=inspect();
    check(result.scaled_architecture==-3 && result.property_value==1097,"negative component truncates towardzero");
    write(world,2,41,-7);p.floors[1]=-3;
    check(inspect().scaled_architecture==-7,"negative combined architecture PC24 round before truncation");
    write(world,2,41,7);p.floors[1]=3;
    rejected(inspect(1000,0x3f333333U,true,static_cast<PropertyValuationProfile>(0)),R::unsupported_profile);
    rejected(inspect(1000,0x3f333333U,true,static_cast<PropertyValuationProfile>(99)),R::unsupported_profile);
    auto omitted=PropertyValuationInput{world,definitions,groups,true,a,p,1000,0x3f333333U};
    rejected(inspect_property_valuation(omitted),R::unsupported_profile);
    p.floors.clear();result=inspect();rejected(result,R::architecture_unresolved);
    check(result.geometry.reason==PropertyArchitectureValueReason::incomplete_floor_lookup && !result.scale_reason,"missing price retains component reason without scale");
    p.floors[1]=3;rejected(inspect(1000,0x3f333333U,false),R::object_assets_unresolved);
    auto missing=groups;missing.pop_back();
    result=inspect_property_valuation({world,definitions,missing,true,a,p,1000,0x3f333333U,PropertyValuationProfile::comparison_pc24});
    rejected(result,R::object_assets_unresolved);check(result.objects.reason==PropertyObjectAssetsReason::incomplete_inventory,"missing partition identity");
    World untyped;check(untyped.create(1,EntityKind::object),"untyped native object");
    const std::vector<SavedMultitileGroup> single{{1,{}, {1}}};
    result=inspect_property_valuation({untyped,definitions,single,true,a,p,1000,0x3f333333U,PropertyValuationProfile::comparison_pc24});
    rejected(result,R::object_assets_unresolved);check(result.objects.reason==PropertyObjectAssetsReason::missing_identity,"missing native identity");
    p.floors[1]=std::numeric_limits<std::int32_t>::max();rejected(inspect(),R::combined_architecture_overflow);
    write(world,2,41,-7);p.floors[1]=std::numeric_limits<std::int32_t>::min();
    rejected(inspect(),R::combined_architecture_overflow);
    write(world,2,41,7);p.floors[1]=3;
    result=inspect(1000,0x7f7fffffU);rejected(result,R::scale_unresolved);
    check(result.scale_reason==PropertyValueScaleReason::out_of_range,"scale range reason");
    result=inspect(1000,0x7f800000U);rejected(result,R::scale_unresolved);
    check(result.scale_reason==PropertyValueScaleReason::nonfinite_multiplier,"nonfinite explicit profile rejected");
    rejected(inspect(std::numeric_limits<std::int32_t>::max()),R::property_value_overflow);
    write(world,1,41,-100);write(world,2,41,-7);p.floors[1]=-3;
    rejected(inspect(std::numeric_limits<std::int32_t>::min()),R::property_value_overflow);
    write(world,1,41,100);write(world,2,41,7);p.floors[1]=3;
    const auto before=world.snapshot();const auto geometry_before=a;const auto prices_before=p;const auto groups_before=groups;
    const auto definition_count=definitions.candidate_count();
    (void)inspect();const auto after=world.snapshot();
    check(after.word_count==before.word_count && after.generation_sequence==before.generation_sequence &&
        after.spatial_revision==before.spatial_revision && after.entities.size()==before.entities.size(),"World metadata unchanged");
    for(std::size_t n=0;n<before.entities.size();++n)
        check(after.entities[n].object_words==before.entities[n].object_words && after.entities[n].generation==before.entities[n].generation,
            "live banks/generations unchanged");
    for(std::size_t s=0;s<2;++s) {
        check(a.levels[s].floors==geometry_before.levels[s].floors && a.levels[s].flags==geometry_before.levels[s].flags,"geometry banks unchanged");
        for(std::size_t n=0;n<a.levels[s].walls.size();++n)
            check(a.levels[s].walls[n].segments==geometry_before.levels[s].walls[n].segments,"raw wall masks unchanged");
    }
    check(a.pools==geometry_before.pools && p.floors==prices_before.floors && p.wallpaper==prices_before.wallpaper &&
        p.structural==prices_before.structural && p.pool_tile_price==prices_before.pool_tile_price,"raw pool and prices unchanged");
    for(std::size_t n=0;n<groups.size();++n)
        check(groups[n].leader==groups_before[n].leader && groups[n].master_guid==groups_before[n].master_guid &&
            groups[n].members==groups_before[n].members,"partition unchanged");
    check(definitions.candidate_count()==definition_count && definitions.unique(2)->definition.build_mode_type==3,"definition index unchanged");
    check(std::string(property_valuation_reason_name(R::unsupported_profile))=="unsupported_profile","named reason");
    std::cout<<"native whole-property composition, explicit profile, failures and immutable inputs passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
