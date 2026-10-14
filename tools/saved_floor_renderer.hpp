#pragma once
#include "odh/floor.hpp"
#include "odh/saved_architecture.hpp"
#include <memory>
struct SavedFloorMaterial {
    std::uint16_t pattern,sprite_id,palette_id;
    std::uint32_t width,height;
};
struct SavedFloorDraw {
    std::uint32_t x,y;
    std::uint8_t level;
    std::uint16_t pattern;
    odh::FloorHalf half;
    std::array<float,4> altitude;
};
struct SavedFloorResult {
    odh::RasterFrame image;
    std::vector<SavedFloorMaterial> materials;
    std::vector<SavedFloorDraw> draws;
    std::uint64_t triangles=0,texture_bytes=0;
    std::uint64_t geometry_texture_pixel_visits=0;
};
// Explicit global floors.iff only; caller rejects unresolved FLRm mappings.
// Texture cache contains frame0+FloorCopy and is keyed by pattern/zoom.
// Painted floors only: no grass surface, pool/water, walls, lightmaps or rooms.
class SavedFloorRenderer {
public:
    explicit SavedFloorRenderer(const std::string& floors_iff);
    ~SavedFloorRenderer();
    SavedFloorResult render(const odh::SavedArchitecture&,const odh::WorldProjection&,
                           bool apply_terrain=true,std::int32_t base_alt=0);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
