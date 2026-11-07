// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "catalog_info_view.hpp"
namespace odh_host {
// Optional Linux font adapter; the catalog/layout/core have no Pango types.
CatalogInfoTextPainter catalog_info_text_painter(std::uint8_t language);
CatalogInfoScrollPainter catalog_info_scroll_painter(std::uint8_t language);
CatalogInfoTextMeasure catalog_info_text_measure(std::uint8_t language);
}
