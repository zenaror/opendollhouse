// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "buy_mode_view.hpp"
namespace odh_host {
enum class OriginalOptionsAction {save,neighborhood,quit,graphics,sound,play};
struct OriginalOptionsLayout {
    std::array<BuyUiRect,6> buttons;
    BuyUiRect horizontal_divider,vertical_divider;
};
OriginalOptionsLayout original_options_layout(std::uint32_t width,std::uint32_t height);
// Presentation only. Enabled actions must have a real host consumer.
class OriginalOptionsView {
public:
    explicit OriginalOptionsView(const std::filesystem::path& game);
    std::optional<std::size_t> hit(const OriginalOptionsLayout&,int x,int y,
        const std::array<bool,6>& enabled)const;
    void draw(odh::RasterFrame&,const std::array<bool,6>& enabled,
        std::optional<std::array<int,2>> pointer={},std::optional<std::size_t> pressed={},std::optional<std::size_t> selected={})const;
private:
    OriginalUiArt art_;
};
}
