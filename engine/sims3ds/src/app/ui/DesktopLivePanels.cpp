/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __3DS__
#include "app/ui/DesktopLivePanels.h"
#include "app/ui/OriginalTextBackend.h"
#include "app/ui/OriginalPCUI.h"
#include "app/ui/DesktopHousePanel.h"
#include "app/ui/OriginalTextEncoding.h"
#include "app/LotScreen.h"
#include "app/ui/UIGraphics.h"
#include "app/front/ResGraphics.h"
#include "app/front/TS1Font.h"
#include "catalog_info_text.hpp"
#include "content/ContentManager.h"
#include "content/TS1JobProvider.h"
#include "content/TS1PersonalText.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "simantics/model/VMStackObjectVariable.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include "files/formats/iff/chunks/BMP.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "files/formats/iff/chunks/STR.h"
#include "files/formats/iff/chunks/NBRS.h"
#include "original_control_panel.hpp"
#include "character_ui_images.hpp"
#include "linux_ui_strings.hpp"
#include "resource.hpp"
#include "original_live_help.hpp"
#include "linux_motive_panel.hpp"
#include "app/ui/OriginalPresentation.h"
#include "app/ui/OriginalNeedsPresentation.h"
#include "simantics/model/VMMotive.h"
#include "linux_text_codec.hpp"
#include "linux_text_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace simapp { namespace original {
namespace {
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF::Chunks;
using Neighborhood=FSO::SimAntics::Engine::Utils::VMTS1Neighbourhood;
struct PanelResources {
    std::string root;
    uint8_t language;
    std::shared_ptr<odh_host::OriginalControlPanelArt> art;
    odh_host::LivePanelText text;
    std::string needs_heading="Needs",pet_needs_heading;
    std::array<std::string,8> needs_help;
    std::array<std::string,8> pet_needs_help,pet_needs_labels;
    std::array<std::array<std::string,36>,2> personality_help;
    odh_host::LivePanelText pet_text;
    std::array<std::string,10> tab_tips;
    std::array<std::string,21> control_tips;
    std::array<std::string,8> needs_labels{{"Hunger","Comfort","Hygiene","Bladder","Energy","Fun","Social","Room"}};
    odh::RasterFrame surface;
    std::map<uint32_t,odh_host::CharacterUiImages> portraits;
    std::array<std::string,32> career_help;
    std::array<std::string,2> school_help;
    front::TS1Font school_grade_font;
    PanelResources(const std::string& r,uint8_t lang):root(r),language(lang),
        art(std::make_shared<odh_host::OriginalControlPanelArt>(r)) {
        odh_host::GameUiStrings labels(r,odh_host::GameUiStringResource::live_panels);
        auto label=[&](int table,int index,const std::string& fallback){
            return labels.get(table,index,lang,fallback.c_str());};
        needs_heading=label(130,0,needs_heading);pet_needs_heading=label(180,0,needs_heading);
        constexpr int needs_indexes[8]={1,5,9,13,3,7,11,15};
        for(size_t i=0;i<needs_labels.size();++i){
            needs_labels[i]=label(130,needs_indexes[i],needs_labels[i]);
            needs_help[i]=label(130,needs_indexes[i]+1,"");
        }
        for(unsigned i=0;i<8;++i){const auto h=*odh_host::original_needs_help(i,true);
            pet_needs_labels[i]=label(h.table,h.title,needs_labels[i]);
            pet_needs_help[i]=label(h.table,h.description,"");}
        for(unsigned bank=0;bank<2;++bank)for(unsigned i=0;i<36;++i)
            personality_help[bank][i]=label(bank?170:131,i,"");
        for(size_t i=0;i<tab_tips.size();++i)tab_tips[i]=label(134,i,"");
        odh_host::GameUiStrings controls(r);
        for(size_t i=0;i<control_tips.size();++i)control_tips[i]=controls.get(138,i,lang,"");
        text.report_card=label(139,0,"");school_help={label(139,1,""),label(139,2,"")};
        {
            ColdResourceStage fontStage("live-panel-font");
        school_grade_font.Load(front::ResGraphics::Get().Raw("Fonts/variablesans_18.ffn"));
        }
        text.career=label(136,0,text.career);text.unemployed=label(136,7,text.unemployed);
        text.salary_unavailable=label(136,8,"");text.performance_unavailable=label(136,9,"");
        for(unsigned i=0;i<career_help.size();++i)career_help[i]=label(137,i,"");
        text.personality=label(131,0,text.personality);text.relationships=label(132,0,text.relationships);
        text.interests=label(140,0,text.interests);
        for(unsigned i=0;i<19;++i){
            text.interest_names[i]=label(140,i+1,text.interest_names[i]);
            text.interest_descriptions[i]=label(142,i+1,"");
        }
        text.house=label(133,0,text.house);text.house_area_units=odh_host::original_house_area_units(lang);
        for(unsigned i=0;i<6;++i)text.skill_names[i]=label(136,i+1,text.skill_names[i]);
        for(unsigned i=0;i<5;++i){text.personality_names[i]=label(131,1+7*i,text.personality_names[i]);
            text.house_rating_names[i]=label(133,i+1,text.house_rating_names[i]);}
        for(unsigned i=0;i<4;++i)text.house_value_formats[i]=label(133,i+6,text.house_value_formats[i]);
        pet_text=text;pet_text.personality=label(170,0,text.personality);
        for(unsigned i=0;i<5;++i)pet_text.personality_names[i]=label(170,1+7*i,text.personality_names[i]);
    }
};
std::optional<std::string> selected_work_label(STR* strings,std::size_t index){
    if(!strings)return {};
    std::size_t language=TextLanguage()-1;
    const auto available=[&](std::size_t set){return set<strings->LanguageSets.size()&&index<strings->LanguageSets[set].Strings.size();};
    if(!available(language))language=0;
    if(!available(language))return {};
    const auto& label=strings->LanguageSets[language].Strings[index].Value;
    if(label.empty())return {};
    return PersonalToUTF8(label,uint8_t(language+1),strings->SourceFormat==-1||strings->SourceFormat==-4);
}
PanelResources& resources(){
    static std::unique_ptr<PanelResources> current;
    const auto root=FSO::Content::Content::Get()->SimsCompleteDir;const auto lang=TextLanguage();
    if(!current||current->root!=root||current->language!=lang){
        // Inclusive parent of art, localized text and font preparation.
        ColdResourceStage stage("live-panel-resources");
        current.reset(new PanelResources(root,lang));
    }
    return *current;
}

}
std::optional<odh::UiBitmapRect> DesktopNeedsPanelBounds(int width,int height,LotSession& session){
    const auto* sim=session.SelectedSim();if(!sim||sim->Dead)return {};
    auto& assets=resources();
    const auto common=odh_host::original_control_panel_layout(width,height);
    const auto geometry=odh_host::original_motive_panel_geometry(width,height,common,assets.language,
        assets.art->find("cpanel\\backgrounds\\tallsubpanel.tga"));
    return geometry.tall?std::optional<odh::UiBitmapRect>{geometry.panel}:std::nullopt;
}
void DrawDesktopNeedsPanel(Canvas& canvas,LotSession& session){
    DrawDesktopNeedsPanel(canvas,session,{});
}
void DrawDesktopNeedsPanel(Canvas& canvas,LotSession& session,const std::array<int,8>& change_arrows){
    auto* sim=session.SelectedSim();if(!sim||sim->Dead)return;
    auto& assets=resources();
    constexpr VMMotive order[7]={VMMotive::Hunger,VMMotive::Comfort,VMMotive::Hygiene,VMMotive::Bladder,VMMotive::Energy,VMMotive::Fun,VMMotive::Social};
    odh_host::StatusHud status;status.needs.emplace();
    for(size_t i=0;i<7;++i)(*status.needs)[i]=sim->GetMotiveData(order[i]);
    status.room_score=sim->GetMotiveData(VMMotive::Room);
    const auto layout=odh_host::original_control_panel_layout(canvas.Width,canvas.Height);
    const auto* tall_art=assets.art->find("cpanel\\backgrounds\\tallsubpanel.tga");
    const auto geometry=odh_host::original_motive_panel_geometry(canvas.Width,canvas.Height,layout,assets.language,tall_art);
    (void)DrawNeeds(canvas,assets.surface,status,assets.language,tall_art,layout,
        sim->IsPet()?&assets.pet_needs_labels:&assets.needs_labels,
        sim->IsPet()?&assets.pet_needs_heading:&assets.needs_heading,geometry,change_arrows);
}

