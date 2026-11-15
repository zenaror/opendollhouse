// SPDX-License-Identifier: MPL-2.0
// Compile the preserved Linux implementation under backend names. Its shaping,
// wrapping, UTF-8 validation and runtime ownership remain unchanged.
#define measure_text_overlay pango_measure_text_overlay
#define draw_text_overlay pango_draw_text_overlay
#define text_overlay_line_ranges pango_text_overlay_line_ranges
#include "linux_text_renderer.cpp"
