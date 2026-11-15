// SPDX-License-Identifier: MPL-2.0
#include "linux_text_renderer.hpp"
#include <stdexcept>
namespace odh_host {
#ifdef ODH_TEXT_PANGO_BACKEND
TextOverlayExtent pango_measure_text_overlay(std::string_view,std::uint8_t,int,int);
TextOverlayResult pango_draw_text_overlay(odh::RasterFrame&,std::string_view,std::uint8_t,TextOverlayStyle);
std::vector<TextOverlayLineRange> pango_text_overlay_line_ranges(std::string_view,std::uint8_t,int,int);
static thread_local TextOverlayBackend backend{pango_measure_text_overlay,pango_draw_text_overlay,pango_text_overlay_line_ranges};
#else
static thread_local TextOverlayBackend backend{};
#endif
bool text_overlay_backend_available() noexcept{return backend.measure && backend.draw;}
TextOverlayBackend set_text_overlay_backend(TextOverlayBackend next){
    if(!next.measure||!next.draw)throw std::invalid_argument("text backend needs matching measure and draw");
    auto previous=backend;backend=next;return previous;
}
TextOverlayExtent measure_text_overlay(std::string_view text,std::uint8_t language,int size,int wrap){
    if(!backend.measure)throw std::runtime_error("platform text backend unavailable");
    return backend.measure(text,language,size,wrap);
}
std::vector<TextOverlayLineRange> text_overlay_line_ranges(std::string_view text,std::uint8_t language,int size,int wrap){
    return backend.lines?backend.lines(text,language,size,wrap):std::vector<TextOverlayLineRange>{};
}
TextOverlayResult draw_text_overlay(odh::RasterFrame& frame,std::string_view text,std::uint8_t language,TextOverlayStyle style){
    if(!backend.draw)throw std::runtime_error("platform text backend unavailable");
    return backend.draw(frame,text,language,style);
}
}