void OfferDesktopControlHelp(Canvas& canvas,LotScreen& screen){
    auto& assets=resources();const auto common=odh_host::original_control_panel_layout(canvas.Width,canvas.Height);
    const auto offer=[&](unsigned index,const odh::UiBitmapRect& r){
        screen.OfferDesktopHover(assets.control_tips[index],"control:"+std::to_string(index),"controls",0,
            Rect(r.x,r.y,r.width,r.height));
    };
    offer(3,common.rotate_left);offer(4,common.rotate_right);offer(5,common.zoom_in);offer(6,common.zoom_out);
    offer(7,common.walls_up);offer(8,common.walls_cutaway);offer(9,common.walls_down);
    offer(10,common.modes[0]);offer(11,common.modes[1]);offer(12,common.modes[2]);offer(13,common.options);
    offer(15,common.pause);for(unsigned i=0;i<common.speeds.size();++i)offer(16+i,common.speeds[i]);
}

void OfferDesktopNeedsHelp(Canvas& canvas,LotScreen& screen){
    auto* sim=screen.S->SelectedSim();if(!sim||sim->Dead)return;
    auto& assets=resources();const auto common=odh_host::original_control_panel_layout(canvas.Width,canvas.Height);
    const auto geometry=odh_host::original_motive_panel_geometry(canvas.Width,canvas.Height,common,assets.language,
        assets.art->find("cpanel\\backgrounds\\tallsubpanel.tga"));
    const bool pet=sim->IsPet();
    for(unsigned i=0;i<geometry.labels.size();++i){const auto& r=geometry.labels[i];
        const auto help=*odh_host::original_needs_help(i,pet);
        screen.OfferDesktopHelp(pet?assets.pet_needs_labels[i]:assets.needs_labels[i],
            pet?assets.pet_needs_help[i]:assets.needs_help[i],"need:"+std::to_string(i),
            "needs",uint64_t(sim->ObjectID),Rect(r.x,r.y,r.width,r.height),{},help.objects);
    }
}
void OfferDesktopLiveTabHelp(Canvas& canvas,LotScreen& screen){
    auto& assets=resources();const auto common=odh_host::original_control_panel_layout(canvas.Width,canvas.Height);
    const unsigned indices[]={0,1,2,4,5,3};
    for(unsigned i=0;i<common.tabs.size();++i){const auto& r=common.tabs[i];
        screen.OfferDesktopHover(assets.tab_tips[indices[i]],"live-tab:"+std::to_string(i),"live-tabs",0,
            Rect(r.x,r.y,r.width,r.height));
    }
    const auto& r=common.tabs[5];
    screen.OfferDesktopHover(assets.tab_tips[6],"live-tab:inventory","live-tabs",0,
        Rect(common.tabs[3].x,r.y,r.width,r.height));
}

