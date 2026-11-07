#pragma once
#include "odh/definition_index.hpp"
#include "odh/projection.hpp"
#include "odh/compositor.hpp"
#include <memory>

// Caller validates visibility, logical identity and graphics/containment position.
struct SavedObjectVisual {
    std::int16_t id;
    std::uint32_t guid;
    std::int16_t graphic;
    std::uint8_t direction;
    std::array<float,3> tile_position;
    std::vector<std::int16_t> dynamic_sprite_flags;
    std::array<float,3> linear_light{1,1,1};
    bool grayscale=false;
    bool invert=false;
};
struct SavedObjectDrawRecord {
    std::int16_t id;
    std::uint32_t guid;
    std::string source, asset;
    std::uint16_t objd_id, dgrp_id;
    std::uint32_t direction, zoom;
    std::size_t layers, omitted_dynamic_layers, omitted_empty_layers;
    // Selected DGRP absent with NumGraphics0, or the source's explicit initial
    // BaseGraphic0/Graphic0 null DrawGroup. Never a fallback to another frame.
    bool no_graphics = false;
    std::size_t offscreen_layers = 0; // Fully outside this camera; never sent to compositor.
};
struct SavedObjectRenderResult {
    odh::CompositeFrame composite;
    std::vector<SavedObjectDrawRecord> records;
    std::size_t layers = 0, cached_assets = 0, cached_groups = 0, cached_frames = 0;
    std::uint64_t cached_pixels = 0, cached_frame_bytes = 0, offscreen_layers = 0;
    std::uint64_t clipped_layers = 0; // Partly outside the supplied camera.
};
// Host resource cache: immutable DefinitionIndex must outlive this object.
// Strict SPR2+pixel+Z profile; SPR# and no-depth sprites fail explicitly.
// Decoded frames used by one view <=2Mi pixels (RGBA+index+Z <=12MiB); obsolete
// frames are evicted on pixel pressure and after successful composition.
// Repeated visible draw extents <=4Mi pixels, compositor samples <=16Mi,
// viewport <=1Mi. Fully offscreen layers are culled before those draw budgets.
// Empty and offscreen frames retain only validated SPR2 metadata; their
// RLE and palette are checked without allocating decoded pixel planes.
// Whole render failures leave inputs unchanged, but may populate bounded cache.
// Optional record_picks returns per-pixel object IDs in composite.owner_ids;
// the same alpha/depth pass decides ownership. Disabled leaves that array empty.
// record_sprite_rgba adds the compositor's isolated straight-alpha sprite plane;
// the opaque image and its depth/picking profile remain unchanged.
class SavedObjectRenderer {
public:
    explicit SavedObjectRenderer(const odh::DefinitionIndex&);
    ~SavedObjectRenderer();
    SavedObjectRenderer(SavedObjectRenderer&&) noexcept;
    SavedObjectRenderer& operator=(SavedObjectRenderer&&) noexcept;
    SavedObjectRenderResult render(const std::vector<SavedObjectVisual>&,
                                  const odh::WorldProjection&, const odh::RasterFrame&,
                                  bool record_picks=false, bool record_sprite_rgba=false);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
