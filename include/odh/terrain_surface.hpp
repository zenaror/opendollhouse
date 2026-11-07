#pragma once
#include "odh/terrain.hpp"
#include "odh/raster.hpp"
#include "odh/projection.hpp"
namespace odh {
enum class TerrainTheme : std::uint8_t {grass, sand, rock, snow, water, dark_grass, autumn_grass, cloud};
struct TerrainSurface {
    std::vector<RasterTriangle> triangles;
    // One interior point per triangle before projection, in VM sixteenths and
    // one-based level. Triangle splits coincide with diagonal room boundaries.
    std::vector<std::array<std::int32_t,3>> room_samples;
    std::uint32_t whole_tiles=0,half_tiles=0;
};
// Ground-level material0 only, including empty diagonal halves. Interior tiles
// x/y1..dimension-2; square2..64 grids. Preserves serialized grass; rendering
// decodes (127-byte) modulo256 and the reference's corner offset (y-1)*W+x-1.
// Byte-quantized palette Lerp, normalized reference slope normals, shared depth.
// Theme is explicit; sand mixes light grass with dark sand as in the loader.
// No blades/noise, room/day lighting, gamma, water/pools or original proof.
TerrainSurface terrain_surface(const SavedArchitecture&,const WorldProjection&,
                               TerrainTheme,std::int32_t base_alt=0,
                               bool record_owners=false);
}
