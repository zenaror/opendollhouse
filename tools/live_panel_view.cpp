// SPDX-License-Identifier: MPL-2.0
#include "live_panel_view.hpp"
#include "buy_mode_view.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace odh_host {
namespace {
constexpr std::size_t relationships_per_page=5;
template<std::size_t N> bool complete(const std::array<std::optional<std::int16_t>,N>& values) noexcept {
    return std::all_of(values.begin(),values.end(),[](const auto& value){return value.has_value();});
}
}
HouseAreaUnits original_house_area_units(std::uint8_t language){
    if(language<1||language>20)throw odh::FormatError("House area language outside authenticated range");
    return language==1||language==2?HouseAreaUnits::square_feet:HouseAreaUnits::square_metres;
}
std::uint32_t house_display_area(std::uint32_t square_feet,HouseAreaUnits units){
    if(units==HouseAreaUnits::square_feet)return square_feet;
    if(units!=HouseAreaUnits::square_metres)throw odh::FormatError("unknown House area units");
    // Original52e79d: rounded float32 conversion affects presentation only.
    float value=static_cast<float>(square_feet)*0.09290304034948349f;
    value+=0.5f;return static_cast<std::uint32_t>(value);
}
LiveCareerSource live_career_source(std::int16_t age,std::int16_t job,std::int16_t level)noexcept{
    // Original004eaa44/004ea85e: school owns CARR-1/level0 and uses PD57.
    return age<18?LiveCareerSource{-1,0,57,true}:LiveCareerSource{job,level,63,false};
}
std::optional<std::string> live_career_title(const LiveCareerLabelReader& base,const LiveCareerLabelReader& alternate,
        unsigned level,bool gender,bool panel){
    if(level>=10)return {};
    // Original observed panel uses the base bank; profession consultation
    // uses the admitted alternate bank, falling back to base when absent.
    const auto& reader=!panel&&alternate?alternate:base;
    if(!reader)return {};
    if(gender)if(auto text=reader(23+level);text&&!text->empty())return text;
    return reader(3+2*level);
}
std::optional<std::string> live_career_hours(std::string format,int start,int end,std::uint8_t language){
    if(format.empty()||start<0||start>=24||end<0||end>=24)return {};
    const auto time=[&](int hour){
        const bool twelve=language==1||language==2||language==19;const bool afternoon=hour>=12;
        if(twelve){if(!hour)hour=12;else if(hour>12)hour-=12;}
        auto value=(hour<10?std::string{" "}:std::string{})+std::to_string(hour)+":00";
        if(twelve)value+=afternoon?" pm":" am";
        return value;
    };
    for(const auto& pair:std::array<std::pair<std::string,std::string>,3>{{{"$Start",time(start)},{"$End",time(end)},{"$CarTime",time((start+23)%24)}}}){
        std::size_t at=0;while((at=format.find(pair.first,at))!=std::string::npos){format.replace(at,pair.first.size(),pair.second);at+=pair.second.size();}
    }
    return format;
}
std::optional<std::string> live_school_grade_label(std::int16_t raw,const LiveCareerLabelReader& reader){
    // Original4034e6 passes raw+1 to the one-based accessor4705f2: the
    // selected Work4097 index is raw itself, not a career threshold or clamp.
    if(raw<0||!reader)return {};return reader(unsigned(raw));
}
void LiveCareerSelection::reconcile(std::string identity){
    if(identity_!=identity){identity_=std::move(identity);selected_.reset();negative_since_.reset();}
}
bool LiveCareerSelection::toggle(unsigned control){
    if(identity_.empty()||control>=1)return false;
    if(selected_==control)selected_.reset();else selected_=control;return true;
}
bool LiveCareerSelection::show(unsigned control){
    if(identity_.empty()||control>=1)return false;selected_=control;return true;
}
bool LiveCareerSelection::alert(bool negative,std::uint64_t now)noexcept{
    if(!negative||identity_.empty()){negative_since_.reset();return false;}
    if(!negative_since_||now<*negative_since_)negative_since_=now;
    // Original5307ab schedules333ms;52c727 alternates the text palette.
    return ((now-*negative_since_)/333)%2!=0;
}
std::array<bool,6> live_panel_capabilities(const LivePanelSnapshot& snapshot) noexcept {
    return {true,snapshot.has_selected_person&&snapshot.relationships.has_value(),
        snapshot.has_selected_person&&(snapshot.career.has_value()||complete(snapshot.skills)),
        snapshot.has_selected_person&&complete(snapshot.personality),snapshot.has_selected_person&&snapshot.interests.has_value(),snapshot.house&&
            (snapshot.house->area_square_feet||snapshot.house->bedrooms||snapshot.house->bathrooms||
                (snapshot.house->lot_name&&!snapshot.house->lot_name->empty())||
                std::any_of(snapshot.house->ratings.begin(),snapshot.house->ratings.end(),[](const auto& v){return v.has_value();}))};
}
std::optional<LiveInterestsInfo> live_interests_snapshot(const LivePersonDataReader& read){
    if(!read)return {};
    const auto age=read(58);if(!age)return {};
    LiveInterestsInfo interests;interests.age=*age;
    for(std::size_t topic=0;topic<interests.raws.size();++topic){
        const auto raw=read(live_interest_indices[topic]);if(!raw)return {};
        interests.raws[topic]=*raw;
    }
    return interests;
}
std::vector<unsigned> live_interest_topics(std::int16_t age){
    // EXE0051e1a1:age0 follows the adult list, unlike some animation rules.
    if(age==0||age>=18)return {0,1,2,3,4,5,6,7,12,13,14,15,16,17,18};
    return {8,9,10,11,4,5,6,7};
}
unsigned live_job_performance_grade(std::int16_t raw) noexcept {
    return raw<-60?0u:raw<-30?1u:raw<30?2u:raw<60?3u:4u;
}
std::optional<std::string> live_job_performance_label(std::int16_t raw,const LiveCareerLabelReader& read){
    return read?read(live_job_performance_grade(raw)):std::nullopt;
}
unsigned live_panel_points(std::int32_t raw) noexcept {
    return static_cast<unsigned>(std::clamp(raw,0,1000)/100);
}
LivePanelView::LivePanelView(std::shared_ptr<const OriginalControlPanelArt> art,LivePanelText text)
    :art_(std::move(art)),text_(std::move(text)) {
    if(!art_)throw std::invalid_argument("live panel requires original control artwork");
}
bool LivePanelView::select(LivePanelTab tab,const LivePanelSnapshot& snapshot) noexcept {
    const auto index=static_cast<unsigned>(tab);
    if(index>=6||!live_panel_capabilities(snapshot)[index])return false;
    if(tab_!=tab){relationship_page_=0;interest_page_=0;}
    tab_=tab;return true;
}
void LivePanelView::reconcile(const LivePanelSnapshot& snapshot) noexcept {
    if(!live_panel_capabilities(snapshot)[static_cast<unsigned>(tab_)])tab_=LivePanelTab::needs;
    const auto size=snapshot.relationships?snapshot.relationships->size():0;
    relationship_page_=std::min(relationship_page_,size?(size-1)/relationships_per_page:0);
}
namespace {
odh::UiBitmapRect panel_rect(const OriginalControlPanelLayout& common,float x,float y,float w,float h){
    const auto s=common.scale;
    // Local authored coordinates relative to original content origin522,500.
    return {common.right_content.x+static_cast<int>(std::lround(x*s)),
        common.right_content.y+static_cast<int>(std::lround(y*s)),
        static_cast<int>(std::lround(w*s)),static_cast<int>(std::lround(h*s))};
}
void paint_rect(odh::RasterFrame& frame,const odh::UiBitmapRect& box,std::array<std::uint8_t,4> color){
    for(int y=std::max(0,box.y);y<std::min<int>(frame.height,box.y+box.height);++y)
        for(int x=std::max(0,box.x);x<std::min<int>(frame.width,box.x+box.width);++x)
            frame.pixels[static_cast<std::size_t>(y)*frame.width+static_cast<std::size_t>(x)]=color;
}
void skill_bar(odh::RasterFrame& frame,const OriginalControlPanelArt& art,const odh::UiBitmapRect& box,
        std::int32_t value,bool personality,std::optional<std::int32_t> required={}){
    const auto* off=art.find(personality?"cpanel\\backgrounds\\persbkg.bmp":"cpanel\\backgrounds\\jobsubbarsoff.bmp");
    const auto* on=art.find("cpanel\\jobsubbars.bmp");
    if(off)odh::blit_color_keyed_bitmap(frame,*off,box,std::nullopt,{255,0,255});
    const auto points=live_panel_points(value);
    if(on&&points){
        const auto width=static_cast<int>(points*4);
        auto target=box;target.width=box.width*static_cast<int>(points)/10;
        target.height=std::min(box.height,static_cast<int>(std::lround(box.width*11.0/40)));
        odh::blit_color_keyed_bitmap(frame,*on,target,odh::UiBitmapRect{0,0,width,11},{255,0,255});
    }
    // Required career skill is outlined using the game's authored highlight,
    // with no mutation of the actual skill or requirement.
    if(required&&*required>value)if(const auto* highlight=art.find("cpanel\\skillshilite.bmp")){
        const auto wanted=live_panel_points(*required);
        for(unsigned n=points;n<wanted;++n){auto target=box;target.x+=box.width*static_cast<int>(n)/10;target.width=std::max(1,box.width/10);
            odh::blit_color_keyed_bitmap(frame,*highlight,target,std::nullopt,{255,0,255});}
    }
}
// Original relationship portraits occupy plain40px cells, not the45px
// PeopleTemplate frames used by the household selector. The original's green
// portrait backdrop is observed, but its state/color rule is not yet sourced;
// leave the existing panel background rather than inventing a friendship tint.
void relationship_portrait(odh::RasterFrame& frame,const odh::BitmapImage& head,odh::UiBitmapRect box){
    if(head.pixels.size()!=std::uint64_t(head.width)*head.height)
        throw odh::FormatError("relationship portrait bitmap malformed");
    std::uint32_t left=head.width,top=head.height,right=0,bottom=0;
    for(std::uint32_t y=0;y<head.height;++y)for(std::uint32_t x=0;x<head.width;++x){
        const auto& pixel=head.pixels[std::size_t(y)*head.width+x];
        if((pixel[0]==255&&pixel[1]==0&&pixel[2]==255)||(head.bit_count==32&&!pixel[3]))continue;
        left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);
    }
    if(right<=left||bottom<=top)return;
    const auto scale=std::min(float(box.width)/(right-left),float(box.height)/(bottom-top));
    const auto width=static_cast<int>(std::lround((right-left)*scale));
    const auto height=static_cast<int>(std::lround((bottom-top)*scale));
    odh::blit_color_keyed_bitmap(frame,head,
        {box.x+(box.width-width)/2,box.y+(box.height-height)/2,width,height},
        odh::UiBitmapRect{static_cast<int>(left),static_cast<int>(top),static_cast<int>(right-left),static_cast<int>(bottom-top)},
        {255,0,255});
}
void relation_bar(odh::RasterFrame& frame,odh::UiBitmapRect box,std::int16_t value){
    paint_rect(frame,box,{60,60,102,255});
    const int extent=box.width*std::clamp(std::abs(int(value)),0,100)/100;
    box.width=extent;paint_rect(frame,box,value<0?std::array<std::uint8_t,4>{228,35,25,255}:std::array<std::uint8_t,4>{0,213,24,255});
}
}
LiveHouseLayout live_house_layout(const OriginalControlPanelLayout& common){
    LiveHouseLayout out;out.title=panel_rect(common,6,3,95,17);
    constexpr int ys[]={6,24,42,60,79};
    for(unsigned n=0;n<5;++n){
        out.rating_labels[n]=panel_rect(common,85,float(ys[n]+(n<4?1:0)),69,16);
        out.rating_bars[n]=panel_rect(common,157,float(ys[n]),40,16);
    }
    for(unsigned n=0;n<4;++n)out.values[n]=panel_rect(common,0,float(18+20*n),80,16);
    return out;
}
namespace {
std::string house_value_text(std::string format,std::string_view token,const std::string& value){
    const auto at=format.find(token);
    if(at!=std::string::npos)format.replace(at,token.size(),value);
    return format;
}
}
LiveSchoolLayout live_school_layout(const OriginalControlPanelLayout& common){
    // Original532d31..532d7e sizes the grade button to the whole school
    // container width and its font height, then centers it vertically.
    const auto x=common.right_content.x-int(std::lround(2*common.scale));
    const auto width=common.base.width-x;
    const auto height=int(std::lround(24*common.scale)); // Authored18pt control, measured against original grade ink.
    return {{x,common.right_content.y,width,int(std::lround(16*common.scale))},
            {x,common.right_content.y+(int(std::lround(100*common.scale))-height)/2,width,height}};
}
LivePanelLayout live_panel_layout(const OriginalControlPanelLayout& common){
    LivePanelLayout out;
    const auto r=[&](float x,float y,float w,float h){return panel_rect(common,x,y,w,h);};
    out.title=r(6,3,155,17);out.zodiac=r(6,72,95,25);
    // Personality measured in the running original at1024x768: bars679,676,
    // pitch18; the content origin is522,668, independent of desktop width.
    for(unsigned n=0;n<5;++n){out.personality_labels[n]=r(70,6+18*n,84,17);out.personality_bars[n]=r(157,8+18*n,40,14);}
    const auto authored_width=static_cast<int>(std::lround((common.base.width-common.right_content.x)/common.scale));
    const auto columns=std::clamp((authored_width-53)/42,1,32);
    for(int n=0;n<columns;++n){out.relationship_faces.push_back(r(35+42*n,17,40,40));
        out.relationship_daily.push_back(r(35+42*n,69,40,5));out.relationship_long_term.push_back(r(35+42*n,89,40,5));}
    out.previous=r(authored_width-22,21,9,49);out.next=r(authored_width-10,21,9,49);
    out.career_icon=r(1,3,28,22);
    out.career_title=r(2,24,130,18);out.salary=r(18,51,110,17);
    // Original1024x768 performance arrow matches the complete13x12 frame at
    //523,736. The original parent starts at520,668, two pixels before this
    //layout's content origin522,668. Its caption uses a16px image/text inset.
    out.performance=r(1,67,95,15);out.performance_icon=r(1,68,13,12);
    out.performance_title=r(17,67,79,15);
    for(unsigned n=0;n<6;++n){out.skill_labels[n]=r(70,4+15*n,83,14);out.skill_bars[n]=r(155,7+15*n,40,11);}
    return out;
}
LiveInterestLayout live_interest_layout(const OriginalControlPanelLayout& common,std::int16_t age,std::size_t page){
    LiveInterestLayout result;const auto topics=live_interest_topics(age);
    const auto width=static_cast<int>(std::lround((common.base.width-common.right_content.x)/common.scale));
    // Original cell125x25, column pitch128 and row pitch27. Running original
    //1024x768 places the first75px illustration at533,690 and its meter613,695.
    constexpr std::size_t rows=3;
    const auto columns=static_cast<std::size_t>(std::clamp((width-22)/128,1,16));
    const auto per_page=rows*columns;
    result.pages=(topics.size()+per_page-1)/per_page;
    result.page=std::min(page,result.pages-1);
    const auto first=result.page*per_page;
    for(std::size_t n=0;n<per_page&&first+n<topics.size();++n){
        const auto x=11+128*static_cast<int>(n/rows),y=22+27*static_cast<int>(n%rows);
        result.cells.push_back({topics[first+n],panel_rect(common,float(x),float(y),75,25),
            panel_rect(common,float(x+80),float(y+5),40,16)});
    }
    result.previous=panel_rect(common,2,36,9,49);
    result.next=panel_rect(common,float(width-11),36,9,49);
    // Edge rounding on reduced portable viewports must not overlap the first illustration.
    if(!result.cells.empty())result.previous.width=std::min(result.previous.width,std::max(1,result.cells.front().icon.x-result.previous.x));
    return result;
}
bool LivePanelView::click(const OriginalControlPanelLayout& common,std::int32_t x,std::int32_t y,const LivePanelSnapshot& snapshot){
    reconcile(snapshot);
    if(tab_==LivePanelTab::interests&&snapshot.interests){
        const auto layout=live_interest_layout(common,snapshot.interests->age,interest_page_);
        interest_page_=layout.page;
        if(layout.previous.contains(x,y)&&interest_page_){--interest_page_;return true;}
        if(layout.next.contains(x,y)&&interest_page_+1<layout.pages){++interest_page_;return true;}
        return false;
    }
    if(tab_!=LivePanelTab::relationships||!snapshot.relationships)return false;
    const auto layout=live_panel_layout(common);
    const auto columns=layout.relationship_faces.size();
    const auto last=snapshot.relationships->empty()?0:(snapshot.relationships->size()-1)/columns;
    relationship_page_=std::min(relationship_page_,last);
    if(layout.previous.contains(x,y)&&relationship_page_){--relationship_page_;return true;}
    if(layout.next.contains(x,y)&&relationship_page_<last){
        ++relationship_page_;return true;
    }
    return false;
}
void LivePanelView::draw(odh::RasterFrame& frame,const LivePanelSnapshot& snapshot,LivePanelTextPainter painter)const{
    if(tab_==LivePanelTab::needs||!live_panel_capabilities(snapshot)[static_cast<unsigned>(tab_)])return;
    if(frame.pixels.size()!=std::uint64_t(frame.width)*frame.height)throw odh::FormatError("live panel raster malformed");
    const auto common=original_control_panel_layout(frame.width,frame.height);
    const auto layout=live_panel_layout(common);
    const auto text=[&](std::string_view value,const odh::UiBitmapRect& box,bool title=false,LiveTextAlignment align=LiveTextAlignment::left){if(painter&&!value.empty())painter(frame,value,box,title,align);};
    if(tab_==LivePanelTab::house&&snapshot.house){
        const auto house_layout=live_house_layout(common);const auto& house=*snapshot.house;
        text(text_.house,house_layout.title,true);
        const std::array<std::optional<std::uint32_t>,3> counts{house.area_square_feet,house.bedrooms,house.bathrooms};
        for(unsigned n=0;n<3;++n)text(house_value_text(text_.house_value_formats[n],"%d",
            counts[n]?std::to_string(n==0?house_display_area(*counts[n],text_.house_area_units):*counts[n]):text_.house_missing),house_layout.values[n],false,LiveTextAlignment::right);
        text(house_value_text(text_.house_value_formats[3],"%s",house.lot_name?*house.lot_name:text_.house_missing),
            house_layout.values[3],false,LiveTextAlignment::right);
        for(unsigned n=0;n<5;++n){
            text(text_.house_rating_names[n],house_layout.rating_labels[n],false,LiveTextAlignment::right);
            const auto& box=house_layout.rating_bars[n];
            if(!house.ratings[n]){text(text_.house_missing,box,false,LiveTextAlignment::center);continue;}
            if(const auto* off=art_->find("cpanel\\backgrounds\\housesubbarsoff.bmp")){
                if(off->width!=40||off->height!=16)throw odh::FormatError("original House off meter dimensions differ");
                odh::blit_color_keyed_bitmap(frame,*off,box,std::nullopt,{255,0,255});
            }
            const unsigned points=static_cast<unsigned>(std::clamp<int>(*house.ratings[n],0,100)/10);
            if(points)if(const auto* on=art_->find("cpanel\\housesubbars.bmp")){
                if(on->width!=40||on->height!=16)throw odh::FormatError("original House meter dimensions differ");
                auto filled=box;filled.width=box.width*int(points)/10;
                odh::blit_color_keyed_bitmap(frame,*on,filled,odh::UiBitmapRect{0,0,int(points)*4,16},{255,0,255});
            }
        }
    }else if(tab_==LivePanelTab::personality){
        text(text_.personality,layout.title,true);
        for(unsigned n=0;n<5;++n){text(text_.personality_names[n],layout.personality_labels[n],false,LiveTextAlignment::right);
            skill_bar(frame,*art_,layout.personality_bars[n],*snapshot.personality[n],true);}
        if(snapshot.zodiac)text(*snapshot.zodiac,layout.zodiac,true);
    }else if(tab_==LivePanelTab::career&&snapshot.career&&snapshot.career->school){
        const auto school=live_school_layout(common);text(text_.report_card,school.title,true);
        if(snapshot.career->report_card&&snapshot.career->report_card->grade)
            text(*snapshot.career->report_card->grade,school.grade,false,LiveTextAlignment::center);
    }else if(tab_==LivePanelTab::career){
        text(text_.career,panel_rect(common,34,3,68,21),true);
        if(snapshot.career){const auto& career=*snapshot.career;
            if(career.job_type)if(const auto* icons=art_->find("cpanel\\buttons\\jobiconmultibutton.bmp")){
                const auto row=*career.job_type;
                if(icons->width==112&&icons->height%22==0&&row<icons->height/22)
                    odh::blit_color_keyed_bitmap(frame,*icons,panel_rect(common,1,3,28,22),odh::UiBitmapRect{0,int(row)*22,28,22},{255,0,255});
            }
            if(!career.employed){text(text_.unemployed,layout.career_title);
                text(text_.salary_unavailable,layout.salary);text(text_.performance_unavailable,layout.performance_title);
                if(art_->find("cpanel\\buttons\\salarybutton.bmp"))art_->draw_button(frame,"cpanel\\buttons\\salarybutton.bmp",panel_rect(common,1,54,13,11),0);
                if(art_->find("cpanel\\buttons\\performancebutton.bmp"))art_->draw_button(frame,"cpanel\\buttons\\performancebutton.bmp",layout.performance_icon,0);
            }
            else {if(career.title)text(*career.title,layout.career_title);
                if(career.salary){std::string salary="§"+std::to_string(*career.salary);
                    text(salary,layout.salary);
                    if(art_->find("cpanel\\buttons\\salarybutton.bmp"))art_->draw_button(frame,"cpanel\\buttons\\salarybutton.bmp",panel_rect(common,1,54,13,11),0);}
                if(career.performance){
                    // The original paints a localized grade, never the raw word.
                    // Missing selected Work metadata stays absent.
                    if(career.performance_label)text(*career.performance_label,layout.performance_title);
                    if(const auto* icon=art_->find("cpanel\\buttons\\performancebutton.bmp")){
                        if(icon->width!=52||icon->height!=12)throw odh::FormatError("original performance button dimensions differ");
                        art_->draw_button(frame,"cpanel\\buttons\\performancebutton.bmp",layout.performance_icon,0);
                    }
                }
            }
        }
        for(unsigned n=0;n<6;++n){text(text_.skill_names[n],layout.skill_labels[n],false,LiveTextAlignment::right);
            if(snapshot.skills[n])skill_bar(frame,*art_,layout.skill_bars[n],*snapshot.skills[n],false,
                snapshot.career?snapshot.career->required_skills[n]:std::nullopt);}
    }else if(tab_==LivePanelTab::interests&&snapshot.interests){
        text(text_.interests,layout.title);
        const auto interest_layout=live_interest_layout(common,snapshot.interests->age,interest_page_);
        for(const auto& cell:interest_layout.cells){
            // Each75px illustration contains three pictures of ONE topic.
            // It is not a three-state button strip and must not be divided.
            if(const auto* icon=art_->find(live_interest_icon_keys[cell.topic])){
                if(icon->width!=75||(icon->height!=25&&icon->height!=26))throw odh::FormatError("original interest illustration dimensions differ");
                auto target=cell.icon;target.height=static_cast<int>(std::lround(icon->height*common.scale));
                odh::blit_color_keyed_bitmap(frame,*icon,target,std::nullopt,{255,0,255});
            }
            if(const auto* off=art_->find("cpanel\\backgrounds\\interestbarsbackground.bmp"))
                odh::blit_color_keyed_bitmap(frame,*off,cell.bar,std::nullopt,{255,0,255});
            // The World/FreeSO PersonData contract retains NBRS interest
            // points (0..10), unlike hundredths used by skills/personality.
            // Original EXE UI reads its separate internal scaled bank; its
            // /100 operation must not be repeated on these native VM words.
            const auto points=static_cast<unsigned>(std::clamp<int>(snapshot.interests->raws[cell.topic],0,10));
            if(points)if(const auto* on=art_->find("cpanel\\interestbars.bmp")){
                auto target=cell.bar;target.width=cell.bar.width*static_cast<int>(points)/10;
                odh::blit_color_keyed_bitmap(frame,*on,target,odh::UiBitmapRect{0,0,static_cast<int>(points)*4,16},{255,0,255});
            }
        }
        if(interest_layout.page)art_->draw_button(frame,"cpanel\\buttons\\scrollleft.bmp",interest_layout.previous,0);
        if(interest_layout.page+1<interest_layout.pages)art_->draw_button(frame,"cpanel\\buttons\\scrollright.bmp",interest_layout.next,0);
    }else if(tab_==LivePanelTab::relationships&&snapshot.relationships){
        text(text_.relationships,layout.title);
        if(snapshot.relationships->empty()){text(text_.no_relationships,panel_rect(common,35,32,220,32));return;}
        const auto columns=layout.relationship_faces.size();
        const auto pages=(snapshot.relationships->size()-1)/columns;
        const auto page=std::min(relationship_page_,pages),first=page*columns;
        for(std::size_t n=0;n<columns&&first+n<snapshot.relationships->size();++n){const auto& person=(*snapshot.relationships)[first+n];
            if(person.portrait)relationship_portrait(frame,*person.portrait,layout.relationship_faces[n]);
            const auto number=[&](std::optional<std::int16_t> value,const odh::UiBitmapRect& bar){if(value){relation_bar(frame,bar,*value);auto label=bar;label.y-=std::max(1,int(std::lround(12*common.scale)));label.height=std::max(1,int(std::lround(12*common.scale)));text(std::to_string(*value),label,false,LiveTextAlignment::center);}};
            number(person.daily,layout.relationship_daily[n]);number(person.long_term,layout.relationship_long_term[n]);
        }
        art_->draw_button(frame,"cpanel\\buttons\\scrollleft.bmp",layout.previous,page?0:3);
        art_->draw_button(frame,"cpanel\\buttons\\scrollright.bmp",layout.next,page<pages?0:3);
    }
}
std::optional<std::string> LivePanelView::hover_text(const OriginalControlPanelLayout& common,std::int32_t x,std::int32_t y,const LivePanelSnapshot& snapshot)const{
    if(tab_==LivePanelTab::house&&snapshot.house){
        const auto layout=live_house_layout(common);
        for(unsigned n=0;n<5;++n)if(layout.rating_labels[n].contains(x,y)||layout.rating_bars[n].contains(x,y)){
            auto result=text_.house_rating_names[n];
            if(!text_.house_rating_descriptions[n].empty())result+="\n"+text_.house_rating_descriptions[n];
            if(!snapshot.house->ratings[n])result+="\n"+text_.house_missing;
            return result.empty()?std::optional<std::string>{}:std::optional<std::string>{std::move(result)};
        }
    }
    if(tab_==LivePanelTab::relationships&&snapshot.relationships&&!snapshot.relationships->empty()){
        const auto layout=live_panel_layout(common);const auto columns=layout.relationship_faces.size();
        const auto first=std::min(relationship_page_,(snapshot.relationships->size()-1)/columns)*columns;
        for(std::size_t n=0;n<columns&&first+n<snapshot.relationships->size();++n)
            if(layout.relationship_faces[n].contains(x,y)&&!(*snapshot.relationships)[first+n].name.empty())return (*snapshot.relationships)[first+n].name;
    }
    if(tab_==LivePanelTab::interests&&snapshot.interests){
        for(const auto& cell:live_interest_layout(common,snapshot.interests->age,interest_page_).cells)
            if(cell.icon.contains(x,y)||cell.bar.contains(x,y)){
                auto result=text_.interest_names[cell.topic];
                if(!text_.interest_descriptions[cell.topic].empty())result+="\n"+text_.interest_descriptions[cell.topic];
                return result;
            }
    }
    if(tab_==LivePanelTab::career&&snapshot.career&&snapshot.career->employed){
        const auto layout=live_panel_layout(common);const auto& career=*snapshot.career;
        if(layout.salary.contains(x,y)&&career.start_hour&&career.end_hour)
            return std::to_string(*career.start_hour)+":00–"+std::to_string(*career.end_hour)+":00";
    }
    return std::nullopt;
}
} // namespace odh_host
