// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "linux_cas_layout.hpp"
#include "original_ui_art.hpp"
#include <functional>
namespace odh_host {
inline constexpr odh::UiBitmapRect cas_preview_box{618,145,100,220};
std::vector<std::string> cas_art_keys();
// Pure original design-screen presentation. Draft ownership and save stay in
// each caller; callbacks only supply art/labels and already prepared previews.
struct CasPresentation {
    bool character=false;
    std::string family_name,name,biography,feedback;
    std::array<std::uint8_t,5> traits{};
    std::vector<std::string> labels,members;
    std::vector<CasWidget> controls;
    std::optional<std::size_t> focus,hovered,pressed,selected,cursor;
    std::function<bool(CasControl)> enabled,on;
    std::function<const odh::BitmapImage*(std::size_t)> portrait;
    std::function<const odh::BitmapImage*()> body_preview;
    odh::RasterFrame draw(const OriginalUiArt&,std::uint8_t language)const;
};
}
