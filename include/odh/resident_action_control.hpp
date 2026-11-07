// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/interaction.hpp"
namespace odh {
struct ResidentControlIdentity {
    std::int16_t actor=0;
    std::uint64_t generation=0,run_generation=0;
};
// Host intent bridge only: the existing TS1 queue remains scheduling authority.
std::optional<ResidentControlIdentity> resident_control_identity(const World&,const Vm&,const InteractionQueue&);
InteractionCancelResult cancel_resident_action(const World&,const Vm&,InteractionQueue&,
    ResidentControlIdentity,std::uint64_t uid,std::string* diagnostic=nullptr);
bool enqueue_resident_user_action(const World&,const Vm&,InteractionQueue&,ResidentControlIdentity,
    const VmCall&,std::uint64_t target_generation,std::uint32_t flags,
    std::optional<RoutineKey> check={},std::optional<std::int32_t> interaction_number={},
    std::string* diagnostic=nullptr);
}
