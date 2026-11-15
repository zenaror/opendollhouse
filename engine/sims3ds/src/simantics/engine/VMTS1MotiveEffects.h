/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "files/formats/iff/chunks/NGBH.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace FSO { namespace SimAntics { namespace Engine { namespace Utils {
// Type-2 slots 0..6 are consumable motive effects. Other slots carry the
// travel clock/party and may remain after the arrival controller runs again.
// Keep the first matching effect, as GetSimData does, and consume its copies.
template<class Apply>
void ApplyTS1MotiveEffects(std::vector<FSO::Files::Formats::IFF::Chunks::InventoryItem>& inventory,
                          Apply apply) {
    for (uint32_t slot=0; slot<7; ++slot) {
        auto found=std::find_if(inventory.begin(),inventory.end(),[slot](const auto& item) {
            return item.Type==2 && item.GUID==slot;
        });
        if(found!=inventory.end()) apply(slot,int16_t(found->Count));
    }
    inventory.erase(std::remove_if(inventory.begin(),inventory.end(),[](const auto& item) {
        return item.Type==2 && item.GUID<7;
    }),inventory.end());
}
}}}}
