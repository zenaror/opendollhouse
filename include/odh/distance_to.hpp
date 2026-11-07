#pragma once
#include "odh/world.hpp"

namespace odh {
// TS1 GetDistanceTo (opcode11). Reads its target selector using the BHAV
// operand's VM scope and writes the computed distance to its selected Temp.
// Unsupported target scopes stay fail-closed when the supplied memory backend
// cannot resolve them. World must outlive the returned primitive.
VmPrimitive get_distance_to_primitive(const World&, VmMemory memory = {});
}
