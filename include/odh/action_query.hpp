// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/interaction.hpp"
namespace odh {
struct ActionQueryThread {
    const Vm* thread=nullptr;
    const InteractionQueue* interactions=nullptr;
};
// Resolve the persistent Caller thread, including when the executing dispatch
// belongs to a disposable check VM. Missing facts are not a negative answer.
using ActionQueryThreadResolver=std::function<std::optional<ActionQueryThread>(std::int16_t,std::uint64_t)>;
// TS1 TestSimInteractingWith37 has no operand fields. It reads the LIVE Caller
// thread's top ActionTree and ActiveAction.Callee, never the check VM's frame.
VmPrimitive test_sim_interacting_with_primitive(const World&,ActionQueryThreadResolver);
}
