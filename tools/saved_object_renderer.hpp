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
};
struct SavedObjectDrawRecord {
    std::int16_t id;
    std::uint32_t guid;
    std::string source, asset;
    std::uint16_t objd_id, dgrp_id;
    std::uint32_t direction, zoom;
    std::size_t layers, omitted_dynamic_layers, omitted_empty_layers;
    bool no_graphics = false; // NumGraphics0 and selected DGRP absent: no pixels.
};
struct SavedObjectRenderResult {
    odh::CompositeFrame composite;
    std::vector<SavedObjectDrawRecord> records;
    std::size_t layers = 0, cached_assets = 0, cached_groups = 0, cached_frames = 0;
    std::uint64_t cached_pixels = 0, cached_frame_bytes = 0;
};
// Host resource cache: immutable DefinitionIndex must outlive this object.
// Strict SPR2+pixel+Z profile; SPR# and no-depth sprites fail explicitly.
// Decoded frames used by one view <=1Mi pixels (RGBA+index+Z <=6MiB); obsolete
// frames are evicted on pixel pressure and after successful composition.
// Repeated draw extents <=4Mi pixels, compositor samples <=16Mi, viewport <=1Mi.
// Empty decoded frames used by the view are retained without placement.
// Whole render failures leave inputs unchanged, but may populate bounded cache.
// Optional record_picks returns per-pixel object IDs in composite.owner_ids;
// the same alpha/depth pass decides ownership. Disabled leaves that array empty.
class SavedObjectRenderer {
public:
    explicit SavedObjectRenderer(const odh::DefinitionIndex&);
    ~SavedObjectRenderer();
    SavedObjectRenderer(SavedObjectRenderer&&) noexcept;
    SavedObjectRenderer& operator=(SavedObjectRenderer&&) noexcept;
    SavedObjectRenderResult render(const std::vector<SavedObjectVisual>&,
                                  const odh::WorldProjection&, const odh::RasterFrame&,
                                  bool record_picks=false);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
