// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/lot_context.hpp"
namespace odh {
// Original0042ba25 result domain, separate from native label-derived0/1 rows.
enum class OriginalLotClass : std::int16_t {unclassified=0,residential=1,community=2};
inline std::optional<OriginalLotClass> classify_original_lot(const LotZoning* rows,std::int16_t house) noexcept {
    // Original key0 returns1 without consulting the selected row bank.
    if(!house)return OriginalLotClass::residential;
    if(!rows)return std::nullopt;
    const auto found=rows->find(house);
    if(found==rows->end())return OriginalLotClass::unclassified;
    if(found->second==0)return OriginalLotClass::residential;
    if(found->second==1)return OriginalLotClass::community;
    return std::nullopt; // Unsupported native label domain is not a source class.
}
} // namespace odh
