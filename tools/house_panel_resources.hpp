// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "house_panel_snapshot.hpp"
#include "odh/named_float_constants.hpp"
#include "odh/saved_architecture.hpp"

namespace odh_host {
enum class HousePanelTuningKey {yard_multiplier,dirty_penalty,broken_penalty,new_history_fill,outdoor_impact,outdoor_good_count,count};
struct HousePanelResources {
    HousePanelTuning tuning{};
    std::int32_t new_layout_history_fill=0;
    std::optional<odh::Ts1Curve> size_curve,furnishings_curve;
    std::array<bool,static_cast<std::size_t>(HousePanelTuningKey::count)> defaulted{};
};
struct HousePanelResourceLimits {
    std::uint64_t iff_bytes=16*1024*1024;
    std::uint32_t chunks=4096;
    odh::NamedFloatLimits named;
    odh::Ts1CurveLimits curve;
};
// Caller MUST supply the authenticated current selected Global resource.
// Full IFF traversal proves absence before any compiled per-key default is
// admitted. Duplicate/malformed FCNS3/4 or STR505 fail, never become absence.
// Curves have no invented fallback. Integer tunings must be exact nonnegative
// Int32 values; fractional custom penalties/fill remain unsupported.
HousePanelResources read_house_panel_resources(const odh::View& authenticated_selected_global,HousePanelResourceLimits={});
// Current ARRY9 pool bytes<16 contribute once per distinct roomID in each
// packed XY cell/story. Pool lookup deliberately ignores story. Missing pools
// returns absence; invalid extents/room identities fail. Never reload an older
// saved map or substitute grass/target_grass for the current snapshot.
std::optional<std::vector<std::uint32_t>> house_pool_good_cells(
    const odh::SavedArchitecture& current,const odh::GeometricRoomMap& current_rooms);
}
