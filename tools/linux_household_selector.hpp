#pragma once

#include "odh/raster.hpp"
#include "odh/saved_world.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace odh_host {

struct UiRect {
    std::int32_t x=0,y=0,width=0,height=0;
};

UiRect household_selector_button_rect(std::uint32_t width,std::uint32_t height);
UiRect household_selector_panel_rect(std::uint32_t width,std::uint32_t height,
    std::size_t resident_count=8);
bool household_selector_button_hit(std::uint32_t width,std::uint32_t height,
                                   std::int32_t x,std::int32_t y);
using HouseholdSelectorPortrait=std::array<std::array<std::uint8_t,4>,74*74>;
struct HouseholdSelectorResident {
    std::int16_t runtime_id=0;
    std::string label;
    const HouseholdSelectorPortrait* portrait=nullptr;
};
// Look up the saved object's OBJT name by its original ID and GUID. The
// restored World owns these records after the loader moves its source vectors.
std::optional<std::string> household_selector_saved_name(std::int16_t runtime_id,
    std::uint32_t guid,const std::vector<odh::SavedWorldEntity>& saved_records,
    const odh::Objt& object_types);
std::optional<std::int16_t> household_selector_pick(const std::vector<HouseholdSelectorResident>& residents,
    std::uint32_t width,std::uint32_t height,std::int32_t x,std::int32_t y);

// This host-side strip selects loaded household members by runtime ID and
// displays their game-data name when available, with a runtime-ID fallback.
void draw_household_selector_button(odh::RasterFrame&,std::uint8_t language);
void draw_household_selector(odh::RasterFrame&,const std::vector<HouseholdSelectorResident>& residents,
    std::int16_t selected,std::uint8_t language);

} // namespace odh_host
