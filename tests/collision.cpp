#include "odh/collision.hpp"
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
odh::ObjectDefinition definition(std::uint16_t width,std::uint16_t mask){
    odh::ObjectDefinition result{};result.tile_width=width;result.footprint_mask=mask;return result;
}
}
int main(){
 try {
    const auto def=definition(32,0x1234);
    const odh::EntityPosition north{100,200,1,0};
    const auto shape=odh::object_collision_shape(def,north,0,false);
    require(shape.state==odh::CollisionShapeState::present&&shape.box==odh::CollisionBox{85,188,113,214},
            "north uses raw footprint nibbles and half TileWidth");
    const odh::CollisionBox cardinal[4]={{85,188,113,214},{86,185,112,213},
        {87,186,115,212},{88,187,114,215}};
    for(std::uint8_t direction=0;direction<8;++direction){
        auto p=north;p.direction=direction;const auto actual=odh::object_collision_shape(def,p,0,false);
        const auto expected=cardinal[direction%2?0:direction/2];
        require(actual.state==odh::CollisionShapeState::present&&actual.box==expected,
                "direction follows pinned DirectionToWallOff footprint rotation");
    }
    const auto fallback=odh::object_collision_shape(definition(0,0),north,0,false);
    require(fallback.state==odh::CollisionShapeState::present&&fallback.box==odh::CollisionBox{92,192,108,208},
            "zero TileWidth uses the reference eight-unit half-width fallback");
    require(odh::object_collision_shape(odh::ObjectDefinition{},north,4,false).state==odh::CollisionShapeState::absent&&
            odh::object_collision_shape(odh::ObjectDefinition{},north,0,true).state==odh::CollisionShapeState::absent,
            "zero-extent and contained objects have no persistent obstacle");
    require(odh::object_collision_shape(odh::ObjectDefinition{},north,0,false).state==odh::CollisionShapeState::unsupported,
            "missing OBJD collision fields are not guessed");
    auto invalid=north;invalid.direction=8;
    require(odh::object_collision_shape(def,invalid,0,false).state==odh::CollisionShapeState::unsupported,
            "out-of-profile direction is rejected");
    const auto avatar_shape=odh::avatar_candidate_collision_shape(north);
    require(avatar_shape.state==odh::CollisionShapeState::present&&avatar_shape.box==odh::CollisionBox{97,197,103,203},
            "avatar candidate uses fixed three-unit radius");
    auto negative=north;negative.x=-1;
    require(odh::avatar_candidate_collision_shape(negative).state==odh::CollisionShapeState::unsupported,
            "avatar obstacle rejects invalid coordinates before arithmetic");
    require(!odh::collision_boxes_intersect({0,0,10,10},{10,2,20,8})&&
            odh::collision_boxes_intersect({0,0,10,10},{9,2,20,8}),
            "obstacle intersection matches strict reference edge semantics");
    std::cout<<checks<<" collision geometry checks passed (reference formula only)\n";return 0;
 } catch(const std::exception& e){std::cerr<<"Collision geometry test failed: "<<e.what()<<'\n';return 1;}
}
