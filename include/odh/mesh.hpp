#pragma once
#include "odh/formats.hpp"

namespace odh {
struct MeshLimits {
    std::uint32_t bones=4096,faces=131072,bindings=4096,vertices=65536,
                  blends=65536,appearances=4096,appearance_bindings=4096;
    std::size_t text_bytes=4194304;
};
struct MeshBoneBinding {
    std::int32_t bone_index,first_real,real_count,first_blend,blend_count;
};
struct MeshBlend {std::int32_t weight,other_vertex;};
struct MeshVertex {
    std::array<std::uint32_t,3> position_bits,normal_bits;
    std::array<std::uint32_t,2> texcoord_bits;
};
struct Mesh {
    std::string skin_name,texture_name;
    std::vector<std::string> bones;
    std::vector<std::array<std::int32_t,3>> faces;
    std::vector<MeshBoneBinding> bindings;
    std::vector<MeshVertex> real_vertices;
    std::vector<MeshBlend> blend_data;
    std::vector<MeshVertex> blend_vertices;
    std::int32_t vertex_count2;
    std::uint64_t remaining_bytes;
};
struct AppearanceBinding {
    std::string bone,mesh_name;
    std::int32_t censor_flags,zero;
};
struct Appearance {
    std::string name;
    std::int32_t type,zero;
    std::vector<AppearanceBinding> bindings;
};
struct AppearanceCollection {
    std::vector<Appearance> appearances;
    std::uint64_t remaining_bytes;
};
// Binary BMF structure only. Counts are bounded; signed indices, ranges,
// weights, vertex_count2 and float bits remain raw for later skin validation.
// Blend vertices have no stored texture coordinates (their bits are zero).
Mesh read_bmf(const View&,MeshLimits limits={});
// Appearance-only binary BCF: skeleton and animation counts must be zero.
// Text and binding limits are cumulative across the collection. No text SKN
// or mixed-section BCF inference is attempted.
AppearanceCollection read_bcf_appearances(const View&,MeshLimits limits={});
// Text CMX profile used for TS1 clothing appearances with no skeleton or
// animation payload. Nonzero sections are rejected rather than guessed.
AppearanceCollection read_cmx_appearances(const View&,MeshLimits limits={});
}
