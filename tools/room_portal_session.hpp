#pragma once
#include "odh/room_obstacles.hpp"
#include "odh/vm.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace odh_host {
using RoomPortalOwnerMap=std::map<std::tuple<std::string,std::string,std::uint16_t>,odh::CodeOwner>;
struct RoomPortalContext {
    std::vector<odh::IgnoredRoomPortal> ignored;
    std::optional<odh::RoomPortalEdge> pending_edge;
    std::optional<odh::VmCall> pending_call;
    std::uint64_t portal_generation=0;
};
// One instance per resident VM. The host enforces 16 live contexts, 32 steps
// and 32 ignored edges per context before growing these collections.
struct RoomPortalSession {
    std::map<std::uint64_t,RoomPortalContext> contexts;
    RoomPortalOwnerMap code_owners;
    std::size_t planned_calls=0,returns=0,transitions=0;
    std::string diagnostic;

    void prune(const odh::Vm& vm) {
        // Suspended Route45 parents remain external frames while a nested
        // Route45 owns pending(). Inspect only the owning resident's VM.
        for(auto entry=contexts.begin();entry!=contexts.end();){
            const auto live=(vm.pending()&&vm.pending()->ticket==entry->first)||
                std::any_of(vm.stack().begin(),vm.stack().end(),[&](const auto& frame){
                    return frame.return_mode==odh::VmCallReturnMode::external_frame_next_tick&&
                        frame.invocation_ticket==entry->first;
                });
            if(!live)entry=contexts.erase(entry);else ++entry;
        }
    }
};
} // namespace odh_host
