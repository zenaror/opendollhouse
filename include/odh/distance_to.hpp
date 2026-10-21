#pragma once
#include "odh/world.hpp"

namespace odh {
// TS1 GetDistanceTo (opcode11), bounded to Temp0, Caller target (including
// Destination Go Here's MyObject[11] encoding), same-level positioned entities.
// Returns floor(Euclidean distance in 1/16-tile coordinates / 16).
VmPrimitive get_distance_to_primitive(const World& world);
}
