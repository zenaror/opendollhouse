#include "linux_text_renderer.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void sample(const std::string& text,std::uint8_t language) {
    odh::RasterFrame frame{320,240,std::vector<std::array<std::uint8_t,4>>(320U*240U,{245,245,242,255}),{},0,0};
    auto without_glyphs=frame;
    const auto result=odh_host::draw_text_overlay(frame,text,language);
    check(result.glyphs>0,"Pango shaped the visible label");
    check(result.missing_glyphs==0,"system font fallback covered the sample glyphs");
    check(result.pixels_changed>80,"localized label changed pixels in the scene frame");
    odh_host::TextOverlayStyle background_only;background_only.foreground[3]=0;
    (void)odh_host::draw_text_overlay(without_glyphs,text,language,background_only);
    const auto visible_glyph_pixels=std::inner_product(frame.pixels.begin(),frame.pixels.end(),
        without_glyphs.pixels.begin(),std::size_t{0},std::plus<>{},
        [](const auto& with_text,const auto& background){return with_text!=background?std::size_t{1}:std::size_t{0};});
    check(visible_glyph_pixels>0,"visible glyph pixels differ from the background-only overlay");
}
}

int main(){try{
    sample("Cadeiras de Jantar",14);
    sample("ダイニング椅子",15);
    sample("餐椅",17);
    sample("שלום",12);
    sample("เกมหยุด",19);
    bool rejected=false;
    try{odh::RasterFrame frame{1,1,{{0,0,0,255}},{},0,0};(void)odh_host::draw_text_overlay(frame,"abc",21);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"unsupported language is rejected");
    rejected=false;
    try{odh::RasterFrame frame{1,1,{{0,0,0,255}},{},0,0};(void)odh_host::draw_text_overlay(frame,std::string("\xff",1),14);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"invalid UTF-8 is rejected");
    odh::RasterFrame missing{320,240,std::vector<std::array<std::uint8_t,4>>(320U*240U,{245,245,242,255}),{},0,0};
    const auto missing_result=odh_host::draw_text_overlay(missing,"\U0010FFFF",14);
    check(missing_result.missing_glyphs>0,"missing font glyph is reported");
    rejected=false;
    try{odh::RasterFrame frame{1,1,{{0,0,0,255}},{},0,0};odh_host::TextOverlayStyle style;style.padding=16;
        (void)odh_host::draw_text_overlay(frame,"abc",14,style);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"invalid overlay geometry is rejected");
    std::cout<<"linux text renderer: 19 checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"linux text renderer: "<<error.what()<<'\n';return 1;}}
