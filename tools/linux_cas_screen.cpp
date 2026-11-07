#include "linux_cas_screen.hpp"
#include "linux_cas_draft.hpp"
#include "linux_cas_layout.hpp"
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

constexpr std::array<std::uint8_t,3> magenta{255,0,255};
constexpr const char* family_background="Nbhd/DsgnFamBkg.bmp";
constexpr const char* character_background="Nbhd/CreateACharBack.bmp";
constexpr const char* trait_bar="Nbhd/DesignCharPersLED.bmp";
constexpr const char* remaining_bar="Nbhd/CACPointRemainingBars.bmp";
constexpr const char* portrait_frame="CPanel/People/PeopleTemplate.bmp";

std::vector<std::string> art_keys(){
    std::vector<std::string> keys{family_background,character_background,trait_bar,remaining_bar,portrait_frame,"Shared/Sys/WinBtn.bmp","CPanel/Backgrounds/PopupInfoTiles.bmp"};
    for(const bool character:{false,true})for(const auto& widget:cas_layout(character,0))
        if(widget.image)keys.emplace_back(widget.image);
    return keys;
}
void label(odh::RasterFrame& frame,std::string_view text,const odh::UiBitmapRect& box,std::uint8_t language,
           bool focused=false,bool centered=false,bool wrap=false,int font_size=15,bool right=false,bool disabled=false){
    if(text.empty())return;
    TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
    style.font_size_px=font_size;style.padding=2;style.centered=centered;style.right_aligned=right;style.wrap_lines=wrap;
    style.background={0,0,0,0};style.foreground=focused?cas_input_color:cas_label_color;
    if(disabled)style.foreground=cas_disabled_label_color;
    draw_text_overlay(frame,text,language,style);
}
void paint_field(odh::RasterFrame& frame,std::string_view value,const CasWidget& widget,
        std::uint8_t language,bool focused){
    const bool biography=widget.control==CasControl::bio;
    if(biography){
        // The existing wrapped editor owns paging; retain its end caret until
        // full cursor/selection editing is implemented, but never recolor text.
        label(frame,std::string(value)+(focused?"|":""),widget.box,language,true,false,true);
        return;
    }
    const int inset=widget.control==CasControl::family_name?5:4;
    TextOverlayStyle style;style.x=widget.box.x;style.y=widget.box.y;
    style.width=widget.box.width;style.height=widget.box.height;style.padding=inset;
    style.font_size_px=15;style.foreground=cas_input_color;style.background={0,0,0,0};
    if(!value.empty())draw_text_overlay(frame,value,language,style);
    if(focused){
        const auto width=value.empty()?0:measure_text_overlay(value,language,style.font_size_px).width;
        const auto caret=cas_edit_caret(widget.box,width,inset);
        for(int y=caret.y;y<caret.y+caret.height;++y)if(y>=0&&y<int(frame.height)&&caret.x>=0&&caret.x<int(frame.width))
            frame.pixels[std::size_t(y)*frame.width+std::size_t(caret.x)]={255,255,255,255};
    }
}
void paint_strip(odh::RasterFrame& frame,const OriginalUiArt& art,const CasWidget& widget,unsigned state){
    const auto& image=art.at(widget.image);
    if(!image.width||image.width%4||state>=4)throw odh::FormatError("invalid original CAS button state strip");
    const int width=int(image.width/4);
    odh::blit_color_keyed_bitmap(frame,image,widget.box,odh::UiBitmapRect{width*int(state),0,width,int(image.height)},magenta);
}
void paint_meter(odh::RasterFrame& frame,const odh::BitmapImage& image,int x,int y,int filled){
    const int width=std::min(filled,int(image.width));
    if(width>0)odh::blit_color_keyed_bitmap(frame,image,{x,y,width,int(image.height)},
        odh::UiBitmapRect{0,0,width,int(image.height)},magenta);
}
} // namespace

