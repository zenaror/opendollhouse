// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/projection.hpp"
#include "odh/raster.hpp"
#include "odh/skinning.hpp"

namespace odh_host {
std::array<float,3> selected_sim_crystal_color(std::int16_t mood);
// Original directional diffuse plus packed ambient. Direction points toward the
// light; its exact original camera transform is not yet recovered.
std::array<float,3> selected_sim_crystal_light(std::int16_t mood,
    std::array<float,3> normal,std::array<float,3> toward_light);
// Depth-compose a decorative crystal while retaining every scene picking token.
odh::RasterFrame overlay_selected_sim_crystal(const odh::RasterFrame& base,
    const odh::RasterFrame& layer,odh::RasterLimits={});
struct SelectedSimCrystalLimits {
    std::uint64_t archive_bytes=512*1024*1024,asset_bytes=1024*1024;
    std::uint32_t archive_entries=20000,vertices=4096,triangles=4096,texture_pixels=65536;
};
class SelectedSimCrystal {
public:
    SelectedSimCrystal(const std::string& animations_far,const std::string& textures_far,
                       SelectedSimCrystalLimits={});
    // Head/offset use graphics tiles (X/Y ground, Z up). Scale multiplies the
    // authored world-unit mesh. Fixed local X, Y, Z basis rotations are applied
    // in that order, then rotation is a direct rotation about world Y.
    // The caller supplies the original game's placement/scale/time rules.
    // No resource I/O or VM access occurs here. Use overlay_selected_sim_crystal
    // to preserve scene picking while composing this unowned depth layer.
    // Uses classic per-vertex diffuse/ambient lighting. The camera-facing light
    // direction and white material response remain approximations.
    odh::RasterFrame render(const odh::WorldProjection&,std::array<float,3> head_tile,
        float rotation_radians,std::int16_t mood,float scale,
        std::array<float,3> offset_tile,std::array<float,3> basis_radians={0,0,0},
        odh::RasterLimits={}) const;
    // Integrated HUD keeps only the projected coverage. The origin is in the
    // supplied viewport and pixels outside the returned frame are clear.
    odh::RasterFrame render(const odh::WorldProjection&,std::array<float,3> head_tile,
        float rotation_radians,std::int16_t mood,float scale,std::array<float,3> offset_tile,
        std::array<float,3> basis_radians,odh::RasterLimits,std::array<std::uint32_t,2>* origin) const;
    std::array<std::array<float,3>,2> asset_bounds() const;
private:
    odh::SkinnedMesh mesh_;
    std::vector<odh::BitmapImage> textures_;
};
}
