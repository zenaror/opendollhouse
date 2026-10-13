#pragma once

#include "odh/dgrp.hpp"
#include "odh/sprite.hpp"

namespace odh {

struct WorldProjection {
    std::uint32_t width = 400, height = 400;
    std::uint8_t zoom = 3, rotation = 0;
    std::array<float, 3> center_tile{0, 0, 0}; // X/Y planar, Z up.
};

// Shared orthographic projection for meshes and sprites. World axes are Y up;
// one tile is three world units, with tile Y mapped to world Z. Screen Y grows
// downward. Depth is negative rotated world Z: smaller values win. It has no
// near/far normalization, so geometry and sprite depth must use this same API.
// Profile: FreeSO 4c6b3e8f5835b228723caea3c9f683c62f244f73, Y rotations
// 315/225/135/45 degrees followed by X 30 degrees; zoom widths 32/64/128.
// Planar pixel coefficients are simplified algebraically to preserve integer
// tile boundaries, without claiming bitwise equivalence to GPU trig matrices.
// Viewports are positive, <=2048 per axis and <=1048576 pixels. Invalid camera,
// nonfinite inputs, or unrepresentable float results throw FormatError.
std::array<float, 3> project_world(const WorldProjection&,
                                 std::array<float, 3> world);
std::array<float, 3> project_tile(const WorldProjection&,
                                std::array<float, 3> tile);

struct SpritePlacement {
    std::int32_t x, y;
    float back_depth, front_depth_delta;
    bool flip;
};

// DGRP offsets are scaled by (1/16,1/16,1/5), rotated around tile Z by
// object_radians, and applied once. SPR2 x/y are crop metadata and are not added.
// The local cadge rectangle is truncated toward zero before subtracting its
// integer cadge origin and adding the projected object position, then the final
// position is truncated toward zero. This preserves negative fractional cases.
// This CPU profile does not reproduce FreeSO's rounded scroll offset or the
// CameraController2D half-pixel correction; original calibration is unverified.
// The rotation anchor is (0,+height/2),(-width/2,0),(0,-height/2),(+width/2,0),
// where width/height are tile pixel dimensions. The sprite's frame height is
// subtracted. Frame dimensions use the same bounds as the viewport; coordinates
// including the exclusive right/bottom edge must fit int32. No pixels are read.
// A Z sample z uses back_depth + ((1-z/255)/0.4)*front_depth_delta, matching
// the reference shader's affine profile in the shared, unnormalized depth space.
SpritePlacement place_sprite(const WorldProjection&, const DgrpSprite&,
                             const Spr2Frame&, std::array<float, 3> object_tile,
                             float object_radians = 0);

} // namespace odh
