// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/vm.hpp"
namespace odh {
// Finite Simitone/FreeSO7e76 parity profile. No-effect reference modes do NOT
// implement their corresponding original game features. Mode14 is stateful:
// it writes Temp0 to the shared runtime global31 through the VM's effective
// memory. Stateful family/travel services, existing mode15 and unknown bytes are
// deliberately outside this profile. See the per-mode source inventory in .cpp.
enum class GenericTs1CompatibilityEffect { unsupported, no_effect, global_state };
GenericTs1CompatibilityEffect generic_ts1_compatibility_effect(std::uint8_t mode) noexcept;
bool generic_ts1_compatibility_mode(std::uint8_t mode) noexcept;
// Opcode1 reads operand[0] only; operand[1..7] are ignored by the reference.
// Recognized modes return GOTO_TRUE; mode14 stages an exclusive variable_write.
// It never mutates captured World/global state directly or bypasses check memory.
// All other instructions return unsupported for the host's existing dispatch.
VmPrimitive generic_ts1_compatibility_primitive();
} // namespace odh
