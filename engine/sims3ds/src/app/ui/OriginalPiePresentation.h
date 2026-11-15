/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "app/ui/OriginalPresentation.h"
#include "linux_pie_menu.hpp"

namespace simapp { namespace original {
// Preserve the original disc -> animated head -> menu artwork/plaques order.
// Head and submenu centers can differ, so their bounds must remain independent.
template<class DrawHead>
void DrawDesktopPie(Canvas& canvas,odh::RasterFrame& frame,const odh::PieMenuLayout& layout,
        std::array<std::int32_t,2> head_center,const std::vector<odh_host::PieMenuCaption>& captions,
        std::size_t selected,const std::optional<odh_host::PieMenuCaption>& parent,
        const odh::BitmapImage* art,bool pressed,bool highlighted,DrawHead draw_head){
    const Rect head{head_center[0]-100,head_center[1]-100,200,200};
    // Import the menu's exact rectangles before drawing the head. They can
    // overlap each other; the Canvas has not changed yet, so imports commute.
    const auto menu_regions=[&](auto visit){
        if(art)visit(Rect{layout.center[0]-100,layout.center[1]-100,201,201});
        const auto box=[&](const odh::PieMenuRect& r){if(r.width>0&&r.height>0)visit(Rect{r.x,r.y,r.width,r.height});};
        if(layout.back_button)box(*layout.back_button);
        if(layout.previous_page)box(*layout.previous_page);
        if(layout.next_page)box(*layout.next_page);
        for(auto i=layout.page_first;i<captions.size()&&i-layout.page_first<layout.page_size;++i)box(layout.items[i]);
    };
    menu_regions([&](const Rect& r){Raster(canvas,frame,r);});
    Raster(canvas,frame,head);
    auto head_layout=layout;head_layout.center=head_center;
    odh_host::draw_linux_pie_menu_head(frame,head_layout,nullptr);
    Copy(frame,canvas,head);
    draw_head(canvas);
    // Both original head renderers are clipped to this same 200px surface.
    Raster(canvas,frame,head);
    odh_host::draw_linux_pie_menu(frame,layout,captions,selected,parent,art,pressed,
        odh_host::PieMenuBackground::live_face,highlighted);
    menu_regions([&](const Rect& r){Copy(frame,canvas,r);});
}
}}
#endif
