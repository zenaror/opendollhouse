// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/autonomy.hpp"
#include "odh/interaction.hpp"

namespace odh {
// Exact source table identity and final byte-truncated GetAction checks.
void validate_saved_autonomy_table(const Ttab&);
bool saved_autonomy_action_available(const Interaction&,const VmProgram& global,
    const VmProgram& resource);
using SavedAutonomyString=std::function<odh::AutonomyResult<std::optional<std::string>>(
    const odh::VmFrame&,const odh::Instruction&,const odh::Ts1Temps&)>;
using SavedAutonomyPrimitiveFactory=std::function<odh::VmPrimitive(const odh::VmMemory&)>;
// Disposable source AutoCheck: shares live caller registers/effects while keeping
// advertisements and variants local. Referenced resources/handlers must outlive
// this synchronous call; factories see the effective scratch memory. The optional
// owner resolver belongs to these resource banks, independently of caller Temps;
// candidate-World namespaces must supply their own provider or remain closed.
AutonomyResult<AutonomyCheck> evaluate_saved_autonomy_check(
    World&,Vm& caller,std::int16_t actor,std::int16_t target,CodeOwner owner,
    const Interaction&,const std::shared_ptr<const VmProgram>& global,
    const std::shared_ptr<const VmProgram>& resource,VmMemory,
    const VmPrimitive&,const SavedAutonomyString&,std::uint64_t budget=10000,
    const SavedAutonomyPrimitiveFactory& factory={},const Vm::OwnerResolver& owner_resolver={});
} // namespace odh
