// SPDX-License-Identifier: MPL-2.0
#include "house_panel_finance.hpp"
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <cstring>
#include <limits>
namespace {
void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
template<class F>void rejects(F f){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error("unsafe House partition admitted");}
void put(std::string& s,std::uint32_t value,unsigned width=4){for(unsigned i=0;i<width;++i)s.push_back(static_cast<char>(value>>(8*i)));}
void at(std::string& s,unsigned offset,std::uint32_t value,unsigned width,bool big=false){for(unsigned i=0;i<width;++i)s.at(offset+i)=static_cast<char>(value>>(8*(big?width-i-1:i)));}
std::string named(float value){std::string s;put(s,0);put(s,1);s+="SNCF";put(s,1);
    const std::string key="architecture value multiplier";s+=key;s.push_back(0);if(key.size()%2==0)s.push_back(0);
    std::uint32_t bits;std::memcpy(&bits,&value,4);put(s,bits);s.push_back(0);s.push_back(0);return s;}
std::string chunk(const std::string& payload){std::string s(76,'\0');s.replace(0,4,"FCNS");at(s,4,static_cast<unsigned>(76+payload.size()),4,true);at(s,8,4,2,true);return s+payload;}
std::string iff(const std::vector<std::string>& chunks){std::string s="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";s.resize(64,'\0');for(const auto& c:chunks)s+=c;return s;}
odh_host::HousePropertyPolicy policy(const std::string& bytes,const odh::Simi& simi,
        odh::PropertyValuationProfile profile=odh::PropertyValuationProfile::comparison_pc24){
    std::istringstream input(bytes,std::ios::binary);return odh_host::read_house_property_policy(odh::View(input,bytes.size()),simi,profile);}
void property_quote(){
    using namespace odh;using namespace odh_host;
    Simi simi{};simi.lot_value=10000;simi.objects_value=32000;simi.architecture_value=-32000;
    auto selected=policy(iff({chunk(named(.7f))}),simi);
    need(selected.lot_base==10000&&selected.multiplier_bits==0x3f333333u&&!selected.multiplier_defaulted,
        "selected FCNS4 exact bits and only SIMI lot base supply policy");
    const auto fallback=policy(iff({}),simi);
    need(fallback.multiplier_defaulted&&fallback.multiplier_bits==selected.multiplier_bits,
        "only authenticated absent FCNS4 permits compiled original fallback");
    need(policy(iff({chunk(named(.5f))}),simi).multiplier_bits==0x3f000000u,
        "installation override remains actual binary32 value");
    rejects([&]{policy(iff({chunk(named(.7f)),chunk(named(.7f))}),simi);});
    rejects([&]{policy(iff({chunk("malformed")}),simi);});
    rejects([&]{policy(iff({chunk(named(std::numeric_limits<float>::infinity()))}),simi);});
    rejects([&]{policy(iff({}),simi,static_cast<PropertyValuationProfile>(0));});
    ObjectDefinition d{};d.version=138;d.guid=10;d.object_type=4;d.build_mode_type=0;
    DefinitionIndex definitions({{"fixture.far","fixture.iff",1,d}});
    World world;need(world.create_runtime_object(1,10,{0,0,0},{})&&world.set_position(1,{24,24,1,0})&&
        world.declare_object_word(1,41,150),"current valued object");
    SavedWorld original;SavedWorldEntity origin{};origin.saved.object_id=1;origin.saved.guid=10;
    origin.imported_generation=world.find(1)->generation;original.records.push_back(origin);
    SavedArchitecture terrain;terrain.width=terrain.height=4;terrain.pools=std::vector<std::uint8_t>(16,255);
    for(auto& level:terrain.levels){level.floors.resize(16);level.walls.resize(16);level.flags=std::vector<std::uint8_t>(16,0);}
    auto geometry=terrain;geometry.pools.reset();geometry.levels[0].floors[5]=1;
    PreparedArchitecture underlay;underlay.width=underlay.height=4;
    for(auto& level:underlay.levels)level.floors.resize(16);
    PropertyArchitecturePrices prices;prices.floors[1]=5;
    const std::vector<ObjectGroup> groups;const std::vector<WorldEntityReference> standalone;
    const auto input=[&]{return HousePropertyInput{world,original,definitions,groups,standalone,terrain,geometry,underlay,prices,selected};};
    const auto old_words=world.find(1)->object_words;const auto revision=world.spatial_revision();
    auto value=inspect_house_property(input());
    need(value.status==PropertyValuationStatus::resolved&&value.objects.primary==150&&value.geometry.total==5&&
        value.combined_architecture==5&&value.scaled_architecture==3&&value.property_value==10153,
        "shared owner recomputes live objects plus current edited geometry with source truncation");
    simi.objects_value=-999;simi.architecture_value=999;selected=policy(iff({chunk(named(.7f))}),simi);
    need(inspect_house_property(input()).property_value==value.property_value,
        "stale cached SIMI aggregates cannot change current valuation");
    prices.floors.clear();const auto missing=inspect_house_property(input());
    need(missing.status==PropertyValuationStatus::unresolved&&!missing.property_value,
        "missing actual material price never falls back to saved aggregates");
    need(world.spatial_revision()==revision&&world.find(1)->object_words==old_words,
        "quote leaves live simulation and cash-bearing banks unchanged");
}
}
int main(){try{
    property_quote();
    odh::World world;
    odh::ObjectDefinition d{};d.version=138;d.guid=10;d.object_type=4;d.build_mode_type=0;
    odh::DefinitionIndex definitions({{"objects.far","linked.iff",1,d}});
    for(std::int16_t id:{1,2})need(world.create(id,odh::EntityKind::object,{0,0,0,0})&&world.define_type(id,10),"fixture bodies");
    odh::SavedWorld source;
    for(std::int16_t id:{1,2}){odh::SavedWorldEntity row{};row.saved.object_id=id;row.saved.guid=10;row.imported_generation=world.find(id)->generation;source.records.push_back(row);}
    source.multitile_groups.push_back({1,{}, {1,2}});
    auto partition=odh_host::house_asset_partition(world,source,definitions,{});
    need(partition.size()==1&&partition[0].leader==1&&partition[0].members==std::vector<std::int16_t>{1,2},"actual linked group and root retained");
    odh::ObjectGroup current{0,true,2,{{1,world.find(1)->generation,0,0,0},{2,world.find(2)->generation,0,0,0}}};
    current.kind=odh::ObjectGroupKind::live_association;
    for(auto& member:current.members){odh::ObjectGroupMemberOrigin origin;
        origin.source_kind=odh::ObjectGroupMemberSource::original_import;origin.guid=10;
        origin.source="objects.far";origin.asset="linked.iff";origin.definition_chunk_id=1;member.origin=origin;}
    odh::validate_object_groups({{current},{}},world.snapshot());
    auto actual=odh_host::house_asset_partition(world,source,definitions,{current});
    need(actual.size()==1&&actual[0].leader==2&&actual[0].members==std::vector<std::int16_t>{1,2},
        "current complete topology owns imported members once and preserves actual current base");
    const odh::ObjectGroup split_a{10,false,1,{{1,world.find(1)->generation,0,0,0}}};
    const odh::ObjectGroup split_b{10,false,2,{{2,world.find(2)->generation,0,0,0}}};
    actual=odh_host::house_asset_partition(world,source,definitions,{split_a,split_b});
    need(actual.size()==2&&actual[0].members.size()==1&&actual[1].members.size()==1,
        "two complete current groups supersede the original joined topology without double counting");
    rejects([&]{odh_host::house_asset_partition(world,source,definitions,{split_a});});
    rejects([&]{odh_host::house_asset_partition(world,source,definitions,{current,split_a});});
    auto stale=current;++stale.members[0].generation;
    rejects([&]{odh_host::house_asset_partition(world,source,definitions,{stale});});
    auto missing=current;missing.members.push_back({3,1,0,0,0});
    rejects([&]{odh_host::house_asset_partition(world,source,definitions,{missing});});
    auto wrong=current;wrong.members[0].origin->guid=11;
    rejects([&]{odh_host::house_asset_partition(world,source,definitions,{wrong});});
    auto rootless=current;rootless.base_id=3;
    rejects([&]{odh_host::house_asset_partition(world,source,definitions,{rootless});});
    auto association=current;association.kind=odh::ObjectGroupKind::live_association;
    association.definition_guid=0;association.multitile=true;
    for(auto& member:association.members){odh::ObjectGroupMemberOrigin origin;origin.guid=10;member.origin=origin;}
    actual=odh_host::house_asset_partition(world,source,definitions,{association});
    need(actual.size()==1&&!actual[0].master_guid&&actual[0].leader==2,
        "current live association uses per-member identities without inventing a common master0");
    association.members[0].origin.reset();
    rejects([&]{odh_host::house_asset_partition(world,source,definitions,{association});});
    const auto old=world.find(2)->generation;
    need(world.remove(2),"retire member through actual World removal");
    rejects([&]{odh_host::house_asset_partition(world,source,definitions,{});});
    actual=odh_host::house_asset_partition(world,source,definitions,{split_a});
    need(actual.size()==1&&actual[0].leader==1,
        "current complete surviving topology supersedes a partly retired historical group");
    need(world.create(2,odh::EntityKind::object,{0,0,0,0})&&world.define_type(2,10)&&world.find(2)->generation!=old,"reused numeric ID");
    rejects([&]{odh_host::house_asset_partition(world,source,definitions,{});});
    need(world.remove(1),"retire old group completely");
    rejects([&]{odh_host::house_asset_partition(world,source,definitions,{});});
    const std::vector<odh::WorldEntityReference> standalone{{2,world.find(2)->generation}};
    partition=odh_host::house_asset_partition(world,source,definitions,{},standalone);
    need(partition.size()==1&&partition[0].leader==2&&partition[0].members.size()==1,"new authenticated singleton does not inherit old membership");
    odh::ObjectGroup dynamic{10,false,2,{{2,world.find(2)->generation,0,0,0}}};
    partition=odh_host::house_asset_partition(world,source,definitions,{dynamic});
    need(partition.size()==1&&partition[0].leader==2,"actual dynamic singleton root preserved");
    rejects([&]{odh_host::house_asset_partition(world,source,definitions,{dynamic,dynamic});});
    odh::SavedWorld incomplete;incomplete.records=source.records;incomplete.multitile_groups=source.multitile_groups;incomplete.multitile_groups.push_back({3,{}, {3}});
    rejects([&]{odh_host::house_asset_partition(world,incomplete,definitions,{});});
    odh::SavedArchitecture terrain;terrain.width=terrain.height=2;
    terrain.pools=std::vector<std::uint8_t>{255,1,255,255};terrain.water=std::vector<std::uint8_t>(4,255);
    auto geometry=terrain;geometry.pools.reset();geometry.water.reset();
    odh::PreparedArchitecture underlay;underlay.width=underlay.height=2;
    for(unsigned level=0;level<2;++level){
        terrain.levels[level].flags=std::vector<std::uint8_t>{0,0,32,0};
        geometry.levels[level].floors={1,65535,0,2};geometry.levels[level].walls.resize(4);
        underlay.levels[level].floors={1,7,0,2};
    }
    auto financial=odh_host::house_metric_architecture(terrain,geometry,underlay);
    need(financial.pools==terrain.pools&&financial.levels[0].flags==terrain.levels[0].flags&&
        financial.levels[0].floors[1]==7&&geometry.levels[0].floors[1]==65535,
        "financial aux/underlay are retained without reapplying overlays to rendering");
    geometry.levels[0].floors[1]=99;
    need(odh_host::house_metric_architecture(terrain,geometry,underlay).levels[0].floors[1]==99,"current floor edits win over immutable original underlay");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
