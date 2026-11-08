// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/npc_avatar.hpp"
namespace odh {
// Original6e598 fresh constructor and synchronous reset path. It operates on the
// real newly allocated body/Thread; failure requires discarding the candidate.
// Does not reinterpret or reinitialize any existing/reopened avatar.
NpcAvatarStartupResult initialize_original_avatar(World&,Vm&,std::int16_t,CodeOwner,
    const NpcAvatarRequest&,const EntryPointTable&,const NpcAvatarBodyInputs&,RandomState&,
    std::uint64_t,const VmPrimitive&,bool,const std::function<bool()>&);
// Frontend load has no script Caller or waiting VM ticket. This scoped source
// authority is checked again at allocation and during synchronous initialization.
NpcAvatarRequestResult resolve_original_load_avatar_request(std::int16_t,const NeighborContext&,
    const World&,bool residential,std::function<bool()> source_current);
}
