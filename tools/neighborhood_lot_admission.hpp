// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
namespace odh_host {
enum class NeighborhoodLotUse { residential,community,studio };
// Caller authenticates the zoning table and verifies the house is on its map.
// Selected UIHouseSelectPanel181-199 defaults absent rows to community/studio.
inline NeighborhoodLotUse neighborhood_lot_use(std::uint16_t house,
        std::optional<std::int16_t> zoning) {
    if(house<1||house>98)throw std::invalid_argument("lot outside admitted neighborhood range");
    if(zoning){
        if(*zoning==0)return NeighborhoodLotUse::residential;
        if(*zoning==1)return NeighborhoodLotUse::community;
        throw std::invalid_argument("unsupported explicit lot zoning");
    }
    return house>=81&&house<=89?NeighborhoodLotUse::studio:NeighborhoodLotUse::community;
}
}
