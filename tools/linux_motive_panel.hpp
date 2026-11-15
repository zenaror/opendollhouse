#pragma once

#include "odh/bitmap.hpp"
#include "odh/ui_bitmap.hpp"
#include "status_hud.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <string>

namespace odh_host {
struct OriginalControlPanelLayout;

// One geometry contract for painting, pointer occlusion and camera edges.
// Tall is the observed Japanese Needs presentation, not a global HUD resize.
struct OriginalMotivePanelGeometry {
    odh::UiBitmapRect panel,artwork,title;
    std::array<odh::UiBitmapRect,8> labels,meters;
    int title_font_size_px=14,label_font_size_px=11;
    bool tall=false;
};
OriginalMotivePanelGeometry original_motive_panel_geometry(
    std::uint32_t width,std::uint32_t height,const OriginalControlPanelLayout&,
    std::uint8_t language,const odh::BitmapImage* tall_art=nullptr);

struct MotivePanelResult {
    std::size_t labels_drawn=0;
    std::size_t missing_glyphs=0;
    std::size_t bar_pixels=0;
    std::size_t arrow_pixels=0;
};

// Legacy standalone fallback labels. The integrated PC consumer supplies the
// authored Live.iff STR130 labels and heading through GameUiStrings instead.
// Unknown language codes are rejected.
const std::array<std::string_view,8>& motive_labels(std::uint8_t language);

// Draw the original paired columns of four needs when its shared layout is
// supplied; the standalone fallback remains separate from the numeric debug HUD.
// Without original_layout the optional bitmap is the standalone backdrop.
// The integrated consumer supplies prepared geometry plus TallSubPanel art.
// Existing layout-only callers retain their standard presentation contract;
// they do not silently acquire a raised painted area without matching input.
MotivePanelResult draw_motive_panel(odh::RasterFrame&,const StatusHud&,
                                    std::uint8_t language,
                                    const odh::BitmapImage* original_panel_art=nullptr,
                                    const OriginalControlPanelLayout* original_layout=nullptr,
                                    const std::array<std::string,8>* localized_labels=nullptr,
                                    const std::string* localized_heading=nullptr);
// Explicit prepared overload: the legacy symbol and standard layout stay
// available, while the desktop supplies the SAME geometry used by input.
MotivePanelResult draw_motive_panel(odh::RasterFrame&,const StatusHud&,
                                    std::uint8_t language,
                                    const odh::BitmapImage* original_panel_art,
                                    const OriginalControlPanelLayout* original_layout,
                                    const std::array<std::string,8>* localized_labels,
                                    const std::string* localized_heading,
                                    const OriginalMotivePanelGeometry* prepared_geometry);

// Existing history states are presentation inputs only; no VM or OS dependency.
MotivePanelResult draw_motive_panel(odh::RasterFrame&,const StatusHud&,
                                    std::uint8_t language,
                                    const odh::BitmapImage* original_panel_art,
                                    const OriginalControlPanelLayout* original_layout,
                                    const std::array<std::string,8>* localized_labels,
                                    const std::string* localized_heading,
                                    const OriginalMotivePanelGeometry* prepared_geometry,
                                    const std::array<int,8>* change_arrows);

} // namespace odh_host
