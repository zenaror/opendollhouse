// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/architecture_object_state.hpp"
#include "odh/object_position_change.hpp"
#include "odh/prepared_architecture.hpp"
#include "odh/wall_appearance.hpp"
#include <array>

namespace odh_host {
// Host replay and native checkpoint storage share one authoritative state.
using LotPositionLoadState=odh::ArchitectureObjectState;
struct LotPositionLoadContext {
    odh::World* world=nullptr;
    const odh::PreparedArchitecture* prepared=nullptr;
    // Complete original imported entity sequence, ordered by source ObjectID.
    // Native checkpoint restores must NOT run this fresh-load constructor.
    std::vector<odh::ObjectPositionIdentity> source_order;
    std::function<bool()> source_still_matches;
    // Source load capability: validates exact resource/thread/group/lighting
    // consumers, separately from ordinary placement admission. No effects.
    std::function<std::optional<bool>(odh::ObjectPositionIdentity)> admit_loaded_entity;
    std::function<std::optional<std::uint16_t>(const odh::EntityPosition&)> room_at;
    std::function<bool(odh::ObjectPositionIdentity,bool)> unregister_position;
    std::function<bool(odh::ObjectPositionIdentity,std::uint16_t,bool)> register_position;
    // Actual synchronous source EP6; returned bool certifies execution, not the
    // BHAV result. Source args are {0,0,0,0}; stack object is the same entity.
    std::function<bool(odh::ObjectPositionIdentity,std::uint16_t,std::int16_t,const std::array<std::int16_t,4>&)> execute_entry;
    // Exact provider-resolved OBJD wall style, including dynamic sprite-backed
    // identity when necessary. Absence rejects, never invents a style number.
    std::function<std::optional<odh::WallAppearanceStyle>(odh::ObjectPositionIdentity)> resolved_wall_style;
    // Real UpdateDynamicMultitile including combine/recursion and EP8 flags.
    std::function<bool(odh::ObjectPositionIdentity)> dynamic_multitile;
};
struct LotPositionLoadResult {
    std::optional<LotPositionLoadState> state;
    std::size_t roots_replayed=0;
    bool effects_started=false;
    std::string diagnostic;
};
struct LotPositionWallConsumers {
    std::function<bool(odh::ObjectPositionIdentity,bool,bool)> wall_use;
    std::function<bool(odh::ObjectPositionIdentity)> architectural_wall_style;
    // Normal Pre clears style0; normal Post applies actual provider style.
    std::function<bool(odh::ObjectPositionIdentity,bool set)> architectural_wall_style_change;
};
// Bind recovery/ordinary position consumers to the SAME authoritative live
// architecture state. No constructors, occupancy reset or ledger replacement.
// State, World and prepared architecture must outlive the returned callbacks;
// the source lease is rechecked for each operation and after script callbacks.
// Invalid grid extents/source authority throw before returning consumers.
LotPositionWallConsumers bind_lot_position_wall_consumers(LotPositionLoadContext,LotPositionLoadState&);
// Fresh imported-load safe point only. Preflights the complete original cohort
// before events. After effects_started, failure is NOT rollback: discard the
// private candidate/bootstrap, never continue a partly replayed live session.
LotPositionLoadResult replay_imported_lot_positions(LotPositionLoadContext);
}
