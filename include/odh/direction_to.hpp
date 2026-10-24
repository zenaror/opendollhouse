#pragma once
#include "odh/world.hpp"

namespace odh {
// TS1 GetDirectionTo (opcode12): quantize the vector from the selected source
// entity to StackObject into one of eight notches and publish to its VM target.
VmPrimitive get_direction_to_primitive(const World&,VmMemory memory={});
}
