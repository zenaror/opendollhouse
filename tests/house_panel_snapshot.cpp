// SPDX-License-Identifier: MPL-2.0
#include "house_panel_snapshot.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace odh_host;
void need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
void le32(std::string& bytes,std::uint32_t value){for(unsigned n=0;n<4;++n)bytes.push_back(char(value>>(8*n)));}
std::string hous(std::uint32_t version=0x3e){
    std::string out;le32(out,0);le32(out,version);out+="SUOH";out.append(2,'\0');
    for(unsigned n=0;n<6;++n){le32(out,n);le32(out,10+n);}return out;
}
std::optional<HouseLayoutHistory> decode(const std::string& bytes){std::istringstream stream(bytes);return read_house_layout_history(odh::View(stream,bytes.size()));}
template<class F>void rejects(F fn,const char* reason){try{fn();}catch(const odh::FormatError&){return;}throw std::runtime_error(reason);}
void entity(odh::World& world,std::int16_t id,int room,int category,int impact,bool avatar=false){
    need(world.create(id,avatar?odh::EntityKind::avatar:odh::EntityKind::object,{0,avatar?88u:0u,0,avatar?16u:0u}),"create native entity");
    for(const auto item:{std::pair<int,int>{15,0},{39,0},{34,0},{29,room},{59,category},{7,impact}})
        need(world.declare_object_word(id,std::int16_t(item.first),std::int16_t(item.second)),"declare real object data");
    for(int field:{39,40,69})need(world.declare_definition_word(id,std::int16_t(field),field==39?1:0),"declare actual OBJD words");
    need(world.set_position(id,{room==1?0:16,room==1?0:16,1}),"position real entity");
}
odh::GeometricRoomMap rooms(){
    odh::GeometricRoomMap result;result.width=result.height=8;result.floor_resources_resolved=true;
    result.rooms.resize(3);for(unsigned n=0;n<3;++n)result.rooms[n].id=std::uint16_t(n);
    result.rooms[2].outside=true;
    // Twenty actual outdoor terrain contributions require enough outdoor cells;
    // the former2x2 fixture incorrectly supplied20 and failed closed correctly.
    result.levels[0].assign(64,0x00020002);
    result.levels[0][0]=result.levels[0][1]=result.levels[0][2]=0x00010001;
    result.levels[0][3]=0x00020001;
    result.levels[1].assign(64,0);return result;
}
}
int main(){try{
    need(house_lot_class(0)==2&&house_lot_class(39)==0&&house_lot_class(40)==1&&house_lot_class(49)==1&&
        house_lot_class(50)==2&&house_lot_class(64)==2&&!house_lot_class({})&&!house_lot_class(-1)&&!house_lot_class(3)&&!house_lot_class(65),
        "lot classification uses authenticated terrain dimension with original zero fallback and bounded profile");
    const auto imported=decode(hous());need(imported&&imported->past[0].samples==10&&imported->past[0].routing==0&&
        imported->current.samples==15&&imported->current.routing==5,"HOUS owns B,A serialized order and six real pairs");
    need(!decode(hous(0x25)),"old HOUS without history remains absent");
    auto bad=hous();bad.resize(61);rejects([&]{decode(bad);},"truncated last counter is rejected");
    bad=hous();bad[8]='X';rejects([&]{decode(bad);},"SIMI/other signature cannot supply history");
    bad=hous();bad[14]=99;rejects([&]{decode(bad);},"routing samples cannot exceed observed samples");
    auto fresh=new_house_layout_history(200);
    need(fresh.past[4].samples==200&&fresh.past[4].routing==200&&fresh.current.samples==0,
        "new-history fill200 is explicit and is not an absent-save fallback");
    auto rotation=*imported;rotate_house_layout_history(rotation);
    need(rotation.past[0].samples==15&&rotation.past[1].samples==10&&rotation.past[4].samples==10&&
        rotation.current.samples==0,"original forward-overlap rotation propagates previous first pair");
    odh::World world;entity(world,1,1,4,5,true);entity(world,2,1,5,0);
    entity(world,3,2,0,-2);entity(world,4,2,0,8);
    auto map=rooms();const auto tuning=original_house_panel_default_tuning();
    auto size=odh::parse_ts1_curve("(0;0) (150;25) (300;50) (600;75) (1200;100)");
    auto furnishings=odh::parse_ts1_curve("(5;0) (4;10) (3;20) (2;35) (1;60) (0;100)");
    odh::PropertyValuation valuation;valuation.status=odh::PropertyValuationStatus::resolved;
    valuation.objects.primary=1000;valuation.combined_architecture=2000;
    HouseLayoutHistory history;for(auto& pair:history.past)pair={10,1};history.current={10,1};
    HousePanelInput input{world,map,3};input.valuation=&valuation;input.size_curve=&size;
    input.furnishings_curve=&furnishings;input.tuning=&tuning;input.layout_history=&history;
    input.bindings.object_word=[](const odh::Entity& e,std::int16_t field)->std::optional<std::int16_t>{
        // This fixture explicitly owns a current Room29 bank; real host uses
        // the active geometric map after architecture/rebind operations.
        const auto at=e.object_words.find(field);return at==e.object_words.end()?std::optional<std::int16_t>{}:at->second;};
    input.bindings.definition_word=[](const odh::Entity& e,std::int16_t field)->std::optional<std::int16_t>{
        const auto at=e.definition_words.find(field);return at==e.definition_words.end()?std::optional<std::int16_t>{}:at->second;};
    input.bindings.outdoor_good_terrain_cells=[](std::uint16_t room)->std::optional<std::uint32_t>{return room==2?20:0;};
    const auto snapshot=prepare_house_panel_snapshot(input);
    need(snapshot.complete()&&snapshot.area_square_feet==27&&snapshot.bedrooms==1&&snapshot.bathrooms==1,
        "whole and diagonal halves use original per-room truncation and room category counts");
    need(snapshot.ratings==std::array<std::optional<std::int16_t>,5>{1,35,5,100,80},
        "all five ratings use independently authenticated native inputs and original curves");
    auto components=input;components.valuation=nullptr;
    odh::PropertyObjectAssets assets;assets.status=odh::PropertyObjectAssetsStatus::resolved;
    assets.primary=1000;assets.architecture_objects=500;
    odh::PropertyArchitectureValue architecture;architecture.status=odh::PropertyArchitectureValueStatus::resolved;architecture.total=1500;
    components.object_assets=&assets;components.architecture_value=&architecture;
    need(prepare_house_panel_snapshot(components).ratings[1]==35,"resolved native financial parts supply furnishings without final property price authority");
    architecture.status=odh::PropertyArchitectureValueStatus::unresolved;
    need(!prepare_house_panel_snapshot(components).ratings[1],"unresolved part cannot publish a partial financial total");
    auto missing_room=input;missing_room.bindings.object_word={};
    const auto without_room=prepare_house_panel_snapshot(missing_room);
    need(without_room.area_square_feet==27&&without_room.ratings[0]==1&&!without_room.bedrooms&&
        !without_room.bathrooms&&!without_room.ratings[2],"missing live room authority cannot read stored Room29 as current");
    // The bank still says bed1 and bathroom1. A rebind can place the bed
    // outside without rewriting that bank: only the current map may decide.
    auto rebound=input;const auto known_words=input.bindings.object_word;
    rebound.bindings.object_word=[known_words](const odh::Entity& e,std::int16_t field)->std::optional<std::int16_t>{
        if(field==29&&e.id==1)return 2;
        return known_words(e,field);
    };
    const auto moved=prepare_house_panel_snapshot(rebound);
    need(moved.bedrooms==0&&moved.bathrooms==1&&moved.area_square_feet==27&&world.find(1)->object_words.at(29)==1,
        "fresh room callback overrides stale stored room without mutating the World bank");
    auto absent=input;absent.layout_history=nullptr;absent.valuation=nullptr;absent.family_members.reset();absent.bindings.outdoor_good_terrain_cells={};
    const auto unknown=prepare_house_panel_snapshot(absent);
    need(!unknown.complete()&&!unknown.ratings[0]&&!unknown.ratings[1]&&!unknown.ratings[2]&&!unknown.ratings[4]&&unknown.ratings[3]==100,
        "missing history/finance/family/terrain remain absent while independent true values survive");
    auto zero_history=new_house_layout_history(0);input.layout_history=&zero_history;
    need(prepare_house_panel_snapshot(input).ratings[4]==100,"authenticated zero history is a real100 rather than unavailable");
    input.family_members=0;valuation.objects.primary=0;
    const auto zero=prepare_house_panel_snapshot(input);
    need(zero.ratings[0]==0&&zero.ratings[1]==0,"real zero denominators follow distinct original branches");
    input.family_members=3;valuation.objects.primary=1000;
    need(world.declare_object_word(2,39,1),"change actual dirt");
    need(prepare_house_panel_snapshot(input).ratings[3]==0,"dirty penalty6 uses eligibility count not dirty population");
    auto custom=tuning;custom.dirty_penalty=1;input.tuning=&custom;
    need(prepare_house_panel_snapshot(input).ratings[3]==75,"authenticated tuning overrides affect actual upkeep arithmetic");
    input.bindings.definition_word={};need(!prepare_house_panel_snapshot(input).ratings[3],"missing definition authority cannot invent eligible count");
    auto before=world.find(1)->person_data;HouseLayoutHistory observed;
    unsigned queries=0;const auto active=[&](odh::WorldEntityReference ref)->std::optional<bool>{++queries;need(ref.id==1&&ref.generation==world.find(1)->generation,"routing query retains exact entity generation");return true;};
    need(sample_house_layout_history(observed,world,1,active)&&queries==0,"only every tenth simulation tick samples");
    need(sample_house_layout_history(observed,world,10,active)&&observed.current.samples==1&&observed.current.routing==1,"awake visible resident counts active route");
    need(!sample_house_layout_history(observed,world,10,active)&&queries==1,"duplicate sampling never double counts");
    const auto unavailable=[](odh::WorldEntityReference)->std::optional<bool>{return {};};
    need(!sample_house_layout_history(observed,world,20,unavailable)&&observed.current.samples==1&&observed.last_sample_tick==10,
        "unknown route state preserves history atomically");
    need(world.set_motive(1,11,-1),"set actual sleeping state");
    need(sample_house_layout_history(observed,world,20,unavailable)&&observed.current.samples==1,"sleeping resident does not query or add route sample");
    need(world.find(1)->person_data==before,"panel and history sampling do not mutate person data");
    map.levels[0][0]=0x7fff7fff;
    const auto invalid=prepare_house_panel_snapshot(input);
    need(!invalid.area_square_feet&&!invalid.bedrooms&&!invalid.ratings[0]&&!invalid.ratings[2],"invalid room identity does not expose partial geometry metrics");
    std::cout<<"PASS original House ratings, real missing inputs, HOUS history and atomic sampling\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
