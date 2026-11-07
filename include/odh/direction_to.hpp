#pragma once
#include "odh/world.hpp"

namespace odh {
// TS1 GetDirectionTo (opcode12): quantize the vector from the selected source
// entity to StackObject with the pinned double Normalize/PosMod/Math.Round.
// The rounded result is0..8 (8 near north is not reduced modulo8). OOW uses
// the source sentinel. ResultOwner is a ushort; only defined setters are
// admitted, and publication goes through the VM's effective memory/output path.
VmPrimitive get_direction_to_primitive(const World&,VmMemory memory={});
}
