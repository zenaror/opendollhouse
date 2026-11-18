// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>

namespace FSO { namespace Common { namespace TS1 {
// The PC generator stores the low word but returns a different, carry-aware
// slice of the full product. It is not the usual 15-bit C-library rand().
inline uint32_t RandomStep(uint64_t& state) {
    const uint64_t product = uint64_t(uint32_t(state)) * UINT64_C(0x41c64e6d) + UINT64_C(12345);
    state = uint32_t(product);
    return uint32_t(product >> 16);
}
}}}
