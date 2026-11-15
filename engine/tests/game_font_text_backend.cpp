/* SPDX-License-Identifier: MPL-2.0 */
// Exercise the real non-Pango service used by the other platform adapters.
#include "app/ui/OriginalTextBackend.h"
#include "linux_text_renderer.hpp"
#include <stdexcept>
#include <iostream>
#include <algorithm>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    const auto backend=simapp::original::GameFontTextBackend();
    const auto size=backend.measure("Família São João",14,14,0);
    require(size.width>0&&size.height>0,"font backend must measure accented labels");
    odh::RasterFrame frame;frame.width=240;frame.height=96;
    const std::array<uint8_t,4> sentinel{17,27,37,255};frame.pixels.assign(240*96,sentinel);
    odh_host::TextOverlayStyle style;style.x=8;style.y=10;style.width=220;style.height=48;
    style.padding=2;style.font_size_px=14;style.background={0,0,0,0};
    const auto result=backend.draw(frame,"Família São João",14,style);
    require(result.pixels_changed>0&&result.missing_glyphs==0,"portable labels must render without losing Portuguese accents");
    int top=96;
    for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){
        const bool inside=x>=8&&x<228&&y>=10&&y<58;
        require(inside||frame.pixels[y*frame.width+x]==sentinel,"label paint must stay in its viewport");
        if(frame.pixels[y*frame.width+x]!=sentinel)top=std::min(top,int(y));
    }
    require(top>style.y+style.padding+3,"single-line labels must be vertically centered");
    const auto unsupported=backend.draw(frame,"AあB",15,style);
    require(unsupported.missing_glyphs==1,"DBCS conversion must not pretend that the byte atlas supports a Japanese glyph");
    style.width=60;style.height=30;style.wrap_lines=true;style.first_line=999;
    const auto scrolled=backend.draw(frame,"Uma casa para a família e os amigos",14,style);
    require(scrolled.line_count>1&&scrolled.first_line==scrolled.max_first_line&&!scrolled.has_more,
            "wrapped text must stop on the last full page");
    std::cout<<"Portable game-font labels, accents, viewport, pagination and missing-glyph reporting passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
