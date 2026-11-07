#pragma once

#include "odh/vm.hpp"
#include "odh/world.hpp"

namespace odh {

// Implements TS1 Set Motive Change (BHAV opcode 29), including the one-shot
// operation and the per-avatar continuous controller.
PrimitiveOutcome ts1_motive_change(World&, const VmMemory&,
                                  const VmFrame&, const Instruction&,
                                  const Ts1Temps& = {});

VmPrimitive ts1_motive_change_primitive(World&, VmMemory);

} // namespace odh
