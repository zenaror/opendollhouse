#pragma once

#include "odh/vm.hpp"
#include "odh/world.hpp"

namespace odh {

// Implements the TS1 one-shot form of Set Motive Change (BHAV opcode 29).
// Continuous changes remain unsupported until their per-avatar tick state is
// represented by the runtime and save format.
PrimitiveOutcome ts1_motive_change_once(World&, const VmMemory&,
                                       const VmFrame&, const Instruction&);

VmPrimitive ts1_motive_change_primitive(World&, VmMemory);

} // namespace odh