std::optional<LinuxCasFamilyDraft> show_linux_cas_family_creator(
        LinuxWindow& window,const LinuxCasCatalog& catalog,std::uint8_t language,const std::filesystem::path& game_root){
    if(language<1||language>20)throw std::invalid_argument("CAS screen language outside 1..20");
    const OriginalUiArt art(game_root,art_keys());
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
        odh::RasterFrame frame{};frame.width=800;frame.height=600;
        frame.pixels.assign(800U*600U,{0,0,0,255});
        frame.depth.assign(frame.pixels.size(),std::numeric_limits<float>::infinity());
        const auto& bg=art.at(character?character_background:family_background);
        if(bg.width!=800||bg.height!=600)throw odh::FormatError("invalid original CAS background");
        odh::blit_color_keyed_bitmap(frame,bg,0,0,magenta);
        const auto& text=character?character_text:family_text;
        const auto& title=text[character?15:9];window.set_title("OpenDollhouse | "+title);
        label(frame,title,{0,6,800,32},language,false,true,false,21);
        label(frame,text[character?26:12],{10,50,246,31},language,false,false,false,17,true);
        if(character){
            label(frame,character_text[16],{143,87,220,29},language,false,false,false,18);
            auto biography_label=character_text[24];
            if(!trim(form.name).empty()){
                biography_label=character_text[25];
                const auto marker=biography_label.find("%s");
                if(marker!=std::string::npos)biography_label.replace(marker,2,trim(form.name)+" "+trim(family_name));
            }
            label(frame,biography_label,{24,388,750,24},language,false,false,false,14);
            const auto remaining=25-std::accumulate(form.personality.begin(),form.personality.end(),0);
            paint_meter(frame,art.at(remaining_bar),117,342,remaining*6);
            if(!bodies.empty()&&!heads.empty()){
                const auto person=make_person(bodies,heads,form,form.name.empty()?" ":form.name,form.biography,language);
                if(person)if(const auto* image=preview(*person,false))
                    odh::blit_color_keyed_bitmap(frame,*image,{618,145,100,220},std::nullopt,magenta);
            }
        }
        for(std::size_t i=0;i<controls.size();++i){
            const auto& widget=controls[i];const auto c=widget.control;const bool focused=i==focus;
            if(widget.image){
                const unsigned state=cas_button_state(enabled(c),on(c),hovered==i,
                    hovered==i&&pointer.pressed_target()==i);
                paint_strip(frame,art,widget,state);
                if(c==C::cancel||c==C::done)label(frame,text[widget.text_index],widget.box,language,false,true,false,17,false,!enabled(c));
            }else if(c==C::member){
                const auto& member=draft.members()[widget.index];
                const odh::UiBitmapRect box{widget.box.x+15,widget.box.y+10,45,45};
                const auto& strip=art.at(portrait_frame);
                if(strip.width!=180||strip.height!=45)throw odh::FormatError("invalid CAS portrait frame");
                const int state=selected_member==widget.index?1:0;
                odh::blit_color_keyed_bitmap(frame,strip,box,odh::UiBitmapRect{state*45,0,45,45},magenta);
                if(const auto* image=preview(member,true))
                    odh::blit_color_keyed_bitmap(frame,*image,{box.x+2,box.y+2,41,41},std::nullopt,magenta);
                label(frame,member.name_utf8,{widget.box.x,widget.box.y+61,widget.box.width,28},
                    language,false,true,false,15);
            }else if(c==C::trait){
                label(frame,text[widget.text_index],{40,widget.box.y-1,178,32},language,false,false,false,19,true);
                paint_meter(frame,art.at(trait_bar),widget.box.x,widget.box.y,form.personality[widget.index]*6);
            }else{
                const auto& value=c==C::name?form.name:c==C::bio?form.biography:family_name;
                paint_field(frame,value,widget,language,focused);
            }
        }
        if(!feedback.empty()||!preview_error.empty())
            label(frame,feedback.empty()?preview_error:feedback,{36,565,728,29},language,true,true);
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
                const int target=clicked?((*click)[0]-widget.box.x+3)/6:
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
