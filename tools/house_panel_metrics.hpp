// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/room_map.hpp"
#include "odh/ts1_curve.hpp"
#include <functional>
namespace odh_host {
struct HousePanelTuning {
    float yard_multiplier,outdoor_negative_impact_factor,outdoor_good_count_factor;
    std::int32_t dirty_penalty,broken_penalty;
};
HousePanelTuning original_house_panel_default_tuning();
// FAMI's actual home assignment owns home identity. An explicit zoning record
// can disprove residential status; an absent record is not a guessed community.
bool house_panel_is_home(std::uint16_t loaded_house,std::int32_t family_house,
    std::optional<std::int16_t> zoning,bool visiting,std::int16_t pending_switch) noexcept;
struct HousePanelObjectMetrics {
    bool positioned=false;
    std::optional<std::int16_t> hidden,room,category,impact,repair,dirty;
    std::array<std::optional<std::int16_t>,3> upkeep_definition_words{};
};
struct HousePanelLayoutPair {std::int32_t samples=0,routing=0;};
struct HousePanelLayoutMetrics {
    std::array<HousePanelLayoutPair,5> past{};
    HousePanelLayoutPair current;
    std::optional<std::uint64_t> last_sample_tick;
};
struct HousePanelMetrics {
    const odh::GeometricRoomMap& rooms;
    std::optional<std::uint32_t> family_members;
    const odh::Ts1Curve* size_curve=nullptr;
    const odh::Ts1Curve* furnishings_curve=nullptr;
    const HousePanelTuning* tuning=nullptr;
    std::optional<HousePanelLayoutMetrics> layout_history;
    // Exact resolved (architecture + architecture-objects) / primary objects.
    // Caller owns same-state valuation authority; SIMI cached totals forbidden.
    std::optional<float> furnishings_ratio;
    bool objects_complete=false,object_words_current=false;
    std::vector<HousePanelObjectMetrics> objects;
    std::function<std::optional<std::uint32_t>(std::uint16_t)> outdoor_good_terrain_cells;
};
struct HousePanelSnapshot {
    std::optional<std::uint32_t> area_square_feet,bedrooms,bathrooms;
    std::array<std::optional<std::int16_t>,5> ratings;
    bool complete()const;
};
std::optional<unsigned> house_lot_class(std::optional<std::int16_t> loaded_side);
// Pure shared kernel: no World, VM, object ownership, callbacks that execute
// scripts, resource selection, mutation or persistent bank reconstruction.
HousePanelSnapshot prepare_house_panel_metrics(const HousePanelMetrics&);
}
