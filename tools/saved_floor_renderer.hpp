#pragma once
#include "odh/floor.hpp"
#include "odh/saved_architecture.hpp"
#include "odh/architecture_resource_catalog.hpp"
#include <memory>
#include <functional>
struct SavedFloorMaterial {
    std::uint16_t pattern,sprite_id,palette_id;
    std::uint32_t width,height;
    std::string source,asset;
    std::uint8_t frame=0;
};
struct SavedFloorDraw {
    std::uint32_t x,y;
    std::uint8_t level;
    std::uint16_t pattern;
    odh::FloorHalf half;
    std::array<float,4> altitude;
    std::uint16_t surface_sprite=0;
    std::array<std::uint16_t,4> corner_sprites{};
};
struct SavedFloorResult {
    odh::RasterFrame image;
    std::vector<SavedFloorMaterial> materials;
    std::vector<SavedFloorDraw> draws;
    std::uint64_t triangles=0,texture_bytes=0,liquid_layers=0;
    std::uint64_t geometry_texture_pixel_visits=0;
};
// Explicit global floors.iff and optional catalog of external .flr resources.
// Global IDs 1..255 keep their original sprite rule; dynamic IDs use 1/257/513.
// Texture cache contains frame0+FloorCopy and is keyed by pattern/zoom.
// Pool/water uses the selected global SPR2 frames, separately from opaque painted materials.
class SavedFloorRenderer {
public:
    explicit SavedFloorRenderer(const std::string& floors_iff,
        const odh::ArchitectureResourceCatalog& catalog = {});
    ~SavedFloorRenderer();
    SavedFloorResult render(const odh::SavedArchitecture&,const odh::WorldProjection&,
                           bool apply_terrain=true,std::int32_t base_alt=0,std::uint8_t level=2,
                           bool record_owners=false,
                           const std::function<std::array<float,3>(std::uint32_t,std::uint32_t,std::uint8_t)>& light = {},
                           const odh::RasterFrame* ground = nullptr);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
