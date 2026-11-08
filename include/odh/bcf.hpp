#pragma once
#include "odh/skeleton.hpp"
#include "odh/mesh.hpp"
namespace odh {
// All binary BCF sections, decoded once with the same native section parsers.
// Each section retains its cumulative structural limits. One shared text budget
// is the minimum of their configured text limits. CFP samples remain external.
struct BcfResources {
    std::vector<Skeleton> skeletons;
    std::vector<Appearance> appearances;
    std::vector<AnimationHeader> animations;
    std::uint64_t remaining_bytes=0;
};
BcfResources read_bcf_resources(const View&,SkeletonLimits={},MeshLimits={},AnimationLimits={});
}
