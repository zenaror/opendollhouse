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

// Pure visual anchor for an uncontained entity; terrain is added by the lot.
// Shared with placement previews so their sprite matches the committed object.
std::array<float,3> free_graphics_position(EntityKind,const EntityPosition&);

// Limited FreeSO 4c6b3e8f5835b228723caea3c9f683c62f244f73 reference profile:
// free objects use (x/16-.5,y/16-.5), free avatars use (x/16,y/16),
// and both use (level-1)*2.95 for Z. Positioned scale-one avatars may occupy
// storage SLOTs in free objects. Saved avatars may have a validated attachment
// instead of a runtime positioned attachment. Contained objects use SLOT height
// classes1..9 (0/5 use raw offset Z/5) and do not add avatar's half-tile offset.
// Storage ordinals follow type-zero file order;
// their XY offsets use /16 and cardinal rotation (diagonals use zero rotation,
// as in ObjectComponent). Quarter turns are simplified algebraically.
// Container graphics replaces the copied logical child position; never sum both.
// Terrain, interpolation, nested containers, avatar bone
// slots and other scales are unsupported. Unsupported/missing/stale state
// throws FormatError. A resolver must return a table whose lifetime covers this
// call. This profile is not evidence of behavior in the original game.
GraphicsEntity graphics_entity(const World&,std::int16_t id,GraphicsSlotResolver={});

} // namespace odh
