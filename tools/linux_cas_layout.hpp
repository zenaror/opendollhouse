// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ui_bitmap.hpp"
#include "linux_window.hpp"
#include <algorithm>
#include <array>
#include <numeric>
#include <vector>

namespace odh_host {
enum class CasQuestion { remove_member,discard_family,finish_family,unused_points };
struct CasQuestionText { bool character;std::size_t title,message; };
inline CasQuestionText cas_question_text(CasQuestion question){
    switch(question){
    case CasQuestion::remove_member:return {false,5,6};
    case CasQuestion::discard_family:return {false,7,8};
    case CasQuestion::finish_family:return {false,13,14};
    case CasQuestion::unused_points:return {true,13,14};
    }
    throw odh::FormatError("unknown CAS confirmation");
}
inline bool cas_question_uses_ok_cancel(CasQuestion question){return question==CasQuestion::discard_family;}
enum class CasControl {
    family_name,add,remove,edit,done,cancel,member,name,bio,adult,child,female,male,dark,medium,light,
    body_previous,body_next,head_previous,head_next,trait
};
inline bool cas_control_enabled(CasControl control,bool character,bool family_named,bool sim_named,
                                std::size_t members,bool member_selected){
    if(control==CasControl::add)return family_named&&members<8;
    if(control==CasControl::edit||control==CasControl::remove)return member_selected;
    if(control==CasControl::done)return character?sim_named:family_named&&members>0;
    return true;
}
inline void step_cas_trait(std::array<std::uint8_t,5>& traits,std::size_t index,int target){
    auto& points=traits.at(index);
    if(target<points&&points>0)--points;
    else if(target>points&&points<10&&std::accumulate(traits.begin(),traits.end(),0)<25)++points;
}
struct CasWidget {
    CasControl control;
    odh::UiBitmapRect box;
    const char* image=nullptr;
    int text_index=-1;
    std::size_t index=0;
};
inline std::vector<CasWidget> cas_layout(bool character,std::size_t members){
    using C=CasControl;
    if(members>8)throw odh::FormatError("CAS family exceeds eight slots");
    // cWinDesignFamily 00548897 and cWinDesignCharacter 00545b04 use these
    // coordinates relative to their centered 800x600 backgrounds.
    if(!character){
        std::vector<CasWidget> result{
            {C::family_name,{274,50,255,25}},
            {C::add,{593,103,139,103},"Nbhd/AddPersonBtn.bmp",0},
            {C::remove,{594,208,149,99},"Nbhd/DeletePersonBtn.bmp",1},
            {C::edit,{594,311,137,99},"Nbhd/EditPersonBtn.bmp",2},
            {C::done,{144,529,112,62},"Nbhd/NbhdTileBtn.bmp",3},
            {C::cancel,{530,529,140,62},"Nbhd/NbhdTileBtn.bmp",4}};
        for(std::size_t i=0;i<members;++i)result.push_back({C::member,
            {111+int(i%4)*120,121+int(i/4)*140,85,105},nullptr,-1,i});
        return result;
    }
    std::vector<CasWidget> result{
        {C::name,{275,52,255,25}}, {C::bio,{22,425,761,96}},
        {C::adult,{497,101,57,100},"Nbhd/DesignCharAdultBtn.bmp",0},
        {C::child,{406,111,92,90},"Nbhd/DesignCharChildBtn.bmp",1},
        {C::female,{499,274,57,121},"Nbhd/DesignCharFemaleBtn.bmp",2},
        {C::male,{400,274,98,111},"Nbhd/DesignCharMaleBtn.bmp",3},
        {C::dark,{499,202,57,71},"Nbhd/DesignCharDarkBtn.bmp",4},
        {C::medium,{452,202,46,71},"Nbhd/DesignCharMediumBtn.bmp",5},
        {C::light,{396,202,55,71},"Nbhd/DesignCharLightBtn.bmp",6},
        {C::body_previous,{580,291,18,34},"Nbhd/DesignCharSkinsLeftBtn.bmp",7},
        {C::body_next,{729,291,18,34},"Nbhd/DesignCharSkinsRightBtn.bmp",8},
        {C::head_previous,{597,160,18,34},"Nbhd/DesignCharSkinsLeftBtn.bmp",9},
        {C::head_next,{714,160,18,34},"Nbhd/DesignCharSkinsRightBtn.bmp",10},
        {C::done,{144,529,112,62},"Nbhd/NbhdTileBtn.bmp",11},
        {C::cancel,{530,529,140,62},"Nbhd/NbhdTileBtn.bmp",12}};
    for(std::size_t i=0;i<5;++i)result.push_back({C::trait,{228,131+int(i)*32,59,25},
        nullptr,17+int(i),i});
    return result;
}
// Original design screens use subdued labels and white editable text, not
// the diagnostic yellow used by the former keyboard-focus presentation.
inline constexpr std::array<std::uint8_t,4> cas_label_color{187,198,195,255};
inline constexpr std::array<std::uint8_t,4> cas_disabled_label_color{62,89,86,255};
inline constexpr std::array<std::uint8_t,4> cas_input_color{248,248,248,255};
inline odh::UiBitmapRect cas_edit_caret(const odh::UiBitmapRect& box,int text_width,int inset){
    const auto right=box.x+box.width-inset-1;
    return {std::min(right,box.x+inset+std::max(0,text_width)),box.y+3,1,std::max(1,box.height-6)};
}
inline unsigned cas_button_state(bool enabled,bool selected,bool hovered,bool pressed){
    return !enabled?3U:selected||pressed?1U:hovered?2U:0U;
}
inline std::optional<std::size_t> cas_widget_at(const std::vector<CasWidget>& controls,int x,int y){
    // cTSWinBtn 005e3f57 checks its rectangle. Its separate pixel-test flag
    // (0x200) is not requested by the design screens (they set 0x2000).
    for(std::size_t i=controls.size();i-->0;)if(controls[i].box.contains(x,y))return i;
    return {};
}
class CasPointer {
public:
    std::optional<std::array<std::int32_t,2>> click(const std::vector<CasWidget>& controls,const WindowInput& input){
        return click_on(input,[&](int x,int y){return cas_widget_at(controls,x,y);});
    }
    template<class Hit>
    std::optional<std::array<std::int32_t,2>> click_on(const WindowInput& input,const Hit& hit){
        if(input.pointer_cancelled||input.pointer_overflow){reset();return {};}
        if(input.pointer_events.empty())return input.primary_click;
        for(const auto& event:input.pointer_events){
            if(event.kind==WindowPointerKind::motion)continue;
            const std::optional<std::size_t> target=hit(event.x,event.y);
            if(event.kind==WindowPointerKind::press)pressed_=target;
            else{
                const bool activate=target&&pressed_==target;
                reset();
                if(activate)return std::array<std::int32_t,2>{event.x,event.y};
            }
        }
        return {};
    }
    std::optional<std::size_t> pressed_target()const noexcept{return pressed_;}
    void reset(){pressed_.reset();}
private:
    std::optional<std::size_t> pressed_;
};
}
