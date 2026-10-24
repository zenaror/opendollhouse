#include "linux_cas_screen.hpp"

#include "linux_text_renderer.hpp"
#include "odh/raster.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace odh_host {
namespace {
constexpr std::array<const char*,3> kSkins{"lgt","med","drk"};
constexpr std::array<const char*,5> kPersonality{
    "Neat", "Outgoing", "Active", "Playful", "Nice"};

std::string trim(std::string value) {
    const auto first=std::find_if_not(value.begin(),value.end(),[](unsigned char ch){return std::isspace(ch)!=0;});
    const auto last=std::find_if_not(value.rbegin(),value.rend(),[](unsigned char ch){return std::isspace(ch)!=0;}).base();
    return first<last?std::string(first,last):std::string{};
}

void erase_last_utf8(std::string& value) {
    if(value.empty())return;
    auto position=value.size()-1;
    while(position>0&&(static_cast<unsigned char>(value[position])&0xC0U)==0x80U)--position;
    value.erase(position);
}

struct MemberForm {
    std::string name;
    std::string biography;
    bool female=true;
    bool child=false;
    std::size_t skin=0,body=0,head=0;
    std::array<std::uint8_t,5> personality{5,5,5,5,5};
};

std::string avatar_type(const MemberForm& form) {
    return std::string(form.female?"f":"m")+(form.child?"c":"a");
}

std::vector<LinuxCasOutfitChoice> body_choices(const LinuxCasCatalog& catalog,const MemberForm& form) {
    auto type=avatar_type(form);
    if(form.child)type+="chd";
    return catalog.bodies(type,kSkins.at(form.skin));
}

std::vector<LinuxCasOutfitChoice> head_choices(const LinuxCasCatalog& catalog,const MemberForm& form) {
    return catalog.heads(avatar_type(form),kSkins.at(form.skin));
}

void adjust_index(std::size_t& value,int delta,std::size_t count) {
    if(!count)return;
    if(delta<0)value=value?value-1:count-1;
    else value=(value+1)%count;
}

std::optional<SimitoneCasPersonInput> make_person(const LinuxCasCatalog& catalog,
        const MemberForm& form,const std::string& name,const std::string& biography,
        std::uint8_t language) {
    const auto bodies=body_choices(catalog,form);
    const auto heads=head_choices(catalog,form);
    if(name.empty()||bodies.empty()||heads.empty())return std::nullopt;
    const auto& body=bodies.at(form.body%bodies.size());
    const auto& head=heads.at(form.head%heads.size());
    SimitoneCasPersonInput result;
    result.name_utf8=name;result.biography_utf8=biography;result.language=language;
    result.female=form.female;result.child=form.child;result.skin=kSkins.at(form.skin);
    result.body_type=body.body_type;result.body_appearance=body.appearance;
    result.body_texture=body.texture;result.handgroup_texture=body.handgroup_texture;
    result.head_appearance=head.appearance;result.head_texture=head.texture;
    result.personality_points=form.personality;
    return result;
}

void draw_field(odh::RasterFrame& frame,std::size_t index,std::size_t selected,
        const std::string& label,const std::string& value,std::int32_t y,
        std::uint8_t language,bool compact=false) {
    TextOverlayStyle style;style.x=28;style.y=y;style.width=744;style.height=compact?25:30;
    style.font_size_px=compact?15:17;style.padding=5;
    style.foreground={245,245,245,255};
    style.background=index==selected?std::array<std::uint8_t,4>{47,93,138,255}:
                                        std::array<std::uint8_t,4>{52,57,68,255};
    draw_text_overlay(frame,label+": "+value,language,style);
}

void draw_button(odh::RasterFrame& frame,std::size_t index,std::size_t selected,
        const std::string& label,std::int32_t x,std::uint8_t language) {
    TextOverlayStyle style;style.x=x;style.y=536;style.width=330;style.height=38;
    style.font_size_px=16;style.padding=7;style.foreground={255,255,255,255};
    style.background=index==selected?std::array<std::uint8_t,4>{47,115,100,255}:
                                        std::array<std::uint8_t,4>{52,57,68,255};
    draw_text_overlay(frame,label,language,style);
}

} // namespace

