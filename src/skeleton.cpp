#include "odh/skeleton.hpp"
#include "bcf_sections.hpp"
namespace odh {
SkeletonCollection read_bcf_skeletons(const View& view,SkeletonLimits limits) {
    detail::Cursor c(view,limits.text_bytes);
    SkeletonCollection result{detail::bcf_skeleton_section(c,limits),0};
    if(c.i32()!=0)throw FormatError("BCF appearance section outside skeleton-only profile");
    if(c.i32()!=0)throw FormatError("BCF animation section outside skeleton-only profile");
    result.remaining_bytes=c.remaining();return result;
}
}
