#pragma once
#include "odh/skeleton.hpp"
#include "odh/avatar_head_seek.hpp"

namespace odh {
struct PoseBone {
    std::string name;
    std::int32_t parent_index;
    std::array<float,3> translation;
    std::array<float,4> rotation;
    // Row-major 3x4 affine matrix for COLUMN vectors: parentGlobal*local,
    // local=translation*rotation. Position occupies elements 3,7,11.
    std::array<float,12> absolute_matrix;
    std::array<float,3> absolute_position;
};
struct SkeletonPose {
    std::string skeleton,animation;
    double requested_frame;
    std::uint32_t frame;
    float fraction;
    bool completed;
    std::vector<PoseBone> bones; // Original skeleton order, including unsorted trees.
    std::vector<std::string> missing_motion_bones; // Motion order, duplicates retained.
};
// Pure, bounded single-clip sampling at weight1. Both resource pointers null
// samples rest transforms. Translations become (-x,y,z); rotations (x,-y,-z,-w).
// Quaternions are never implicitly normalized; Can flags do not filter motions.
// Continuous negative frames clamp to0/fraction0 and complete; nonnegative
// frames split into floor/fraction before clamping to the clip frame count.
// Invalid hierarchy, resource pairing, ranges, nonfinite values or overflow
// throws FormatError before returning a pose. Inputs are never mutated.
// Optional carry channels override only their active local transforms at frame0,
// weight1 after the ordinary clip, before rebuilding global matrices.
SkeletonPose sample_pose(const Skeleton&,const AnimationHeader* =nullptr,
                         const CfpPlanes* =nullptr,double frame=0,
                         const AnimationHeader* carry_animation=nullptr,
                         const CfpPlanes* carry_cfp=nullptr,double carry_frame=0);
struct PoseClipSample {
    const AnimationHeader* animation=nullptr;
    const CfpPlanes* cfp=nullptr;
    double frame=0;
    float weight=0;
};
// Source VMAvatar.FractionalAnim's ordered collection: each active channel is
// blended with weight / cumulative-prefix-weight, using translation Lerp and
// quaternion Slerp. Missing channels preserve preceding transforms. Zero-weight
// clips contribute no transforms (avoiding source 0/0 on a leading idle clip).
// At most three clips, matching the current native locomotion collection.
// Carry overrides its channels at its explicit static frame after the entire collection.
SkeletonPose sample_pose_collection(const Skeleton&,const std::vector<PoseClipSample>&,
                         const AnimationHeader* carry_animation=nullptr,
                         const CfpPlanes* carry_cfp=nullptr,double carry_frame=0);
// Value-only source neck/target calculation and post-carry HEAD overlay.
HeadSeekQuaternion calculate_head_seek(const std::array<float,12>& neck,const std::array<float,3>& target,float radians);
SkeletonPose apply_head_seek(const SkeletonPose&,const AvatarHeadSeekSnapshot&);

}
