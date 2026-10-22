#include "odh/neighborhood_picker.hpp"
#include <algorithm>
#include <limits>
namespace odh {
namespace {
int radius(unsigned scale) {
    if(scale != 1 && scale != 2) throw FormatError("unsupported neighborhood marker scale");
    return static_cast<int>(6 * scale);
}
bool contains(const NeighborhoodMarker& marker, int r, std::int32_t x, std::int32_t y) {
    const auto dx=std::int64_t{x}-marker.location.x,dy=std::int64_t{y}-marker.location.y;
    return dx>=-r && dx<=r && dy>=-r && dy<=r;
}
}
std::optional<NeighborhoodMapConfig> neighborhood_map_config(unsigned mode) {
    if(!neighborhood_mode_supported(mode)) return std::nullopt;
    switch(mode) {
        case 1: return NeighborhoodMapConfig{"Nbhd\\NScreen.BMP",1};
        case 2: return NeighborhoodMapConfig{"Downtown\\DScreen.bmp",1};
        case 3: return NeighborhoodMapConfig{"VIsland\\visland.bmp",1};
        case 4: return NeighborhoodMapConfig{"Community\\NScreen_unleashed.bmp",2};
        case 5: return NeighborhoodMapConfig{"Studiotown\\DScreen.bmp",1};
        case 7: return NeighborhoodMapConfig{"Magicland\\DScreen.bmp",1};
    }
    throw FormatError("invalid neighborhood mode");
}
std::optional<std::uint16_t> pick_neighborhood_lot(
        const std::vector<NeighborhoodMarker>& markers,unsigned scale,std::int32_t x,std::int32_t y) {
    const int r=radius(scale);
    if(x<0 || x>=800 || y<0 || y>=600) return std::nullopt;
    for(auto it=markers.rbegin();it!=markers.rend();++it)
        if(contains(*it,r,x,y)) return it->file_exists ? std::optional<std::uint16_t>{it->location.house} : std::nullopt;
    return std::nullopt;
}
RasterFrame draw_neighborhood_map(const BitmapImage& image,
        const std::vector<NeighborhoodMarker>& markers,unsigned scale) {
    const int r=radius(scale);
    if(image.width!=800 || image.height!=600 || image.pixels.size()!=800*600)
        throw FormatError("neighborhood map must be 800x600");
    RasterFrame frame{};frame.width=800;frame.height=600;frame.pixels=image.pixels;
    frame.depth.resize(800*600,std::numeric_limits<float>::infinity());
    for(const auto& marker:markers) {
        const std::array<std::uint8_t,4> color=!marker.file_exists ?
            std::array<std::uint8_t,4>{120,120,120,255} : marker.occupied ?
            std::array<std::uint8_t,4>{245,185,30,255} : std::array<std::uint8_t,4>{40,220,120,255};
        const auto cx=std::int64_t{marker.location.x},cy=std::int64_t{marker.location.y};
        for(auto y=std::max<std::int64_t>(0,cy-r);y<=std::min<std::int64_t>(599,cy+r);++y)
            for(auto x=std::max<std::int64_t>(0,cx-r);x<=std::min<std::int64_t>(799,cx+r);++x)
                frame.pixels[static_cast<std::size_t>(y*800+x)]=
                    (x==cx-r || x==cx+r || y==cy-r || y==cy+r) ?
                    std::array<std::uint8_t,4>{0,0,0,255} : color;
    }
    return frame;
}
}
