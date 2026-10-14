#pragma once
#include "odh/definition_index.hpp"
#include "odh/projection.hpp"
#include "odh/wall_appearance.hpp"
#include "odh/wall_sprite.hpp"
#include "odh/wall_junction.hpp"
#include <memory>

struct SavedWallFace {
    std::uint32_t x,y,owner_x,owner_y;
    std::uint8_t level,orientation,owner_segment;
    std::uint16_t pattern;
    odh::WallAppearanceStyle style;
    std::int16_t object_owner=0;
    bool door=false;
};
// Pure standing/up RotateWall profile: visible TL/TR; reverse BR/BL use
// neighboring forward-face style/appearance owners. Diagonals select rotated
// BottomRightPattern. Rotated diagonals intentionally do not carry ObjSetTR,
// as in pinned FreeSO RotateWall. Cardinal working styles follow DecodeWalls:
// absent side0; present valid fence2/12/13/14 retained, others become1. Raw saved
// grids remain unchanged. Working style !=1 precedes object overrides.
// Strict grids <=64x64x2, known/exclusive segment bits, unique appearance faces.
std::vector<SavedWallFace> saved_wall_faces(const odh::SavedArchitecture&,
    const odh::WallAppearance&,std::uint8_t rotation);

struct SavedWallDraw {
    SavedWallFace face;
    std::string pattern_source,mask_source,mask_asset;
    std::uint16_t pattern_sprite,pattern_palette,mask_sprite=0,mask_palette=0;
    bool mask_fallback=false; // Missing selected SPR# mask: pattern alpha.
    bool pattern_palette_missing=false,mask_palette_missing=false;
    std::int32_t x,y;
    std::uint32_t width,height;
    float back_depth,front_depth_delta;
};
struct SavedWallJunctionDraw {
    odh::WallJunction junction;
    std::string source;
    std::uint16_t sprite_id,palette_id;
    bool palette_missing=false;
    std::int32_t x,y;
    std::uint32_t width,height;
    float back_depth,front_depth_delta;
};
struct SavedWallRenderResult {
    odh::CompositeFrame composite;
    std::vector<SavedWallDraw> draws;
    std::vector<SavedWallJunctionDraw> junctions;
    std::uint32_t unmapped_junctions=0;
    std::size_t cached_assets=0,cached_frames=0,mask_fallbacks=0;
    std::uint64_t cached_pixels=0,cached_frame_bytes=0;
};
// Explicit walls.iff + immutable DefinitionIndex, which must outlive renderer.
// Global pattern0..255 only; missing patterns/frames or malformed masks fail.
// Missing PALT follows FreeSO's explicit opaque black256 DEFAULT_PALT policy,
// reported separately for pattern and mask. Empty source frames fail strictly;
// FreeSO's empty-texture normalization is outside this adapter profile.
// A missing selected mask chunk alone follows FreeSO's documented
// pattern-alpha fallback, reported per draw. No SPR2 substitution/conversion.
// Global up mask =2*style+(zoom-1)*512; object up masks = sprites[zoom-1].
// Resource budgets:16 sources,256 assets,65536 chunks,4096 frames,1Mi decoded
// pixels (RGBA+indices <=5MiB),256KiB provenance. Draw/compositor budgets use
// WallSpriteLimits defaults; every layer, including offscreen, is resolved.
// Placement uses shared ODH projection, source destination rectangles minus
// cadge origin plus rotation anchor; depth uses shared unnormalized coordinates.
// This preserves ODH's unverified scroll-round/half-pixel calibration boundary.
// Standing/up caps use core FreeSO linear-endpoint-spill profile, square grids
// <=64x64x2. SPR#4096/4097/4098 is sampled by rotated JunctionMap frame, using
// its own alpha; no object mask or wall paint. Caps are submitted before WALL
// layers, matching the reference's grouped Z_BUFFER-before-WALL ordering.
// Their source position is(x-.5,y-.5,level*2.95+altitude_at_integer_x_y).
// Junction and wall layers share all compositor budgets and decoded-frame cache.
// Standing/up only: no cutaway, thick-wall extensions, rooms/lighting,
// resource remapping or claim of original/GPU equivalence. Terrain is optional.
// Failure preserves inputs but may populate bounded resource cache.
class SavedWallRenderer {
public:
    SavedWallRenderer(const std::string& walls_iff,const odh::DefinitionIndex&);
    ~SavedWallRenderer();
    SavedWallRenderer(SavedWallRenderer&&) noexcept;
    SavedWallRenderer& operator=(SavedWallRenderer&&) noexcept;
    SavedWallRenderResult render(const odh::SavedArchitecture&,const odh::WallAppearance&,
        const odh::WorldProjection&,const odh::RasterFrame&,
        bool apply_terrain=true,std::int32_t base_alt=0);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
