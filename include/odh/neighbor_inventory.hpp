// SPDX-License-Identifier: MPL-2.0
// Native adaptation of FreeSO TS1 inventory operations; see header for pinned provenance.
#pragma once
#include "odh/inventory_find.hpp"

namespace odh {
class NeighborContext;
enum class Ts1TravelProtocol : std::uint8_t { legacy_pin=1, original_6e598=2 };

// Ordered items and signed NIDs are preserved, including duplicates, zero counts
// and unknown types. The enclosing session resource identity authenticates NGBH.
struct NeighborInventorySnapshot {
    std::uint32_t version = 1;
    std::map<std::int16_t,std::vector<InventoryItem>> inventory_by_id;
};
struct NeighborInventoryLimits {
    std::size_t neighbors = 4096;
    std::size_t items = 65536;
};
class NeighborInventory;
class PreparedNeighborInventory {
    const NeighborInventory* owner_=nullptr;
    NeighborInventorySnapshot expected_,replacement_;
    std::size_t replacement_count_=0;
    friend class NeighborInventory;
};
class NeighborInventory {
public:
    explicit NeighborInventory(const Ngbh&, NeighborInventoryLimits = {},Ts1TravelProtocol = Ts1TravelProtocol::legacy_pin);
    Ts1TravelProtocol protocol() const {return static_cast<Ts1TravelProtocol>(state_.version);}
    PreparedNeighborInventory prepare_replacement(const NeighborInventorySnapshot&) const;
    bool replacement_current(const PreparedNeighborInventory&) const noexcept;
    bool commit_replacement(PreparedNeighborInventory&&) noexcept;
    NeighborInventorySnapshot snapshot() const;
    // Validate before publishing; a failed restore leaves this authority intact.
    void restore(const NeighborInventorySnapshot&);
    const std::vector<InventoryItem>* items(std::int16_t) const;
    void initialize(std::int16_t);
    std::optional<std::uint16_t> get_data(std::int16_t, std::uint32_t) const;
    void set_data(std::int16_t, std::uint32_t, std::uint16_t);
    std::optional<std::uint16_t> consume_data(std::int16_t, std::uint32_t);
    bool changed() const { return changed_; }
    PrimitiveOutcome execute(std::int16_t neighbor,const Instruction&,const Ts1Temps&,std::uint32_t guid);
    PrimitiveOutcome execute_original(std::int16_t neighbor,const Instruction&,const Ts1Temps&,std::uint32_t guid,
        std::optional<std::uint32_t> companion_guid);
private:
    void validate(const NeighborInventorySnapshot&) const;
    std::vector<InventoryItem>& ensure(std::int16_t);
    void check_append() const;
    NeighborInventorySnapshot state_;
    NeighborInventoryLimits limits_;
    std::size_t item_count_ = 0;
    bool changed_ = false;
};
// FreeSO 7e76c1960252ddd033d8937d5edb47d326f5b69c,
// VMTS1InventoryOperations.cs (MPL-2.0): modes0..6 only. Companion modes
// intentionally retain its apparent defect: search Type2 GUID0/1, insert10/11.
// Unknown modes are explicitly unsupported rather than successful no-ops.
VmPrimitive neighbor_inventory_primitive(NeighborInventory&,const World&,
    std::function<void(const InventoryFind&)> observed = {},const NeighborContext* neighbors=nullptr);
bool same_neighbor_inventory_snapshot(const NeighborInventorySnapshot&,const NeighborInventorySnapshot&) noexcept;
}
