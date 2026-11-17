// SPDX-License-Identifier: MPL-2.0
// Extracted from buy_mode_view.cpp; the existing portable panel implementation.
#include "original_control_panel.hpp"
#include "original_button_state.hpp"
#include "retained_panel_art.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>

namespace odh_host {
using namespace odh;
namespace original_panel_detail {
void blit(RasterFrame& frame,const BitmapImage& image,BuyUiRect box){
    blit_color_keyed_bitmap(frame,image,box,std::nullopt,{255,0,255});
}
void blit(RasterFrame& frame,const BitmapImage& image,BuyUiRect box,BuyUiRect crop){
    blit_color_keyed_bitmap(frame,image,box,crop,{255,0,255});
}
}
using original_panel_detail::blit;
static void blit(UiBitmapSurface frame,const BitmapImage& image,BuyUiRect box){
    blit_color_keyed_bitmap(frame,image,box,std::nullopt,{255,0,255});
}
static void blit(UiBitmapSurface frame,const BitmapImage& image,BuyUiRect box,BuyUiRect crop){
    blit_color_keyed_bitmap(frame,image,box,crop,{255,0,255});
}
OriginalControlPanelLayout original_control_panel_layout(std::uint32_t width,std::uint32_t height){
    if(width<320||width>3840||height<200||height>2160||static_cast<std::uint64_t>(width)*height>8294400)throw FormatError("original panel viewport outside native raster bounds");
    // Presentation-only budget matches the current desktop canvas API. This
    // does not enlarge the separate world/scene rasterizer budgets.
    // Desktop TS1 keeps its authored UI pixels when the world viewport grows.
    // Only reduce the complete composition for smaller portable viewports.
    OriginalControlPanelLayout l;l.scale=std::min({1.0f,static_cast<float>(width)/800.0f,static_cast<float>(height-32)/183.0f});
    const auto s=l.scale;const auto baseline=static_cast<float>(height)-600*s;
    const auto rect=[&](float x,float y,float w,float h){return BuyUiRect{static_cast<std::int32_t>(std::lround(x*s)),static_cast<std::int32_t>(std::lround(baseline+y*s)),static_cast<std::int32_t>(std::lround(w*s)),static_cast<std::int32_t>(std::lround(h*s))};};
    l.base={0,rect(0,500,0,100).y,static_cast<std::int32_t>(width),rect(0,500,0,100).height};
    l.panel_background=rect(220,500,804,100);l.left_patch=rect(0,417,220,183);
    l.modes={rect(10,423,50,50),rect(69,437,45,47),rect(117,462,39,39)};
    l.clock=rect(83,535,82,14);l.budget=rect(96,577,92,18);l.portraits=rect(229,505,180,90);l.right_content=rect(522,500,278,100);
    // Original LiveGadget is the tab divider BETWEEN family portraits and
    // needs. Asset dimensions are authored; positioning follows the original
    // control-panel composition, not a right-anchored desktop toolbar.
    l.live_gadget=rect(414,500,108,100);l.camera_patch=rect(158,500,55,100);
    l.rotate_left=rect(43,544,27,27);l.rotate_right=rect(9,544,27,27);
    l.zoom_in=rect(26,522,27,27);l.zoom_out=rect(26,566,27,27);
    l.walls_up=rect(56,497,21,21);l.walls_down=rect(103,512,22,15);l.pause=rect(85,552,15,15);
    l.level_up=rect(5,494,22,15);l.level_down=rect(5,509,21,11);l.roof=rect(32,495,22,19);
    l.walls_cutaway=rect(79,502,21,21);l.camera=rect(175,512,29,29);l.options=rect(193,542,23,23);
    l.speeds={rect(105,552,11,15),rect(120,552,20,15),rect(141,552,32,15)};
    l.tabs={rect(422,531,32,39),rect(489,504,27,30),rect(489,535,27,30),
        rect(457,535,27,30),rect(457,504,27,30),rect(489,566,27,30)};
    l.needs_title=rect(528,503,164,17);
    l.green_mood=rect(425,503,27,25);l.red_mood=rect(425,573,27,25);
    // Measured from the occupied original at1024x768: meters540..599 and
    //640..699, top rows701/720/739/758, each five pixels high.
    for(std::size_t n=0;n<8;++n)l.needs[n]=rect(540+static_cast<float>(n/4)*100,520+static_cast<float>(n%4)*19,60,19);
    return l;
}
std::array<BuyUiRect,8> original_portrait_rects(const OriginalControlPanelLayout& layout,std::size_t residents){
    std::array<BuyUiRect,8> portraits{};
    constexpr int columns=4;
    const auto side=std::max(1,std::min({static_cast<int>(std::lround(45*layout.scale)),
        layout.portraits.width/columns,layout.portraits.height/2}));
    for(std::size_t n=0;n<std::min(residents,portraits.size());++n)
        portraits[n]={layout.portraits.x+static_cast<int>(n%columns)*side,
            layout.portraits.y+static_cast<int>(n/columns)*side,side,side};
    return portraits;
}
namespace {
std::vector<std::string> required_panel_art(){
    std::vector<std::string> needed{"cpanel\\backgrounds\\panelback.bmp","cpanel\\backgrounds\\livepatch.bmp","cpanel\\backgrounds\\buypatch.bmp","cpanel\\backgrounds\\buildpatch.bmp",
        "cpanel\\catalogunknown.bmp","cpanel\\buttons\\objects.bmp","cpanel\\buttons\\people.bmp","cpanel\\buttons\\arch.bmp","cpanel\\buttons\\scrollleft.bmp","cpanel\\buttons\\scrollright.bmp"};
    for(const auto* name:original_panel_category_art)needed.emplace_back(name);
    return needed;
}
std::vector<std::string> optional_panel_art(){
    std::vector<std::string> optional{"cpanel\\backgrounds\\backpatch.bmp","cpanel\\backgrounds\\universalback.tga","cpanel\\catalog\\thumbtemplate.bmp","cpanel\\people\\peopletemplate.bmp","cpanel\\backgrounds\\livegadget.bmp","cpanel\\backgrounds\\camerapatch.bmp","cpanel\\greenbars.bmp","cpanel\\redbars.bmp","cpanel\\people\\thumbback.bmp","cpanel\\people\\unknownthumbnail.bmp",
        "cpanel\\jobsubbars.bmp","cpanel\\backgrounds\\jobsubbarsoff.bmp","cpanel\\backgrounds\\persbkg.bmp",
        "cpanel\\backgrounds\\tallsubpanel.tga",
        "cpanel\\skillshilite.bmp","cpanel\\buttons\\jobiconmultibutton.bmp",
        "cpanel\\buttons\\salarybutton.bmp","cpanel\\buttons\\performancebutton.bmp"};
    for(const auto* leaf:{"mood.bmp","relationship.bmp","job.bmp","house.bmp","interest.bmp","personality.bmp","speed1.bmp","speed2.bmp","speed3.bmp","pause.bmp","rotleft.bmp","rotright.bmp","zoomin.bmp","zoomout.bmp","lev1.bmp","lev2.bmp","levroof.bmp","dynacut.bmp","nocut.bmp","nowall.bmp","camera.bmp","options.bmp","unknownface.bmp","gift.bmp"})optional.emplace_back(std::string("cpanel\\buttons\\")+leaf);
    for(const auto key:live_interest_icon_keys)optional.emplace_back(key);
    optional.emplace_back("cpanel\\housesubbars.bmp");
    optional.emplace_back("cpanel\\backgrounds\\housesubbarsoff.bmp");
    optional.emplace_back("cpanel\\interestbars.bmp");
    optional.emplace_back("cpanel\\backgrounds\\interestbarsbackground.bmp");
    return optional;
}
}
OriginalControlPanelArt::OriginalControlPanelArt(const std::filesystem::path& root)
    :art_(root,required_panel_art(),optional_panel_art()){
    for(const auto* key:{"cpanel\\backgrounds\\backpatch.bmp","cpanel\\backgrounds\\panelback.bmp",
            "cpanel\\backgrounds\\universalback.tga","cpanel\\backgrounds\\livepatch.bmp",
            "cpanel\\backgrounds\\buypatch.bmp","cpanel\\backgrounds\\buildpatch.bmp"})
        if(const auto* image=find(key))prepare_backdrop(*image);
}
void OriginalControlPanelArt::prepare_backdrop(const BitmapImage& image){
    if(image.width>UINT16_MAX || image.height>UINT16_MAX)return;
    std::vector<BackdropRun> runs;
    const auto opacity=[&](const std::array<std::uint8_t,4>& p){
        if(p[0]==255 && !p[1] && p[2]==255)return 0;
        return image.bit_count==32?int(p[3]):255;
    };
    for(unsigned y=0;y<image.height;++y){
        const auto* row=image.pixels.data()+size_t(y)*image.width;
        for(unsigned x=0;x<image.width;){
            const auto alpha=opacity(row[x]);
            if(!alpha){++x;continue;}
            const bool translucent=alpha!=255;
            const auto start=x++;
            while(x<image.width){
                const auto next=opacity(row[x]);
                if(!next || (next!=255)!=translucent)break;
                ++x;
            }
            runs.push_back({uint16_t(start),uint16_t(y),uint16_t(x-start),translucent});
            // Highly fragmented artwork costs less to paint directly.
            if(runs.size()*sizeof(BackdropRun)>image.pixels.size())return;
        }
    }
    backdrop_runs_.emplace(&image,std::move(runs));
}
void OriginalControlPanelArt::draw_backdrop(UiBitmapSurface frame,const BitmapImage& image,BuyUiRect box) const{
    const auto found=backdrop_runs_.find(&image);
    if(found==backdrop_runs_.end() || box.width!=int(image.width) || box.height!=int(image.height)){
        blit(frame,image,box);return;
    }
    for(const auto& run:found->second){
        const int y=box.y+run.y;
        if(y<0 || y>=int(frame.height))continue;
        const int left=std::max(0,box.x+run.x),right=std::min(int(frame.width),box.x+run.x+run.width);
        if(left>=right)continue;
        const auto* src=image.pixels.data()+size_t(run.y)*image.width+size_t(left-box.x);
        auto* dst=frame.pixel(size_t(y)*frame.width+left);
        if(!run.translucent){std::memcpy(dst,src,size_t(right-left)*4);continue;}
        for(int x=left;x<right;++x,++src,dst+=4){
            const unsigned alpha=(*src)[3];
            for(unsigned c=0;c<3;++c)
                dst[c]=uint8_t(((*src)[c]*alpha+dst[c]*(255-alpha)+127)/255);
            dst[3]=255;
        }
    }
}
const BitmapImage& OriginalControlPanelArt::at(const std::string& key)const{return art_.at(key);}
const BitmapImage* OriginalControlPanelArt::find(const std::string& key)const{return art_.find(key);}
void OriginalControlPanelArt::draw_button(RasterFrame& frame,const std::string& key,const BuyUiRect& box,unsigned state)const{
    draw_button(UiBitmapSurface::from(frame),key,box,state);
}
void OriginalControlPanelArt::draw_button(UiBitmapSurface frame,const std::string& key,const BuyUiRect& box,unsigned state)const{
    const auto& image=at(key);if(state>3||image.width<4||image.width%4)throw FormatError("original four-state button strip is invalid");
    const auto span=static_cast<std::int32_t>(image.width/4);blit(frame,image,box,{span*static_cast<std::int32_t>(state),0,span,static_cast<std::int32_t>(image.height)});
}
void OriginalControlPanelArt::draw_portrait(RasterFrame& frame,BuyUiRect box,bool selected,const BitmapImage* head,bool authored_icon)const{
    draw_portrait_state(frame,box,selected?1:0,head,authored_icon);
}
void OriginalControlPanelArt::draw_portrait_state(RasterFrame& frame,BuyUiRect box,unsigned state,const BitmapImage* head,bool authored_icon)const{
    if(art_.find("cpanel\\people\\peopletemplate.bmp"))
        draw_button(frame,"cpanel\\people\\peopletemplate.bmp",box,state);
    if(head){
        if(authored_icon){
            if(!head->width||!head->height||head->pixels.size()!=std::uint64_t(head->width)*head->height)
                throw FormatError("original portrait bitmap malformed");
            // Original faces are authored 35x41 cells inside a45px button.
            // Preserve the whole icon and its background; no head-cropping.
            const auto scale=std::min(float(box.width)*41/(45*head->width),float(box.height)*41/(45*head->height));
            const int width=std::max(1,int(std::lround(head->width*scale)));
            const int height=std::max(1,int(std::lround(head->height*scale)));
            blit(frame,*head,{box.x+(box.width-width)/2,box.y+(box.height-height)/2,width,height},
                {0,0,int(head->width),int(head->height)});
            return;
        }
        const auto inset=std::max(1,box.width*4/45);
        // Portrait renders contain camera padding around the head. Fill the
        // authored face frame with its visible head, preserving aspect ratio;
        // shrinking that entire camera canvas made the family faces tiny.
        std::uint32_t left=head->width,top=head->height,right=0,bottom=0;
        if(head->pixels.size()!=std::uint64_t(head->width)*head->height)throw FormatError("original portrait bitmap malformed");
        for(std::uint32_t y=0;y<head->height;++y)for(std::uint32_t x=0;x<head->width;++x){
            const auto& p=head->pixels[std::size_t(y)*head->width+x];
            if((p[0]==255&&p[1]==0&&p[2]==255)||(head->bit_count==32&&!p[3]))continue;
            left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);
        }
        if(right>left&&bottom>top){
            const auto scale=std::min(float(box.width-2*inset)/(right-left),float(box.height-2*inset)/(bottom-top));
            const auto width=static_cast<int>(std::lround((right-left)*scale)),height=static_cast<int>(std::lround((bottom-top)*scale));
            blit(frame,*head,{box.x+(box.width-width)/2,box.y+(box.height-height)/2,width,height},
                {static_cast<int>(left),static_cast<int>(top),static_cast<int>(right-left),static_cast<int>(bottom-top)});
        }
    }
}
OriginalPanelAction OriginalControlPanelArt::hit(const OriginalControlPanelLayout& l,std::int32_t x,std::int32_t y,OriginalPanelState state)const{
    const auto visible=[&](const char* leaf,const BuyUiRect& box){
        if(!box.contains(x,y))return false;
        const auto* image=art_.find(std::string("cpanel\\buttons\\")+leaf+".bmp");
        if(!image)return false;
        if(image->width<4||image->width%4||!image->height||
           image->pixels.size()!=std::uint64_t(image->width)*image->height)
            throw FormatError("original control hit artwork malformed");
        const auto height=std::string_view(leaf)=="pause"?std::min(15u,image->height):image->height;
        const auto sx=std::uint64_t(std::int64_t(x)-box.x)*(image->width/4)/box.width;
        const auto sy=std::uint64_t(std::int64_t(y)-box.y)*height/box.height;
        const auto& pixel=image->pixels[static_cast<std::size_t>(sy)*image->width+static_cast<std::size_t>(sx)];
        return !(pixel[0]==255&&pixel[1]==0&&pixel[2]==255)&&
            (image->bit_count!=32||pixel[3]!=0);
    };
    // Match painter order. These circular/diamond buttons overlap at their
    // transparent corners; rectangle order alone turns zoom into rotation.
    const std::array<std::tuple<const char*,BuyUiRect,OriginalPanelAction>,7> controls{{
        {"rotleft",l.rotate_left,OriginalPanelAction::rotate_camera_left},
        {"rotright",l.rotate_right,OriginalPanelAction::rotate_camera_right},{"zoomin",l.zoom_in,OriginalPanelAction::zoom_in},
        {"zoomout",l.zoom_out,OriginalPanelAction::zoom_out},{"nocut",l.walls_up,OriginalPanelAction::walls_up},
        {"nowall",l.walls_down,OriginalPanelAction::walls_down},{"pause",l.pause,OriginalPanelAction::pause}}};
    for(auto control=controls.rbegin();control!=controls.rend();++control)
        if(visible(std::get<0>(*control),std::get<1>(*control)))return std::get<2>(*control);
    if(state.can_level_up.value_or(false)&&visible("lev2",l.level_up))return OriginalPanelAction::level_up;
    if(state.can_level_down.value_or(false)&&visible("lev1",l.level_down))return OriginalPanelAction::level_down;
    if(state.cutaway&&visible("dynacut",l.walls_cutaway))return OriginalPanelAction::walls_cutaway;
    if(state.roof&&visible("levroof",l.roof))return OriginalPanelAction::roof;
    if(state.options_enabled&&visible("options",l.options))return OriginalPanelAction::options;
    if(!state.options_open&&state.simulation_speed&&
       (*state.simulation_speed==1||*state.simulation_speed==3||*state.simulation_speed==10)){
        const OriginalPanelAction speed_actions[]{OriginalPanelAction::speed_normal,
            OriginalPanelAction::speed_fast,OriginalPanelAction::speed_ultra};
        for(unsigned n=0;n<3;++n)if(visible(("speed"+std::to_string(n+1)).c_str(),l.speeds[n]))
            return speed_actions[n];
    }
    const char* tabs[]={"mood","relationship","job","personality","interest","house"};
    const OriginalPanelAction actions[]={OriginalPanelAction::live_needs,OriginalPanelAction::live_relationships,OriginalPanelAction::live_career,OriginalPanelAction::live_personality,OriginalPanelAction::live_interests,OriginalPanelAction::live_house};
    for(unsigned n=0;n<6;++n)if(state.live_tabs[n]&&visible(tabs[n],l.tabs[n]))return actions[n];
    if(state.inventory_enabled&&visible("gift",{l.tabs[3].x,l.tabs[5].y,l.tabs[5].width,l.tabs[5].height}))return OriginalPanelAction::live_inventory;
    return OriginalPanelAction::none;
}
void OriginalControlPanelArt::draw(RasterFrame& frame,OriginalPanelMode mode,OriginalPanelState state)const{
    draw(UiBitmapSurface::from(frame),mode,state);
}
void OriginalControlPanelArt::draw(UiBitmapSurface frame,OriginalPanelMode mode,OriginalPanelState state)const{
    draw(frame,mode,state,nullptr);
}
void OriginalControlPanelArt::draw(UiBitmapSurface frame,OriginalPanelMode mode,OriginalPanelState state,RetainedPanelArt* retained)const{
    const auto l=original_control_panel_layout(frame.width,frame.height);
    if(!frame.rgba || frame.pixel_count!=std::uint64_t(frame.width)*frame.height)
        throw FormatError("UI bitmap destination frame is malformed");
    // Keep retained tiles within the canvas budget for this target.
    if(frame.width>RetainedPanelArt::max_width)retained=nullptr;
    if(retained)retained->begin();
    const auto paint=[&](const BitmapImage& image,BuyUiRect box,BuyUiRect crop){
        if(retained)retained->add(image,box,crop);
        else blit(frame,image,box,crop);
    };
    const auto backdrop=[&](const BitmapImage& image,BuyUiRect box){
        if(retained)retained->add(image,box,{0,0,int(image.width),int(image.height)});
        else draw_backdrop(frame,image,box);
    };
    const auto control=[&](const std::string& key,BuyUiRect box,unsigned selected){
        const auto& image=at(key);
        if(selected>3||image.width<4||image.width%4)throw FormatError("original four-state button strip is invalid");
        const int width=int(image.width/4);
        paint(image,box,{width*int(selected),0,width,int(image.height)});
    };
    // Opaque backing under the transparent edge of UniversalBack. It ends
    // at authored x220, adjoining PanelBack at its original pixel size. Draw it first
    // so it cannot cover UniversalBack's teal curve and button surrounds.
    if(const auto* backing=art_.find("cpanel\\backgrounds\\backpatch.bmp"))
        backdrop(*backing,{static_cast<int>(std::lround(168*l.scale)),l.base.y,
            static_cast<int>(std::lround(52*l.scale)),l.base.height});
    // PanelBack's authored rounded corner must cover the backing overlap.
    // Reversing these layers exposes BackPatch's straight right edge as a tip.
    const auto& panel_back=at("cpanel\\backgrounds\\panelback.bmp");
    backdrop(panel_back,l.panel_background);
    // Larger desktop canvases retain authored UI pixels. Continue the actual
    // background's right edge across the additional panel area, rather than
    // exposing the world behind a bitmap authored for a 1024px display.
    const int right=l.panel_background.x+l.panel_background.width;
    if(right<int(frame.width))paint(panel_back,
        {right,l.panel_background.y,int(frame.width)-right,l.panel_background.height},
        {int(panel_back.width)-1,0,1,int(panel_back.height)});
    const auto baseline=static_cast<float>(frame.height)-600*l.scale;
    const auto patch=[&](const char* key,int h,int inset=0){const auto& image=at(key);backdrop(image,{static_cast<std::int32_t>(std::lround(inset*l.scale)),static_cast<std::int32_t>(std::lround(baseline+(600-h)*l.scale)),static_cast<std::int32_t>(std::lround(image.width*l.scale)),static_cast<std::int32_t>(std::lround(h*l.scale))});};
    const auto* universal=art_.find("cpanel\\backgrounds\\universalback.tga");
    if(universal)backdrop(*universal,l.left_patch);
    // UniversalBack supplies the base, not LivePatch's white curve and grey
    // connector. The authored live overlay starts seven pixels to its right.
    if(mode==OriginalPanelMode::live||mode==OriginalPanelMode::options)patch("cpanel\\backgrounds\\livepatch.bmp",153,7);
    else if(!universal&&mode==OriginalPanelMode::buy)patch("cpanel\\backgrounds\\buypatch.bmp",148);
    else if(!universal)patch("cpanel\\backgrounds\\buildpatch.bmp",132);
    const auto visual=[&](const std::string& key,const BuyUiRect& box,unsigned base){
        if(!state.pointer)return base; // Existing speed-pointer consumers retain their explicit frames.
        const auto* image=art_.find(key);
        const auto contains=[&](const std::optional<std::array<int,2>>& point){
            if(!image||!point||!box.contains((*point)[0],(*point)[1])||image->width%4||!image->width||!image->height)return false;
            const auto x=std::uint64_t((*point)[0]-box.x)*(image->width/4)/box.width;
            const auto height=key.find("pause.bmp")!=std::string::npos?std::min(15U,image->height):image->height;
            const auto y=std::uint64_t((*point)[1]-box.y)*height/box.height;
            const auto& p=image->pixels.at(std::size_t(y)*image->width+x);
            return !(p[0]==255&&p[1]==0&&p[2]==255)&&(image->bit_count!=32||p[3]);
        };
        return original_button_frame(base!=3,base==1,contains(state.pointer),contains(state.press_origin));
    };
    control("cpanel\\buttons\\people.bmp",l.modes[0],visual("cpanel\\buttons\\people.bmp",l.modes[0],state.live_enabled?(mode==OriginalPanelMode::live?1:0):3));
    control("cpanel\\buttons\\objects.bmp",l.modes[1],visual("cpanel\\buttons\\objects.bmp",l.modes[1],state.build_buy_enabled?(mode==OriginalPanelMode::buy?1:0):3));
    control("cpanel\\buttons\\arch.bmp",l.modes[2],visual("cpanel\\buttons\\arch.bmp",l.modes[2],state.build_buy_enabled?(mode==OriginalPanelMode::build?1:0):3));
    const auto optional_image=[&](const char* key,const BuyUiRect& box){if(const auto* image=art_.find(key))paint(*image,box,{0,0,int(image->width),int(image->height)});};
    if(!universal)optional_image("cpanel\\backgrounds\\camerapatch.bmp",l.camera_patch);
    const auto button=[&](const char* leaf,const BuyUiRect& box,unsigned selected){const auto key=std::string("cpanel\\buttons\\")+leaf+".bmp";if(art_.find(key))control(key,box,visual(key,box,selected));};
    button("rotleft",l.rotate_left,0);button("rotright",l.rotate_right,0);
    button("zoomin",l.zoom_in,0);button("zoomout",l.zoom_out,0);
    button("nocut",l.walls_up,!state.walls_down&&!state.cutaway.value_or(false)&&!state.roof.value_or(false)?1:0);button("nowall",l.walls_down,state.walls_down?1:0);
    button("lev2",l.level_up,state.can_level_up?(state.can_level_up.value()?0:3):3);
    button("lev1",l.level_down,state.can_level_down?(state.can_level_down.value()?0:3):3);
    button("levroof",l.roof,state.roof?(*state.roof?1:0):3);button("dynacut",l.walls_cutaway,state.cutaway?(*state.cutaway?1:0):3);
    button("camera",l.camera,3);button("options",l.options,state.options_enabled?(state.options_open?1:0):3);
    if(const auto* pause=art_.find("cpanel\\buttons\\pause.bmp")){
        const auto width=static_cast<int>(pause->width/4);
        paint(*pause,l.pause,{width*int(visual("cpanel\\buttons\\pause.bmp",l.pause,state.paused?1:0)),0,width,std::min(15,static_cast<int>(pause->height))});
    }
    // UIDesktopUCP maps the three speed choices to actual VM multipliers
    // 1/3/10. Pausing selects Pause, not the remembered running-speed button.
    const unsigned multipliers[]{1,3,10};
    const bool speed_enabled=mode==OriginalPanelMode::live&&!state.options_open&&state.simulation_speed&&
        (*state.simulation_speed==1||*state.simulation_speed==3||*state.simulation_speed==10);
    for(unsigned n=0;n<3;++n){
        const bool hover=state.hovered_simulation_speed==multipliers[n];
        const bool pressed=hover&&state.pressed_simulation_speed==multipliers[n];
        const bool selected=speed_enabled&&!state.paused&&*state.simulation_speed==multipliers[n];
        button(("speed"+std::to_string(n+1)).c_str(),l.speeds[n],
            !speed_enabled?3U:selected||pressed?1U:hover?2U:0U);
    }
    if(mode==OriginalPanelMode::live){
        optional_image("cpanel\\backgrounds\\livegadget.bmp",l.live_gadget);
        // Greenbars/Redbars are opposing 27x25 mood gauges cut to the
        // curved divider; the eight individual needs are straight meters.
        if(state.mood&&*state.mood){
            const bool positive=*state.mood>0;
            const auto* gauge=art_.find(positive?"cpanel\\greenbars.bmp":"cpanel\\redbars.bmp");
            if(gauge&&gauge->width==27&&gauge->height==25){
                const auto box=positive?l.green_mood:l.red_mood;
                const auto steps=std::clamp((std::abs(int(*state.mood))+19)/20,1,5);
                const auto rows=steps*5,source_y=positive?25-rows:0;
                const auto top=box.y+source_y*box.height/25;
                const auto bottom=box.y+(source_y+rows)*box.height/25;
                paint(*gauge,{box.x,top,box.width,bottom-top},{0,source_y,27,rows});
            }
        }
        const char* tabs[]={"mood","relationship","job","personality","interest","house"};
        for(unsigned n=0;n<6;++n)button(tabs[n],l.tabs[n],state.live_tabs[n]?(!state.inventory_selected&&static_cast<unsigned>(state.live_tab)==n?1:0):3);
        button("gift",{l.tabs[3].x,l.tabs[5].y,l.tabs[5].width,l.tabs[5].height},state.inventory_enabled?(state.inventory_selected?1:0):3);
    }
    if(retained)retained->draw(frame,{0,l.left_patch.y,int(frame.width),int(frame.height)-l.left_patch.y});
}
} // namespace odh_host
