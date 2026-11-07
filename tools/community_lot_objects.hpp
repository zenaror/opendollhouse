// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "object_trade_session_adapter.hpp"
#include "odh/actorless_object_trade.hpp"
namespace odh_host {
// Authenticate immutable resource banks and return their owned programs. This
// restores no VM and executes no instruction; unsupported closures throw.
ObjectTradeThreadPrograms reconstruct_community_object_programs(ObjectTradeResourceLibrary&,
    std::int16_t id,const odh::SessionBundle& component_state);
struct CommunityObjectCommandServices {
    // Exact source groups, not inferred only from matching OBJD masters.
    std::vector<odh::ObjectTradeGroup> groups;
    // Frozen saved people still need actual loaded Thread authority for uses.
    std::vector<odh::ObjectTradeAvatarThread> avatar_threads;
    bool avatar_threads_complete=false;
    std::function<bool()> source_still_matches;
    // Missing extra primitives remain unsupported, never successful no-ops.
    ObjectTradeSessionHooks bindings;
};
// Preloads exact resources before candidate execution; no live writes or publication. Default program
// reconstruction supports exact ordinary resource-owned object Threads; a caller
// may provide richer authenticated thread_programs in bindings.
odh::ActorlessObjectTradeResult prepare_community_object_command(ObjectTradeResourceLibrary&,
    const odh::ActorlessObjectTradeState&,const odh::PreparedArchitecture&,
    const odh::SavedArchitecture&,const odh::GeometricRoomMap&,
    const ObjectTradeSessionCommand&,CommunityObjectCommandServices);
}
