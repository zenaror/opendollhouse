// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "house_panel_metrics.hpp"
#include "odh/house_layout_history.hpp"
#include "odh/property_valuation.hpp"
#include "odh/ts1_curve.hpp"
#include <array>
#include <functional>

namespace odh_host {
using odh::HouseLayoutPair;
using odh::HouseLayoutHistory;
using odh::read_house_layout_history;
using odh::new_house_layout_history;
using odh::sample_house_layout_history;
using odh::rotate_house_layout_history;

struct HousePanelBindings {
    // Room29 MUST come from the caller's current room-map authority, never a
    // potentially stale stored bank after architecture changes. Other fields
    // may use the same World.GetValue adapter; absent callback only permits
    // direct known-bank reads for fields other than Room29.
    std::function<std::optional<std::int16_t>(const odh::Entity&,std::int16_t)> object_word;
    // Unique full OBJD word from each entity's own definition. Missing differs0.
    std::function<std::optional<std::int16_t>(const odh::Entity&,std::int16_t)> definition_word;
    // Original467c93 adds terrain<16 cells to room+54. Supply those actual
    // per-room counts; do not substitute Simitone's room score or floor count.
    std::function<std::optional<std::uint32_t>(std::uint16_t)> outdoor_good_terrain_cells;
};
struct HousePanelInput {
    const odh::World& world;
    const odh::GeometricRoomMap& rooms;
    // Original family GUID-vector count (4102e1), including absent members;
    // do not substitute the currently visible/rendered resident count.
    std::optional<std::uint32_t> family_members;
    const odh::PropertyValuation* valuation=nullptr; // Current resolved parts.
    const odh::Ts1Curve* size_curve=nullptr; // Global.iff STR505[0].
    const odh::Ts1Curve* furnishings_curve=nullptr; // STR505[1].
    const HousePanelTuning* tuning=nullptr;
    const HouseLayoutHistory* layout_history=nullptr;
    HousePanelBindings bindings;
    // Casa needs only these resolved financial components, not final lot price.
    const odh::PropertyObjectAssets* object_assets=nullptr;
    const odh::PropertyArchitectureValue* architecture_value=nullptr;
};
// Original loaded terrain side comes from authenticated SIMI.GlobalData[23].
// Zero selects the constructor64 fallback; unsupported values stay unknown.
std::optional<unsigned> house_lot_class(std::optional<std::int16_t> loaded_side);
HousePanelSnapshot prepare_house_panel_snapshot(const HousePanelInput&);
} // namespace odh_host
