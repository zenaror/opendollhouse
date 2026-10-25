// SPDX-License-Identifier: MPL-2.0
// TS1 VMExpression.Execute semantics adapted from FreeSO 7e76c1960252ddd033d8937d5edb47d326f5b69c.
#include "odh_expression.h"

#include <limits>

namespace {
constexpr int32_t minimum = std::numeric_limits<int32_t>::min();

// Conversions back to signed types are explicit and in range, not dependent on
// implementation-defined unsigned->signed conversion or C++ signed overflow.
int32_t signed_bits(uint32_t bits) {
    if (bits <= UINT32_C(0x7fffffff)) return static_cast<int32_t>(bits);
    return minimum + static_cast<int32_t>(bits - UINT32_C(0x80000000));
}
int16_t signed_short(uint16_t bits) {
    if (bits <= UINT16_C(0x7fff)) return static_cast<int16_t>(bits);
    return static_cast<int16_t>(-32768 + static_cast<int32_t>(bits - UINT16_C(0x8000)));
}
int16_t little_short(const uint8_t* bytes) {
    return signed_short(static_cast<uint16_t>(static_cast<uint16_t>(bytes[0]) |
        (static_cast<uint16_t>(bytes[1]) << 8)));
}
int32_t add(int32_t a, int32_t b) {
    return signed_bits(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}
int32_t sub(int32_t a, int32_t b) {
    return signed_bits(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}
uint32_t square_root(uint32_t value) {
    // Exact floor(sqrt(value)), avoiding floating-point conversion differences
    // and domain errors on target CPUs. Input is a nonnegative int32 value.
    uint32_t result = 0;
    uint32_t bit = UINT32_C(1) << 30;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else result >>= 1;
        bit >>= 2;
    }
    return result;
}

struct Execution {
    void* context;
    odh_expression_read_fn read;
    odh_expression_write_fn write;
    odh_expression_levels_fn levels;
    uint8_t op, lhs_scope, rhs_scope;
    int16_t lhs_index, rhs_index;
    uint32_t* result;

    int32_t read_lhs(int32_t& value) const {
        return read(context, lhs_scope, lhs_index, &value) == 0 ? ODH_EXPRESSION_OK : ODH_EXPRESSION_CALLBACK_FAILED;
    }
    int32_t read_rhs(int32_t& value) const {
        return read(context, rhs_scope, rhs_index, &value) == 0 ? ODH_EXPRESSION_OK : ODH_EXPRESSION_CALLBACK_FAILED;
    }
    int32_t store(int32_t value, bool observe) const {
        uint32_t accepted = 0;
        if (write(context, lhs_scope, lhs_index, value, &accepted) != 0) return ODH_EXPRESSION_CALLBACK_FAILED;
        if (observe && accepted > 1) return ODH_EXPRESSION_CALLBACK_FAILED;
        *result = observe ? accepted : 1;
        return ODH_EXPRESSION_OK;
    }
    int32_t run() const {
        int32_t lhs = 0, rhs = 0;
        int32_t status = 0;
        // Assign and sqrt only evaluate RHS. Inc/Dec have a write between reads.
        if (op == 5 || op == 20) {
            if ((status = read_rhs(rhs)) != ODH_EXPRESSION_OK) return status;
            if (op == 5) return store(rhs, true);
            // .NET 9/10 oracle: negative sqrt cast to short is zero. For positive
            // input, truncate sqrt then preserve low 16 bits as a signed short.
            const auto root = rhs < 0 ? UINT32_C(0) : square_root(static_cast<uint32_t>(rhs));
            return store(signed_short(static_cast<uint16_t>(root)), true);
        }
        if ((status = read_lhs(lhs)) != ODH_EXPRESSION_OK) return status;
        if (op == 11 || op == 17) {
            lhs = op == 11 ? add(lhs, 1) : sub(lhs, 1);
            // Ignore accepted, but propagate an actual callback failure. Do not
            // publish success before the subsequent RHS read has succeeded.
            uint32_t ignored = 0;
            if (write(context, lhs_scope, lhs_index, lhs, &ignored) != 0) return ODH_EXPRESSION_CALLBACK_FAILED;
            if ((status = read_rhs(rhs)) != ODH_EXPRESSION_OK) return status;
            *result = (op == 11 ? lhs < rhs : lhs > rhs) ? 1 : 0;
            return ODH_EXPRESSION_OK;
        }
        if ((status = read_rhs(rhs)) != ODH_EXPRESSION_OK) return status;

        if ((op == 1 || op == 14) && rhs == 1024 && lhs_scope == 3 && lhs_index == 29) {
            if (!levels) return ODH_EXPRESSION_INVALID_ARGUMENT;
            int32_t caller = 0, callee = 0, offset = 0;
            if (levels(context, &caller, &callee, &offset) != 0) return ODH_EXPRESSION_CALLBACK_FAILED;
            const auto delta = sub(caller, sub(callee, offset));
            *result = (op == 1 ? delta <= 0 : delta > 0) ? 1 : 0;
            return ODH_EXPRESSION_OK;
        }

        const auto lhs_bits = static_cast<uint32_t>(lhs);
        const auto rhs_bits = static_cast<uint32_t>(rhs);
        switch (op) {
            case 0: *result = lhs > rhs ? 1 : 0; return ODH_EXPRESSION_OK;
            case 1: *result = lhs < rhs ? 1 : 0; return ODH_EXPRESSION_OK;
            case 2: *result = lhs == rhs ? 1 : 0; return ODH_EXPRESSION_OK;
            case 3: return store(add(lhs, rhs), false);
            case 4: return store(sub(lhs, rhs), false);
            case 6: return store(signed_bits(lhs_bits * rhs_bits), false);
            case 7:
                if (rhs == 0) return store(-1, false);
                if (lhs == minimum && rhs == -1) return ODH_EXPRESSION_OVERFLOW;
                return store(lhs / rhs, false);
            case 8: {
                const auto mask = UINT32_C(1) << ((rhs_bits - 1) & 31);
                // The source compares a SIGNED bitwise result with zero.
                *result = signed_bits(lhs_bits & mask) > 0 ? 1 : 0;
                return ODH_EXPRESSION_OK;
            }
            case 9: {
                const auto mask = UINT32_C(1) << ((rhs_bits - 1) & 31);
                return store(signed_bits(lhs_bits | mask), true);
            }
            case 10: {
                const auto mask = UINT32_C(1) << ((rhs_bits - 1) & 31);
                return store(signed_bits(lhs_bits & ~mask), true);
            }
            case 12:
                if (rhs == 0) return store(lhs, false);
                if (lhs == minimum && rhs == -1) return ODH_EXPRESSION_OVERFLOW;
                // Preserve the source's TWO remainders and wrapping addition.
                // The rhs==-1 overflow pair is excluded before C++ remainder.
                lhs = add(lhs % rhs, rhs);
                if (lhs == minimum && rhs == -1) return ODH_EXPRESSION_OVERFLOW;
                return store(lhs % rhs, false);
            case 13: return store(signed_bits(lhs_bits & rhs_bits), false);
            case 14: *result = lhs >= rhs ? 1 : 0; return ODH_EXPRESSION_OK;
            case 15: *result = lhs <= rhs ? 1 : 0; return ODH_EXPRESSION_OK;
            case 16: *result = lhs != rhs ? 1 : 0; return ODH_EXPRESSION_OK;
            case 18: return store(signed_bits(lhs_bits | rhs_bits), false);
            case 19: return store(signed_bits(lhs_bits ^ rhs_bits), false);
            default: return ODH_EXPRESSION_UNKNOWN_OPERATOR;
        }
    }
};
}

extern "C" uint32_t ODH_SIM_CALL odh_expression_abi_version(void) { return 1; }

extern "C" int32_t ODH_SIM_CALL odh_expression_execute_ts1_v1(
    const uint8_t* operand, uint32_t length, void* context,
    odh_expression_read_fn read, odh_expression_write_fn write,
    odh_expression_levels_fn levels, uint32_t* goto_true) {
    if (goto_true) *goto_true = 0;
    if (!operand || length != 8 || !read || !write || !goto_true) return ODH_EXPRESSION_INVALID_ARGUMENT;
    if (operand[5] > 20) return ODH_EXPRESSION_UNKNOWN_OPERATOR;
    try {
        const Execution execution { context, read, write, levels,
            operand[5], operand[6], operand[7], little_short(operand), little_short(operand + 2), goto_true };
        return execution.run();
    } catch (...) {
        *goto_true = 0;
        return ODH_EXPRESSION_INTERNAL;
    }
}