std::optional<LinuxCasFamilyDraft> show_linux_cas_family_creator(
        LinuxWindow& window,const LinuxCasCatalog& catalog,std::uint8_t language) {
    if(language<1||language>20)throw std::invalid_argument("CAS screen language outside 1..20");
    if(window.width()!=800||window.height()!=600)window.resize(800,600);
    std::string family_name;
    std::vector<SimitoneCasPersonInput> members;
    MemberForm form;
    std::size_t selected=0;
    constexpr std::size_t add_index=13,save_index=14;
    std::string feedback;
    auto current_bodies=body_choices(catalog,form);
    auto current_heads=head_choices(catalog,form);

    const auto render=[&]() {
        odh::RasterFrame frame{};frame.width=window.width();frame.height=window.height();
        frame.pixels.assign(static_cast<std::size_t>(frame.width)*frame.height,{35,39,48,255});
        frame.depth.assign(frame.pixels.size(),std::numeric_limits<float>::infinity());
        TextOverlayStyle title;title.x=28;title.y=10;title.width=744;title.height=30;
        title.font_size_px=20;title.padding=3;title.foreground={245,245,245,255};
        title.background={35,39,48,0};
        draw_text_overlay(frame,"Create a family | Tab: next | arrows: change",language,title);
        draw_field(frame,0,selected,"Family",family_name,45,language);
        draw_field(frame,1,selected,"Sim name",form.name,80,language);
        draw_field(frame,2,selected,"Biography",form.biography,115,language);
        draw_field(frame,3,selected,"Gender",form.female?"Female":"Male",150,language);
        draw_field(frame,4,selected,"Age",form.child?"Child":"Adult",181,language);
        draw_field(frame,5,selected,"Skin",kSkins.at(form.skin),212,language);
        const auto body_label=current_bodies.empty()?"No matching body assets":
            current_bodies[form.body%current_bodies.size()].appearance+" | "+
            current_bodies[form.body%current_bodies.size()].texture;
        const auto head_label=current_heads.empty()?"No matching head assets":
            current_heads[form.head%current_heads.size()].appearance+" | "+
            current_heads[form.head%current_heads.size()].texture;
        draw_field(frame,6,selected,"Body",body_label,243,language);
        draw_field(frame,7,selected,"Head",head_label,274,language);
        for(std::size_t i=0;i<form.personality.size();++i)
            draw_field(frame,8+i,selected,kPersonality[i],std::to_string(form.personality[i])+" / 10",
                static_cast<std::int32_t>(310+i*31),language,true);
        draw_button(frame,add_index,selected,members.size()<8?"Tab: Add another Sim":"Household full",48,language);
        draw_button(frame,save_index,selected,"Tab: Save family",422,language);
        TextOverlayStyle footer;footer.x=28;footer.y=503;footer.width=744;footer.height=25;
        footer.font_size_px=14;footer.padding=3;footer.foreground={215,220,225,255};
        footer.background={35,39,48,0};
        const auto total=std::accumulate(form.personality.begin(),form.personality.end(),0U);
        draw_text_overlay(frame,"Residents: "+std::to_string(members.size()+1)+"/8 | Personality: "+
            std::to_string(total)+"/25 | Esc: cancel"+(feedback.empty()?std::string{}:" | "+feedback),language,footer);
        window.set_title("OpenDollhouse | Create family");window.present(frame);
    };

    render();
    for(;;) {
        const auto input=window.poll();
        if(input.quit)return std::nullopt;
        if(input.interaction_selection_delta) {
            const auto count=save_index+1;
            const auto delta=input.interaction_selection_delta;
            selected=static_cast<std::size_t>((static_cast<long long>(selected)+delta+static_cast<long long>(count))%
                static_cast<long long>(count));
            feedback.clear();
            render();
        }
        const bool text_field=selected==0||selected==1||selected==2;
        if(text_field&&!input.text_input_utf8.empty()) {
            auto& target=selected==0?family_name:(selected==1?form.name:form.biography);
            const auto maximum=selected==2?1024U:128U;
            if(target.size()+input.text_input_utf8.size()<=maximum)target+=input.text_input_utf8;
            render();
        }
        if(text_field&&input.text_backspace) {
            auto& target=selected==0?family_name:(selected==1?form.name:form.biography);
            erase_last_utf8(target);render();
        }
        if(input.rotation||input.zoom) {
            feedback.clear();
            const int delta=input.rotation?input.rotation:input.zoom;
            if(selected==3)form.female=!form.female;
            else if(selected==4)form.child=!form.child;
            else if(selected==5)adjust_index(form.skin,delta,kSkins.size());
            else if(selected==6)adjust_index(form.body,delta,current_bodies.size());
            else if(selected==7)adjust_index(form.head,delta,current_heads.size());
            else if(selected>=8&&selected<=12) {
                const auto slider=selected-8;
                const int next=static_cast<int>(form.personality[slider])+(delta<0?-1:1);
                const auto total=std::accumulate(form.personality.begin(),form.personality.end(),0U);
                if(next>=0&&next<=10&&(delta<0||total<25))form.personality[slider]=static_cast<std::uint8_t>(next);
            }
            current_bodies=body_choices(catalog,form);current_heads=head_choices(catalog,form);
            if(!current_bodies.empty())form.body%=current_bodies.size();else form.body=0;
            if(!current_heads.empty())form.head%=current_heads.size();else form.head=0;
            render();
        }
        bool add=false,save=false;
        if(input.primary_click) {
            const auto x=(*input.primary_click)[0],y=(*input.primary_click)[1];
            if(y>=536&&x<400){selected=add_index;add=true;}
            else if(y>=536&&x>=400){selected=save_index;save=true;}
            else if(y>=45&&y<75)selected=0;
            else if(y>=80&&y<110)selected=1;
            else if(y>=115&&y<145)selected=2;
            else if(y>=150&&y<181)selected=3;
            else if(y>=181&&y<212)selected=4;
            else if(y>=212&&y<243)selected=5;
            else if(y>=243&&y<274)selected=6;
            else if(y>=274&&y<305)selected=7;
            else if(y>=310&&y<465)selected=8+static_cast<std::size_t>((y-310)/31);
            render();
        }
        if(input.confirm) {
            if(selected==add_index)add=true;
            else if(selected==save_index)save=true;
            else selected=selected==save_index?0:selected+1;
            render();
        }
        if(add||save) {
            if(trim(family_name).empty()) {selected=0;feedback="Enter a family name";}
            else if(save&&trim(form.name).empty()&&!members.empty())
                return LinuxCasFamilyDraft{trim(family_name),std::move(members)};
            else if(members.size()>=8&&add)feedback="Household limit reached";
            else if(current_bodies.empty()||current_heads.empty())
                feedback="Choose a Sim type with available head and body assets";
            else {
                const auto sim_name=trim(form.name);
                const auto draft=make_person(catalog,form,sim_name,form.biography,language);
                if(draft) {
                    if(members.size()<8)members.push_back(*draft);
                    if(save&&!members.empty())return LinuxCasFamilyDraft{trim(family_name),std::move(members)};
                    form=MemberForm{};current_bodies=body_choices(catalog,form);
                    current_heads=head_choices(catalog,form);selected=1;feedback.clear();
                } else feedback="Enter a Sim name";
            }
            render();
        }
        if(input.redraw)render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

} // namespace odh_host
