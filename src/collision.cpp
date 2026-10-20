#include "odh/collision.hpp"
#include <cstdint>

namespace odh {
namespace {
constexpr std::uint16_t has_zero_extent=1U<<2;
bool valid_position(const EntityPosition& p) {
    return p.x>=0&&p.y>=0&&p.x<=32767&&p.y<=32767&&
           p.level>=1&&p.level<=2&&p.direction<=7;
}
}
CollisionShapeResult object_collision_shape(const ObjectDefinition& definition,const EntityPosition& p,
                                             std::uint16_t flags,bool contained) {
    if((flags&has_zero_extent)||contained) return {CollisionShapeState::absent,{}};
    if(!valid_position(p)||!definition.tile_width||!definition.footprint_mask)
        return {CollisionShapeState::unsupported,{}};

    // DirectionToWallOff in Simitone recognizes N/E/S/W; diagonal enum
    // values fall through to zero. World directions are N,NE,E,...,NW.
    const auto quarter_turns=(p.direction%2U)==0U?static_cast<unsigned>(p.direction/2U):0U;
    const auto shift=quarter_turns*4U;
    const auto shifted=static_cast<std::uint32_t>(*definition.footprint_mask)<<shift;
    const auto mask=static_cast<std::uint16_t>((shifted>>16)|(shifted&0xffffU));
    auto half_width=static_cast<std::int32_t>(*definition.tile_width/2U);
    if(half_width==0) half_width=8;

    const auto x1=static_cast<std::int64_t>(p.x)-half_width+((mask>>12)&0xfU);
    const auto y1=static_cast<std::int64_t>(p.y)-half_width+(mask&0xfU);
    const auto x2=static_cast<std::int64_t>(p.x)+half_width-((mask>>4)&0xfU);
    const auto y2=static_cast<std::int64_t>(p.y)+half_width-((mask>>8)&0xfU);
    if(x1>x2||y1>y2) return {CollisionShapeState::unsupported,{}};
    return {CollisionShapeState::present,{static_cast<std::int32_t>(x1),static_cast<std::int32_t>(y1),
                                          static_cast<std::int32_t>(x2),static_cast<std::int32_t>(y2)}};
}
CollisionShapeResult avatar_candidate_collision_shape(const EntityPosition& p) {
    if(!valid_position(p)) return {CollisionShapeState::unsupported,{}};
    return {CollisionShapeState::present,{p.x-3,p.y-3,p.x+3,p.y+3}};
}
bool collision_boxes_intersect(const CollisionBox& a,const CollisionBox& b) {
    return !((b.x1>=a.x2||b.x2<=a.x1)||(b.y1>=a.y2||b.y2<=a.y1));
}
}
