#pragma once
#include "odh/world.hpp"

namespace odh {
// TS1 GetDistanceTo (opcode11), bounded to the observed profile: Temp0,
// default Caller target, same-level positioned entities, zeroed operand tail.
// Returns floor(Euclidean distance in 1/16-tile coordinates / 16).
VmPrimitive get_distance_to_primitive(const World& world);
}
