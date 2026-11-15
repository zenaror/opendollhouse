// SPDX-License-Identifier: MPL-2.0
// Painter moved from linux_cas_screen.cpp, independent of its window/catalog/writer.
#include "cas_presentation.hpp"
#include "linux_text_renderer.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
namespace odh_host {
constexpr std::array<std::uint8_t,3> magenta{255,0,255};
constexpr const char* family_background="Nbhd/DsgnFamBkg.bmp";
constexpr const char* character_background="Nbhd/CreateACharBack.bmp";
constexpr const char* trait_bar="Nbhd/DesignCharPersLED.bmp";
constexpr const char* remaining_bar="Nbhd/CACPointRemainingBars.bmp";
constexpr const char* portrait_frame="CPanel/People/PeopleTemplate.bmp";

std::vector<std::string> cas_art_keys(){
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
        std::uint8_t language,bool focused,std::optional<std::size_t> cursor={}){
    const bool biography=widget.control==CasControl::bio;
    if(biography){
        // The existing wrapped editor owns paging; retain its end caret until
        // full cursor/selection editing is implemented, but never recolor text.
        auto display=std::string(value);if(focused)display.insert(std::min(cursor.value_or(value.size()),value.size()),"|");
        label(frame,display,widget.box,language,true,false,true);
        return;
    }
    const int inset=widget.control==CasControl::family_name?5:4;
    TextOverlayStyle style;style.x=widget.box.x;style.y=widget.box.y;
    style.width=widget.box.width;style.height=widget.box.height;style.padding=inset;
    style.font_size_px=15;style.foreground=cas_input_color;style.background={0,0,0,0};
    if(!value.empty())draw_text_overlay(frame,value,language,style);
    if(focused){
        const auto prefix=value.substr(0,std::min(cursor.value_or(value.size()),value.size()));
        const auto width=prefix.empty()?0:measure_text_overlay(prefix,language,style.font_size_px).width;
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
odh::RasterFrame CasPresentation::draw(const OriginalUiArt& art,std::uint8_t language)const{
    using C=CasControl;
        odh::RasterFrame frame{};frame.width=800;frame.height=600;
        frame.pixels.assign(800U*600U,{0,0,0,255});
        frame.depth.assign(frame.pixels.size(),std::numeric_limits<float>::infinity());
        const auto& bg=art.at(character?character_background:family_background);
        if(bg.width!=800||bg.height!=600)throw odh::FormatError("invalid original CAS background");
        odh::blit_color_keyed_bitmap(frame,bg,0,0,magenta);
        const auto& text=labels;
        const auto& title=text[character?15:9];
        label(frame,title,{0,6,800,32},language,false,true,false,21);
        label(frame,text[character?26:12],{10,50,246,31},language,false,false,false,17,true);
        if(character){
            label(frame,text[16],{143,87,220,29},language,false,false,false,18);
            auto biography_label=text[24];
            if(!name.empty()){
                biography_label=text[25];
                const auto marker=biography_label.find("%s");
                if(marker!=std::string::npos)biography_label.replace(marker,2,name+" "+family_name);
            }
            label(frame,biography_label,{24,388,750,24},language,false,false,false,14);
            const auto remaining=25-std::accumulate(traits.begin(),traits.end(),0);
            paint_meter(frame,art.at(remaining_bar),117,342,remaining*6);
            if(body_preview)if(const auto* image=body_preview())
                odh::blit_color_keyed_bitmap(frame,*image,cas_preview_box,std::nullopt,magenta);
        }
        for(std::size_t i=0;i<controls.size();++i){
            const auto& widget=controls[i];const auto c=widget.control;const bool focused=i==focus;
            if(widget.image){
                const unsigned state=cas_button_state(enabled(c),on(c),hovered==i,
                    hovered==i&&pressed==i);
                paint_strip(frame,art,widget,state);
                if(c==C::cancel||c==C::done)label(frame,text[widget.text_index],widget.box,language,false,true,false,17,false,!enabled(c));
            }else if(c==C::member){
                const auto& member=members[widget.index];
                const odh::UiBitmapRect box{widget.box.x+15,widget.box.y+10,45,45};
                const auto& strip=art.at(portrait_frame);
                if(strip.width!=180||strip.height!=45)throw odh::FormatError("invalid CAS portrait frame");
                const int state=selected==widget.index?1:0;
                odh::blit_color_keyed_bitmap(frame,strip,box,odh::UiBitmapRect{state*45,0,45,45},magenta);
                if(const auto* image=portrait?portrait(widget.index):nullptr)
                    odh::blit_color_keyed_bitmap(frame,*image,{box.x+5,box.y+2,35,41},std::nullopt,magenta);
                label(frame,member,{widget.box.x,widget.box.y+61,widget.box.width,28},
                    language,false,true,false,15);
            }else if(c==C::trait){
                label(frame,text[widget.text_index],{40,widget.box.y-1,178,32},language,false,false,false,19,true);
                paint_meter(frame,art.at(trait_bar),widget.box.x,widget.box.y,traits[widget.index]*6);
            }else{
                const auto& value=c==C::name?name:c==C::bio?biography:family_name;
                paint_field(frame,value,widget,language,focused,cursor);
            }
        }
        if(!feedback.empty())
            label(frame,feedback,{36,565,728,29},language,true,true);
        return frame;
}
}
