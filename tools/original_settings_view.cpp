// SPDX-License-Identifier: MPL-2.0
#include "original_settings_view.hpp"
#include "linux_text_renderer.hpp"
#include "linux_ui_strings.hpp"
#include <algorithm>
#include <cmath>

namespace odh_host {
namespace {
constexpr const char* checkbox="CPanel/Buttons/optcheckbox.bmp";
constexpr const char* radio="CPanel/Buttons/optradio.bmp";
constexpr const char* slider_off="CPanel/Buttons/optslideroff.bmp";
constexpr const char* tutorial_reset="CPanel/Buttons/OptTutReset.bmp";
std::vector<std::string> settings_assets(bool tutorial){
    std::vector<std::string> keys{checkbox,radio,"CPanel/Buttons/optslideroff.bmp","CPanel/Buttons/optslideron.bmp"};
    if(tutorial)keys.emplace_back(tutorial_reset);return keys;
}
constexpr const char* slider_on="CPanel/Buttons/optslideron.bmp";
std::size_t index(OriginalSettingId id){return static_cast<std::size_t>(id);}
int maximum(OriginalSettingControlKind kind){return kind==OriginalSettingControlKind::slider?100:kind==OriginalSettingControlKind::radio?2:1;}
bool enabled(const OriginalSettingControl& control,const OriginalSettingsState& state,const OriginalSettingsCapabilities& capabilities){
    const auto i=index(control.id);
    return i<original_setting_count&&capabilities[i]&&state.values[i]&&*state.values[i]>=0&&*state.values[i]<=maximum(control.kind);
}
int thumb_width(const BuyUiRect& rect){return std::min(rect.width,std::max(1,int(std::lround(rect.height*8./14))));}
}
OriginalSettingsLayout original_settings_layout(std::uint32_t width,std::uint32_t height,OriginalSettingsPage page,const OriginalGraphicsMetrics& graphics_metrics){
    OriginalSettingsLayout out;out.scale=original_control_panel_layout(width,height).scale;
    const auto rect=[&](int x,int y,int w,int h){return BuyUiRect{int(std::lround(x*out.scale)),
        int(height-std::lround((768-y)*out.scale)),std::max(1,int(std::lround(w*out.scale))),std::max(1,int(std::lround(h*out.scale)))};};
    const auto check=[&](OriginalSettingId id,std::size_t label,int x,int y){
        out.controls.push_back({id,OriginalSettingControlKind::checkbox,rect(x,y,20,21),0});
        out.labels.push_back({label,rect(x+30,y-1,145,20),false});
    };
    if(page==OriginalSettingsPage::graphics){
        // UIText145[12..41], original constructor0051449d. Coordinates
        // remain local to the Options parent (220,height-100 at scale1).
        const auto language_code=graphics_metrics.original_language_code;
        const int checkbox_dy=language_code==0x11?4:0;
        const int label_dy=language_code==0x11?4:language_code==0x13?-6:0;
        const int detail_dy=language_code==0x13?-3:0;
        const int label_height=std::clamp(graphics_metrics.label_text_height,1,30);
        const OriginalSettingId ids[]={OriginalSettingId::anti_alias,OriginalSettingId::shadows,
            OriginalSettingId::lighting,OriginalSettingId::interface_effects};
        for(unsigned row=0;row<4;++row){
            out.controls.push_back({ids[row],OriginalSettingControlKind::checkbox,
                rect(465,675+int(row)*20+checkbox_dy,20,21),0});
            out.labels.push_back({14+row*3,rect(495,677+int(row)*20+label_dy,200,label_height),false});
        }
        const int title_width=std::clamp(graphics_metrics.detail_title_width,1,480);
        const auto details=[&](OriginalSettingId id,std::size_t title,int y){
            out.labels.push_back({title,rect(700-title_width,y+detail_dy,title_width,label_height),false,true});
            // Original80px group5378dd: left, midpoint, right positions of
            // three20px children. Shared headings are drawn once below.
            for(int choice=0;choice<3;++choice)
                out.controls.push_back({id,OriginalSettingControlKind::radio,rect(705+choice*30,y,20,20),choice});
        };
        details(OriginalSettingId::terrain_detail,28,698);
        details(OriginalSettingId::character_detail,31,724);
        const int header_height=std::clamp(graphics_metrics.header_text_height,3,30);
        for(int column=0;column<3;++column)
            out.labels.push_back({std::size_t(39+column),rect(697+column*31,691-header_height,31,header_height-2),true});
    }else if(page==OriginalSettingsPage::sound){
        // Authored strip placement measured in the original game's 1024x768
        // sound page. This unchanged page uses the Complete Collection STR145.
        const OriginalSettingId ids[]={OriginalSettingId::sound_effects,OriginalSettingId::music,OriginalSettingId::voices};
        for(unsigned row=0;row<3;++row){
            out.labels.push_back({44+row*3,rect(525,681+int(row)*28,103,20),false});
            out.controls.push_back({ids[row],OriginalSettingControlKind::slider,rect(632,682+int(row)*28,130,14),0});
        }
    }else if(page==OriginalSettingsPage::play){
        check(OriginalSettingId::auto_center,56,465,678);
        check(OriginalSettingId::free_will,59,465,698);
        check(OriginalSettingId::edge_scrolling,62,465,718);
        check(OriginalSettingId::background_simulation,65,465,738);
        check(OriginalSettingId::quick_tips,72,645,676);
        check(OriginalSettingId::auto_snapshot,75,645,696);
        check(OriginalSettingId::live_pip,78,645,716);
        // Actual Complete PC observation confirms Export HTML in this fourth
        // row, alongside the other seven checkboxes (STR145[81]).
        check(OriginalSettingId::export_html,81,645,736);
    }
    return out;
}
OriginalSettingsView::OriginalSettingsView(const std::filesystem::path& game,std::uint8_t language,bool show_anti_alias,bool show_tutorial_reset,
        const OriginalSettingsText* selected_text):
    art_(game,settings_assets(show_tutorial_reset)),language_(language),show_anti_alias_(show_anti_alias),show_tutorial_reset_(show_tutorial_reset){
    if(language<1||language>20)throw odh::FormatError("settings language outside supported range");
    for(const auto* key:{checkbox,radio}){
        const auto& image=art_.at(key);
        if(image.width!=120||image.height!=(key==checkbox?21u:20u))
            throw odh::FormatError("original settings selection strip dimensions differ");
    }
    for(const auto* key:{slider_off,slider_on}){
        const auto& image=art_.at(key);
        if(image.width!=44||image.height!=14)throw odh::FormatError("original settings slider dimensions differ");
    }
    if(show_tutorial_reset_ && (art_.at(tutorial_reset).width!=88 || art_.at(tutorial_reset).height!=30))
        throw odh::FormatError("original tutorial reset strip dimensions differ");
    // Native callers provide the already selected runtime STR (including PIFF)
    // as UTF8. Standalone/legacy callers retain the original read-only fallback.
    if(selected_text)strings_=*selected_text;
    else {
        GameUiStrings strings(game);
        for(std::size_t i=0;i<strings_.size();++i)strings_[i]=strings.get(145,i,language,"");
    }
}
OriginalSettingsLayout OriginalSettingsView::layout(std::uint32_t width,std::uint32_t height,OriginalSettingsPage page)const{
    OriginalGraphicsMetrics metrics;
    if(page==OriginalSettingsPage::graphics){
        const auto scale=original_control_panel_layout(width,height).scale;
        const int font_size=std::max(6,int(std::lround(11*scale)));
        int title_width=0,header_height=0,label_height=0;
        for(const auto id:{28u,31u})title_width=std::max(title_width,
            measure_text_overlay(strings_[id],language_,font_size).width);
        for(const auto id:{14u,17u,20u,23u,28u,31u})label_height=std::max(label_height,
            measure_text_overlay(strings_[id],language_,font_size).height);
        for(const auto id:{39u,40u,41u})header_height=std::max(header_height,
            measure_text_overlay(strings_[id],language_,font_size).height);
        metrics.detail_title_width=std::max(1,int(std::ceil(title_width/scale)));
        metrics.header_text_height=std::max(3,int(std::ceil(header_height/scale)));
        metrics.label_text_height=std::max(1,int(std::ceil(label_height/scale)));
    }
    auto out=original_settings_layout(width,height,page,metrics);
    // Observed Complete PC page omits the unsupported AA checkbox without
    // moving the remaining original rows. Other presentation callers opt in.
    if(!show_anti_alias_ && page==OriginalSettingsPage::graphics){
        out.controls.erase(std::remove_if(out.controls.begin(),out.controls.end(),[](const auto& c){return c.id==OriginalSettingId::anti_alias;}),out.controls.end());
        out.labels.erase(std::remove_if(out.labels.begin(),out.labels.end(),[](const auto& l){return l.string_index==14;}),out.labels.end());
    }
    return out;
}
std::optional<std::size_t> OriginalSettingsView::hit(const OriginalSettingsLayout& layout,int x,int y,
        const OriginalSettingsState& state,const OriginalSettingsCapabilities& capabilities)const{
    for(std::size_t i=0;i<layout.controls.size();++i)
        if(layout.controls[i].rect.contains(x,y)&&enabled(layout.controls[i],state,capabilities))return i;
    return {};
}
std::optional<OriginalSettingsEvent> OriginalSettingsView::activate(const OriginalSettingsLayout& layout,std::size_t selected,int x,
        const OriginalSettingsState& state,const OriginalSettingsCapabilities& capabilities)const{
    if(selected>=layout.controls.size())return {};
    const auto& control=layout.controls[selected];
    if(!enabled(control,state,capabilities)||control.rect.width<=0||control.rect.height<=0)return {};
    int value;
    if(control.kind==OriginalSettingControlKind::checkbox)value=1-*state.values[index(control.id)];
    else if(control.kind==OriginalSettingControlKind::radio){
        if(control.value<0||control.value>2)return {};
        value=control.value;
    }
    else{
        const auto thumb=thumb_width(control.rect),travel=control.rect.width-thumb;
        if(travel<=0)return {};
        const double relative=double(x)-control.rect.x-thumb*.5;
        value=int(std::lround(std::clamp(relative/travel,0.,1.)*100));
    }
    return OriginalSettingsEvent{control.id,value};
}
void OriginalSettingsView::draw(odh::RasterFrame& frame,OriginalSettingsPage page,const OriginalSettingsState& state,
        const OriginalSettingsCapabilities& capabilities,std::optional<std::array<int,2>> pointer,
        std::optional<std::size_t> pressed)const{
    const auto layout=this->layout(frame.width,frame.height,page);
    const auto hovered=pointer?hit(layout,(*pointer)[0],(*pointer)[1],state,capabilities):std::nullopt;
    for(std::size_t i=0;i<layout.controls.size();++i){
        const auto& control=layout.controls[i];const auto available=enabled(control,state,capabilities);
        const auto value=state.values[index(control.id)];
        if(control.kind==OriginalSettingControlKind::slider){
            const auto& image=art_.at(available&&(hovered==i||pressed==i)?slider_on:slider_off);
            odh::blit_color_keyed_bitmap(frame,image,control.rect,odh::UiBitmapRect{0,0,33,14},{255,0,255});
            if(value&&*value>=0&&*value<=100){
                auto thumb=control.rect;thumb.width=thumb_width(thumb);
                thumb.x+=int(std::lround((control.rect.width-thumb.width)*(*value/100.)));
                odh::blit_color_keyed_bitmap(frame,image,thumb,odh::UiBitmapRect{35,0,8,14},{255,0,255});
            }
        }else{
            const auto& image=art_.at(control.kind==OriginalSettingControlKind::checkbox?checkbox:radio);
            const bool selected=value&&(control.kind==OriginalSettingControlKind::checkbox?*value==1:*value==control.value);
            const unsigned cell=(available?(hovered==i?2:0):4)+(selected?1:0);
            odh::blit_color_keyed_bitmap(frame,image,control.rect,
                odh::UiBitmapRect{int(cell)*20,0,20,int(image.height)},{255,0,255});
        }
    }
    if(page==OriginalSettingsPage::play && show_tutorial_reset_){
        // Original frame0 matched observed1024x768 ink at999,675. Keep the
        // source strip's unavailable frame, with no invented tutorial action.
        const auto scaled=[&](int n){return int(std::lround(n*layout.scale));};
        const BuyUiRect rect{int(frame.width)-scaled(25),int(frame.height)-scaled(93),scaled(22),scaled(30)};
        odh::blit_color_keyed_bitmap(frame,art_.at(tutorial_reset),rect,odh::UiBitmapRect{66,0,22,30},{255,0,255});
    }
    for(const auto& label:layout.labels){
        if(label.string_index>=strings_.size()||strings_[label.string_index].empty())continue;
        TextOverlayStyle style;style.x=label.rect.x;style.y=label.rect.y;style.width=label.rect.width;style.height=label.rect.height;
        style.padding=0;style.font_size_px=std::max(6,int(std::lround(11*layout.scale)));
        style.background={0,0,0,0};style.foreground={255,255,255,255};style.centered=label.centered;style.right_aligned=label.right_aligned;
        (void)draw_text_overlay(frame,strings_[label.string_index],language_,style);
    }
}
}

namespace odh_host {
std::optional<OriginalSettingsEvent> OriginalSettingsInteraction::update(const OriginalSettingsView& view,
        const OriginalSettingsLayout& layout,const OriginalSettingsState& state,
        const OriginalSettingsCapabilities& capabilities,OriginalSettingsPointerInput in){
    if(in.cancel || (!in.down&&!in.held&&!in.up)){reset();return {};}
    if(in.down)captured_=view.hit(layout,in.x,in.y,state,capabilities);
    if(!captured_)return {};
    const auto control=*captured_;
    if(control>=layout.controls.size()){reset();return {};}
    if(!view.activate(layout,control,in.x,state,capabilities)){reset();return {};}
    const bool slider=layout.controls[control].kind==OriginalSettingControlKind::slider;
    std::optional<OriginalSettingsEvent> event;
    if(slider && (in.down||in.held||in.up))event=view.activate(layout,control,in.x,state,capabilities);
    else if(in.up && view.hit(layout,in.x,in.y,state,capabilities)==captured_)
        event=view.activate(layout,control,in.x,state,capabilities);
    if(in.up)reset();
    return event;
}
}
