// SPDX-License-Identifier: MPL-2.0
#include "house_panel_metrics.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace odh_host;
void need(bool ok,const char* label){if(!ok)throw std::runtime_error(label);std::cout<<"PASS "<<label<<'\n';}
int main(){try{
    need(house_panel_is_home(1,1,0,false,-1)&&house_panel_is_home(1,1,{},false,-1),"FAMI home identity works for classic saves without SIMI35 or zoning");
    need(!house_panel_is_home(21,1,0,false,-1)&&!house_panel_is_home(1,1,1,false,-1),"destination or explicitly nonresidential lot cannot expose home panel");
    need(!house_panel_is_home(1,1,0,true,-1)&&!house_panel_is_home(1,1,0,false,21),"visit and pending travel suppress home panel");
    need(!house_panel_is_home(0,0,0,false,-1)&&!house_panel_is_home(100,100,0,false,-1),"unsupported home identity fails closed");
    odh::GeometricRoomMap map;map.width=map.height=8;map.floor_resources_resolved=true;
    map.rooms.resize(3);for(unsigned n=0;n<3;++n)map.rooms[n].id=n;map.rooms[2].outside=true;
    map.levels[0].assign(64,0x00020002);map.levels[0][0]=map.levels[0][1]=map.levels[0][2]=0x00010001;
    map.levels[0][3]=0x00020001;map.levels[1].assign(64,0);
    const auto tuning=original_house_panel_default_tuning();
    auto size=odh::parse_ts1_curve("(0;0) (150;25) (300;50) (600;75) (1200;100)");
    auto furnishings=odh::parse_ts1_curve("(5;0) (4;10) (3;20) (2;35) (1;60) (0;100)");
    HousePanelMetrics input{map};input.family_members=3;input.size_curve=&size;input.furnishings_curve=&furnishings;
    input.furnishings_ratio=2.f;input.tuning=&tuning;input.objects_complete=input.object_words_current=true;
    for(int i=0;i<4;++i){HousePanelObjectMetrics row;row.positioned=true;row.hidden=0;row.room=i<2?1:2;
        row.category=i==0?4:i==1?5:0;row.impact=i==2?-2:i==3?8:0;row.repair=row.dirty=0;row.upkeep_definition_words={1,0,0};input.objects.push_back(row);}
    input.outdoor_good_terrain_cells=[](uint16_t room)->std::optional<uint32_t>{return room==2?20:0;};
    HousePanelLayoutMetrics history;history.current={10,1};for(auto& pair:history.past)pair={10,1};input.layout_history=history;
    const auto complete=prepare_house_panel_metrics(input);
    need(complete.complete()&&complete.area_square_feet==27&&complete.bedrooms==1&&complete.bathrooms==1,"DTO original counts incl diagonal halves");
    need(complete.ratings==std::array<std::optional<int16_t>,5>{1,35,5,100,80},"DTO five exact original sample ratings");
    auto absence=input;absence.furnishings_ratio.reset();absence.layout_history.reset();
    const auto partial=prepare_house_panel_metrics(absence);
    need(partial.ratings[0]==1&&!partial.ratings[1]&&partial.ratings[2]==5&&partial.ratings[3]==100&&!partial.ratings[4],"independent source availability leaves only finance/history absent");
    auto bad_finance=input;bad_finance.furnishings_ratio=std::numeric_limits<float>::quiet_NaN();
    need(!prepare_house_panel_metrics(bad_finance).ratings[1],"nonfinite supplied finance ratio stays absent");
    auto dirty=input;dirty.objects[1].dirty=1;
    need(prepare_house_panel_metrics(dirty).ratings[3]==0,"actual dirty word uses authenticated penalty6");
    auto custom=tuning;custom.dirty_penalty=1;dirty.tuning=&custom;
    need(prepare_house_panel_metrics(dirty).ratings[3]==75,"selected tuning overrides default without changing raw object");
    auto broken=input;broken.objects[1].repair=1;broken.objects[1].dirty=1;
    need(prepare_house_panel_metrics(broken).ratings[3]==0,"broken penalty takes precedence over dirt");
    auto missing=input;missing.objects[0].upkeep_definition_words[0].reset();
    need(!prepare_house_panel_metrics(missing).ratings[3],"missing own definition eligibility stays unknown");
    auto incomplete=input;incomplete.objects_complete=false;const auto blocked=prepare_house_panel_metrics(incomplete);
    need(blocked.area_square_feet==27&&!blocked.bedrooms&&!blocked.ratings[2]&&!blocked.ratings[3],"incomplete inventory cannot publish partial room/yard/upkeep results");
    auto no_room=input;no_room.object_words_current=false;
    need(!prepare_house_panel_metrics(no_room).bedrooms&&prepare_house_panel_metrics(no_room).ratings[3]==100,"stale room authority cannot hide independent upkeep");
    auto no_terrain=input;no_terrain.outdoor_good_terrain_cells={};
    need(!prepare_house_panel_metrics(no_terrain).ratings[2],"missing pool topology never replaced with grass");
    auto no_tuning=input;no_tuning.tuning=nullptr;
    need(!prepare_house_panel_metrics(no_tuning).ratings[2]&&!prepare_house_panel_metrics(no_tuning).ratings[3],"unproved constants do not gain defaults in the kernel");
    auto zero=input;zero.family_members=0;zero.furnishings_ratio=100.f;zero.layout_history=HousePanelLayoutMetrics{};
    need(prepare_house_panel_metrics(zero).ratings[0]==0&&prepare_house_panel_metrics(zero).ratings[1]==0&&prepare_house_panel_metrics(zero).ratings[4]==100,"real zero denominators retain source branches");
    auto invalid=input;invalid.layout_history->current={1,2};
    need(!prepare_house_panel_metrics(invalid).ratings[4],"invalid layout history stays absent");
    need(house_lot_class(0)==2&&house_lot_class(39)==0&&house_lot_class(40)==1&&house_lot_class(49)==1&&house_lot_class(50)==2&&!house_lot_class(-1),"loaded SIMI dimension classification boundaries");
    map.levels[0][0]=0x7fff7fff;
    const auto bad=prepare_house_panel_metrics(input);
    need(!bad.area_square_feet&&!bad.ratings[0]&&!bad.ratings[2]&&bad.ratings[3]==100,"malformed geometry does not contaminate independent upkeep");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
