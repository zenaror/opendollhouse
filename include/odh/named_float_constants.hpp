// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/formats.hpp"

namespace odh {
struct NamedFloatConstant {std::string name;float value=0;std::string description;};
struct NamedFloatConstants {std::vector<NamedFloatConstant> records;};
struct NamedFloatLimits {
    std::uint64_t bytes=1024*1024;
    std::uint32_t records=4096;
    std::size_t text_bytes=1024*1024;
};
// Selected original FCNSv1: zero,version1,magicSNCF,count; each record is a
// NUL-terminated name padded to even byte extent, float32LE, then description
// with the same padding. Exact names retain spaces/case. No trailing bytes,
// empty/duplicate names, nonzero padding or nonfinite values are accepted.
NamedFloatConstants read_named_float_constants(const View&,NamedFloatLimits={});
}
