#pragma once

#include "odh/bitmap.hpp"
#include "status_hud.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <string>

namespace odh_host {
struct OriginalControlPanelLayout;

struct MotivePanelResult {
    std::size_t labels_drawn=0;
    std::size_t missing_glyphs=0;
    std::size_t bar_pixels=0;
};

// ODH-owned labels: TS1's UIText.iff and Simitone's f102 table do not provide
// this complete set for every language. Unknown language codes are rejected.
const std::array<std::string_view,8>& motive_labels(std::uint8_t language);

// Draw the original paired columns of four needs when its shared layout is
// supplied; the standalone fallback remains separate from the numeric debug HUD.
// The optional game bitmap is used as original panel art when available.
MotivePanelResult draw_motive_panel(odh::RasterFrame&,const StatusHud&,
                                    std::uint8_t language,
                                    const odh::BitmapImage* original_panel_art=nullptr,
                                    const OriginalControlPanelLayout* original_layout=nullptr,
                                    const std::array<std::string,8>* localized_labels=nullptr);

} // namespace odh_host
