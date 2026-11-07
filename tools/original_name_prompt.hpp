// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/raster.hpp"
#include "odh/ui_bitmap.hpp"
#include <array>
#include <optional>
#include <string_view>

namespace odh_host {
class OriginalUiArt;
// A native composition of existing original popup, button and CAS name-field
// assets. This is not evidence of an original standalone Rename Family screen.
struct OriginalNamePromptLayout {
    odh::UiBitmapRect panel,title,message,field;
    std::array<odh::UiBitmapRect,2> buttons;
};
OriginalNamePromptLayout original_name_prompt_layout(std::uint32_t width,std::uint32_t height);
std::optional<std::size_t> original_name_prompt_target(const OriginalNamePromptLayout&,int x,int y);
void draw_original_name_prompt(odh::RasterFrame&,const OriginalNamePromptLayout&,const OriginalUiArt*,
    std::string_view title,std::string_view message,std::string_view name,
    std::string_view accept,std::string_view cancel,std::uint8_t language,bool field_focused,
    bool select_all,std::optional<std::array<std::int32_t,2>> pointer={},
    std::optional<std::size_t> pressed={},std::optional<std::size_t> keyboard_button={});
}
