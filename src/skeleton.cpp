#include "odh/skeleton.hpp"
#include "bcf_sections.hpp"
namespace odh {
SkeletonCollection read_bcf_skeletons(const View& view,SkeletonLimits limits) {
    detail::Cursor c(view,limits.text_bytes);
    auto result=detail::bcf_resources(c,limits,MeshLimits{},AnimationLimits{});
    return {std::move(result.skeletons),result.remaining_bytes};
}
}
