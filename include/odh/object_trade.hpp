// SPDX-License-Identifier: MPL-2.0
// TS1 rules from pinned FreeSO 7e76c196 and Simitone UIObjectHolder.
#pragma once
#include "odh/world.hpp"
#include <optional>
#include <vector>

namespace odh {
// Decisions only: no creation, placement, deletion, money or save publication.
// Unknown denotes unavailable source facts, not an authored rejection.
enum class ObjectTradeStatus { allowed, rejected, unresolved };
enum class ObjectTradeReason {
    none, unknown_catalog, invalid_catalog, unknown_price,
    unknown_funds, insufficient_funds, invalid_identity, incomplete_group,
    invalid_topology, topology_budget, unknown_flags, unknown_buildable_area,
    unknown_threads, avatar, cannot_move, cannot_delete, out_of_bounds, in_use,
    contained_item, unknown_transaction_state, transaction_incomplete
};
struct ObjectTradeDecision {
    ObjectTradeStatus status=ObjectTradeStatus::unresolved;
    ObjectTradeReason reason=ObjectTradeReason::none;
    std::int16_t entity=0;
};
enum class ObjectTradeCatalogState { unknown, missing, present };
struct ObjectTradeCatalogItem {
    ObjectTradeCatalogState state=ObjectTradeCatalogState::unknown;
    std::uint32_t guid=0,price=0;
};
struct ObjectTradeValue {
    ObjectTradeDecision decision;
    std::optional<std::int32_t> amount;
};
// TS1 discount is zero. Known missing item costs zero in VMNetBuyObjectCmd;
// that is not proof the definition exists or can be constructed/placed.
ObjectTradeValue ts1_object_purchase_value(std::uint32_t requested_guid,const ObjectTradeCatalogItem&);
// VMMultitileGroup.Price: live InitialPrice and base object's Wear, NOT OD41.
// known-null Wear means the source's absent ObjectState fallback 80;
// unknown Wear must not silently select that fallback. C# unchecked int32
// multiplication and cast are reproduced explicitly without C++ overflow UB.
ObjectTradeValue ts1_object_refund_value(std::optional<std::int32_t> initial_price,
    bool wear_known,std::optional<std::uint16_t> wear);
struct ObjectTradeFunds {
    bool known=false;
    // Known absence of CurrentFamily follows TS1 GlobalLink's no-op budget.
    std::optional<std::int32_t> family_budget;
};
struct ObjectTradeBalance {
    ObjectTradeDecision decision;
    std::optional<std::int32_t> budget_after;
};
// Reproduces unchecked signed32 delta/add and source nonnegative-balance test,
// including negative amounts produced by authored/source arithmetic.
ObjectTradeBalance ts1_object_trade_balance(const ObjectTradeFunds&,std::int32_t amount,bool refund);

struct ObjectTradeIdentity { std::int16_t id=0;std::uint64_t generation=0; };
struct ObjectTradeGroup {
    // Actual complete MultitileGroup.Objects order, including singleton groups.
    bool complete=false;
    std::vector<ObjectTradeIdentity> members;
};
struct ObjectTradeAvatarThread {
    ObjectTradeIdentity avatar;
    // Actual entity Thread, not a queue or a reconstructed placeholder stack.
    const Vm* thread=nullptr;
};
struct ObjectTradeBuildableArea {
    std::uint32_t width=0,height=0;
    std::int16_t floors=0;
    std::int32_t x=0,y=0,right=0,bottom=0;
    // Present iff Architecture.FineBuildableArea exists. Preserve source's
    // unusual false result outside the architecture when this map exists.
    std::optional<std::vector<bool>> fine;
};
struct ObjectTradePermissionInput {
    const World& world;
    ObjectTradeIdentity target;
    const std::vector<ObjectTradeGroup>& groups;
    const std::vector<ObjectTradeAvatarThread>& avatar_threads;
    bool avatar_threads_complete=false;
    std::optional<ObjectTradeBuildableArea> buildable;
    bool move_objects=false;
    bool deleting=false;
    // VMNetDeleteObjectCmd.Verify tests only selected object's Disabled bit0,
    // after IsUserMovable; TS1 MoveObjects bypasses that check. Other TSO
    // owner, roommate, donation and inventory restrictions do not apply.
    std::optional<std::uint8_t> selected_disabled;
    // UIObjectHolder asks IsUserMovable(BaseObject) only. The command later
    // verifies the clicked object's transaction flag separately. Default keeps
    // the complete delete-command verification for existing consumers.
    bool verify_delete_transaction=true;
};
// Groups must cover the target and recursively encountered contained groups.
// Completeness is a host proof from its authoritative group registry; World
// identity validation alone cannot detect omitted real multitile members.
// All inputs are read-only and must remain stable for this synchronous call.
ObjectTradeDecision ts1_object_trade_permission(const ObjectTradePermissionInput&);
} // namespace odh
