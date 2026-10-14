#include "odh/terrain.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace odh {
namespace {
void validate(const SavedArchitecture& a) {
    if(a.width<2 || a.height<2 || !a.altitude)
        throw std::runtime_error("terrain altitude grid missing or too small");
    const auto count=static_cast<std::uint64_t>(a.width)*a.height;
    if(count>std::numeric_limits<std::size_t>::max() || a.altitude->size()!=count)
        throw std::runtime_error("terrain altitude grid extent mismatch");
}
float height(const SavedArchitecture& a,std::uint32_t x,std::uint32_t y) {
    return static_cast<float>((*a.altitude)[static_cast<std::size_t>(y)*a.width+x][1])*10.0f;
}
float altitude(float h,std::int32_t base_alt) {
    const float result=(h-static_cast<float>(base_alt))*(3.0f/160.0f);
    if(!std::isfinite(result))throw std::runtime_error("terrain altitude overflow");
    return result;
}
std::uint32_t clamp_index(float value,std::uint32_t extent) {
    if(value<=1.0f)return 1;
    if(static_cast<double>(value)>=static_cast<double>(extent-1))return extent-1;
    return static_cast<std::uint32_t>(value);
}
}
float terrain_corner_altitude(const SavedArchitecture& a,std::uint32_t x,std::uint32_t y,std::int32_t base_alt) {
    validate(a);
    if(x>=a.width || y>=a.height)throw std::runtime_error("terrain corner outside grid");
    return altitude(height(a,x,y),base_alt);
}
float terrain_interp_altitude(const SavedArchitecture& a,float x,float y,std::int32_t base_alt) {
    validate(a);
    if(!std::isfinite(x) || !std::isfinite(y) || x< -32768.0f || y< -32768.0f || x>32767.0f || y>32767.0f)
        throw std::runtime_error("terrain interpolation coordinate outside profile");
    const auto x0=clamp_index(std::floor(x),a.width);
    const auto y0=clamp_index(std::floor(y),a.height);
    const auto x1=clamp_index(std::ceil(x),a.width);
    const auto y1=clamp_index(std::ceil(y),a.height);
    const float fx=std::fmod(x,1.0f),fy=std::fmod(y,1.0f);
    const float low=fx*height(a,x1,y0)+(1.0f-fx)*height(a,x0,y0);
    const float high=fx*height(a,x1,y1)+(1.0f-fx)*height(a,x0,y1);
    return altitude(fy*high+(1.0f-fy)*low,base_alt);
}
}
