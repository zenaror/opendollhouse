/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "simantics/model/VMPersonDataVariable.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace FSO { namespace SimAntics { namespace Engine { namespace Utils {
// Existing VMWorldActivator restore exclusions: these belong to the local
// execution/placement state, not to the person arriving from another lot.
inline constexpr int TS1LocalPersonFields[] = {
    0, 8, 22, 23, 25, 27, 28, 33, 37, 38, 39, 40, 41, 42, 43, 44, 45,
    59, 62, 64, 71, 74, 77, 78
};
inline bool IsTS1LocalPersonField(int index) {
    for (int field : TS1LocalPersonFields) if (field == index) return true;
    return false;
}

// Reuse the canonical neighbour import, but keep the restarted home-lot state.
// The GUID list must be captured before LoadFamily selects the whole household.
template<class Get, class Set, class Import>
bool RefreshReturningTS1Person(uint32_t guid, const std::vector<uint32_t>& travellers,
                               Get get, Set set, Import import) {
    if (std::find(travellers.begin(), travellers.end(), guid) == travellers.end()) return false;
    std::array<int16_t, sizeof(TS1LocalPersonFields)/sizeof(int)> local{};
    for (size_t i=0; i<local.size(); ++i) local[i]=get(TS1LocalPersonFields[i]);
    // InheritNeighbor resets greeting for a newly arriving visitor. This is
    // only a durable-data refresh of an already initialized home resident.
    constexpr int greet = int(Model::VMPersonDataVariable::GreetStatus);
    const int16_t greeting=get(greet);
    const bool imported=import();
    for (size_t i=0; i<local.size(); ++i) set(TS1LocalPersonFields[i],local[i]);
    set(greet,greeting);
    return imported;
}
}}}}
