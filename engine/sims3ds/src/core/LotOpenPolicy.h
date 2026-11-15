/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
namespace simapp {
// Opening intent and authored neighbourhood metadata, never a transient avatar
// list: a travelling household can arrive after the first presented frame.
constexpr bool LotOpeningAllowsLive(bool travel,bool community,bool resident_family) {
    return travel || (!community && resident_family);
}
}
