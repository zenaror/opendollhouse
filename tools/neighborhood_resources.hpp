// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "neighborhood_presentation.hpp"
namespace odh_host {
// A live engine may supply its existing saved/template resolver. Roots are
// explicit read authorities; a resolved symlink must remain under one of them.
struct NeighborhoodHouseSources {
    std::function<std::filesystem::path(std::uint16_t)> resolve;
    std::vector<std::filesystem::path> roots;
};
// Original resource preparation moved from neighborhood_screen.cpp. Resolves
// the selected archive/LotLocations and raw BMP512/513 + THMB, without an old
// family overlay, World, VM, save or window. Occupancy is a current-engine fact.
PreparedNeighborhoodMap prepare_neighborhood_map_resources(
    const std::filesystem::path& user_data,const std::filesystem::path& ui_graphics,
    unsigned mode,std::uint8_t language,const std::function<bool(std::uint16_t)>& occupied={});
PreparedNeighborhoodMap prepare_neighborhood_map_resources(
    const std::filesystem::path& user_data,const std::filesystem::path& ui_graphics,
    unsigned mode,std::uint8_t language,const std::function<bool(std::uint16_t)>& occupied,
    const std::vector<std::string>& location_files);
PreparedNeighborhoodMap prepare_neighborhood_map_resources(
    const std::filesystem::path& user_data,const std::filesystem::path& ui_graphics,
    unsigned mode,std::uint8_t language,const std::function<bool(std::uint16_t)>& occupied,
    const std::vector<std::string>& location_files,const NeighborhoodHouseSources& houses);
}
