#pragma once
#include "odh/mesh.hpp"
#include "odh/pose.hpp"
#include <optional>

namespace odh {
struct SkinnedVertex {
    std::array<float,3> position,normal;
    std::array<float,2> texcoord;
    std::uint32_t primary_bone;
    std::optional<std::uint32_t> blend_bone;
    float blend_weight;
};
struct SkinnedMesh {
    std::string skin_name,texture_name;
    std::vector<SkinnedVertex> vertices;
    std::vector<std::array<std::uint32_t,3>> faces;
    std::vector<std::size_t> clamped_binding_indices;
};
// Pure CPU profile of FreeSO Mesh.Prepare / vsVitaboy: authored bone-local
// positions become (-x,y,z), then blend their two absolute pose transforms.
// Names match with ASCII case folding. Out-of-table nonnegative bone indices
// clamp to the final mesh bone and are reported; ambiguous/missing names fail.
// Normals use only the primary matrix's 3x3, without normalization or inverse
// transpose. A primary zero normal becomes (0,1,0). Blend normals are unused.
// Signed weights must be in [0,32768]; duplicate OtherVertex records apply in
// source order, so the last wins. Bindings must cover every real/blend vertex
// exactly once. Empty ranges ignore their first index. No inverse bind is applied.
// Bounded inputs, invalid ranges/indices, nonfinite values or arithmetic
// overflow throw FormatError without mutating inputs or returning partial data.
SkinnedMesh skin_mesh(const Mesh&,const SkeletonPose&);
}
