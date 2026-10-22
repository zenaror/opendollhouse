#include "linux_text_renderer.hpp"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace odh_host {
namespace {
struct SurfaceDeleter { void operator()(cairo_surface_t* p) const {if(p)cairo_surface_destroy(p);} };
struct ContextDeleter { void operator()(cairo_t* p) const {if(p)cairo_destroy(p);} };
struct ObjectDeleter { void operator()(gpointer p) const {if(p)g_object_unref(p);} };
using Surface=std::unique_ptr<cairo_surface_t,SurfaceDeleter>;
using Context=std::unique_ptr<cairo_t,ContextDeleter>;
template<class T> using Object=std::unique_ptr<T,ObjectDeleter>;

const char* language_tag(std::uint8_t language) {
    switch(language) {
    case 0: case 1:return "en-us";case 2:return "en-gb";case 3:return "fr";
    case 4:return "de";case 5:return "it";case 6:return "es";case 7:return "nl";
    case 8:return "da";case 9:return "sv";case 10:return "nb";case 11:return "fi";
    case 12:return "he";case 13:return "ru";case 14:return "pt";case 15:return "ja";
    case 16:return "pl";case 17:return "zh-cn";case 18:return "zh-tw";case 19:return "th";
    case 20:return "ko";default:throw std::invalid_argument("unsupported STR language code");
    }
}

std::size_t missing_glyphs(PangoLayout* layout,std::size_t& glyph_count) {
    std::size_t missing=0;
    auto* iterator=pango_layout_get_iter(layout);
    if(!iterator)throw std::runtime_error("Pango could not inspect the text layout");
    do {
        const auto* run=pango_layout_iter_get_run_readonly(iterator);
        if(run&&run->glyphs)for(int i=0;i<run->glyphs->num_glyphs;++i) {
            ++glyph_count;
            if(run->glyphs->glyphs[i].glyph&PANGO_GLYPH_UNKNOWN_FLAG)++missing;
        }
    } while(pango_layout_iter_next_run(iterator));
    pango_layout_iter_free(iterator);
    return missing;
}
}

TextOverlayResult draw_text_overlay(odh::RasterFrame& frame,std::string_view utf8,
                                    std::uint8_t language,TextOverlayStyle style) {
    (void)language_tag(language);
    if(utf8.empty()||utf8.size()>4096)throw std::invalid_argument("text overlay input outside byte limit");
    if(!g_utf8_validate(utf8.data(),static_cast<gssize>(utf8.size()),nullptr))
        throw std::invalid_argument("text overlay input is not valid UTF-8");
    if(!frame.width||!frame.height||frame.pixels.size()!=static_cast<std::size_t>(frame.width)*frame.height||
       frame.width>8192||frame.height>8192)throw std::invalid_argument("text overlay frame is malformed or oversized");
    if(style.x<0||style.y<0||style.x>8192||style.y>8192||style.width<=0||style.width>8192||
       style.height<=0||style.height>8192||style.font_size_px<1||style.font_size_px>256||
       style.padding<0||style.padding>4096||style.padding>=style.width/2||style.padding>=style.height/2)
        throw std::invalid_argument("text overlay geometry outside supported bounds");

    Surface surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,static_cast<int>(frame.width),static_cast<int>(frame.height)));
    if(!surface||cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS)
        throw std::runtime_error("Cairo could not allocate the text overlay");
    Context context(cairo_create(surface.get()));
    if(!context||cairo_status(context.get())!=CAIRO_STATUS_SUCCESS)
        throw std::runtime_error("Cairo could not create the text context");
    cairo_set_operator(context.get(),CAIRO_OPERATOR_CLEAR);cairo_paint(context.get());
    cairo_set_operator(context.get(),CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(context.get(),style.background[0]/255.0,style.background[1]/255.0,
                          style.background[2]/255.0,style.background[3]/255.0);
    cairo_rectangle(context.get(),style.x,style.y,style.width,style.height);cairo_fill(context.get());

    auto* font_map=pango_cairo_font_map_get_default();
    Object<PangoContext> pango_context(pango_font_map_create_context(font_map));
    if(!pango_context)throw std::runtime_error("Pango could not create a font context");
    pango_context_set_language(pango_context.get(),pango_language_from_string(language_tag(language)));
    Object<PangoLayout> layout(pango_layout_new(pango_context.get()));
    if(!layout)throw std::runtime_error("Pango could not create a text layout");
    pango_layout_set_text(layout.get(),utf8.data(),static_cast<int>(utf8.size()));
    pango_layout_set_single_paragraph_mode(layout.get(),TRUE);
    pango_layout_set_auto_dir(layout.get(),TRUE);
    pango_layout_set_width(layout.get(),(style.width-style.padding*2)*PANGO_SCALE);
    pango_layout_set_ellipsize(layout.get(),PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout.get(),language==12?PANGO_ALIGN_RIGHT:PANGO_ALIGN_LEFT);
    auto* description=pango_font_description_new();
    if(!description)throw std::runtime_error("Pango could not create a font description");
    pango_font_description_set_family(description,"Sans");
    pango_font_description_set_absolute_size(description,style.font_size_px*PANGO_SCALE);
    pango_layout_set_font_description(layout.get(),description);
    pango_font_description_free(description);

    PangoRectangle logical{};pango_layout_get_pixel_extents(layout.get(),nullptr,&logical);
    const auto text_y=style.y+(style.height-logical.height)/2-logical.y;
    cairo_set_source_rgba(context.get(),style.foreground[0]/255.0,style.foreground[1]/255.0,
                          style.foreground[2]/255.0,style.foreground[3]/255.0);
    cairo_move_to(context.get(),style.x+style.padding,text_y);
    pango_cairo_show_layout(context.get(),layout.get());
    if(cairo_status(context.get())!=CAIRO_STATUS_SUCCESS)
        throw std::runtime_error("Cairo failed while drawing the text overlay");

    TextOverlayResult result;
    result.missing_glyphs=missing_glyphs(layout.get(),result.glyphs);
    cairo_surface_flush(surface.get());
    const auto* data=cairo_image_surface_get_data(surface.get());
    const auto stride=cairo_image_surface_get_stride(surface.get());
    for(int y=std::max(0,style.y);y<std::min<int>(static_cast<int>(frame.height),style.y+style.height);++y)
        for(int x=std::max(0,style.x);x<std::min<int>(static_cast<int>(frame.width),style.x+style.width);++x) {
            std::uint32_t argb=0;
            std::memcpy(&argb,data+static_cast<std::size_t>(y)*static_cast<std::size_t>(stride)+static_cast<std::size_t>(x)*4,4);
            const auto alpha=(argb>>24)&255U;if(!alpha)continue;
            auto& pixel=frame.pixels[static_cast<std::size_t>(y)*frame.width+static_cast<std::size_t>(x)];
            const auto inverse=255U-alpha;
            const auto blend=[&](std::uint8_t old,unsigned premultiplied){
                return static_cast<std::uint8_t>(std::min(255U,premultiplied+(static_cast<unsigned>(old)*inverse+127U)/255U));};
            const auto before=pixel;
            pixel[0]=blend(pixel[0],(argb>>16)&255U);pixel[1]=blend(pixel[1],(argb>>8)&255U);
            pixel[2]=blend(pixel[2],argb&255U);pixel[3]=255;
            if(pixel!=before)++result.pixels_changed;
        }
    return result;
}
} // namespace odh_host
