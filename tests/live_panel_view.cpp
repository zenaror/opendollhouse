// SPDX-License-Identifier: MPL-2.0
#include "live_panel_view.hpp"
#include "buy_mode_view.hpp"
#include <algorithm>
#include <stdexcept>
#include <iostream>
using namespace odh_host;
namespace {void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(){try{
    // Current native adapter owns these raw source words; age0 is school in
    // original career classification, independently of the Interests exception.
    for(const auto age:{0,7,17,18,40}){
        const auto source=live_career_source(int16_t(age),5,3);
        require(source.school==(age<18)&&source.job==(age<18?-1:5)&&
            source.level==(age<18?0:3)&&source.performance_word==(age<18?57u:63u),
            "school and ordinary career select distinct unchanged source words");
    }
    const LiveCareerLabelReader base=[](unsigned i)->std::optional<std::string>{
        if(i==3)return "Paramédico";if(i==23)return "Paramédica";if(i==4)return "Descrição real · 医療";return {};
    };
    const LiveCareerLabelReader panel=[](unsigned i)->std::optional<std::string>{
        if(i==3)return "Paramédico";if(i==23)return "Paramédico";return {};
    };
    require(live_career_title(base,panel,0,true,true)=="Paramédica"&&
            live_career_title(base,panel,0,true,false)=="Paramédico",
            "observed female panel caption and base-bank popup title remain distinct");
    require(live_career_title(base,panel,0,false,true)=="Paramédico"&&
            live_career_title(base,{},0,true,false)=="Paramédica",
            "male and absent alternate-title banks retain authenticated base captions");
    require(!live_career_title({}, {},0,true,true)&&!live_career_title(base,panel,10,true,true),
            "absent Work metadata and invalid levels cannot invent career text");
    const auto hours=live_career_hours("Expediente: $Start-$End.\nChegada: $CarTime.",23,5,14);
    require(hours=="Expediente: 23:00- 5:00.\nChegada: 22:00.","original schedule tokens preserve locale hours and carpool previous hour");
    require(live_career_hours("$Start/$CarTime",0,8,1)=="12:00 am/11:00 pm",
            "midnight carpool wraps with original twelve-hour locale convention");
    require(!live_career_hours("$Start",24,5,14)&&!live_career_hours("",23,5,14),
            "invalid or missing schedule sources never create a plausible time");
    LiveCareerSelection careerSelection;require(!careerSelection.toggle(0),"no selected source means no career popup");
    careerSelection.reconcile("sim1:job5:level0:lang14");
    require(careerSelection.toggle(0)&&careerSelection.selected()==0,"career icon opens its own consultation");
    require(!careerSelection.toggle(4)&&careerSelection.selected()==0,"additional original-only controls cannot invent popup actions");
    careerSelection.reconcile("sim1:job5:level0:lang14");require(careerSelection.selected()==0,"same live source keeps consultation open");
    require(careerSelection.toggle(0)&&!careerSelection.selected(),"same career icon toggles consultation closed");
    careerSelection.toggle(0);careerSelection.reconcile("sim2:job5:level0:lang14");
    require(!careerSelection.selected(),"Sim switch invalidates prior career consultation");
    for(const auto identity:{"sim2:job5:level1:lang14","sim2:job5:level1:lang1",""}){
        careerSelection.toggle(0);careerSelection.reconcile(identity);require(!careerSelection.selected(),"promotion/language/mode invalidates popup state");
    }
    careerSelection.reconcile("sim1:job5");
    require(!careerSelection.alert(true,100)&&!careerSelection.alert(true,432)&&careerSelection.alert(true,433)&&
            !careerSelection.alert(true,766),"negative performance alternates original green text palette every333ms");
    require(!careerSelection.alert(false,800)&&!careerSelection.alert(true,900)&&careerSelection.alert(true,1233),
            "nonnegative performance cancels the original caption alert and a new warning starts normally");
    careerSelection.reconcile("sim2:job5");require(!careerSelection.alert(true,1233)&&!careerSelection.alert(true,1200),
            "Sim replacement and a rewound presentation clock reset caption phase");
    const std::array<std::string,16> reportGrades{"10","9","9","8","7","7","6","5","5","4","3","3","2","2","1 (perigo)","0 (internato)"};
    const LiveCareerLabelReader reportReader=[&](unsigned i)->std::optional<std::string>{return i<reportGrades.size()?std::optional<std::string>{reportGrades[i]}:std::nullopt;};
    for(unsigned i=0;i<reportGrades.size();++i)require(live_school_grade_label(int16_t(i),reportReader)==reportGrades[i],
        "all school raw values address selected Work4097 directly instead of ordinary-career thresholds");
    require(!live_school_grade_label(-1,reportReader)&&!live_school_grade_label(16,reportReader)&&!live_school_grade_label(4,{}),
        "invalid or unavailable school grade metadata cannot invent or clamp a grade");
    for(const auto viewport:{std::array<unsigned,2>{800,600},{1024,768},{1280,720}}){
        const auto school=live_school_layout(original_control_panel_layout(viewport[0],viewport[1]));
        require(school.title.x==520&&school.title.y==int(viewport[1])-100&&
            school.grade.x==520&&school.grade.width==int(viewport[0])-520&&school.grade.height==24&&
            school.grade.y==int(viewport[1])-62,"original report card centers its whole-width24px grade control in the100px panel");
        require(!school.grade.contains(540,int(viewport[1])-85)&&school.grade.contains((520+int(viewport[0]))/2,int(viewport[1])-50),
            "former adult career icon is not a school hit; the actual central grade is the school consumer");
    }
    careerSelection.reconcile("school:sim1");require(careerSelection.show(0)&&careerSelection.show(0)&&careerSelection.selected()==0,
        "school grade reselects its consultation without inventing an automatic close on second click");
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
    for(const auto pair:{std::pair<std::int16_t,unsigned>{-32768,0},{-61,0},{-60,1},{-31,1},
                         {-30,2},{0,2},{29,2},{30,3},{32,3},{59,3},{60,4},{32767,4}})
        require(live_job_performance_grade(pair.first)==pair.second,"original performance grade boundaries retain inclusivity without altering raw data");
    const std::array<std::string,5> current_work_grades{"Baixo","Precisa Melhorar","Satisfatório","Bom","Excelente"};
    require(live_job_performance_label(32,[&](unsigned grade)->std::optional<std::string>{return current_work_grades[grade];})=="Bom",
        "current Portuguese Work4096 resolves actual performance32 to Bom");
    const std::string mixed="Satisfatório · 良好";
    require(live_job_performance_label(-30,[&](unsigned grade)->std::optional<std::string>{return grade==2?std::optional<std::string>{mixed}:std::nullopt;})==mixed,
        "decoded mixed UTF8 label crosses the presentation boundary byte-for-byte");
    require(!live_job_performance_label(32,{})&&!live_job_performance_label(32,[](unsigned)->std::optional<std::string>{return {};}),
        "missing grade provider or resource entry does not invent a numeric or English fallback");
    for(const auto viewport:{std::pair<unsigned,unsigned>{800,600},{1024,768},{1280,720}}){
        const auto performance=live_panel_layout(original_control_panel_layout(viewport.first,viewport.second));
        const int dy=int(viewport.second)-768;
        require(performance.performance_icon.x==523&&performance.performance_icon.y==736+dy&&
            performance.performance_icon.width==13&&performance.performance_icon.height==12,
            "complete original performance arrow retains independently matched pixels at all three desktop sizes");
        require(performance.performance_title.x==539&&performance.performance_title.y==735+dy&&
            performance.performance_title.width==79&&performance.performance_title.height==15&&
            performance.performance_title.x==performance.performance.x+16,
            "grade caption retains the original control width and sixteen-pixel image inset");
        require(performance.skill_bars[0].x==677&&performance.skill_bars[0].y==675+dy,
            "original skill off-background match retains full forty-pixel strip instead of a one-pixel shifted consumer");
    }
    for(const auto value:{-32768,-1,0,99,100,999,1000,32767}){
        const auto expected=value<0?0u:value>1000?10u:static_cast<unsigned>(value/100);
        require(live_panel_points(value)==expected,"raw skill/personality saturates only while painting complete points");
    }
    require(live_skill_indices==std::array<unsigned,6>{10,12,11,17,18,15}&&
        live_personality_indices==std::array<unsigned,5>{7,6,3,5,2},
        "native data extraction order matches pinned TS1 reference instead of enum numeric order");
    require(live_interest_indices==std::array<unsigned,19>{46,47,48,49,50,51,52,53,46,47,48,49,13,14,16,20,26,54,55},
        "all nineteen original topics include the TS1 aliases absent from the TSO enum");
    // The desktop adapter and old facade share this reader contract. Raw
    // interests stay0..10 source words; rendering owns saturation, never extraction.
    std::array<std::int16_t,101> person{};
    for(unsigned n=0;n<person.size();++n)person[n]=std::int16_t(int(n)*3-10);
    const auto before=person;
    for(const auto age:{0,7,17,18}){
        person[58]=std::int16_t(age);
        const auto interests=live_interests_snapshot([&](unsigned index)->std::optional<std::int16_t>{
            return index<person.size()?std::optional<std::int16_t>{person[index]}:std::nullopt;
        });
        require(interests&&interests->age==age,"selected age and complete source bank produce one interests snapshot");
        for(unsigned topic=0;topic<19;++topic)
            require(interests->raws[topic]==person[live_interest_indices[topic]],"all original topic aliases retain actual unmodified source words");
        for(unsigned topic=0;topic<4;++topic)
            require(interests->raws[topic]==interests->raws[topic+8],"adult/child alias pairs read the same physical word");
    }
    for(const auto absent:{13u,14u,16u,20u,26u,46u,47u,48u,49u,50u,51u,52u,53u,54u,55u,58u}){
        require(!live_interests_snapshot([&](unsigned word)->std::optional<std::int16_t>{
            return word==absent?std::nullopt:std::optional<std::int16_t>{person[word]};
        }),"missing required interest or age never fabricates an enabled panel");
    }
    require(!live_interests_snapshot({}),"missing source reader remains unavailable");
    for(unsigned n=0;n<person.size();++n)if(n!=58)require(person[n]==before[n],"snapshot extraction never writes person data");
    const std::vector<unsigned> adult_topics{0,1,2,3,4,5,6,7,12,13,14,15,16,17,18};
    const std::vector<unsigned> child_topics{8,9,10,11,4,5,6,7};
    for(const auto age:{0,18,27,32767})require(live_interest_topics(static_cast<std::int16_t>(age))==adult_topics,
        "original age0 and adult ages use fifteen adult topics");
    for(const auto age:{-1,1,7,17})require(live_interest_topics(static_cast<std::int16_t>(age))==child_topics,
        "original nonzero age below18 uses the eight child topics");
    for(const auto age:{0,7,18})for(const auto width:{320u,800u,1024u,1280u}){
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
