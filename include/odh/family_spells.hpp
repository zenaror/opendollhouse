// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/family.hpp"
#include "odh/lot_context.hpp"
#include "odh/world.hpp"

namespace odh {
// Original analyzed profile6e598c22: Generic43/410b89 and Family FAMI9/410561.
// This is not a claim of executable equivalence with the preferred MAXIS image.
struct FamilySpellsSnapshot {
    std::uint32_t version=1;
    std::uint16_t family_chunk_id=0; // FAMI resource identity, never FamilyNumber.
    std::int32_t family_number=0; // Provenance, distinct from the chunk ID.
    bool loaded_presence=false;
    std::uint32_t extension_dword=0;
    std::array<std::int16_t,6> words{};
};
void validate_family_spells_snapshot(const FamilySpellsSnapshot&);
std::vector<std::uint8_t> encode_family_spells_snapshot(const FamilySpellsSnapshot&);
FamilySpellsSnapshot decode_family_spells_snapshot(const std::vector<std::uint8_t>&);
class FamilySpells {
public:
    FamilySpells()=default; // Authenticated known absence of an active family.
    explicit FamilySpells(const FamilyEntry&);
    FamilySpellsSnapshot snapshot() const {return state_;}
    // Transactional; private identity and loaded flag/extension metadata cannot
    // change through a save. Runtime capture changes only the six words.
    void restore(const FamilySpellsSnapshot&);
private:
    FamilySpellsSnapshot state_;
};
using FamilySpellsGlobals=std::function<std::optional<std::int16_t>(std::int16_t)>;
struct FamilySpellsOutcome {
    bool supported=false,changed=false;
    std::string diagnostic;
};
// Explicit rows only: residential0 -> original1; community1 -> original2;
// absent key -> original0; key0 -> original1. No LotContext.zone fallback.
// Global provider must be read-only. Multiple matching controllers are rejected
// because World ID order does not prove the original lookup's source ordering.
// Existing World GUID enumeration excludes UI ghosts, includes indexed dead
// bodies until removal, and omits already removed retained bodies. No additional
// Dead filter is invented by this adapter.
FamilySpellsOutcome apply_family_spells(World&,FamilySpells*,const LotZoning*,
    const FamilySpellsGlobals&);
inline FamilySpellsOutcome apply_family_spells(World& world,FamilySpells& state,
    const LotZoning* zoning,const FamilySpellsGlobals& globals) {
    return apply_family_spells(world,&state,zoning,globals);
}
// The primitive additionally requires the actual active VM query/dispatch.
VmPrimitive family_spells_primitive(World&,FamilySpells*,const LotZoning*,FamilySpellsGlobals);
// Stage max(controller Attributes[0..5], family words) before save, including
// legacy families without loaded_presence; Global10==0 is a known no-op.
FamilySpellsOutcome capture_family_spells(const World&,FamilySpells&,const LotZoning*,
    const FamilySpellsGlobals&);
} // namespace odh
