/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <cstdint>

namespace simapp {
enum class CatalogType { Residential, Downtown, Community, Vacation, StudioTown, MagicTown };

// Zoning is installation metadata, not the UI language or the current map.
// The VM's studio lot type remains authoritative even for an owned studio lot.
inline CatalogType CatalogTypeForLot(int house, int zoning, int vmLotType = 0) {
    if (vmLotType == 2) return CatalogType::StudioTown;
    if (zoning == 0) return CatalogType::Residential;
    if (house >= 21 && house <= 30) return CatalogType::Downtown;
    if (house >= 40 && house <= 49) return CatalogType::Vacation;
    if (house >= 81 && house <= 89) return CatalogType::StudioTown;
    if (house >= 90 && house <= 99) return CatalogType::MagicTown;
    return CatalogType::Community;
}

struct CatalogSortFields {
    std::uint16_t rooms, residential, community, downtown, vacation, studio, magic;
};
inline std::uint16_t CatalogSubsort(CatalogType type, const CatalogSortFields& f) {
    // Both reference providers carry separate authored sorts. An object sold
    // Downtown need not be sold in Old Town, on holiday, or in a home.
    switch (type) {
        case CatalogType::Downtown: return f.downtown;
        case CatalogType::Community: return f.community;
        case CatalogType::Vacation: return f.vacation;
        case CatalogType::StudioTown: return f.studio;
        case CatalogType::MagicTown: return f.magic;
        default: return f.rooms ? f.residential : 0;
    }
}
}
