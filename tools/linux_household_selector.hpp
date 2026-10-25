#pragma once

#include "odh/raster.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace odh_host {

struct UiRect {
    std::int32_t x=0,y=0,width=0,height=0;
};

UiRect household_selector_button_rect(std::uint32_t width,std::uint32_t height);
UiRect household_selector_panel_rect(std::uint32_t width,std::uint32_t height);
bool household_selector_button_hit(std::uint32_t width,std::uint32_t height,
                                   std::int32_t x,std::int32_t y);
std::optional<std::int16_t> household_selector_pick(const std::vector<std::int16_t>& residents,
    std::uint32_t width,std::uint32_t height,std::int32_t x,std::int32_t y);

// This host-side strip selects among household members already loaded by the
// game runtime. It shows runtime IDs until ODH has a verified name/portrait source.
void draw_household_selector_button(odh::RasterFrame&,std::uint8_t language);
void draw_household_selector(odh::RasterFrame&,const std::vector<std::int16_t>& residents,
    std::int16_t selected,std::uint8_t language);

} // namespace odh_host
