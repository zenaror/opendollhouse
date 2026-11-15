/* SPDX-License-Identifier: MPL-2.0 */
#include "app/ui/OriginalTextBackend.h"
#ifndef __3DS__
#include "app/front/TS1Font.h"
#include "linux_text_renderer.hpp"
#include "linux_text_codec.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace simapp { namespace original {
namespace {
struct EncodedText { std::string bytes; size_t glyphs=0,missing=0; };
EncodedText Encode(std::string_view text,uint8_t language) {
    EncodedText out;
    const bool multibyteFont=language==15||language==17||language==18||language==20;
    size_t unsupported=0;
    for(size_t at=0;at<text.size();) {
        const auto first=static_cast<unsigned char>(text[at++]);
        uint32_t cp=first; int continuation=0; uint32_t minimum=0;
        if(first>=0xc2 && first<=0xdf){cp=first&31;continuation=1;minimum=0x80;}
        else if(first>=0xe0 && first<=0xef){cp=first&15;continuation=2;minimum=0x800;}
        else if(first>=0xf0 && first<=0xf4){cp=first&7;continuation=3;minimum=0x10000;}
        else if(first>=0x80){cp=0xfffd;}
        bool valid=at+size_t(continuation)<=text.size();
        for(int i=0;valid && i<continuation;++i) {
            const auto byte=static_cast<unsigned char>(text[at+i]);
            valid=(byte&0xc0)==0x80; cp=(cp<<6)|(byte&63);
        }
        if(valid){at+=continuation;valid=cp>=minimum && cp<=0x10ffff && !(cp>=0xd800&&cp<=0xdfff);}
        if(multibyteFont && cp>127)++unsupported;
        if(!valid || cp>255 || (multibyteFont && cp>127)){cp='?';++out.missing;}
        out.bytes.push_back(static_cast<char>(cp));
        if(cp>=32)++out.glyphs;
    }
    // This existing atlas addresses individual bytes. A DBCS conversion is not
    // evidence that the resulting pair can be rendered as one glyph.
    try { if(!unsupported){out.bytes=odh_host::utf8_to_legacy_text(text,language);out.missing=0;} }
    catch(const std::invalid_argument&) { /* Explicit missing-glyph result below. */ }
    return out;
}
front::TS1Size Font(int pixels) {
    constexpr front::TS1Size sizes[]={front::TS1Size::S9,front::TS1Size::S10,
        front::TS1Size::S11,front::TS1Size::S12,front::TS1Size::S14,
        front::TS1Size::S16,front::TS1Size::S20};
    auto best=sizes[0];int distance=std::numeric_limits<int>::max();
    for(auto size:sizes){const int delta=std::abs(front::TextH(size)-pixels);
        if(delta<distance){best=size;distance=delta;}}
    return best;
}
std::vector<std::string> Lines(front::TS1Size font,const std::string& text,int width) {
    return front::Wrap(font,text,width>0?width:std::numeric_limits<int>::max()/2);
}
odh_host::TextOverlayExtent Measure(std::string_view text,uint8_t language,int pixels,int wrap) {
    auto font=Font(pixels);const auto lines=Lines(font,Encode(text,language).bytes,wrap);
    int width=0;for(const auto& line:lines)width=std::max(width,front::TextW(font,line));
    return {width,int(lines.size())*std::max(1,front::TextH(font))};
}
odh_host::TextOverlayResult Draw(odh::RasterFrame& frame,std::string_view text,
                               uint8_t language,odh_host::TextOverlayStyle style) {
    odh_host::TextOverlayResult result;
    if(style.width<=0 || style.height<=0 || style.width>3840 || style.height>2160 ||
       frame.pixels.size()!=uint64_t(frame.width)*frame.height)return result;
    const auto encoded=Encode(text,language);result.glyphs=encoded.glyphs;result.missing_glyphs=encoded.missing;
    // Preserve the existing original-font coverage on non-shaping hosts. A
    // missing glyph is reported, never mistaken for full language coverage.
    static bool reported=false;
    if(encoded.missing && !reported){std::fprintf(stderr,"[ui] Original font backend lacks some requested glyphs; host shaping is not installed\n");reported=true;}
    const auto font=Font(style.font_size_px);const int height=std::max(1,front::TextH(font));
    const int padding=std::clamp(style.padding,0,std::min(style.width,style.height)/2);
    const int innerWidth=std::max(1,style.width-2*padding),innerHeight=std::max(0,style.height-2*padding);
    auto singleLine=encoded.bytes;
    if(!style.wrap_lines)for(auto& ch:singleLine)if(ch=='\r'||ch=='\n')ch=' ';
    auto lines=style.wrap_lines?Lines(font,encoded.bytes,innerWidth):
        std::vector<std::string>{front::Fit(font,singleLine,innerWidth)};
    result.line_count=lines.size();
    const size_t rows=std::max<size_t>(1,innerHeight/height);
    result.max_first_line=lines.size()>rows?lines.size()-rows:0;
    result.first_line=std::min(style.first_line,result.max_first_line);
    int offset=int(result.first_line)*height;
    if(style.pixel_scroll_y){offset=std::clamp(*style.pixel_scroll_y,0,
        std::max(0,int(lines.size())*height-innerHeight));result.first_line=size_t(offset/height);}
    Canvas canvas(style.width,style.height);
    for(int y=0;y<style.height;++y)for(int x=0;x<style.width;++x) {
        const int sx=style.x+x,sy=style.y+y;
        if(sx>=0&&sy>=0&&sx<int(frame.width)&&sy<int(frame.height)) {
            const auto& p=frame.pixels[size_t(sy)*frame.width+sx];canvas.Px[size_t(y)*style.width+x]=RGBA(p[0],p[1],p[2],p[3]);
        }
    }
    const auto& bg=style.background;canvas.FillRect(0,0,style.width,style.height,RGBA(bg[0],bg[1],bg[2],bg[3]));
    const auto background=canvas.Px;const auto& fg=style.foreground;
    result.visible_lines=0;
    for(size_t line=result.first_line;line<lines.size();++line) {
        const int y=style.wrap_lines?padding+int(line)*height-offset:(style.height-height)/2;
        if(y>=style.height-padding)break;
        if(y+height<=padding)continue;
        int x=padding;const int width=front::TextW(font,lines[line]);
        if(style.centered)x+=(innerWidth-width)/2;
        else if(style.right_aligned)x+=innerWidth-width;
        front::Text(canvas,font,x,y,lines[line],RGBA(fg[0],fg[1],fg[2],fg[3]));++result.visible_lines;
    }
    result.has_more=style.wrap_lines && offset+innerHeight<int(lines.size())*height;
    for(int y=0;y<style.height;++y)for(int x=0;x<style.width;++x) {
        const int sx=style.x+x,sy=style.y+y;if(sx<0||sy<0||sx>=int(frame.width)||sy>=int(frame.height))continue;
        const size_t local=size_t(y)*style.width+x;
        const auto pixel=(x<padding||x>=style.width-padding||y<padding||y>=style.height-padding)?background[local]:canvas.Px[local];
        std::array<uint8_t,4> next{uint8_t(pixel),uint8_t(pixel>>8),uint8_t(pixel>>16),uint8_t(pixel>>24)};
        auto& target=frame.pixels[size_t(sy)*frame.width+sx];if(target!=next){target=next;++result.pixels_changed;}
    }
    return result;
}
}
odh_host::TextOverlayBackend GameFontTextBackend() { return {Measure,Draw}; }
void InstallTextBackendIfNeeded() {
    if(!odh_host::text_overlay_backend_available())odh_host::set_text_overlay_backend(GameFontTextBackend());
}
}}
#else
namespace simapp { namespace original { void InstallTextBackendIfNeeded() {} }}
#endif
