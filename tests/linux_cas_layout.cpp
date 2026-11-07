// SPDX-License-Identifier: MPL-2.0
#include "linux_cas_layout.hpp"
#include "linux_cas_draft.hpp"
#include <stdexcept>
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){
    using namespace odh_host;
    for(unsigned members=0;members<=8;++members){
        const auto family=cas_layout(false,members);
        require(family.size()==6+members,"all draft member slots remain reachable");
        for(std::size_t i=0;i<family.size();++i){
            const auto& box=family[i].box;
            require(cas_widget_at(family,box.x+box.width/2,box.y+box.height/2)==i,"family point does not select its displayed control");
        }
    }
    const auto editor=cas_layout(true,8);
    for(std::size_t i=0;i<editor.size();++i){
        const auto& box=editor[i].box;
        require(cas_widget_at(editor,box.x+box.width/2,box.y+box.height/2)==i,"editor point does not select its displayed control");
    }
    require(!cas_widget_at(editor,618,145),"live preview does not mutate the outfit");
    const auto family=cas_layout(false,0);
    require(cas_widget_at(family,655,153)==1&&cas_widget_at(family,593,103)==1,
        "family Add accepts its interior and transparent corner");
    require(editor[*cas_widget_at(editor,497,120)].control==CasControl::child,
        "overlap follows frontmost installed control");
    require(family[4].text_index==3&&family[5].text_index==4&&editor[13].text_index==11&&editor[14].text_index==12,
        "active design constructors assign the first Done and Cancel entries in each language");
    require(!cas_control_enabled(CasControl::add,false,false,false,0,false)&&
        cas_control_enabled(CasControl::add,false,true,false,0,false)&&
        !cas_control_enabled(CasControl::add,false,true,false,8,true),"Add requires a surname and an available slot");
    require(!cas_control_enabled(CasControl::done,false,true,false,0,false)&&
        !cas_control_enabled(CasControl::done,false,false,false,1,false)&&
        cas_control_enabled(CasControl::done,false,true,false,1,false),"family completion requires both surname and resident");
    require(!cas_control_enabled(CasControl::done,true,true,false,0,false)&&
        cas_control_enabled(CasControl::done,true,true,true,0,false),"Sim editor Done requires first name");
    require(family[4].box.x+family[4].box.width/2==200&&family[4].box.width==112&&
        family[5].box.x+family[5].box.width/2==600&&family[5].box.width==140,"Portuguese buttons match photographed widths and centers");
    require(cas_question_uses_ok_cancel(CasQuestion::discard_family)&&
        !cas_question_uses_ok_cancel(CasQuestion::unused_points),"discard uses OK/Cancel while unused points use Yes/No");
    std::array<std::uint8_t,5> traits{};
    step_cas_trait(traits,0,5);require(traits[0]==1,"a middle click adds only one point");
    step_cas_trait(traits,0,5);require(traits[0]==2,"a second click adds one more point");
    step_cas_trait(traits,0,0);require(traits[0]==1,"clicking left removes only one point");
    step_cas_trait(traits,0,1);require(traits[0]==1,"current target preserves the trait");
    traits={10,10,5,0,0};step_cas_trait(traits,4,9);
    require(traits[4]==0,"all 25 points already spent");
    step_cas_trait(traits,2,0);step_cas_trait(traits,0,12);step_cas_trait(traits,4,9);
    require(traits[2]==4&&traits[0]==10&&traits[4]==1,"a freed point can be spent within the per-trait maximum");
    step_cas_trait(traits,3,-1);require(traits[3]==0,"zero never underflows");
    CasPointer pointer;
    WindowInput press,release;
    press.primary_click=std::array<std::int32_t,2>{655,153};
    append_window_pointer_event(press,{WindowPointerKind::press,655,153});
    append_window_pointer_event(release,{WindowPointerKind::release,655,153});
    require(!pointer.click(family,press)&&pointer.pressed_target()==1,
        "press exposes the captured Add target for its original pressed artwork without activating");
    require(pointer.click(family,release)==press.primary_click&&!pointer.pressed_target(),
        "ordinary click opens on release and clears its pressed artwork even across polls");
    auto other=release;other.pointer_events[0].y=230;
    require(!pointer.click(family,press)&&!pointer.click(family,other),"drag from Add to Remove triggers neither");
    WindowInput cancelled;cancelled.pointer_cancelled=true;
    require(!pointer.click(family,press)&&!pointer.click(family,cancelled)&&!pointer.click(family,release),
        "lost focus cancels the outstanding CAS press");
    const auto modal_hit=[](int x,int y)->std::optional<std::uint8_t>{
        if(y<0||y>=20||x<0||x>=40)return {};
        return x<20?0:1;
    };
    const auto button_event=[](WindowPointerKind kind,int x){
        WindowInput event;append_window_pointer_event(event,{kind,x,10});
        if(kind==WindowPointerKind::press)event.primary_click=std::array<std::int32_t,2>{x,10};
        return event;
    };
    require(!pointer.click_on(button_event(WindowPointerKind::press,5),modal_hit)&&
        !pointer.click_on(button_event(WindowPointerKind::release,90),modal_hit),"releasing Yes outside does not confirm");
    require(!pointer.click_on(button_event(WindowPointerKind::press,5),modal_hit)&&
        !pointer.click_on(button_event(WindowPointerKind::release,30),modal_hit),"dragging Yes onto No does not answer");
    require(!pointer.click_on(button_event(WindowPointerKind::press,30),modal_hit)&&
        pointer.click_on(button_event(WindowPointerKind::release,30),modal_hit).has_value(),"explicit No completes on release");
    require(!pointer.click_on(button_event(WindowPointerKind::press,5),modal_hit)&&
        !pointer.click_on(cancelled,modal_hit)&&!pointer.click_on(button_event(WindowPointerKind::release,5),modal_hit),
        "modal release after losing focus cannot confirm");
    require(!pointer.click(family,press),"base press only captures");
    pointer.reset(); // Opening a question via Escape clears the old base capture.
    require(!pointer.click_on(button_event(WindowPointerKind::press,30),modal_hit),"modal No press only captures");
    pointer.reset(); // Escape dismisses the question and clears its capture too.
    require(!pointer.click(family,release),"Escape out of a question cannot reactivate a held base control");
    const odh::UiBitmapRect edit_box{275,52,255,25};
    const auto empty_caret=cas_edit_caret(edit_box,0,4),long_caret=cas_edit_caret(edit_box,1024,4);
    require(empty_caret.x==279&&empty_caret.y>=edit_box.y&&empty_caret.y+empty_caret.height<=edit_box.y+edit_box.height,
        "empty name caret uses original inset and stays inside editable field");
    require(long_caret.x+long_caret.width<edit_box.x+edit_box.width&&long_caret.height==empty_caret.height,
        "long name cannot draw its end caret over another control");
    const auto deletion=cas_question_text(CasQuestion::remove_member);
    const auto discard=cas_question_text(CasQuestion::discard_family);
    const auto finish=cas_question_text(CasQuestion::finish_family);
    const auto points=cas_question_text(CasQuestion::unused_points);
    require(!deletion.character&&deletion.title==5&&deletion.message==6&&
        !discard.character&&discard.title==7&&discard.message==8,"family deletion and cancellation use original prompts");
    require(!finish.character&&finish.title==13&&finish.message==14&&
        points.character&&points.title==13&&points.message==14,"family confirmation and character points use their own table");
    LinuxCasDraftMembers draft;
    SimitoneCasPersonInput member;member.name_utf8="José";
    draft.accept(member);
    auto editing=draft.select(0);editing.name_utf8="Ana";draft.begin_new();
    require(draft.members().front().name_utf8=="José","canceling the typed form changed a draft member");
    draft.select(0);draft.accept(editing);
    require(draft.members().size()==1&&draft.members().front().name_utf8=="Ana","edit did not replace the chosen member");
    for(int n=1;n<8;++n)draft.accept(member);
    bool blocked=false;
    try{draft.accept(member);}catch(const std::invalid_argument&){blocked=true;}
    require(blocked&&draft.members().size()==8,"ninth member changed the full family");
    draft.select(3);require(draft.erase_selected()&&draft.members().size()==7,"delete did not remove the chosen draft Sim");
}
