#include "odh/wall_junction.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <tuple>

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
void validate_grid(const SavedArchitecture& a,WallView view){
    if(!a.width||a.width>64||a.width!=a.height||view.level<1||view.level>2)
        throw FormatError("wall junction square grid/view invalid");
    const auto count=std::size_t(a.width)*a.height;
    for(const auto& level:a.levels){
        if(level.walls.size()!=count)throw FormatError("wall junction grid array invalid");
        for(const auto& t:level.walls)
            if(t.segments>63||((t.segments&48)&&t.segments!=16&&t.segments!=32))
                throw FormatError("wall junction unknown/mixed segments invalid");
    }
    if(view.down)for(std::size_t off=0;off<count;++off)
        if(a.levels[view.level-1].walls[off].segments){
            const auto at=static_cast<std::int64_t>(off),w=std::int64_t{a.width};
            for(const auto index:{at+1,at-1,at+w,at-w,at-w-1,at+w-1,at-w+1})
                if(index<0||static_cast<std::uint64_t>(index)>=count)
                    throw FormatError("wall view source cut neighbor outside linear grid");
        }
}
using FaceKey=std::tuple<std::uint8_t,std::uint32_t,std::uint32_t,std::uint8_t>;
std::map<FaceKey,const WallAppearanceFace*> validate_appearance(const SavedArchitecture& a,const WallAppearance& appearance){
    if(appearance.faces.size()>16384)throw FormatError("wall junction appearance budget exceeded");
    std::map<FaceKey,const WallAppearanceFace*> faces;
    for(const auto& f:appearance.faces){
        if(f.x>=a.width||f.y>=a.height||f.level<1||f.level>2||(f.segment!=1&&f.segment!=2)||
           f.owner<=0||!f.generation||f.door_owner<0||
           ((f.door_owner==0)!=(f.door_generation==0))||(f.door&&f.door_owner==0)||
           !faces.emplace(FaceKey{f.level,f.x,f.y,f.segment},&f).second)
            throw FormatError("wall junction appearance identity/door marker invalid");
        if(f.style.raw_style>=256||(f.style.resource_guid&&f.style.raw_style<=21)||
           (!f.style.resource_guid&&f.style.raw_style>21))
            throw FormatError("wall junction appearance style outside profile");
    }
    return faces;
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
    return wall_junctions(a,{},rotation,{});
}
std::array<std::vector<WallCuts>,2> wall_view_cuts(const SavedArchitecture& a,WallView view){
    validate_grid(a,view);const auto count=std::size_t(a.width)*a.height;
    std::array<std::vector<WallCuts>,2> cuts;
    for(auto& c:cuts)c.resize(count);
    if(view.down)for(std::size_t off=0;off<count;++off){
        const auto& t=a.levels[view.level-1].walls[off];auto& c=cuts[view.level-1][off];
        if(thick(t,wall_top_left))c.top_left=3;
        if((t.segments&48)?t.top_right_style==1:thick(t,wall_top_right))c.top_right=3;
    }
    return cuts;
}
WallJunctionResult wall_junctions(const SavedArchitecture& a,const WallAppearance& appearance,
    std::uint8_t rotation,WallView view){
    if(rotation>3)throw FormatError("wall junction rotation invalid");
    validate_grid(a,view);
    const auto faces=validate_appearance(a,appearance);
    const auto cuts=wall_view_cuts(a,view);
    const auto count=std::size_t(a.width)*a.height;
    WallJunctionResult result;
    for(std::uint8_t level=1;level<=view.level;++level){
        std::vector<std::uint8_t> up(count,0),down(count,0);
        const auto& walls=a.levels[level-1].walls;
        for(std::size_t off=0;off<count;++off){const auto& t=walls[off];
            const auto c=cuts[level-1][off];
            const auto door=[&](std::uint8_t side){const auto found=faces.find(FaceKey{level,static_cast<std::uint32_t>(off%a.width),static_cast<std::uint32_t>(off/a.width),side});
                return found!=faces.end()&&found->second->door;};
            const auto add=[&](std::size_t index,std::uint8_t bits,bool cut){if(index<count)(cut?down:up)[index]|=bits;};
            if(thick(t,wall_top_left)&&!(c.top_left&&door(1))){add(off,8,c.top_left!=0);add(off+a.width,2,c.top_left!=0);}
            if(thick(t,wall_top_right)&&!(c.top_right&&door(2))){add(off,4,c.top_right!=0);add(off+1,1,c.top_right!=0);}
            if((t.top_right_style==1||t.top_right_style==255)&&t.segments==wall_vertical_diagonal){
                add(off,128,c.top_right!=0);add(off+a.width+1,32,c.top_right!=0);
            }else if((t.top_right_style==1||t.top_right_style==255)&&t.segments==wall_horizontal_diagonal){
                add(off+1,16,c.top_right!=0);add(off+a.width,64,c.top_right!=0);
            }
        }
        for(std::size_t off=0;off<count;++off){
            const bool is_down=up[off]==0;const auto flags=is_down?down[off]:up[off];
            if(flags==0)continue;
            const auto frame=wall_junction_frame(flags);
            if(!frame){++result.unmapped;continue;}
            const auto rotated=rotate_wall_junction(flags,rotation);
            const auto rotated_frame=wall_junction_frame(rotated);
            if(!rotated_frame)throw FormatError("wall junction rotation map is not closed");
            result.junctions.push_back({static_cast<std::uint32_t>(off%a.width),
                static_cast<std::uint32_t>(off/a.width),level,flags,rotated,*rotated_frame,is_down});
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
