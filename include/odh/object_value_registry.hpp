// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/session_bundle.hpp"
namespace odh {
// Shared strict v2 validation; source/resource authentication belongs to the
// admitting importer/constructor. Never derives prices from ObjectData41.
void validate_object_value_groups(const SessionObjectTradeLedger&,const WorldSnapshot&);
std::optional<ObjectTradeIdentity> object_value_source_base(const std::vector<SessionObjectValueMember>&);
class ObjectValueRegistry {
public:
    ObjectValueRegistry(World&,SessionObjectTradeLedger&);
    ~ObjectValueRegistry();
    ObjectValueRegistry(const ObjectValueRegistry&)=delete;
    ObjectValueRegistry& operator=(const ObjectValueRegistry&)=delete;
    bool bind(); // Same World/ledger owner must outlive the binding.
    bool migrate_legacy_singletons(); // Explicit representation upgrade only.
    bool admit(SessionObjectValueGroup); // group_id0 allocates a fresh token.
    // Borrowed result lasts until any ledger mutation; stale groups are unknown.
    const SessionObjectValueGroup* query(ObjectTradeIdentity)const;
    ObjectTradeValue refund(ObjectTradeIdentity)const;
    bool retire_member(ObjectTradeIdentity); // Preserve remaining source order/offsets; recompute BaseObject.
    // Source dynamic SetPosition separation: retain old group's order/value,
    // copy its InitialPrice/provenance/MultiTile to a fresh singleton whose raw
    // offsets/lead come from the individual definition. Atomic on failure.
    bool split_member(ObjectTradeIdentity,std::int16_t offset_x,std::int16_t offset_y,
        std::int16_t offset_level,std::optional<std::uint16_t> lead_object);
    // Append the donor cohort in its existing order, using exact supplied
    // transformed offsets only. Member resource/Wear/Disabled remain donor's;
    // the receiver keeps its InitialPrice/provenance/MultiTile. Same group is
    // a validated no-op. No association is inferred from positions.
    bool combine_groups(ObjectTradeIdentity receiver,ObjectTradeIdentity donor,
        const std::vector<ObjectGroupMember>& donor_members);
    bool set_wear(ObjectTradeIdentity,bool known,std::optional<std::uint16_t>);
    // VMNetBuyObjectCmd success-only final InitialPrice assignment, after
    // Init/placement/EP11. Caller supplies exact authenticated catalog-resource
    // provenance. Does not write raw CurrentValue41 or replace per-member state.
    bool set_purchase_price(ObjectTradeIdentity,std::int32_t,SessionObjectPriceOrigin);
    SessionObjectTradeLedger snapshot()const{return ledger_;}
private:
    World& world_;SessionObjectTradeLedger& ledger_;bool bound_=false;
    bool current_value_write(WorldEntityReference,std::int16_t,std::int16_t,const std::function<bool()>&);
};
}
