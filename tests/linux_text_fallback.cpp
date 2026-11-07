#include "linux_text_renderer.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
    {
        const std::string message="Escolha uma família para entrar na casa.";
        const auto natural=odh_host::measure_text_overlay(message,14,14);
        const auto wrapped=odh_host::measure_text_overlay(message,14,14,78);
        require(natural.width>wrapped.width&&wrapped.width<=78&&wrapped.height>natural.height,
            "narrower dialog text wraps onto more lines");
        for(const auto invalid:{std::string("\xff",1),std::string(32769,'x')}){
            bool refused=false;
            try{(void)odh_host::measure_text_overlay(invalid,14,14);}
            catch(const std::invalid_argument&){refused=true;}
            require(refused,"measurement applies the same UTF-8 and byte limits as drawing");
        }
        for(const auto width:{-1,8193}){
            bool refused=false;
            try{(void)odh_host::measure_text_overlay(message,14,14,width);}
            catch(const std::invalid_argument&){refused=true;}
            require(refused,"out-of-bounds wrapping width rejected");
        }
    }
        odh::RasterFrame frame;
        frame.width = 128;
        frame.height = 32;
        frame.pixels.assign(128 * 32, {10, 20, 30, 255});
        frame.depth.assign(frame.pixels.size(), 0.0F);
        odh_host::TextOverlayStyle style;
        style.x = 2;
        style.y = 2;
        style.width = 116;
        style.height = 24;
        style.font_size_px = 14;
        style.padding = 2;
        style.foreground = {250, 240, 230, 255};
        style.background = {30, 40, 50, 220};

        const auto basic = odh_host::draw_text_overlay(frame, "Move In 5", 1, style);
        require(basic.glyphs == 7, "ASCII letters and digits are rendered");
        require(basic.missing_glyphs == 0 && basic.pixels_changed > 100,
                "overlay draws readable letters and a background panel");
        require(std::find(frame.pixels.begin(), frame.pixels.end(),
                          std::array<std::uint8_t, 4>{250, 240, 230, 255}) != frame.pixels.end(),
                "glyph pixels use the requested foreground color");
        require(frame.pixels[0] == std::array<std::uint8_t, 4>{10, 20, 30, 255},
                "pixels outside the overlay remain unchanged");
        require(frame.depth[0] == 0.0F, "text does not change scene depth");

        const auto localized = odh_host::draw_text_overlay(frame, "Família", 14, style);
        require(localized.missing_glyphs == 0,
                "common accented Latin letters use readable base glyphs");
        const auto other_script = odh_host::draw_text_overlay(frame, "日本語", 15, style);
        require(other_script.missing_glyphs == 3 && other_script.glyphs == 3,
                "unsupported scripts are visibly marked instead of disappearing");

        bool invalid_utf8 = false;
        try {
            (void)odh_host::draw_text_overlay(frame, std::string_view("\xc3(", 2), 1, style);
        } catch (const std::invalid_argument&) {
            invalid_utf8 = true;
        }
        require(invalid_utf8, "malformed UTF-8 is rejected");

        odh::RasterFrame wrapped;wrapped.width=240;wrapped.height=120;
        wrapped.pixels.assign(240U*120U,{10,20,30,255});
        style.x=2;style.y=2;style.width=220;style.height=60;style.padding=2;
        style.wrap_lines=true;
        const std::string message="Primeira linha de ajuda com palavras longas.\nSegunda linha visível.\nTerceira linha.\nQuarta linha.\nÚltima linha.";
        const auto initial=wrapped;
        const auto first=odh_host::draw_text_overlay(wrapped,message,14,style);
        require(first.line_count>=5&&first.visible_lines>1&&first.has_more,
            "bitmap fallback wraps text and exposes bounded scrolling instead of single-line truncation");
        const auto first_pixels=wrapped.pixels;wrapped=initial;
        style.first_line=std::numeric_limits<std::size_t>::max();
        const auto last=odh_host::draw_text_overlay(wrapped,message,14,style);
        require(last.first_line==last.max_first_line&&!last.has_more&&last.first_line+last.visible_lines==last.line_count&&
            wrapped.pixels!=first_pixels,"bitmap final page is reachable and overscroll clamps safely");
        require(wrapped.pixels.back()==initial.pixels.back(),"wrapped fallback does not draw outside its text box");

        {
            std::string document;for(unsigned n=0;n<1200;++n)document+="ação ";document+="FIM 日本語";
            require(document.size()>4096,"fallback viewport exercises complete UTF-8 beyond old byte limit");
            const auto extent=odh_host::measure_text_overlay(document,14,7,180);
            require(extent.height>100&&extent.height<8000,"fallback full reference remains within bounded raster profile");
            for(int padding:{0,2}){
                odh_host::TextOverlayStyle viewport;
                viewport.x=10;viewport.y=10;viewport.width=180+2*padding;viewport.height=extent.height+2*padding;
                viewport.padding=padding;viewport.font_size_px=7;viewport.wrap_lines=true;viewport.pixel_scroll_y=0;
                viewport.foreground={250,240,230,255};viewport.background={30,40,50,220};
                const auto make_frame=[](unsigned height){
                    odh::RasterFrame result;result.width=220;result.height=height;
                    result.pixels.assign(220*height,{10,20,30,255});result.depth.assign(result.pixels.size(),17.25f);
                    result.owner_ids.assign(result.pixels.size(),93);return result;
                };
                auto full=make_frame(extent.height+2*padding+20);
                (void)odh_host::draw_text_overlay(full,document,14,viewport);
                constexpr int visible_height=31;
                for(int offset:{1,7,9,extent.height-visible_height}){
                    auto page=make_frame(70);const auto original_depth=page.depth;const auto original_owners=page.owner_ids;
                    viewport.height=visible_height+2*padding;viewport.pixel_scroll_y=offset;
                    auto background=page;auto background_style=viewport;background_style.foreground[3]=0;
                    (void)odh_host::draw_text_overlay(background,document,14,background_style);
                    const auto drawn=odh_host::draw_text_overlay(page,document,14,viewport);
                    require(drawn.pixels_changed>0,"partial and final fallback viewports paint visible document rows");
                    for(unsigned y=0;y<page.height;++y)for(unsigned x=0;x<page.width;++x){
                        const bool inside=int(x)>=10+padding&&int(x)<190+padding&&
                            int(y)>=10+padding&&int(y)<10+padding+visible_height;
                        const auto at=std::size_t(y)*page.width+x;
                        if(inside)require(page.pixels[at]==full.pixels[std::size_t(y+offset)*full.width+x],
                            "fallback pixel viewport equals exact complete-document crop at partial/final offset");
                        else require(page.pixels[at]==background.pixels[at],
                            "scrolled fallback glyphs preserve all padding and pixels outside text viewport");
                    }
                    require(page.depth==original_depth&&page.owner_ids==original_owners,
                        "fallback viewport never changes consumer depth or picking ownership");
                }
            }
        }
        std::cout << "built-in bitmap text fallback passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
