#pragma once
#include "odh/animation.hpp"
namespace odh {
struct SkeletonLimits {
    std::uint32_t skeletons=64,bones=4096,properties=65536,skipped_records=4096;
    std::size_t text_bytes=4194304;
};
struct SkeletonBone {
    std::string name,parent;
    std::vector<AnimationProperties> properties;
    std::array<std::uint32_t,3> translation_bits;
    std::array<std::uint32_t,4> rotation_bits;
    std::int32_t can_translate,can_rotate,can_blend;
    std::uint32_t wiggle_value_bits,wiggle_power_bits;
};
struct Skeleton {std::string name;std::vector<SkeletonBone> bones;};
struct SkeletonCollection {std::vector<Skeleton> skeletons;std::uint64_t remaining_bytes;};
// Skeleton-only binary BCF profile (zero appearance and animation counts).
// Empty-name bone records contain only the name and parent Pascal strings and
// are skipped without advancing the declared decoded bone count. Limits are
// cumulative across the collection; properties charges both groups and pairs.
// Float32 bits and flags are raw, without pose validation.
SkeletonCollection read_bcf_skeletons(const View&,SkeletonLimits limits={});
}