odh_host::LivePanelSnapshot DesktopLiveSnapshot(LotSession& session,odh_host::LivePanelTab tab){
    odh_host::LivePanelSnapshot out;
    auto sim=session.SelectedSim();out.has_selected_person=sim&&!sim->Dead;
    if(!out.has_selected_person)return out;
    auto content=FSO::Content::Content::Get();
    for(size_t i=0;i<out.skills.size();++i)out.skills[i]=sim->GetPersonData(VMPersonDataVariable(odh_host::live_skill_indices[i]));
    for(size_t i=0;i<out.personality.size();++i)out.personality[i]=sim->GetPersonData(VMPersonDataVariable(odh_host::live_personality_indices[i]));
    if(tab==odh_host::LivePanelTab::interests&&!sim->IsPet())
        out.interests=odh_host::live_interests_snapshot([&](unsigned word)->std::optional<int16_t>{
            return word<=100?std::optional<int16_t>{sim->GetPersonData(VMPersonDataVariable(word))}:std::nullopt;
        });
    if(tab==odh_host::LivePanelTab::career){
    auto& assets=resources();
    const auto source=odh_host::live_career_source(sim->IsPet()?18:sim->GetPersonData(VMPersonDataVariable::PersonsAge),
        sim->GetPersonData(VMPersonDataVariable::JobType),sim->GetPersonData(VMPersonDataVariable::JobPromotionLevel));
    const auto jobType=source.job;const auto level=source.level;
    const bool gender=(sim->GetPersonData(VMPersonDataVariable::Gender)&1)!=0;
    odh_host::LiveCareerInfo career;career.employed=jobType!=0;career.school=source.school;
    career.job_type=jobType==-1?11u:uint16_t(std::max<int>(0,jobType));
    career.identity=assets.root+":"+std::to_string(assets.language)+":"+std::to_string(sim->ObjectID)+":"+
        std::to_string(Neighborhood::NeighborIDOf(sim))+":"+std::to_string(jobType)+":"+std::to_string(level)+":"+std::to_string(gender);
    if(career.school){
        odh_host::LiveSchoolInfo school;school.raw=sim->GetPersonData(VMPersonDataVariable::JobPromotionLevel);
        if(content->Jobs){const auto grades=content->Jobs->JobStrings(4097);
            school.grade=odh_host::live_school_grade_label(school.raw,[&](unsigned grade){return selected_work_label(grades,grade);});}
        career.report_card=std::move(school);
        career.help[0].title=assets.school_help[0];career.help[0].description=assets.school_help[1];career.help[0].icon_row=11;
        out.career=std::move(career);return out;
    }
    const auto& help=assets.career_help;
    career.help[0].title=help[12];career.help[0].description=help[13];career.help[0].icon_row=0;
    if(career.employed){
        career.performance=sim->GetPersonData(VMPersonDataVariable(source.performance_word));
        if(content->Jobs){
            const auto grades=content->Jobs->JobStrings(4096);
            career.performance_label=odh_host::live_job_performance_label(*career.performance,
                [&](unsigned grade){return selected_work_label(grades,grade);});
        }
        const auto job=content->Jobs?content->Jobs->GetJob(jobType):nullptr;
        if(job&&level>=0&&size_t(level)<job->JobLevels.size()){
            const auto& entry=job->JobLevels[size_t(level)];
            career.salary=entry.Salary;career.start_hour=entry.StartTime;career.end_hour=entry.EndTime;
            auto base=content->Jobs->JobStrings(jobType);
            auto alternate=content->Jobs->JobStrings(int16_t(uint16_t(jobType)+1000u));
            odh_host::LiveCareerLabelReader baseReader=[&](unsigned i){return selected_work_label(base,i);};
            odh_host::LiveCareerLabelReader alternateReader;
            if(alternate)alternateReader=[&](unsigned i){return selected_work_label(alternate,i);};
            career.title=odh_host::live_career_title(baseReader,alternateReader,unsigned(level),gender,true);
            // Some admitted careers (notably CARR-1 school) have no Work STR
            // bank. Retain the actual CARR name as the prior native consumer
            // did, with its known English legacy source; never invent a label.
            if(!career.title&&!entry.JobName.empty())career.title=LegacyToUTF8(entry.JobName,1);
            career.help[0].title=odh_host::live_career_title(baseReader,alternateReader,unsigned(level),gender,false);
            if(!career.help[0].title&&!entry.JobName.empty())career.help[0].title=LegacyToUTF8(entry.JobName,1);
            career.help[0].icon_row=jobType==-1?11u:unsigned(jobType);
            career.hours=odh_host::live_career_hours(help[23],entry.StartTime,entry.EndTime,assets.language);
            const auto description=selected_work_label(base,size_t(4+2*level));
            career.help[0].description=career.hours;
            if(description){if(career.help[0].description)*career.help[0].description+="\n"+*description;else career.help[0].description=description;}
            for(size_t i=0;i<6;++i)career.required_skills[i]=entry.MinRequired[i+1];
        }
    }
    out.career=std::move(career);
    }
    if(tab==odh_host::LivePanelTab::relationships){
    auto& assets=resources();
    const auto nid=Neighborhood::NeighborIDOf(sim);const auto owner=Neighborhood::GetNeighbor(nid);
    if(owner&&owner->Unknown1==1){
        out.relationships.emplace();
        // Preserve source order and directional values; never replace an absent
        // lifetime element with the daily score or zero.
        for(const auto key:owner->OrderedRelationshipKeys()){
            if(key<-32768||key>32767||key==nid)continue;
            auto neighbor=Neighborhood::GetNeighbor(int16_t(key));if(!neighbor||neighbor->Unknown1!=1)continue;
            const auto found=owner->Relationships.find(key);if(found==owner->Relationships.end())continue;
            odh_host::LiveRelationshipInfo person;person.neighbor_id=int16_t(key);
            if(!found->second.empty())person.daily=found->second[0];if(found->second.size()>2)person.long_term=found->second[2];
            auto object=content->WorldObjects->Get(neighbor->GUID,true);
            if(object&&object->Resource&&object->OBJ){
                const auto name=FSO::Content::TS1::ResolvePersonalText(object->Resource->Get<CTSS>(object->OBJ->CatalogStringsID));
                person.name=PersonalLabelUTF8(name.Value(0),name.SourceLanguage,name.UTF8,name.Ambiguous);
                auto portrait=assets.portraits.find(neighbor->GUID);
                if(portrait==assets.portraits.end()){
                    if(assets.portraits.size()>=256)assets.portraits.clear();
                    std::vector<odh_host::CharacterUiImageResource> images;
                    if(auto image=object->Resource->Get<BMP>(2003))images.push_back({2003,image->GetData()});
                    auto decoded=odh_host::read_character_ui_images(images);
                    if(decoded.relations.empty())if(auto strings=object->Resource->Get<STR>(object->OBJ->BodyStringID)){
                        // Generated characters need the same native appearance
                        // service as the household HUD, not name initials.
                        odh::AvatarBodyStrings body;body.avatar_type=strings->GetString(0);
                        body.body=strings->GetString(1);body.body=body.body.substr(0,body.body.find(';'));
                        body.head=strings->GetString(2);
                        for(size_t i=0;i<6;++i)body.hands[i]=strings->GetString(int(i+17));
                        Canvas faces;
                        if(OriginalPCUI::Get().GenerateFaces(body,faces)&&faces.Width>=3&&faces.Height>0){
                            odh::BitmapImage face;face.width=faces.Width/3;face.height=faces.Height;face.bit_count=32;
                            face.pixels.resize(size_t(face.width)*face.height);
                            for(unsigned y=0;y<face.height;++y)for(unsigned x=0;x<face.width;++x){
                                const auto p=faces.Px[size_t(y)*faces.Width+x];
                                face.pixels[size_t(y)*face.width+x]={uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)};
                            }
                            // Neutral generated portrait has no invented
                            // relationship tint. Authored five-frame strips win.
                            decoded.thumbnail=std::move(face);
                        }
                    }
                    portrait=assets.portraits.emplace(neighbor->GUID,std::move(decoded)).first;
                }
                const auto frame=odh_host::character_relationship_frame(person.long_term);
                if(frame<portrait->second.relations.size())person.portrait=std::make_shared<const odh::BitmapImage>(portrait->second.relations[frame]);
                else if(portrait->second.thumbnail)person.portrait=std::make_shared<const odh::BitmapImage>(*portrait->second.thumbnail);
            }
            out.relationships->push_back(std::move(person));
        }
    }
    }
    if(tab==odh_host::LivePanelTab::house)out.house=DesktopHouseSnapshot(session);
    return out;
}
void DrawDesktopLivePanel(Canvas& canvas,LotScreen& screen,odh_host::LivePanelTab tab,
                          const odh_host::LivePanelSnapshot& snapshot,int& relationshipPage,int& interestPage,DesktopCareerState& careerState){
    auto& assets=resources();auto* help_sim=screen.S->SelectedSim();
    const bool pet=help_sim&&help_sim->IsPet();
    odh_host::LivePanelView view(assets.art,pet?assets.pet_text:assets.text);
    if(!view.select(tab,snapshot))return;
    const auto common=odh_host::original_control_panel_layout(canvas.Width,canvas.Height);
    const auto layout=odh_host::live_panel_layout(common);
    const auto schoolLayout=odh_host::live_school_layout(common);
    if(tab==odh_host::LivePanelTab::career)for(unsigned i=0;i<layout.skill_labels.size();++i){
        if(!snapshot.skills[i])continue;
        const auto& r=layout.skill_labels[i];
        screen.OfferDesktopHelp(assets.career_help[2*i],assets.career_help[2*i+1],"skill:"+std::to_string(i),
            "career-skills",0,Rect(r.x,r.y,r.width,r.height));
    }
    if(tab==odh_host::LivePanelTab::personality)for(unsigned i=0;i<layout.personality_labels.size();++i){
        if(!help_sim||!snapshot.personality[i])continue;
        const auto h=*odh_host::original_personality_help(i,*snapshot.personality[i],pet);
        const auto& strings=assets.personality_help[pet?1:0];
        const auto& r=layout.personality_labels[i];
        screen.OfferDesktopHelp(strings[h.title],strings[h.description],
            "personality:"+std::to_string(i)+":"+std::to_string(h.bucket),"personality",uint64_t(help_sim->ObjectID),
            Rect(r.x,r.y,r.width,r.height),{},{},true);
    }
    careerState.popup.visible=false;
    const auto prior=careerState.selection.selected();
    careerState.selection.reconcile(tab==odh_host::LivePanelTab::career&&snapshot.career?snapshot.career->identity:std::string{});
    if(prior&&!careerState.selection.selected())careerState.popup.Close();
    const bool alert=tab==odh_host::LivePanelTab::career&&snapshot.career&&careerState.selection.alert(
        (snapshot.career->school?snapshot.career->report_card&&snapshot.career->report_card->raw>=10:
         snapshot.career->performance&&*snapshot.career->performance<0),uint64_t(std::max(0.,screen.Time)));
    if(tab==odh_host::LivePanelTab::relationships&&snapshot.relationships){
        const auto columns=layout.relationship_faces.size();
        const int last=snapshot.relationships->empty()?0:int((snapshot.relationships->size()-1)/columns);
        relationshipPage=std::clamp(relationshipPage,0,last);
        for(int i=0;i<relationshipPage;++i)view.click(common,layout.next.x,layout.next.y,snapshot);
        auto rect=[](const odh::UiBitmapRect& r){return Rect(r.x,r.y,r.width,r.height);};
        if(relationshipPage)screen.Hit(rect(layout.previous),"rel_prev");
        if(relationshipPage<last)screen.Hit(rect(layout.next),"rel_next");
        const auto first=size_t(relationshipPage)*columns;
        for(size_t i=0;i<columns&&first+i<snapshot.relationships->size();++i){
            const auto& person=(*snapshot.relationships)[first+i];
            screen.OfferDesktopHover(person.name,"neighbor:"+std::to_string(person.neighbor_id),"relationships",
                                     uint64_t(person.neighbor_id),rect(layout.relationship_faces[i]));
        }
    }
    if(tab==odh_host::LivePanelTab::interests&&snapshot.interests){
        auto cells=odh_host::live_interest_layout(common,snapshot.interests->age,size_t(std::max(0,interestPage)));
        interestPage=int(cells.page);
        for(int i=0;i<interestPage;++i)view.click(common,cells.next.x,cells.next.y,snapshot);
        auto rect=[](const odh::UiBitmapRect& r){return Rect(r.x,r.y,r.width,r.height);};
        if(cells.page)screen.Hit(rect(cells.previous),"interest_prev");
        if(cells.page+1<cells.pages)screen.Hit(rect(cells.next),"interest_next");
        for(const auto& cell:cells.cells){
            if(auto text=view.hover_text(common,cell.icon.x,cell.icon.y,snapshot)){
                // The preserved PC consumer shows the topic NAME on its
                // illustration. STR142 help is separate, never a new popup.
                const auto newline=text->find('\n');if(newline!=std::string::npos)text->resize(newline);
                screen.OfferDesktopHover(*text,"interest:"+std::to_string(cell.topic),"interests",
                    uint64_t(cell.topic),rect(cell.icon));
            }
        }
    }
    if(tab==odh_host::LivePanelTab::career&&snapshot.career){
        const auto& career=*snapshot.career;
        const auto hit=[&](const odh::UiBitmapRect& r,unsigned i){
            if(career.help[i].title&&career.help[i].description&&!career.help[i].title->empty()&&!career.help[i].description->empty())
                screen.Hit(Rect(r.x,r.y,r.width,r.height),"pc_career_info"+std::to_string(i));
        };
        hit(career.school?schoolLayout.grade:layout.career_icon,0);
        if(const auto text=view.hover_text(common,screen.P.X,screen.P.Y,snapshot)){
            const auto& box=layout.salary;
            screen.OfferDesktopHover(*text,"career-hours","career",0,Rect(box.x,box.y,box.width,box.height));
        }
    }
    auto& frame=assets.surface;
    const int top=std::max(0,common.right_content.y);
    const Rect region{0,top,canvas.Width,canvas.Height-top};
    Raster(canvas,frame,region);
    InstallTextBackendIfNeeded();
    view.draw(frame,snapshot,[&](odh::RasterFrame& image,std::string_view text,const odh::UiBitmapRect& box,bool title,odh_host::LiveTextAlignment alignment){
        odh_host::TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
        // Preserve the original PC consumer's common caption style. The
        // emphasis flag chooses text size; it is not a green hover state.
        style.padding=0;style.font_size_px=std::max(7,int((title?13:11)*common.scale));style.background={0,0,0,0};
        style.foreground={187,198,195,255};
        const bool school=tab==odh_host::LivePanelTab::career&&snapshot.career&&snapshot.career->school;
        const bool schoolGrade=school&&box.x==schoolLayout.grade.x&&box.y==schoolLayout.grade.y;
        if(alert&&((schoolGrade)||(!school&&box.x==layout.performance_title.x&&box.y==layout.performance_title.y)))
            style.foreground={0,202,57,255};
        if(school){
            style.foreground=alert&&schoolGrade?std::array<uint8_t,4>{0,202,57,255}:std::array<uint8_t,4>{195,205,205,255};
            const auto& font=assets.school_grade_font;
            const bool ascii=std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32&&c<127;});
            if(schoolGrade&&font.Loaded()&&common.scale==1.f&&ascii){
                // Reuse the existing authenticated FFN service, not a guessed
                // enlarged Pango face. Original532ce3 selects font18, and its
                // authored7 mask matches the observed school screenshot.
                Canvas cell(box.width,box.height);
                for(int y=0;y<box.height;++y)for(int x=0;x<box.width;++x){
                    const auto sx=box.x+x,sy=box.y+y;
                    if(sx>=0&&sy>=0&&sx<int(image.width)&&sy<int(image.height)){
                        const auto& p=image.pixels[size_t(sy)*image.width+sx];cell.Px[size_t(y)*box.width+x]=RGBA(p[0],p[1],p[2],p[3]);}
                }
                const std::string bytes{text};const int x=schoolGrade?(box.width-font.Width(bytes))/2:0;
                const auto color=style.foreground;font.Draw(cell,x,0,bytes,RGBA(color[0],color[1],color[2],color[3]));
                for(int y=0;y<box.height;++y)for(int x=0;x<box.width;++x){
                    const auto sx=box.x+x,sy=box.y+y;if(sx<0||sy<0||sx>=int(image.width)||sy>=int(image.height))continue;
                    const auto p=cell.Px[size_t(y)*box.width+x];image.pixels[size_t(sy)*image.width+sx]={uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)};
                }
                return;
            }
            if(schoolGrade)style.font_size_px=std::max(7,int(std::lround(24*common.scale)));
        }
        style.centered=alignment==odh_host::LiveTextAlignment::center;style.right_aligned=alignment==odh_host::LiveTextAlignment::right;
        (void)odh_host::draw_text_overlay(image,text,assets.language,style);
    });
    Copy(frame,canvas,region);
    const auto selected=careerState.selection.selected();
    if(tab==odh_host::LivePanelTab::career&&snapshot.career&&selected){
        const auto& career=*snapshot.career;const auto& info=career.help[*selected];
        if(!info.title||!info.description){careerState.Close();return;}
        auto& popup=careerState.popup;
        if(!popup.view||popup.root!=assets.root){popup.view=std::make_shared<odh_host::CatalogInfoView>(assets.root);popup.root=assets.root;popup.scroll.reset();}
        const auto identity=career.identity+":"+std::to_string(*selected);
        if(!popup.open||popup.identity!=identity)popup.scroll.reset();
        popup.identity=identity;popup.open=true;
        odh_host::CatalogInfoContent content;content.name=*info.title;content.description=*info.description;
        odh::BitmapImage icon;
        if(info.icon_row)if(const auto strip=UIGraphics::Get().Image("CPanel/Backgrounds/JobIconMultiPopup.bmp")){
            if(strip->Height%22==0&&*info.icon_row<22){
                icon.width=strip->Width;icon.height=strip->Height/22;icon.bit_count=32;icon.pixels.resize(size_t(icon.width)*icon.height);
                for(unsigned y=0;y<icon.height;++y)for(unsigned x=0;x<icon.width;++x){
                    const auto p=strip->Px[size_t(y+*info.icon_row*icon.height)*strip->Width+x];
                    const bool key=(p&0xFFFFFF)==0xFF00FF;
                    icon.pixels[size_t(y)*icon.width+x]={uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(key?0:p>>24)};
                }
                content.icon=&icon;content.icon_rgba=true;
            }
        }
        content.text_only_live=!content.icon;
        const auto measure=odh_host::catalog_info_text_measure(assets.language,true,common.scale);
        popup.layout=popup.scroll.prepare(canvas.Width,canvas.Height,content,measure);
        for(size_t i=0;i<canvas.Px.size();++i){const auto p=canvas.Px[i];frame.pixels[i]={uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)};}
        popup.view->draw(frame,content,odh_host::catalog_info_text_painter(assets.language,true,common.scale),measure,
            &popup.scroll,odh_host::catalog_info_scroll_painter(assets.language,true,common.scale),odh_host::catalog_info_heading_painter(assets.language,true,common.scale));
        const auto r=popup.layout.panel;
        for(int y=std::max(0,r.y);y<std::min(canvas.Height,r.y+r.height);++y)for(int x=std::max(0,r.x);x<std::min(canvas.Width,r.x+r.width);++x){
            const auto& p=frame.pixels[size_t(y)*canvas.Width+x];canvas.Px[size_t(y)*canvas.Width+x]=RGBA(p[0],p[1],p[2],p[3]);}
        popup.visible=true;screen.Hit(Rect(r.x,r.y,r.width,r.height),"pc_career_close");
    }else careerState.popup.Close();
}
bool HandleDesktopCareerInput(LotSession& session,DesktopCareerState& state,const std::string& id){
    if(id=="pc_career_close"){state.Close();return true;}
    constexpr auto prefix="pc_career_info";
    if(id.rfind(prefix,0)!=0)return false;
    auto snapshot=DesktopLiveSnapshot(session,odh_host::LivePanelTab::career);
    state.selection.reconcile(snapshot.career?snapshot.career->identity:std::string{});
    if(!snapshot.career)return true;
    const auto suffix=id.substr(std::char_traits<char>::length(prefix));
    if(suffix!="0")return true;
    const unsigned i=unsigned(suffix[0]-'0');const auto& help=snapshot.career->help[i];
    if(help.title&&help.description&&!help.title->empty()&&!help.description->empty()){
        if(snapshot.career->school)state.selection.show(i);else state.selection.toggle(i);
    }
    if(!state.selection.selected())state.popup.Close();
    return true;
}
}}
#endif
