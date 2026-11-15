// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "catalog_info_view.hpp"
namespace odh_host {
// Opt-in live text uses the observed13px face/17px line advance; catalog defaults remain unchanged.
// A positive live_scale selects the same typography for an image-column live
// popup; zero preserves existing text-only width inference and catalog callers.
// Optional Linux font adapter; the catalog/layout/core have no Pango types.
CatalogInfoTextPainter catalog_info_text_painter(std::uint8_t language,bool text_only_live=false,float live_scale=0.f);
CatalogInfoHeadingPainter catalog_info_heading_painter(std::uint8_t language,bool text_only_live=false,float live_scale=0.f);
CatalogInfoScrollPainter catalog_info_scroll_painter(std::uint8_t language,bool text_only_live=false,float live_scale=0.f);
CatalogInfoTextMeasure catalog_info_text_measure(std::uint8_t language,bool text_only_live=false,float live_scale=0.f);
}
