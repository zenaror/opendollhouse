/* SPDX-License-Identifier: MPL-2.0 */
#include "catalog_info_text.hpp"
#include "linux_text_renderer.hpp"
#include "original_control_panel.hpp"
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <algorithm>
using namespace odh_host;
static void Require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static int measuredFont=0,measuredWidth=0,paintedFont=0;
static TextOverlayExtent VariableMeasure(std::string_view,std::uint8_t,int font,int width){measuredFont=font;measuredWidth=width;return {width,41};}
static std::vector<TextOverlayLineRange> VariableLines(std::string_view,std::uint8_t,int,int){return {{0,19},{19,41}};}
static TextOverlayResult VariableDraw(odh::RasterFrame&,std::string_view text,std::uint8_t,TextOverlayStyle style){
    paintedFont=style.font_size_px;Require(text=="Família · 医療","fallback painter sliced mixed UTF8");
    Require(style.wrap_lines&&style.pixel_scroll_y==7,"variable-height fallback lost the full shaped document viewport");return {};
}
int main(){try{
    // Real registered Linux shaper: all image-column metrics use13px at full
    // panel scale instead of treating373px as a reduced530px text-only panel.
    const std::string document="Expediente:23:00–5:00.\nChegada da carona:22:00.\nFamília · 医療 e uma descrição extensa de profissão.";
    for(const auto size:{std::array<unsigned,2>{800,600},{1024,768},{1280,720}}){
        const auto scale=original_control_panel_layout(size[0],size[1]).scale;
        const int width=int(373*scale),font=std::max(8,int(std::lround(13*scale)));
        const auto natural=measure_text_overlay(document,14,font,width);
        const auto ranges=text_overlay_line_ranges(document,14,font,width);
        int expected=natural.height;bool uniform=!ranges.empty();
        int previous=0,line=ranges.empty()?0:ranges.front().bottom-ranges.front().top;
        for(const auto& r:ranges){if(r.top<previous||r.bottom-r.top!=line)uniform=false;previous=r.bottom;}
        if(uniform&&line>0)expected=int(ranges.size()-1)*int(std::lround(17*scale))+line;
        Require(catalog_info_text_measure(14,true,scale)(document,width)==expected,"image popup measured another font or invented line height");
        CatalogInfoContent info;info.name="Paramédico";info.description=document;
        odh::BitmapImage icon;icon.width=128;icon.height=96;icon.pixels.resize(128*96);info.icon=&icon;info.icon_rgba=true;
        const auto layout=catalog_info_layout(size[0],size[1],info,catalog_info_text_measure(14,true,scale));
        Require(layout.description.x==layout.panel.x+int(178*scale)&&layout.description.width==width,"profession consultation lost original picture column");
    }
    // Explicitly injected variable-height shaper: this independent path must
    // retain natural metrics and the complete text rather than infer Ag/averages.
    const auto old=set_text_overlay_backend({VariableMeasure,VariableDraw,VariableLines});
    Require(catalog_info_text_measure(14,true,1.f)("Família · 医療",373)==41&&measuredFont==13&&measuredWidth==373,
            "variable line heights were compacted or image profile lost explicit scale");
    Require(catalog_info_text_measure(14,true)("Família · 医療",530)==41&&measuredFont==13,
            "existing inventory/Fame default typography changed");
    odh::RasterFrame frame;frame.width=1024;frame.height=768;frame.pixels.resize(1024*768);
    catalog_info_scroll_painter(14,true,1.f)(frame,"Família · 医療",{643,540,373,70},false,7);
    Require(paintedFont==13,"scroll paint and measure fonts differ");
    set_text_overlay_backend(old);
    std::cout<<"Career popup: real shaper/image column matrix and variable-height UTF8 fallback passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
