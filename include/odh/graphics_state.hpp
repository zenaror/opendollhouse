#pragma once

#include "odh/world.hpp"
#include "odh/formats.hpp"

namespace odh {

struct GraphicsEntity {
    std::int16_t id;
    std::uint64_t generation;
    EntityKind kind;
    std::array<float,3> tile_position;
    std::uint8_t direction; // Eight clockwise indices, north=0; DGRP flag is 1<<direction.
    std::int16_t container; // Zero for a free entity.
};
using GraphicsSlotResolver=std::function<const SlotTable*(const Entity&)>;

// Limited FreeSO 4c6b3e8f5835b228723caea3c9f683c62f244f73 reference profile:
// free objects use (x/16-.5,y/16-.5), free avatars use (x/16,y/16),
// and both use (level-1)*2.95 for Z. Positioned scale-one avatars may occupy
// storage SLOTs in free objects. Storage ordinals follow type-zero file order;
// their XY offsets use /16 and cardinal rotation (diagonals use zero rotation,
// as in ObjectComponent). Quarter turns are simplified algebraically.
// Container graphics replaces the copied logical child position; never sum both.
// Terrain, interpolation, contained objects, nested containers, avatar bone
// slots and other scales are unsupported. Unsupported/missing/stale state
// throws FormatError. A resolver must return a table whose lifetime covers this
// call. This profile is not evidence of behavior in the original game.
GraphicsEntity graphics_entity(const World&,std::int16_t id,GraphicsSlotResolver={});

} // namespace odh
