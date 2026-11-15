#include "linux_text_renderer.hpp"
#include "linux_text_runtime.hpp"
#include <pango/pangocairo.h>
#include <memory>

#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void sample_with_reference(const std::string& text,std::uint8_t language,PangoFontMap* reference) {
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
    const auto extent=odh_host::measure_text_overlay(text,language,15);
    auto borrowed_frame=odh::RasterFrame{320,240,std::vector<std::array<std::uint8_t,4>>(320U*240U,{245,245,242,255}),{},0,0};
    {
        // Same constructor/configuration as the old default map, owned here
        // explicitly. The grouped language matrix checks unchanged pixels,
        // metrics and glyph fallback for owned versus externally leased maps.
        odh_host::LinuxTextRuntime borrowed(reference);
        const auto measured=odh_host::measure_text_overlay(text,language,15);
        const auto shaped=odh_host::draw_text_overlay(borrowed_frame,text,language);
        check(measured.width==extent.width&&measured.height==extent.height,"owned and borrowed maps preserve metrics");
        check(shaped.glyphs==result.glyphs&&shaped.missing_glyphs==result.missing_glyphs,"owned and borrowed maps preserve fallback");
    }
    check(frame.pixels==borrowed_frame.pixels,"owned and borrowed maps preserve exact pixels");
}
}

int main(){try{
    // Standalone callers have a finite local lease without installing a TLS
    // default; the explicit host scope then shares one map across this matrix.
    const auto standalone=odh_host::measure_text_overlay("ação",14,15);
    odh_host::LinuxTextRuntime text_runtime;
    const auto scoped=odh_host::measure_text_overlay("ação",14,15);
    check(standalone.width==scoped.width&&standalone.height==scoped.height,"standalone and host scopes preserve metrics");
    {
        odh_host::LinuxTextRuntime nested;
        bool refused=false;
        try{odh_host::LinuxTextRuntime invalid(nullptr);}catch(const std::invalid_argument&){refused=true;}
        check(refused,"invalid external map is rejected before changing the active scope");
        const auto after=odh_host::measure_text_overlay("ação",14,15);
        check(after.width==scoped.width&&after.height==scoped.height,"nested/error scope keeps the parent usable");
    }
    const auto unref=[](PangoFontMap* value){if(value)g_object_unref(value);};
    std::unique_ptr<PangoFontMap,decltype(unref)> reference(pango_cairo_font_map_new(),unref);
    check(bool(reference),"reference map allocated");
    const auto sample=[&](const std::string& text,std::uint8_t language){sample_with_reference(text,language,reference.get());};
    {
        const std::string message="Escolha uma família para entrar na casa.";
        const auto natural=odh_host::measure_text_overlay(message,14,14);
        const auto wrapped=odh_host::measure_text_overlay(message,14,14,78);
        check(natural.width>wrapped.width&&wrapped.width<=78&&wrapped.height>natural.height,
            "narrower dialog text wraps onto more lines");
        for(const auto invalid:{std::string("\xff",1),std::string(32769,'x')}){
            bool refused=false;
            try{(void)odh_host::measure_text_overlay(invalid,14,14);}
            catch(const std::invalid_argument&){refused=true;}
            check(refused,"measurement applies the same UTF-8 and byte limits as drawing");
        }
        for(const auto width:{-1,8193}){
            bool refused=false;
            try{(void)odh_host::measure_text_overlay(message,14,14,width);}
            catch(const std::invalid_argument&){refused=true;}
            check(refused,"out-of-bounds wrapping width rejected");
        }
    }
    {
        std::string long_text;for(unsigned n=0;n<1200;++n)long_text+="ação ";long_text+="Fim 日本語";
        const auto extent=odh_host::measure_text_overlay(long_text,14,11,160);
        check(long_text.size()>4096&&extent.height>200&&extent.height<8000,"full UTF-8 document beyond old byte limit has exact shaped height");
        odh::RasterFrame whole{200,static_cast<std::uint32_t>(extent.height+20),
            std::vector<std::array<std::uint8_t,4>>(200*(extent.height+20),{25,25,25,255}),{}};
        odh_host::TextOverlayStyle style;style.x=10;style.y=10;style.width=160;style.height=extent.height;
        style.padding=0;style.font_size_px=11;style.wrap_lines=true;style.background={0,0,0,0};style.pixel_scroll_y=0;
        (void)odh_host::draw_text_overlay(whole,long_text,14,style);
        odh::RasterFrame last{200,90,std::vector<std::array<std::uint8_t,4>>(200*90,{25,25,25,255}),{}};
        style.height=60;style.pixel_scroll_y=extent.height-60;
        const auto result=odh_host::draw_text_overlay(last,long_text,14,style);
        check(result.pixels_changed>0,"last shaped UTF-8 rows remain reachable without slicing characters");
        for(unsigned y=0;y<60;++y)for(unsigned x=0;x<160;++x)
            check(last.pixels[(y+10)*200+x+10]==whole.pixels[(y+10+extent.height-60)*200+x+10],
                "pixel viewport equals exact full shaped document crop through final rows");
        check(last.pixels[9*200+10]==std::array<std::uint8_t,4>{25,25,25,255}&&
            last.pixels[70*200+10]==std::array<std::uint8_t,4>{25,25,25,255},"scroll rendering cannot overwrite outside its viewport");
    }
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
    std::cout<<"linux text renderer checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"linux text renderer: "<<error.what()<<'\n';return 1;}}
