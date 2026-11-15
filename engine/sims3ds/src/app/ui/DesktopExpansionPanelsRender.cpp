/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __3DS__
#include "app/ui/DesktopExpansionPanels.h"
#include "app/ui/OriginalTextBackend.h"
#include "app/ui/UIGraphics.h"
#include "app/LotScreen.h"
#include "content/ContentManager.h"
#include "original_control_panel.hpp"
#include "original_ui_art.hpp"
#include "linux_text_renderer.hpp"
#include "catalog_info_text.hpp"
#include <algorithm>
#include <cmath>

namespace simapp { namespace original {
std::optional<DesktopFameLayout> DesktopOriginalFameLayout(int width,int height,std::uint8_t language) {
    if(!language||language>20||language==15||language>=17)return {};
    const auto common=odh_host::original_control_panel_layout(width,height);
    auto rect=[&](int x,int y,int w,int h) {
        return Rect(common.right_content.x+int(std::lround(x*common.scale)),
                    common.right_content.y+int(std::lround(y*common.scale)),
                    int(std::lround(w*common.scale)),int(std::lround(h*common.scale)));
    };
    DesktopFameLayout layout;
    // Original006f3be8,006f3d08,006f3e90,006f3c08. These are independent
    // subpanel elements, not positions borrowed from the portable career view.
    layout.title=rect(5,0,120,17);layout.stars=rect(3,30,80,16);
    layout.level=rect(3,45,102,16);layout.friends=rect(3,75,120,15);
    for(std::size_t i=0;i<6;++i) {
        layout.skill_labels[i]=rect(145,6+15*int(i),62,16);
        layout.skill_bars[i]=rect(210,7+15*int(i),40,11);
    }
    return layout;
}
namespace {
odh::UiBitmapRect box(const Rect& r){return {r.X,r.Y,r.W,r.H};}
struct FameArt {
    std::string root;
    odh_host::OriginalUiArt images;
    odh::RasterFrame surface;
    explicit FameArt(const std::string& path):root(path),images(path,{},
        {"cpanel\\FameSubBars.bmp","cpanel\\Backgrounds\\FameSubBarsOff.bmp",
         "cpanel\\JobSubBars.bmp","cpanel\\Backgrounds\\JobSubBarsOff.bmp",
         "cpanel\\Buttons\\SkillReqHilite.bmp"}){}
};
Rect DrawTextPopup(Canvas& canvas,DesktopExpansionPopupViewport& popup,const std::string& root,
                   std::uint8_t language,const std::string& identity,
                   const std::string& title,const std::string& description) {
    if(!popup.view||popup.root!=root){popup.view=std::make_shared<odh_host::CatalogInfoView>(root);popup.root=root;popup.scroll.reset();}
    if(!popup.open||popup.identity!=identity)popup.scroll.reset();
    popup.identity=identity;popup.open=true;
    odh_host::CatalogInfoContent content;content.name=title;content.description=description;content.text_only_live=true;
    const auto measure=odh_host::catalog_info_text_measure(language,true);
    popup.layout=popup.scroll.prepare(canvas.Width,canvas.Height,content,measure);
    odh::RasterFrame frame;frame.width=canvas.Width;frame.height=canvas.Height;frame.pixels.resize(canvas.Px.size());
    for(std::size_t i=0;i<canvas.Px.size();++i){const auto p=canvas.Px[i];frame.pixels[i]={uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)};}
    popup.view->draw(frame,content,odh_host::catalog_info_text_painter(language,true),measure,
        &popup.scroll,odh_host::catalog_info_scroll_painter(language,true),odh_host::catalog_info_heading_painter(language,true));
    const auto& panel=popup.layout.panel;
    for(int y=std::max(0,panel.y);y<std::min(canvas.Height,panel.y+panel.height);++y)
        for(int x=std::max(0,panel.x);x<std::min(canvas.Width,panel.x+panel.width);++x){
            const auto index=std::size_t(y)*canvas.Width+x;const auto& p=frame.pixels[index];canvas.Px[index]=RGBA(p[0],p[1],p[2],p[3]);}
    popup.visible=true;
    return Rect(panel.x,panel.y,panel.width,panel.height);
}
}
bool ScrollDesktopExpansionPopup(DesktopExpansionPopupViewport& popup,int wheel,int x,int y) {
    return popup.open&&popup.visible&&popup.scroll.consume({-wheel,0},popup.layout,x,y);
}
bool DrawDesktopFame(Canvas& canvas,LotScreen& screen,const DesktopFameSnapshot& snapshot,DesktopFameSelection& state) {
    state.popup.visible=false;
    state.Reconcile(snapshot);
    const auto layout=DesktopOriginalFameLayout(canvas.Width,canvas.Height,snapshot.language);
    auto content=FSO::Content::Content::Get();
    if(!snapshot.has_selected_person||!snapshot.title||!snapshot.half_stars||!layout||!content){state.ClosePopup();return false;}
    static std::unique_ptr<FameArt> resources;
    if(!resources||resources->root!=content->SimsCompleteDir)
        resources=std::make_unique<FameArt>(content->SimsCompleteDir);
    auto& frame=resources->surface;
    frame.width=canvas.Width;frame.height=canvas.Height;frame.pixels.resize(canvas.Px.size());
    for(std::size_t i=0;i<canvas.Px.size();++i){const auto p=canvas.Px[i];frame.pixels[i]={uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)};}
    InstallTextBackendIfNeeded();
    auto text=[&](const std::string& value,const Rect& r,bool title=false,bool right=false) {
        odh_host::TextOverlayStyle style;style.x=r.X;style.y=r.Y;style.width=r.W;style.height=r.H;
        style.padding=0;style.font_size_px=title?11:10;style.background={0,0,0,0};
        // Original004ff397 creates DAT006f2a44 from RGB195,205,205.
        style.foreground={195,205,205,255};
        style.right_aligned=right;(void)odh_host::draw_text_overlay(frame,value,snapshot.language,style);
    };
    auto image=[&](const char* key,const Rect& target,std::optional<odh::UiBitmapRect> crop={}) {
        if(const auto* bitmap=resources->images.find(key))
            odh::blit_color_keyed_bitmap(frame,*bitmap,box(target),crop,{255,0,255});
    };
    if(snapshot.title)text(*snapshot.title,layout->title,true);
    if(snapshot.level_name)text(*snapshot.level_name,layout->level);
    if(snapshot.half_stars) {
        image("cpanel\\Backgrounds\\FameSubBarsOff.bmp",layout->stars);
        const auto half=std::clamp(int(*snapshot.half_stars),0,10);
        if(half){auto target=layout->stars;target.W=target.W*half/10;
            image("cpanel\\FameSubBars.bmp",target,odh::UiBitmapRect{0,0,half*8,16});}
    }
    for(std::size_t i=0;i<6;++i) {
        text(snapshot.skill_names[i],layout->skill_labels[i],false,true);
        const auto bar=layout->skill_bars[i];
        image("cpanel\\Backgrounds\\JobSubBarsOff.bmp",bar);
        if(snapshot.skills[i]) {
            const auto points=std::clamp(int(*snapshot.skills[i]),0,1000)/100;
            if(points){auto target=bar;target.W=bar.W*points/10;
                image("cpanel\\JobSubBars.bmp",target,odh::UiBitmapRect{0,0,points*4,11});}
        }
        // Required markers are the original pale brackets, not filled
        // invented skill values. No threshold is synthesized when absent.
        if(snapshot.required_skills[i]&&*snapshot.required_skills[i]>0) {
            const auto points=std::clamp(int(*snapshot.required_skills[i]),0,1000)/100;
            const auto x=bar.X-1+points*bar.W/10;
            const auto y=bar.Y+bar.H-1;
            const auto color=std::array<uint8_t,4>{195,205,205,255};
            auto pixel=[&](int px,int py){if(px>=0&&py>=0&&px<int(frame.width)&&py<int(frame.height))frame.pixels[std::size_t(py)*frame.width+px]=color;};
            for(int py=bar.Y;py<=y;++py)pixel(x,py);
            for(int px=x-points*bar.W/10-15;px<=x;++px)pixel(px,y);
        }
    }
    // Unknown friend eligibility remains unknown; do not paint a fabricated
    // total or requirements line sourced from the one-way promotion helper.
    if(snapshot.friend_half_stars) {
        const auto half=*snapshot.friend_half_stars;
        std::string value=std::to_string(half/2)+(half%2?".5":".0");
        if(snapshot.required_friend_stars&&snapshot.friends_format&&half<*snapshot.required_friend_stars*2) {
            auto format=*snapshot.friends_format;
            auto at=format.find("%s");if(at!=std::string::npos)format.replace(at,2,value);
            at=format.find("%d");if(at!=std::string::npos)format.replace(at,2,std::to_string(*snapshot.required_friend_stars));
            value=std::move(format);
        }
        text(value,layout->friends);
    }
    for(std::size_t i=0;i<canvas.Px.size();++i){const auto& p=frame.pixels[i];canvas.Px[i]=RGBA(p[0],p[1],p[2],p[3]);}
    if(snapshot.level_title)
        screen.OfferDesktopHover(*snapshot.level_title,"fame-level","fame",0,layout->level);
    screen.Hit(layout->level,"pc_fame_level");
    if(state.level_open&&snapshot.level_title&&snapshot.level_description){
        const auto popup=DrawTextPopup(canvas,state.popup,content->SimsCompleteDir,snapshot.language,
            "fame:"+std::to_string(snapshot.neighbor_id.value_or(-1))+":"+std::to_string(*snapshot.half_stars),
            *snapshot.level_title,*snapshot.level_description);
        screen.Hit(popup,"pc_fame_close");
    }else state.popup.Close();
    return true;
}
std::optional<DesktopInventoryLayout> DesktopOriginalInventoryLayout(int width,int height,std::uint8_t language) {
    if(!language||language>20||language==15||language>=17)return {};
    const auto common=odh_host::original_control_panel_layout(width,height);
    // Native original screenshot1024: sort button origin522,701. The outer
    // inventory container starts two pixels before the common content inset.
    const auto x=common.right_content.x-2,y=common.right_content.y;
    DesktopInventoryLayout layout;
    layout.title=Rect(x+5,y,180,13);
    // Original0051aaec positions fields+1ac,+1b4,+1b0 top to bottom
    // (51aeeb..51af58). Keep these rectangles indexed by logical filter:
    // Magic4113/type7, Other4114/type8, Gift4112/type0/4/5.
    static constexpr int sort_by_row[]{1,2,0};
    for(int row=0;row<3;++row)layout.sorts[sort_by_row[row]]=Rect(x+2,y+33+21*row,20,20);
    const auto panel_width=width-x;
    layout.previous=Rect(x+34,y+34,9,49);
    layout.next=Rect(x+panel_width-11,y+34,9,49);
    // Original005186da sets114=10 at1024,6 at800;0051e3b5 uses114-1.
    // Larger desktop windows retain the1024 nine-item page and authored pixels.
    const std::size_t columns=width>=1024?9:5;
    for(std::size_t n=0;n<columns;++n)layout.cells.emplace_back(x+45+int(n)*42,y+31,42,60);
    return layout;
}
bool DrawDesktopInventory(Canvas& canvas,LotScreen& screen,const DesktopInventorySnapshot& snapshot,
                          DesktopInventorySelection& state) {
    state.popup.visible=false;
    const auto layout=DesktopOriginalInventoryLayout(canvas.Width,canvas.Height,snapshot.language);
    state.Reconcile(snapshot,layout?layout->cells.size():0);
    if(!snapshot.has_selected_person||!snapshot.items||!layout){state.ClosePopup();return false;}
    auto visible=state.VisibleItems(snapshot);
    // Existing original button painter uses authored four-state strips at1:1.
    static const char* artwork[]{"cpanel/Buttons/InventoryGiftSort.bmp","cpanel/Buttons/InventoryMagicSort.bmp","cpanel/Buttons/InventoryOtherSort.bmp"};
    for(int n=0;n<3;++n){
        UIGraphics::Get().DrawButton(canvas,artwork[n],int(state.sort())==n?ButtonState::Highlight:ButtonState::Normal,layout->sorts[n]);
        screen.Hit(layout->sorts[n],"pc_inv_sort"+std::to_string(n));
    }
    InstallTextBackendIfNeeded();
    odh::RasterFrame frame;frame.width=canvas.Width;frame.height=canvas.Height;frame.pixels.resize(canvas.Px.size());
    for(std::size_t i=0;i<canvas.Px.size();++i){const auto p=canvas.Px[i];frame.pixels[i]={uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)};}
    auto text=[&](std::string_view value,const Rect& r,bool centered=false,bool counter=false){
        odh_host::TextOverlayStyle style;style.x=r.X;style.y=r.Y;style.width=r.W;style.height=r.H;
        style.padding=0;style.font_size_px=9;style.background={0,0,0,0};
        // Original count uses the tinted0050e78c/49a996 palette result;
        // the observed digits recover pale cyan, separately from title gray.
        style.foreground=counter?std::array<uint8_t,4>{64,205,205,255}:std::array<uint8_t,4>{195,205,205,255};style.centered=centered;
        (void)odh_host::draw_text_overlay(frame,value,snapshot.language,style);
    };
    text(snapshot.title,layout->title);
    const auto first=state.page()*layout->cells.size();
    for(std::size_t n=0;n<layout->cells.size()&&first+n<visible.size();++n){
        const auto source=visible[first+n];
        // Original active entries may alias one cached GUID tile. Repeated
        // SetPos leaves that single child at its final slot on this page.
        const auto page_end=visible.begin()+std::min(visible.size(),first+layout->cells.size());
        if(std::find(visible.begin()+first+n+1,page_end,source)!=page_end)continue;
        auto found=std::find_if(snapshot.items->begin(),snapshot.items->end(),[&](const auto& item){return item.source_index==source;});
        if(found==snapshot.items->end())continue;
        const auto& item=*found;const auto cell=layout->cells[n];
        int icon_height=0;
        if(item.icon_strip){
            const auto& strip=*item.icon_strip;const auto width=strip.Width/2;icon_height=strip.Height;
            const int left=cell.X+(cell.W-width)/2;
            for(int y=0;y<strip.Height;++y)for(int x=0;x<width;++x){
                const auto p=strip.Px[std::size_t(y)*strip.Width+x];
                const int tx=left+x,ty=cell.Y+y;
                if(!(p>>24)||tx<0||ty<0||tx>=canvas.Width||ty>=canvas.Height)continue;
                frame.pixels[std::size_t(ty)*frame.width+tx]={uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)};
            }
        }
        // Original0050e78c prints count under the native bitmap, even one.
        const Rect count(cell.X,cell.Y+icon_height+3,42,13);
        for(int y=std::max(0,count.Y);y<std::min(canvas.Height,count.Bottom());++y)
            for(int x=std::max(0,count.X);x<std::min(canvas.Width,count.Right());++x)
                frame.pixels[std::size_t(y)*frame.width+x]={0,0,74,255};
        const auto signed_count=item.count<=32767?int(item.count):int(item.count)-65536;
        text(std::to_string(signed_count),count,true,true);
        screen.Hit(cell,"pc_inv_item"+std::to_string(source));
        if(item.name)screen.OfferDesktopHover(*item.name,"inventory:"+std::to_string(source),"inventory",source,cell);
    }
    for(std::size_t i=0;i<canvas.Px.size();++i){const auto& p=frame.pixels[i];canvas.Px[i]=RGBA(p[0],p[1],p[2],p[3]);}
    if(state.page()){
        UIGraphics::Get().DrawButton(canvas,"cpanel/Buttons/ScrollLeft.bmp",ButtonState::Normal,layout->previous);
        screen.Hit(layout->previous,"pc_inv_prev");
    }
    if(first+layout->cells.size()<visible.size()){
        UIGraphics::Get().DrawButton(canvas,"cpanel/Buttons/ScrollRight.bmp",ButtonState::Normal,layout->next);
        screen.Hit(layout->next,"pc_inv_next");
    }
    if(state.popup_open()&&state.selected_source()) {
        const auto found=std::find_if(snapshot.items->begin(),snapshot.items->end(),[&](const auto& item){return item.source_index==*state.selected_source();});
        if(found!=snapshot.items->end()&&(found->name||found->description)) {
            const auto content=FSO::Content::Content::Get();
            const auto popup=DrawTextPopup(canvas,state.popup,content->SimsCompleteDir,snapshot.language,
                "inventory:"+std::to_string(snapshot.neighbor_id.value_or(-1))+":"+std::to_string(found->type)+":"+std::to_string(found->guid),
                found->name.value_or(""),found->description.value_or(""));
            screen.Hit(popup,"pc_inv_close");
        }else state.popup.Close();
    }else state.popup.Close();
    return true;
}
}}
#endif
