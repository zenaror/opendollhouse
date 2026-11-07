// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "original_ui_art.hpp"
#include "odh/raster.hpp"
#include "odh/ui_bitmap.hpp"
#include <functional>
#include <string_view>
#include <algorithm>
namespace odh_host {
struct CatalogInfoContent {
    std::string name,description,price,effects;
    const odh::BitmapImage* icon=nullptr;
    bool icon_rgba=false; // Sprite thumbnail: one RGBA image, no strip or colour key.
};
struct CatalogInfoLayout {
    odh::UiBitmapRect panel,icon,name,description,price,effects,body;
    int description_height=0,effects_height=0,max_scroll=0;
};
struct CatalogInfoScrollInput {int wheel_steps=0,page_steps=0;};
// Presentation-only document viewport shared by both catalogs. Measurements
// are cached by full text and viewport; offsets never enter a session archive.
class CatalogInfoScrollState {
public:
    void reset(){offset_=0;valid_=false;}
    const CatalogInfoLayout& prepare(std::uint32_t,std::uint32_t,const CatalogInfoContent&,
        const std::function<int(std::string_view,int)>&)const;
    bool consume(CatalogInfoScrollInput,const CatalogInfoLayout&,int x,int y);
    int offset()const{return valid_?std::min(offset_,layout_.max_scroll):offset_;}
private:
    mutable int offset_=0;
    mutable bool valid_=false;
    mutable std::uint32_t width_=0,height_=0;
    mutable std::string description_,effects_;
    mutable CatalogInfoLayout layout_;
};
using CatalogInfoTextMeasure=std::function<int(std::string_view,int)>;
CatalogInfoLayout catalog_info_layout(std::uint32_t width,std::uint32_t height,
    const CatalogInfoContent& content={},CatalogInfoTextMeasure={});
using CatalogInfoScrollPainter=std::function<void(odh::RasterFrame&,std::string_view,
    const odh::UiBitmapRect& clip,bool emphasis,int pixel_scroll)>;
using CatalogInfoTextPainter=std::function<void(odh::RasterFrame&,std::string_view,const odh::UiBitmapRect&,bool)>;
// Original resource-backed catalog information. Decoding occurs only at
// construction; drawing never accesses files or changes the selected item.
class CatalogInfoView {
public:
    explicit CatalogInfoView(const std::filesystem::path& game_root);
    void draw(odh::RasterFrame&,const CatalogInfoContent&,CatalogInfoTextPainter={},CatalogInfoTextMeasure={},
        const CatalogInfoScrollState* scroll=nullptr,CatalogInfoScrollPainter={})const;
private:
    OriginalUiArt art_;
};
}
