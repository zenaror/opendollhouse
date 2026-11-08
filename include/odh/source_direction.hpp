// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <cmath>
#include "odh/world.hpp"
namespace odh {
// Pinned VMGameObject.RadianDirection: FLOAT multiplication by FLOAT(pi/4), promoted to double,
// normalize against double pi, then cast float. Direction is a native notch.
inline float source_game_object_radians(std::uint8_t notch) noexcept {
    constexpr double pi=3.141592653589793238462643383279502884;
    const float product=static_cast<float>(notch)*static_cast<float>(pi/4.0);
    double value=product;
    if(value>pi)value-=pi*2.0;
    return static_cast<float>(value);
}
inline std::optional<float> source_entity_radian_direction(const World& world,std::int16_t id,std::uint64_t generation){
    // Source entity references retain their exact body after Delete. This is
    // a read-only orientation lookup, never authority to mutate that body.
    const auto* entity=world.resolve_reference({id,generation});if(!entity)return {};
    if(entity->kind==EntityKind::avatar)return std::isfinite(entity->radian_direction)?std::optional<float>{entity->radian_direction}:std::nullopt;
    if(entity->position)return entity->position->direction<=7?std::optional<float>{source_game_object_radians(entity->position->direction)}:std::nullopt;
    // Read this body's own scalar: an unbound numeric VM lookup could resolve
    // a replacement entity after ID reuse instead of the retained source body.
    const auto direction=entity->object_words.find(1);
    if(direction==entity->object_words.end()||direction->second<0||direction->second>7)return {};
    return source_game_object_radians(static_cast<std::uint8_t>(direction->second));
}
}
