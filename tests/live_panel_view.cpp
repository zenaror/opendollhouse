// SPDX-License-Identifier: MPL-2.0
#include "live_panel_view.hpp"
#include "buy_mode_view.hpp"
#include <algorithm>
#include <stdexcept>
#include <iostream>
using namespace odh_host;
namespace {void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(){try{
    LivePanelSnapshot snapshot;
    require(live_panel_capabilities(snapshot)==std::array<bool,6>{true,false,false,false,false,false},
        "actorless lot has no invented person panels");
    snapshot.has_selected_person=true;
    snapshot.skills={0,100,200,300,400,1000};
    snapshot.personality={700,600,300,500,200};
    snapshot.relationships=std::vector<LiveRelationshipInfo>{};
    require(live_panel_capabilities(snapshot)==std::array<bool,6>{true,true,true,true,false,false},
        "real selected-person skills/personality and known empty relationship list enable their panels");
    snapshot.personality[4].reset();snapshot.skills[5].reset();snapshot.relationships.reset();
    require(!live_panel_capabilities(snapshot)[1]&&!live_panel_capabilities(snapshot)[2]&&!live_panel_capabilities(snapshot)[3],
        "incomplete source data stays unavailable instead of filling zero bars");
    snapshot.career=LiveCareerInfo{};
    require(live_panel_capabilities(snapshot)[2],"unemployed career is real metadata even without all skills");
    snapshot.has_selected_person=false;
    require(!live_panel_capabilities(snapshot)[2],"retired selection cannot retain previous person's career");
    for(const auto value:{-32768,-1,0,99,100,999,1000,32767}){
        const auto expected=value<0?0u:value>1000?10u:static_cast<unsigned>(value/100);
        require(live_panel_points(value)==expected,"raw skill/personality saturates only while painting complete points");
    }
    require(live_skill_indices==std::array<unsigned,6>{10,12,11,17,18,15}&&
        live_personality_indices==std::array<unsigned,5>{7,6,3,5,2},
        "native data extraction order matches pinned TS1 reference instead of enum numeric order");
    require(live_interest_indices==std::array<unsigned,19>{46,47,48,49,50,51,52,53,46,47,48,49,13,14,16,20,26,54,55},
        "all nineteen original topics include the TS1 aliases absent from the TSO enum");
    const std::vector<unsigned> adult_topics{0,1,2,3,4,5,6,7,12,13,14,15,16,17,18};
    const std::vector<unsigned> child_topics{8,9,10,11,4,5,6,7};
    for(const auto age:{0,18,27,32767})require(live_interest_topics(static_cast<std::int16_t>(age))==adult_topics,
        "original age0 and adult ages use fifteen adult topics");
    for(const auto age:{-1,1,7,17})require(live_interest_topics(static_cast<std::int16_t>(age))==child_topics,
        "original nonzero age below18 uses the eight child topics");
    for(const auto age:{0,7,18})for(const auto width:{320u,800u,1024u}){
        const auto common=original_control_panel_layout(width,600);
        const auto first=live_interest_layout(common,static_cast<std::int16_t>(age),0);
        require(first.page==0&&first.pages>0&&!first.cells.empty(),"real topic layout exposes a first page at each supported viewport");
        std::vector<unsigned> observed;
        for(std::size_t page=0;page<first.pages;++page){
            const auto layout=live_interest_layout(common,static_cast<std::int16_t>(age),page);
            require(layout.page==page&&!layout.cells.empty(),"every advertised page contains actual topics");
            for(const auto& cell:layout.cells){
                observed.push_back(cell.topic);
                const bool separated=cell.topic<19&&cell.icon.width>0&&cell.icon.height>0&&cell.bar.width>0&&cell.bar.height>0&&
                    cell.icon.x+cell.icon.width<=cell.bar.x&&cell.icon.x>=layout.previous.x+layout.previous.width&&cell.bar.x+cell.bar.width<=layout.next.x;
                if(!separated)throw std::runtime_error("interest geometry overlaps: age="+std::to_string(age)+
                    " width="+std::to_string(width)+" page="+std::to_string(page)+" topic="+std::to_string(cell.topic)+
                    " prevRight="+std::to_string(layout.previous.x+layout.previous.width)+
                    " icon="+std::to_string(cell.icon.x)+":"+std::to_string(cell.icon.x+cell.icon.width)+
                    " bar="+std::to_string(cell.bar.x)+":"+std::to_string(cell.bar.x+cell.bar.width)+
                    " nextLeft="+std::to_string(layout.next.x));
            }
        }
        require(observed==live_interest_topics(static_cast<std::int16_t>(age)),"pagination exposes each applicable real topic once, in original column-major order");
        require(live_interest_layout(common,static_cast<std::int16_t>(age),std::size_t(-1)).page==first.pages-1,
            "out-of-range retained page clamps safely after viewport or age changes");
    }
    const auto original_interests=live_interest_layout(original_control_panel_layout(1024,768),27,0);
    require(original_interests.pages==2&&original_interests.cells.size()==9&&
        original_interests.cells[0].icon.x==533&&original_interests.cells[0].icon.y==690&&
        original_interests.cells[0].icon.width==75&&original_interests.cells[0].bar.x==613&&
        original_interests.cells[0].bar.y==695&&original_interests.cells[3].icon.x==661,
        "original runtime measurement retains the complete three-picture illustration for one topic");
    snapshot.has_selected_person=true;snapshot.interests=LiveInterestsInfo{};
    require(live_panel_capabilities(snapshot)[4],"actual interest snapshot enables its original tab");
    snapshot.interests.reset();require(!live_panel_capabilities(snapshot)[4],"missing interest provider cannot enable an empty tab");
    snapshot.has_selected_person=false;snapshot.house=LiveHouseInfo{};
    require(!live_panel_capabilities(snapshot)[5],"empty House DTO cannot enable an invented panel");
    snapshot.house->area_square_feet=0;
    require(live_panel_capabilities(snapshot)[5],"known zero area enables real House information without a selected person");
    snapshot.house->area_square_feet.reset();snapshot.house->ratings[4]=0;
    require(live_panel_capabilities(snapshot)[5],"known zero rating differs from absent House authority");
    snapshot.house.reset();require(!live_panel_capabilities(snapshot)[5],"house authority removal disables the panel");
    require(house_display_area(1089,original_house_area_units(14))==101&&
        house_display_area(1845,original_house_area_units(14))==171&&
        house_display_area(1089,original_house_area_units(1))==1089&&
        house_display_area(1089,original_house_area_units(2))==1089&&
        house_display_area(0,original_house_area_units(14))==0,
        "House metric conversion follows original locale and rounded float32 formula without changing source area");
    for(unsigned language=3;language<=20;++language)
        require(original_house_area_units(language)==HouseAreaUnits::square_metres,"non-English locales use original metric policy");
    const auto house=live_house_layout(original_control_panel_layout(1024,768));
    const int authored_y[]={674,692,710,728,747};
    for(unsigned row=0;row<5;++row)require(house.rating_bars[row].x==679&&house.rating_bars[row].y==authored_y[row]&&
        house.rating_bars[row].width==40&&house.rating_bars[row].height==16&&
        house.rating_labels[row].x+house.rating_labels[row].width==676,
        "original House meters retain authored source offsets and three-pixel label gap");
    for(unsigned row=0;row<4;++row)require(house.values[row].x==522&&house.values[row].y==686+int(row)*20,
        "original House area/room/classification text follows the authored left column");
    std::cout<<"live panel snapshot contracts passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
