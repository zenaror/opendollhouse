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
}}
#endif
