#include "linux_cas_screen.hpp"
#include "linux_cas_draft.hpp"
#include "linux_cas_layout.hpp"
#include "cas_presentation.hpp"
#include "linux_vm_dialog.hpp"
#include "linux_cas_preview.hpp"
#include "linux_ui_strings.hpp"
#include "original_ui_art.hpp"

#include "linux_text_renderer.hpp"
#include "odh/raster.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace odh_host {
namespace {
constexpr std::array<const char*,3> kSkins{"lgt","med","drk"};


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
    bool female=false; // A fresh original editor starts with an adult male.
    bool child=false;
    std::size_t skin=0,body=0,head=0;
    std::array<std::uint8_t,5> personality{};
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

std::optional<SimitoneCasPersonInput> make_person(const std::vector<LinuxCasOutfitChoice>& bodies,
        const std::vector<LinuxCasOutfitChoice>& heads,
        const MemberForm& form,const std::string& name,const std::string& biography,
        std::uint8_t language) {
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

} // namespace

std::optional<LinuxCasFamilyDraft> show_linux_cas_family_creator(
        LinuxWindow& window,const LinuxCasCatalog& catalog,std::uint8_t language,const std::filesystem::path& game_root){
    if(language<1||language>20)throw std::invalid_argument("CAS screen language outside 1..20");
    const OriginalUiArt art(game_root,cas_art_keys());
    GameUiStrings strings(game_root);
    std::vector<std::string> family_text,character_text;
    constexpr const char* family_fallback[]{"Add a New Sim","Delete Selected Sim","Edit Selected Sim","Done","Cancel",
        "Delete Sim?","Delete this Sim?","Cancel?","Discard this family?","Create a Family","Done","Cancel","Enter Last Name:",
        "Finished?","Finish creating this family?"};
    constexpr const char* character_fallback[]{"Adult","Child","Female","Male","Dark","Medium","Light","Previous Body",
        "Next Body","Previous Head","Next Head","Done","Cancel","More Personality!","Finish with unused personality points?",
        "Create a Sim","Personality","Neat","Outgoing","Active","Playful","Nice","Done","Cancel","Bio","Bio","Enter First Name:","Astrological Sign"};
    for(std::size_t i=0;i<std::size(family_fallback);++i)family_text.push_back(strings.get(129,i,language,family_fallback[i]));
    for(std::size_t i=0;i<std::size(character_fallback);++i)character_text.push_back(strings.get(130,i,language,character_fallback[i]));

    const auto ok=strings.get(152,0,language,"OK"),cancel=strings.get(152,1,language,"Cancel");
    const auto yes=strings.get(152,2,language,"Yes"),no=strings.get(152,3,language,"No");
    const VmDialogAppearance dialog_appearance{&art.at("Shared/Sys/WinBtn.bmp"),{},yes,no,{},
        &art.at("CPanel/Backgrounds/PopupInfoTiles.bmp")};
    CasPointer pointer;
    std::optional<std::array<std::int32_t,2>> pointer_position;
    std::optional<std::size_t> hovered;
    std::optional<CasQuestion> question;
    odh::DialogState dialog;
    const auto ask=[&](CasQuestion purpose){
        pointer.reset();hovered.reset();
        const auto keys=cas_question_text(purpose);
        const auto& text=keys.character?character_text:family_text;
        dialog={};dialog.instruction.operands[5]=1; // Yes / No
        dialog.prompt.title=text[keys.title];dialog.prompt.message=text[keys.message];
        if(cas_question_uses_ok_cancel(purpose)){dialog.prompt.yes=ok;dialog.prompt.no=cancel;}
        dialog.selected_button=1;
        question=purpose;
    };
    const auto parent_width=window.width(),parent_height=window.height();
    if(parent_width!=800||parent_height!=600)window.resize(800,600);
    const auto finish=[&](std::optional<LinuxCasFamilyDraft> draft){
        if(parent_width!=800||parent_height!=600)window.resize(parent_width,parent_height);
        return draft;
    };
    std::optional<LinuxCasPreview> previews;
    bool preview_disabled=false;
    std::string preview_error;
    const auto preview=[&](const SimitoneCasPersonInput& person,bool portrait) -> const odh::BitmapImage* {
        if(preview_disabled)return nullptr;
        try{
            if(!previews)previews.emplace(game_root);
            const auto* image=previews->image(person,portrait);
            preview_error=image?"":language==14?"Não foi possível mostrar o Sim.":"Could not display this Sim.";
            return image;
        }catch(const odh::FormatError&){preview_disabled=true;}
         catch(const std::filesystem::filesystem_error&){preview_disabled=true;}
        preview_error=language==14?"Não foi possível mostrar o Sim.":"Could not display this Sim.";
        return nullptr;
    };
    LinuxCasDraftMembers draft;
    MemberForm form;
    std::string family_name,feedback;
    std::size_t focus=0;
    std::optional<std::size_t> selected_member;
    bool character=false;
    auto bodies=body_choices(catalog,form),heads=head_choices(catalog,form);
    auto controls=cas_layout(character,0);
    const auto refresh_outfits=[&](){
        bodies=body_choices(catalog,form);heads=head_choices(catalog,form);
        form.body=bodies.empty()?0:form.body%bodies.size();form.head=heads.empty()?0:form.head%heads.size();
    };
    const auto enter=[&](bool editor){
        pointer.reset();character=editor;focus=0;feedback.clear();controls=cas_layout(editor,draft.members().size());
        hovered=pointer_position?cas_widget_at(controls,(*pointer_position)[0],(*pointer_position)[1]):std::nullopt;
    };
    const auto begin_member=[&](bool edit){
        form=MemberForm{};draft.begin_new();
        if(edit){
            if(!selected_member)return;
            const auto& member=draft.select(*selected_member);
            form.name=member.name_utf8;form.biography=member.biography_utf8;form.female=member.female;
            form.child=member.child;form.personality=member.personality_points;
            const auto skin=std::find(kSkins.begin(),kSkins.end(),member.skin);
            if(skin==kSkins.end())throw odh::FormatError("invalid skin in CAS draft");
            form.skin=std::size_t(skin-kSkins.begin());refresh_outfits();
            const auto body=std::find_if(bodies.begin(),bodies.end(),[&](const auto& c){return c.appearance==member.body_appearance&&
                c.texture==member.body_texture&&c.body_type==member.body_type&&c.handgroup_texture==member.handgroup_texture;});
            const auto head=std::find_if(heads.begin(),heads.end(),[&](const auto& c){return c.appearance==member.head_appearance&&c.texture==member.head_texture;});
            if(body==bodies.end()||head==heads.end())throw odh::FormatError("CAS draft outfit no longer exists in catalog");
            form.body=std::size_t(body-bodies.begin());form.head=std::size_t(head-heads.begin());
        }else refresh_outfits();
        enter(true);
    };
    using C=CasControl;
    const auto enabled=[&](C control){
        return cas_control_enabled(control,character,!trim(family_name).empty(),!trim(form.name).empty(),
            draft.members().size(),selected_member.has_value());
    };
    const auto on=[&](C control){
        return (control==C::female&&form.female)||(control==C::male&&!form.female)||
            (control==C::adult&&!form.child)||(control==C::child&&form.child)||
            (control==C::light&&form.skin==0)||(control==C::medium&&form.skin==1)||(control==C::dark&&form.skin==2);
    };
    const auto accept_member=[&](SimitoneCasPersonInput person){
        draft.accept(std::move(person));selected_member.reset();enter(false);
    };
    std::optional<SimitoneCasPersonInput> pending_member;
    const auto render=[&](){
        CasPresentation view;
        view.character=character;view.family_name=family_name;view.name=form.name;view.biography=form.biography;
        view.traits=form.personality;view.labels=character?character_text:family_text;view.controls=controls;
        view.focus=focus;view.hovered=hovered;view.pressed=pointer.pressed_target();view.selected=selected_member;
        view.enabled=enabled;view.on=on;
        for(const auto& member:draft.members())view.members.push_back(member.name_utf8);
        view.portrait=[&](std::size_t i){return preview(draft.members()[i],true);};
        view.body_preview=[&]()->const odh::BitmapImage*{
            if(bodies.empty()||heads.empty())return nullptr;
            const auto person=make_person(bodies,heads,form,form.name.empty()?" ":form.name,form.biography,language);
            return person?preview(*person,false):nullptr;
        };
        view.feedback=feedback.empty()?preview_error:feedback;
        auto frame=view.draw(art,language);
        window.set_title("OpenDollhouse | "+view.labels[character?15:9]);
        if(question)draw_vm_dialog(frame,dialog,language,0,dialog_appearance);
        window.present(frame);
    };
    render();
    for(;;){
        const auto input=window.poll();
        const auto previous_hover=hovered;
        pointer_position=input.pointer_position;
        if(!question)hovered=pointer_position?cas_widget_at(controls,(*pointer_position)[0],(*pointer_position)[1]):std::nullopt;
        if(input.quit){
            if(input.quit_reason!=WindowQuitReason::escape_key)return finish(std::nullopt);
            window.clear_escape_quit_reason();
            pointer.reset();
            if(question){question.reset();pending_member.reset();}
            else if(character){draft.begin_new();enter(false);}
            else ask(CasQuestion::discard_family);
            render();continue;
        }
        if(question){
            if(input.interaction_selection_delta%2||input.rotation||input.zoom)
                dialog.selected_button=dialog.selected_button?0:1;
            const auto layout=vm_message_dialog_layout(window.width(),window.height(),dialog,language,dialog_appearance);
            const auto button=[&](int x,int y)->std::optional<std::uint8_t>{
                return layout?vm_dialog_button_hit(*layout,1,x,y):std::nullopt;
            };
            const auto click=pointer.click_on(input,button);
            std::optional<std::uint8_t> answer;
            if(click)answer=button((*click)[0],(*click)[1]);
            else if(input.confirm)answer=dialog.selected_button;
            if(answer){
                pointer.reset();
                const auto purpose=*question;question.reset();
                if(*answer==0)switch(purpose){
                case CasQuestion::remove_member:
                    draft.select(*selected_member);draft.erase_selected();selected_member.reset();enter(false);break;
                case CasQuestion::discard_family:return finish(std::nullopt);
                case CasQuestion::finish_family:return finish(LinuxCasFamilyDraft{trim(family_name),draft.members()});
                case CasQuestion::unused_points:accept_member(std::move(pending_member.value()));break;
                }
                pending_member.reset();
            }
            if(answer||input.redraw||input.interaction_selection_delta||input.rotation||input.zoom)render();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));continue;
        }
        if(input.interaction_selection_delta){
            const int n=int(controls.size());
            const int step=input.interaction_selection_delta<0?-1:1;
            for(int moved=0;moved<std::abs(input.interaction_selection_delta%n);++moved){
                do{focus=std::size_t((int(focus)+step+n)%n);}while(!enabled(controls[focus].control));
            }
            feedback.clear();
        }
        bool clicked=false;
        const auto click=pointer.click(controls,input);
        if(click){
            const auto hit=cas_widget_at(controls,(*click)[0],(*click)[1]);
            if(hit&&enabled(controls[*hit].control)){clicked=true;focus=*hit;feedback.clear();}
        }
        const auto widget=controls[focus];const auto c=widget.control;
        std::string* field=c==C::name?&form.name:c==C::bio?&form.biography:c==C::family_name?&family_name:nullptr;
        if(field){
            if(input.text_backspace)erase_last_utf8(*field);
            if(field->size()+input.text_input_utf8.size()<=(c==C::bio?1024U:128U))*field+=input.text_input_utf8;
        }
        const int delta=input.rotation?input.rotation:input.zoom;
        if((clicked||input.confirm||delta)&&enabled(c)){
            const bool activate=clicked||input.confirm;
            switch(c){
            case C::add: if(activate)begin_member(false);break;
            case C::edit: if(activate)begin_member(true);break;
            case C::remove:
                if(activate)ask(CasQuestion::remove_member);break;
            case C::member:
                selected_member=widget.index;
                if(input.confirm)begin_member(true);
                break;
            case C::cancel:
                if(activate){if(character){draft.begin_new();enter(false);}else ask(CasQuestion::discard_family);}break;
            case C::done:
                if(!activate)break;
                if(character){
                    const auto person=make_person(bodies,heads,form,trim(form.name),form.biography,language);
                    if(!person){
                        if(trim(form.name).empty()){feedback=character_text[26];focus=0;}
                        else feedback=language==14?"Não há cabeça ou roupa disponível para esta combinação.":
                            "No head or outfit is available for this combination.";
                        break;
                    }
                    if(std::accumulate(form.personality.begin(),form.personality.end(),0)<25){
                        pending_member=*person;ask(CasQuestion::unused_points);
                    }else accept_member(*person);
                }else if(trim(family_name).empty()){feedback=family_text[12];focus=0;}
                else if(draft.members().empty()){feedback=family_text[0];focus=1;}
                else ask(CasQuestion::finish_family);
                break;
            case C::female: case C::male: form.female=c==C::female;refresh_outfits();break;
            case C::adult: case C::child: form.child=c==C::child;refresh_outfits();break;
            case C::light: case C::medium: case C::dark: form.skin=c==C::light?0:c==C::medium?1:2;refresh_outfits();break;
            case C::body_previous: case C::body_next: adjust_index(form.body,delta?delta:(c==C::body_previous?-1:1),bodies.size());break;
            case C::head_previous: case C::head_next: adjust_index(form.head,delta?delta:(c==C::head_previous?-1:1),heads.size());break;
            case C::trait:{
                const int target=clicked?cas_trait_target(widget,(*click)[0]):
                    int(form.personality[widget.index])+(delta<0?-1:1);
                step_cas_trait(form.personality,widget.index,target);break;}
            case C::family_name:case C::name:case C::bio: if(input.confirm)focus=(focus+1)%controls.size();break;
            }
        }
        if(input.redraw||click||input.confirm||delta||input.text_backspace||
           !input.text_input_utf8.empty()||input.interaction_selection_delta||previous_hover!=hovered||
           !input.pointer_events.empty()||input.pointer_cancelled)render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
} // namespace odh_host
