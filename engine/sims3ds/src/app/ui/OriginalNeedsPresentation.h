/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "app/ui/OriginalPresentation.h"
#include "linux_motive_panel.hpp"
#include <cmath>
namespace simapp { namespace original {
inline Rect NeedsPaintBounds(const Canvas& canvas,const odh_host::OriginalMotivePanelGeometry& geometry){
    Rect bounds{geometry.panel.x,geometry.panel.y,geometry.panel.width,geometry.panel.height};
    const auto include=[&](const odh::UiBitmapRect& r){
        if(r.width<=0||r.height<=0)return;
        const int right=std::max(bounds.Right(),r.x+r.width),bottom=std::max(bounds.Bottom(),r.y+r.height);
        bounds.X=std::min(bounds.X,r.x);bounds.Y=std::min(bounds.Y,r.y);
        bounds.W=right-bounds.X;bounds.H=bottom-bounds.Y;
    };
    include(geometry.title);
    if(geometry.tall)include(geometry.artwork);
    for(std::size_t i=0;i<geometry.labels.size();++i){
        include(geometry.labels[i]);
        const auto& meter=geometry.meters[i];
        // Five chevrons reach 20 scaled pixels on either side. Negative
        // arrows can extend left of the panel itself; retain their backdrop.
        const int margin=int(std::ceil(20.0*meter.height/5.0))+1;
        include({meter.x-margin,meter.y,meter.width+2*margin,meter.height});
    }
    return Clip(canvas,bounds);
}
inline odh_host::MotivePanelResult DrawNeeds(Canvas& canvas,odh::RasterFrame& scratch,
        const odh_host::StatusHud& status,std::uint8_t language,
        const odh::BitmapImage* art,const odh_host::OriginalControlPanelLayout& layout,
        const std::array<std::string,8>* labels,const std::string* heading,
        const odh_host::OriginalMotivePanelGeometry& geometry,const std::array<int,8>& arrows){
    const auto bounds=NeedsPaintBounds(canvas,geometry);
    Raster(canvas,scratch,bounds);
    const auto result=odh_host::draw_motive_panel(scratch,status,language,art,&layout,labels,heading,&geometry,&arrows);
    Copy(scratch,canvas,bounds);
    return result;
}

// Lives with the immutable localized artwork and labels for one data root.
// Compare the underlying pixels as well as the values: UI art can be
// translucent and the world behind the panel may have moved since last time.
class NeedsPanelCache {
public:
    bool draw(Canvas& canvas,odh::RasterFrame& scratch,const odh_host::StatusHud& status,
            std::uint8_t language,const odh::BitmapImage* art,const odh_host::OriginalControlPanelLayout& layout,
            const std::array<std::string,8>* labels,const std::string* heading,
            const odh_host::OriginalMotivePanelGeometry& geometry,const std::array<int,8>& arrows){
        const auto area=NeedsPaintBounds(canvas,geometry);
        const auto count=size_t(area.W)*area.H;
        // The two rectangles together occupy at most 256 KiB.
        if(!count || count>32768){
            valid=false;
            DrawNeeds(canvas,scratch,status,language,art,layout,labels,heading,geometry,arrows);
            return false;
        }
        // Geometry is derived solely from the viewport, the language and the
        // original artwork. Pet and human panels have separate label tables.
        bool matches=valid && width==canvas.Width && height==canvas.Height && last_language==language &&
            area.X==last_area.X && area.Y==last_area.Y && area.W==last_area.W && area.H==last_area.H &&
            last_labels==labels && last_heading==heading && last_art==art && last_arrows==arrows &&
            last_needs==status.needs && last_room==status.room_score;
        const auto row=[&](int y){return canvas.Px.data()+size_t(area.Y+y)*canvas.Width+area.X;};
        for(int y=0;matches && y<area.H;++y)
            matches=std::memcmp(row(y),background.data()+size_t(y)*area.W,size_t(area.W)*4)==0;
        if(matches){
            for(int y=0;y<area.H;++y)
                std::memcpy(row(y),painted.data()+size_t(y)*area.W,size_t(area.W)*4);
            return true;
        }
        valid=false;
        background.resize(count);
        painted.resize(count);
        for(int y=0;y<area.H;++y)
            std::memcpy(background.data()+size_t(y)*area.W,row(y),size_t(area.W)*4);
        DrawNeeds(canvas,scratch,status,language,art,layout,labels,heading,geometry,arrows);
        for(int y=0;y<area.H;++y)
            std::memcpy(painted.data()+size_t(y)*area.W,row(y),size_t(area.W)*4);
        width=canvas.Width; height=canvas.Height; last_area=area; last_language=language; last_art=art;
        last_labels=labels; last_heading=heading; last_arrows=arrows;
        last_needs=status.needs; last_room=status.room_score; valid=true;
        return false;
    }
private:
    bool valid=false;
    int width=0,height=0;
    Rect last_area;
    std::uint8_t last_language=0;
    const std::array<std::string,8>* last_labels=nullptr;
    const std::string* last_heading=nullptr;
    const odh::BitmapImage* last_art=nullptr;
    std::array<int,8> last_arrows{};
    decltype(odh_host::StatusHud::needs) last_needs;
    decltype(odh_host::StatusHud::room_score) last_room;
    std::vector<std::uint32_t> background,painted;
};
}}
#endif
