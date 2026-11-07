// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/session_bundle.hpp"

namespace odh {
// Frozen lot-edit state, not a household save. There is deliberately no
// resident, family, lease, balance, avatar VM, or invented interaction owner.
// Existing saved avatar entities may remain frozen in World; ordinary object
// commands cannot invent their Thread/queue authority.
struct ActorlessObjectTradeState {
    SessionResourceIdentity resources;
    WorldSnapshot world;
    RelationshipSnapshot relationships;
    std::uint64_t random_seed=0;
    std::uint64_t tick=0;
    RuntimeGlobalsSnapshot globals; // Actual source bank, with simless Global32=1.
    std::optional<Ts1ClockSnapshot> clock; // Actual source clock or unknown.
    EntityVmSchedulerSnapshot scheduler; // Frozen object Threads only.
    std::vector<SessionObjectThread> object_threads;
    CodeOwner next_code_owner=0x80000000U;
    bool code_owner_ids_exhausted=false;
    std::vector<SessionCreatedObject> created_objects;
    std::vector<SessionRemovedOriginalObject> removed_original_objects;
    SessionObjectHistory history; // Must explicitly prove complete original history.
    SessionObjectTradeLedger trade;
    std::optional<ArchitectureObjectSupport> object_support;
    std::optional<ArchitectureObjectState> object_state;
    std::optional<BuildBuyPermissionSnapshot> build_buy_permission;
    // Constructed physical groups share OGRP with household sessions; absent
    // preserves older edit-state bytes. No resident/HOST authority is implied.
    std::optional<ObjectGroupSnapshot> object_groups;
};
SessionBundleLimits actorless_object_trade_limits();
void validate_actorless_object_trade_state(const ActorlessObjectTradeState&,SessionBundleLimits = actorless_object_trade_limits());
std::vector<std::uint8_t> encode_actorless_object_trade_state(const ActorlessObjectTradeState&,SessionBundleLimits = actorless_object_trade_limits());
ActorlessObjectTradeState decode_actorless_object_trade_state(const std::vector<std::uint8_t>&,
    const SessionResourceIdentity* expected_resources=nullptr,SessionBundleLimits = actorless_object_trade_limits());
// Internal component carrier for the shared lifecycle kernel and its real
// bindings. This has NO household authority and must never be encoded with
// encode_session_bundle. Conversion rejects all household/active-avatar data.
SessionBundle actorless_object_trade_components(const ActorlessObjectTradeState&);
ActorlessObjectTradeState actorless_object_trade_from_components(const SessionBundle&);
}
