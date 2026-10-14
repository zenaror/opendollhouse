#include "odh/wall_junction.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace odh {
namespace {
constexpr std::array<std::uint8_t,46> frame_flags{{
    2,1,3,8,10,9,11,4,6,5,7,12,14,13,15,
    32,40,36,44,16,18,20,22,48,52,
    128,130,129,131,160,144,146,176,64,65,
    72,73,96,104,80,112,192,193,224,208,240
}};
bool thick(const SavedWallTile& t,std::uint8_t side) {
    if(!(t.segments&side))return false;
    const auto style=side==wall_top_left?t.top_left_style:t.top_right_style;
    // DecodeWalls working normalization before WallTile.TopLeft/RightThick.
    return style!=2&&style!=12&&style!=13&&style!=14;
}

// The junction ramps subtract a binary32 constant from binary32 values in
// [160,263]. Do that exact integer significand subtraction and nearest-even
// rounding here, rather than depending on a mutable host fenv. This implements
// precisely the source's iterative float arithmetic, not a double closed form.
float subtract_ramp(float value,std::uint8_t zoom) {
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559,
                  "junction profile requires IEEE-754 binary32");
    std::uint32_t bits;std::memcpy(&bits,&value,4);
    const auto exponent=(bits>>23)&255U;
    const auto mantissa=(bits&0x7fffffU)|0x800000U;
    // IEEE-754 1.525f has significand12792627 and exponent127 (unit2^-23).
    // Scaling by4/2/1 changes only its exponent; both signs are known positive
    // before subtraction and results remain normal, so no special values arise.
    constexpr std::uint64_t ramp_mantissa=12792627;
    const unsigned ramp_exponent=127U+(3U-zoom);
    const unsigned shift=exponent-ramp_exponent;
    const auto exact=(std::uint64_t(mantissa)<<shift)-ramp_mantissa;
    // A ramp row can cross256, requiring one lower exponent/significand unit.
    const bool lower=exact<(std::uint64_t(0x800000U)<<shift);
    const unsigned result_shift=shift-(lower?1U:0U);
    const auto unit=std::uint64_t{1}<<result_shift;
    auto rounded=exact>>result_shift;
    const auto remainder=exact&(unit-1);
    if(remainder>unit/2 || (remainder==unit/2&&(rounded&1U)))++rounded;
    const std::uint32_t result_bits=((exponent-(lower?1U:0U))<<23)|
        static_cast<std::uint32_t>(rounded-0x800000U);
    float result;std::memcpy(&result,&result_bits,4);return result;
}
} // namespace

std::optional<std::uint8_t> wall_junction_frame(std::uint8_t flags) {
    for(std::size_t i=0;i<frame_flags.size();++i)
        if(frame_flags[i]==flags)return static_cast<std::uint8_t>(i);
    return std::nullopt;
}
std::uint8_t rotate_wall_junction(std::uint8_t flags,std::uint8_t rotation) {
    if(rotation>3)throw FormatError("wall junction rotation invalid");
    const unsigned low=(unsigned(flags)&15U)<<rotation;
    const unsigned high=(unsigned(flags)&240U)<<rotation;
    return static_cast<std::uint8_t>((low&15U)|(low>>4)|((high|(high>>4))&240U));
}
WallJunctionResult standing_wall_junctions(const SavedArchitecture& a,std::uint8_t rotation) {
    if(rotation>3||!a.width||a.width>64||a.width!=a.height)
        throw FormatError("wall junction square grid/rotation invalid");
    const auto count=std::size_t(a.width)*a.height;
    for(const auto& level:a.levels){
        if(level.walls.size()!=count)throw FormatError("wall junction grid array invalid");
        for(const auto& t:level.walls)
            if(t.segments>63 || ((t.segments&48)&&t.segments!=16&&t.segments!=32))
                throw FormatError("wall junction unknown/mixed segments invalid");
    }
    WallJunctionResult result;
    for(std::uint8_t level=1;level<=2;++level){
        std::vector<std::uint8_t> flags(count,0);
        const auto add=[&](std::size_t index,std::uint8_t bits){if(index<count)flags[index]|=bits;};
        const auto& walls=a.levels[level-1].walls;
        for(std::size_t off=0;off<count;++off){const auto& t=walls[off];
            if(thick(t,wall_top_left)){add(off,8);add(off+a.width,2);}
            if(thick(t,wall_top_right)){add(off,4);add(off+1,1);}
            if((t.top_right_style==1||t.top_right_style==255)&&t.segments==wall_vertical_diagonal){
                add(off,128);add(off+a.width+1,32);
            }else if((t.top_right_style==1||t.top_right_style==255)&&t.segments==wall_horizontal_diagonal){
                add(off+1,16);add(off+a.width,64);
            }
        }
        for(std::size_t off=0;off<count;++off){
            if(flags[off]==0)continue;
            const auto frame=wall_junction_frame(flags[off]);
            if(!frame){++result.unmapped;continue;}
            const auto rotated=rotate_wall_junction(flags[off],rotation);
            const auto rotated_frame=wall_junction_frame(rotated);
            if(!rotated_frame)throw FormatError("wall junction rotation map is not closed");
            result.junctions.push_back({static_cast<std::uint32_t>(off%a.width),
                static_cast<std::uint32_t>(off/a.width),level,flags[off],rotated,*rotated_frame});
        }
    }
    return result;
}
WallDepth wall_junction_depth(std::uint8_t zoom) {
    if(zoom<1||zoom>3)throw FormatError("wall junction depth zoom invalid");
    const auto width=32U<<(zoom-1),height=16U<<(zoom-1);
    WallDepth result{width,height,std::vector<std::uint8_t>(std::size_t(width)*height)};
    float value=263.f;
    for(std::uint32_t y=0;y<height;++y){
        const auto clamped=std::min(255.f,value);
        const auto floor=static_cast<unsigned>(clamped);
        // Exact .5 comparison and integral-to-float conversion at this range
        // are rounding-mode independent; no floating addition is used here.
        const auto fraction=clamped-static_cast<float>(floor);
        const auto byte=static_cast<std::uint8_t>(floor+
            (fraction>.5f||(fraction==.5f&&(floor&1U))?1U:0U));
        std::fill_n(result.pixels.begin()+std::size_t(y)*width,width,byte);
        value=subtract_ramp(value,zoom);
    }
    return result;
}
} // namespace odh
