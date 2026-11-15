#pragma once
#include "odh/raster.hpp"

namespace odh {
// When an origin is supplied, return only the rectangular union of clipped
// triangle sample boxes. Pixel centers and interpolation stay in the full
// viewport. An empty union returns an empty frame at (0,0).
RasterFrame rasterize(std::uint32_t width,std::uint32_t height,
    const std::vector<RasterTriangle>&,const std::vector<BitmapImage>&,
    RasterOptions,RasterLimits,std::array<std::uint32_t,2>* origin);
}
