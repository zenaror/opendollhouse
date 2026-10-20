#pragma once

#include "odh/object_definition.hpp"
#include "odh/world.hpp"

namespace odh {
struct CollisionBox {
    std::int32_t x1,y1,x2,y2;
    bool operator==(const CollisionBox& other) const {
        return x1==other.x1&&y1==other.y1&&x2==other.x2&&y2==other.y2;
    }
};
enum class CollisionShapeState { present, absent, unsupported };
struct CollisionShapeResult {
    CollisionShapeState state;
    CollisionBox box{};
};

// Reproduces VMGameObject.GetObstacle's OBJD TileWidth/FootprintMask math.
// Direction is the World eight-notch value; diagonal notches follow the
// reference's DirectionToWallOff fallback (no quarter-turn mask rotation).
// HasZeroExtent and contained objects have no persistent object obstacle.
CollisionShapeResult object_collision_shape(const ObjectDefinition&,const EntityPosition&,
                                            std::uint16_t entity_flags,bool contained);

// Candidate avatar obstacle used by Simitone's GetAvatarPlace is a fixed
// radius of three world units in every direction. Other placement gates are
// deliberately outside this geometric primitive.
CollisionShapeResult avatar_candidate_collision_shape(const EntityPosition&);
// Simitone's VMObstacle.Intersects uses strict overlap: touching edges are not
// an intersection. This is only box geometry, not a complete placement test.
bool collision_boxes_intersect(const CollisionBox&,const CollisionBox&);
}
