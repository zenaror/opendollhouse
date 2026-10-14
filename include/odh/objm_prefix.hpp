#pragma once

#include "odh/objm.hpp"

namespace odh {

struct ObjmPrefixLimits {
    std::uint64_t block_bytes = 8U * 1024U * 1024U;
    std::uint32_t attributes = 4096;
};
struct ObjmFootprint {
    // Serialized order is MinY, MinX, MaxY, MaxX; raw integer coordinates.
    std::int32_t min_y, min_x, max_y, max_x;
};
struct ObjmPrefix {
    ObjmFootprint footprint;
    std::int32_t x, y, level;
    std::int16_t unknown_data;
    std::vector<std::int16_t> attributes;
    std::array<std::int16_t, 8> temps;
    std::array<std::int16_t, 68> object_data;
    std::array<std::int16_t, 5> extras;
    std::uint32_t block_offset; // Same byte12-relative offset as ObjmBlock.
    bool odd_offset; // Initial offset parity for later string decoding.
    std::uint64_t consumed_bits, remaining_bits; // Exact, without byte rounding.
};

// Direct MSB-first signed fields through the fifth ObjectDataExtra word.
// This stable prefix precedes OBJD resolution/version-dependent instance data.
// Retains raw Level, including values outside the reference's runtime 1/2 gate;
// coordinate/level validity belongs to subsequent semantic interpretation.
// Negative/excessive counts and truncation fail before attribute allocation.
// Unlike the reference's EOF zero-fill, every bit must exist in this block.
// Result owns its fields; block order does not establish ObjectID. Raw signed
// ObjectData[11] must be resolved separately through the container's ID map.
ObjmPrefix read_objm_prefix(const ObjmBlock&, const ObjmPrefixLimits& = {});

} // namespace odh
