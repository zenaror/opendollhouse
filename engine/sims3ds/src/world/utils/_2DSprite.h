/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.LotView.Utils._2DSprite / _2DBatchRenderMode (World/utils/_2DSprite.cs, _2DWorldBatch.cs)
#pragma once
#include <cstdint>
#include "core/math.h"

namespace FSO { namespace LotView { namespace Utils {

enum class _2DBatchRenderMode {
    NO_DEPTH,
    Z_BUFFER,
    RESTORE_DEPTH,
    WALL
};

/// STUB(3ds): stand-in for XNA Texture2D. A view onto already decoded sprite data (no copy):
/// Data = colour texels (SPR#/SPR2 PixelData), Alpha8 = single channel texels (SPR2 ZBufferData or a
/// generated wall/floor z-buffer). The renderer samples them exactly like the point samplers of 2DWorldBatch.fx.
struct Texture2D {
    int32_t Width = 0;
    int32_t Height = 0;
    const st::Color* Data = nullptr;
    const uint8_t* Alpha8 = nullptr;
    // FIX(sims) memory: palettised colour texels (SPR2 frames, see SPR2.h) when Data is null:
    // colour = Palette[Index[i]] with alpha Alpha[i]
    const uint8_t* Index = nullptr;
    const uint8_t* Alpha = nullptr;
    const st::Color* Palette = nullptr;
    struct Coverage { uint16_t left, right; };
    // Only immutable frame textures from this batch opt into row clipping.
    const Coverage* coveredRows = nullptr;
    uint8_t drawsUntilCoverage = 0;
    int8_t partialAlpha = -1; // immutable texture view: unknown/no/yes
    bool HasColor() const { return Data != nullptr || (Index != nullptr && Palette != nullptr); }
};

/// Pixel + z-buffer pair (FSO.Files.Utils.WorldTexture).
struct WorldTexture {
    Texture2D* Pixel = nullptr;
    Texture2D* ZBuffer = nullptr;
};

/// Represents a 2D sprite in the game.
class _2DSprite {
public:
    _2DBatchRenderMode RenderMode = _2DBatchRenderMode::NO_DEPTH;
    Texture2D* Pixel = nullptr;
    Texture2D* Depth = nullptr;
    Texture2D* Mask = nullptr;
    st::Vector3 TilePosition;
    st::Vector3 WorldPosition;
    int16_t ObjectID = 0; //used for mouse hit test render mode
    uint16_t Room = 0xFFFF; //room to use for ambient light
    int32_t VisualSample = -1; // transient TS1 cache key; -1 retains legacy treatment
    int8_t LogicalHalf = -1; // canonical split side, explicitly assigned by architecture
    // Original raster coordinates, independent of flipped texture coordinates.
    // 0: none, 1: floor, 2/3: camera-space wall types 1/2.
    uint8_t SpatialSurface = 0;
    st::Rectangle SpatialFrame;
    int32_t SpatialReferenceTop = 0;
    bool SpatialFrameExact = false;
    int16_t SpatialX = 0, SpatialY = 0, SpatialStory = 0;
    st::Point SpatialFrameScreen;
    int32_t SpatialReferenceScreenY = 0;
    bool PlacementRejected = false; // render-only feedback; tile cursors keep their authored colors

    st::Rectangle SrcRect;
    st::Rectangle DestRect;

    //For internal use, do not set this
    int32_t DrawOrder = 0;
    bool FlipHorizontally = false;
    bool FlipVertically = false;

    st::Rectangle AbsoluteDestRect;
    st::Vector3 AbsoluteWorldPosition;
    st::Vector3 AbsoluteTilePosition;

    void Repurpose() {
        Pixel = nullptr;
        Depth = nullptr;
        Mask = nullptr;
        WorldPosition = st::Vector3();
        TilePosition = st::Vector3();
        ObjectID = 0;
        Room = 0xFFFF;
        VisualSample = -1;
        LogicalHalf = -1;
        SpatialSurface = 0;
        SpatialFrameExact = false;
        SpatialX = SpatialY = SpatialStory = 0;
        PlacementRejected = false;

        //rects are written always by sprite drawer
        FlipHorizontally = false;
        FlipVertically = false;
    }
};

}}}
