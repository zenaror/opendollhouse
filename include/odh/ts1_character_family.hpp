// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/family.hpp"
#include "odh/native_personal_state.hpp"
#include <functional>
namespace odh {
class NeighborContext;
// Owned gameplay additions, independent of installed NBRS/FAMI bytes. Identity
// always names the exact newly published private character; no World ID is
// allocated here. Mutable personal/relationship state belongs to its usual owner.
struct Ts1CreatedCharacter {
    NativePersonalIdentity identity;
    NeighborRecord seed;
};
struct Ts1FamilyRoster {
    std::uint16_t family_chunk_id=0;
    std::array<std::uint8_t,32> source_identity{};
    std::uint64_t revision=0;
    std::vector<std::uint32_t> members;
};
struct Ts1CharacterFamilySnapshot {
    std::uint32_t version=1;
    std::array<std::uint8_t,32> neighborhood_identity{};
    std::uint64_t revision=0;
    std::vector<Ts1CreatedCharacter> characters; // creation order; exact NID/GUID
    std::vector<Ts1FamilyRoster> rosters; // ascending family chunk ID
};
void validate_ts1_created_character(const Ts1CreatedCharacter&);
void validate_ts1_character_family_snapshot(const Ts1CharacterFamilySnapshot&);
void validate_ts1_character_family_successor(const Ts1CharacterFamilySnapshot&,const Ts1CharacterFamilySnapshot&);
std::vector<std::uint8_t> encode_ts1_character_family_snapshot(const Ts1CharacterFamilySnapshot&);
Ts1CharacterFamilySnapshot decode_ts1_character_family_snapshot(const std::vector<std::uint8_t>&);
// Helpers preserve original provider identity/order and raw seed words. Callers
// must authenticate private character bytes before adopting these owned records.
Nbrs apply_ts1_character_neighbors(const Nbrs&,const Ts1CharacterFamilySnapshot&);
std::vector<FamilyEntry> apply_ts1_family_rosters(const std::vector<FamilyEntry>&,
    const Ts1CharacterFamilySnapshot&);
const Ts1FamilyRoster* find_ts1_family_roster(const Ts1CharacterFamilySnapshot&,std::uint16_t);
// Exact Simitone AddNeighbor profile shared by CAS and Make19. The input is the
// authored88-word template profile, not arbitrary serialized character data.
NeighborRecord make_simitone_character_neighbor(std::int16_t,std::uint32_t,const std::vector<std::int16_t>&);
// Original Make19 uses the existing original-manager schema0 constructor:
// known zero family backing, absent serialized person bank. name is the actual
// private resource basename, not a fabricated personality/88-word bank.
NeighborRecord make_original_character_neighbor(std::int16_t,std::uint32_t,const std::string& resource_name);
// Allocation sees the complete effective NBRS, including catalog-derived NPCs,
// and reserved historical IDs. Never allocate from only instantiated avatars.
std::int16_t allocate_ts1_neighbor_id(const Nbrs&,const std::vector<std::int16_t>& reserved={});
// Original004257ef picks the LAST empty positive manager slot, then appends.
// Extent/present IDs are real neighbor-manager data, not instantiated World IDs.
std::int16_t allocate_original_neighbor_id(const std::vector<std::int16_t>& present,
    std::size_t manager_extent,const std::set<std::int16_t>& reserved={});
// Exact historical0042a2fe algorithm. The host supplies the already proved
// OriginalLotClass==1 result and actual CurrentFamily GUID vector, not a role
// override or a PersonData61 approximation of membership.
void refresh_ts1_family_person_types(World&,const std::vector<std::uint32_t>& current_family_guids,bool source_context_class_one);
// Native selected-cohort projection is unique even when source FAMI is
// reordered by same-family remove/reappend. Existing selection order survives.
void admit_ts1_runtime_subset_member(std::vector<std::uint32_t>&,std::uint32_t);
// Generic32 SelectWholeFamily replaces only the selected cohort. It does not
// remove any existing body/Thread or rewrite its HHRD/NPCA constructor origin.
void select_ts1_runtime_whole_family(std::vector<std::uint32_t>&,const std::vector<std::uint32_t>&);
}

namespace odh {
// These are data-owner/consumer callbacks, not execution authority. The primitive
// adapter authenticates its actual instruction before invoking this service.
// A failed callback leaves preceding source mutations published in this owner.
struct Ts1FamilyConsumers {
    std::function<std::optional<std::int16_t>(std::int16_t)> neighbor_family;
    std::function<bool(std::int16_t,std::int16_t,bool,std::string&)> set_neighbor_family;
    std::function<bool(std::int16_t,std::int16_t,bool,std::string&)> set_runtime_family;
    std::function<bool(const FamilyEntry&,bool,std::string&)> publish_family;
    std::function<bool(std::uint16_t,std::uint32_t,std::string&)> append_runtime_subset;
    std::function<bool(std::string&)> verify_family;
    // Historical0042a2fe, after GUID-vector publication and before Add's PD61.
    std::function<bool(std::string&)> refresh_person_types;
    // Existing host observes the changed FAMI data and invalidates its actual
    // family display/checkpoint projection after source flags/dirty/event.
    std::function<bool(std::uint16_t,std::string&)> family_changed;
    // Existing live FAMI owns cash/property; membership receipts do not copy
    // an older budget over unrelated legitimate earnings/spending.
    std::function<std::optional<std::pair<std::int32_t,std::int32_t>>(std::uint16_t)> family_assets;
};
class Ts1CharacterFamilyState {
public:
    // effective includes empty FAMI instances; source removal changes the GUID
    // vector without deleting its FAMI/provider identity. identities come from the existing
    // overlay replay's immutable family leases, never current mutable budgets.
    Ts1CharacterFamilyState(Ts1CharacterFamilySnapshot,
        const std::vector<FamilyEntry>& effective,
        std::map<std::uint16_t,std::array<std::uint8_t,32>> identities);
    const Ts1CharacterFamilySnapshot& snapshot() const noexcept{return state_;}
    const FamilyEntry* family(std::uint16_t) const;
    const FamilyEntry* retained_family(std::uint16_t) const;
    void add_created(const Ts1CreatedCharacter&);
    bool add_to_family(std::optional<std::uint16_t> target,const NeighborRecord&,
        const Ts1FamilyConsumers&,bool runtime_subset);
    bool remove_from_family(const NeighborRecord&,const Ts1FamilyConsumers&);
    bool combine_assets(std::optional<std::uint16_t>,std::uint16_t,const Ts1FamilyConsumers&);
    bool verify_current(std::optional<std::uint16_t>,const Ts1FamilyConsumers&);
private:
    void changed(FamilyEntry&,const Ts1FamilyConsumers&);
    Ts1CharacterFamilySnapshot state_;
    std::map<std::uint16_t,FamilyEntry> families_;
    std::map<std::uint16_t,std::array<std::uint8_t,32>> identities_;
};
struct Ts1FamilyPrimitiveServices {
    Ts1CharacterFamilyState* state=nullptr;
    const NeighborContext* neighbors=nullptr;
    std::function<std::optional<std::uint16_t>()> current_family;
    Ts1FamilyConsumers consumers;
};
// Generic4/5/6/32/33 only. A query root/check preview has no lease and cannot
// mutate neighbors/families. The real entity Thread remains independent from
// a linked disposable source_check executor.
VmPrimitive ts1_family_primitive(Ts1FamilyPrimitiveServices);
}
