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
    // Original0050fcbb also serves live help with no picture slots. Keep
    // catalog's preserved left picture column as the unchanged default.
    bool text_only_live=false;
    bool unaffordable=false; // Query-only purchase refusal; never a placement result.
    // Live help has independent authored left/right picture slots. Catalog and
    // settings keep their existing one-picture layout and scaling by default.
    bool live_help=false;
    const odh::BitmapImage* right_icon=nullptr; // One RGBA illustration, not a strip.
    std::uint8_t live_language=1;
};
struct CatalogInfoLayout {
    odh::UiBitmapRect panel,icon,name,description,price,effects,body;
    int description_height=0,effects_height=0,max_scroll=0;
    odh::UiBitmapRect right_icon;
};
struct CatalogInfoScrollInput {int wheel_steps=0,page_steps=0;};
// Original DVD's red preview frame; no text, depth or world-owner mutation.
void draw_catalog_unaffordable_frame(odh::RasterFrame&,const odh::UiBitmapRect& panel,float scale=1);
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
    mutable bool price_present_=false;
    mutable bool text_only_live_=false;
    mutable unsigned live_picture_mask_=0;
    mutable std::string description_,effects_;
    mutable CatalogInfoLayout layout_;
};
using CatalogInfoTextMeasure=std::function<int(std::string_view,int)>;
CatalogInfoLayout catalog_info_layout(std::uint32_t width,std::uint32_t height,
    const CatalogInfoContent& content={},CatalogInfoTextMeasure={});
using CatalogInfoScrollPainter=std::function<void(odh::RasterFrame&,std::string_view,
    const odh::UiBitmapRect& clip,bool emphasis,int pixel_scroll)>;
using CatalogInfoTextPainter=std::function<void(odh::RasterFrame&,std::string_view,const odh::UiBitmapRect&,bool)>;
// Host measures the heading with its actual font; price and name remain
// separate single-line cells; price follows the measured title and three spaces.
using CatalogInfoHeadingPainter=std::function<void(odh::RasterFrame&,std::string_view,std::string_view,
    const odh::UiBitmapRect&,const odh::UiBitmapRect&)>;
// Original resource-backed catalog information. Decoding occurs only at
// construction; drawing never accesses files or changes the selected item.
class CatalogInfoView {
public:
    explicit CatalogInfoView(const std::filesystem::path& game_root);
    void draw(odh::RasterFrame&,const CatalogInfoContent&,CatalogInfoTextPainter={},CatalogInfoTextMeasure={},
        const CatalogInfoScrollState* scroll=nullptr,CatalogInfoScrollPainter={},CatalogInfoHeadingPainter={})const;
private:
    OriginalUiArt art_;
};
}
