#pragma once
#include "odh/hit_resources.hpp"

namespace odh {
// Portable representation of a native HIT byte pointer. The immutable file
// and its bytes must outlive all controllers, catalogs and return frames.
struct HitCodeTarget {
    const HitFile* file = nullptr;
    std::uint32_t pc = 0;
};
}
