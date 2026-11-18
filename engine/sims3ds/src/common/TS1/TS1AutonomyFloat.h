// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <limits>

namespace FSO { namespace Common { namespace TS1 { namespace AutonomyFloat {
// The observed DVD chooser runs with x87 control word0x007f: nearest rounding
// and a24-bit significand. Use binary32 steps, without changing the host FPU.
// The bounded retail scoring values do not rely on x87's wider exponent range.
static_assert(std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<float>::radix == 2 &&
              std::numeric_limits<float>::digits == 24,
              "TS1 autonomy requires IEEE754 binary32");
inline float Round(float value) {
#if FLT_EVAL_METHOD == 0
    return value;
#else
    // Hosts evaluating float expressions in wider registers need an explicit
    // store. Other hosts keep ordinary float operations without extra stores.
    volatile float stored = value;
    return stored;
#endif
}
inline uint32_t Bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
}}}}
