// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/neighborhood.hpp"
#include <array>
#include <set>
namespace odh {
struct NativePersonalCatalogDefinition {
    std::uint32_t provider_ordinal=0,guid=0;
    std::uint16_t object_type=0,definition_chunk_id=0;
    std::string source,asset;
    std::array<std::uint8_t,32> fingerprint{};
};
struct NativePersonalCatalogBinding {std::uint32_t guid=0;std::int16_t neighbor_id=0;};
struct NativePersonalCatalog {
    std::uint32_t version=1;
    std::array<std::uint8_t,32> neighborhood_identity{};
    std::vector<NativePersonalCatalogDefinition> definitions; //Actual native provider order; never GUID sort.
    std::vector<NativePersonalCatalogBinding> bindings; //One per definition; explicit persisted allocation.
};
bool native_personal_catalog_type(std::uint16_t) noexcept;
std::string native_personal_catalog_name(const std::string& source,const std::string& asset);
void validate_native_personal_catalog(const NativePersonalCatalog&);
std::vector<std::uint8_t> encode_native_personal_catalog(const NativePersonalCatalog&);
NativePersonalCatalog decode_native_personal_catalog(const std::vector<std::uint8_t>&);
NativePersonalCatalog build_native_personal_catalog(const Nbrs&,const std::array<std::uint8_t,32>&,
    const std::vector<NativePersonalCatalogDefinition>&,const std::set<std::int16_t>& reserved={});
// Replays ONLY the persisted owned order, verifies all exact binding receipts.
// Current filesystem/resource authentication remains the host's prerequisite.
Nbrs hydrate_native_personal_neighbors(const Nbrs&,const NativePersonalCatalog&,
    const std::set<std::int16_t>& reserved={});
// Merge explicit owned identity receipts into original manager slots without
// reallocating a persisted synthetic NPC binding. Caller authenticates receipt
// origins/resources. Conflicting NID/GUID/seed bytes are never redirected.
Nbrs merge_owned_neighbor_seeds(const Nbrs&,const std::vector<NeighborRecord>&);
Nbrs hydrate_owned_native_personal_neighbors(const Nbrs&,const NativePersonalCatalog&,
    const std::vector<NeighborRecord>&,const std::set<std::int16_t>& reserved={});
std::optional<std::int16_t> native_personal_catalog_lookup(const NativePersonalCatalog&,std::uint32_t);
} //namespace odh
