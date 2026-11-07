#pragma once
#include "odh/route_placement_proof.hpp"
#include "odh/route_execution_proof.hpp"
#include "odh/room_info_obstacles.hpp"
#include "odh/world.hpp"
#include "odh/live_entry_point5.hpp"
#include <array>
#include <functional>
#include <optional>
#include <memory>
#include <string>

namespace odh {
enum class AvatarPositionValidStatus { success, blocked, unsupported };
struct AvatarPositionValidResult {
    AvatarPositionValidStatus status=AvatarPositionValidStatus::unsupported;
    // Pinned VMPlacementError: Success=-1, bounds=0, overlap=10; floor
    // errors retain their original numeric values for the owning primitive.
    std::int16_t placement_error=-1,blocker=0;
    std::uint64_t blocker_generation=0;
    bool blocker_is_avatar=false;
    std::string diagnostic;
};
struct AvatarPositionArchitecture {
    bool out_of_bounds=false,user_out_of_bounds=false;
    std::uint16_t precise_floor=0;
    std::optional<std::int16_t> level_offset;
};
struct AvatarPositionEntryPoint5Request {
    std::int16_t tested_entity=0,other=0;
    std::uint64_t tested_generation=0,other_generation=0;
    std::uint16_t action=0;
    std::int16_t ghost=0;
    std::array<std::int16_t,4> arguments{};
    // Exact active placement5/16/46, callback-scoped range45 proof, or the separate
    // source45/27 execution lease. Proof kinds remain mutually exclusive.
    const Vm* source_vm=nullptr;
    const VmFrame* source_frame=nullptr;
    const Instruction* source_instruction=nullptr;
    VmDispatch source_dispatch{};
    std::optional<RoutePlacementProof> route_placement_proof;
    std::optional<RouteExecutionProof> route_execution_proof;
    RouteExecutionPhase route_execution_phase=RouteExecutionPhase::attempt_walk;
    std::optional<LiveObjectCreationPlacementSource> object_creation_source;
    std::optional<LiveRetiredContainerPlacementSource> retired_container_source;
    std::optional<LiveOriginalRecoveryPlacementSource> original_recovery_source;
    std::optional<LiveAvatarConstructorPlacementSource> avatar_constructor_source;
};
struct AvatarPositionValidServices {
    std::function<std::optional<AvatarPositionArchitecture>(std::int16_t,const EntityPosition&)> architecture;
    // Complete source RoomInfo intersections. They are stably sorted by owner
    // ID here, matching GetAvatarPlace's combined static/dynamic enumeration.
    std::function<RoomInfoObstacleQuery(std::int16_t,const EntityPosition&,std::uint8_t)> intersections;
    std::function<std::optional<bool>(std::int16_t,std::int16_t)> same_group;
    // Executes real tested-entity EP5 using its OWN register owner and exact
    // resource owner; Caller=Callee=tested_entity, StackObject=other. No cloned
    // World or read-only diagnostic checks. Unknown execution stays unsupported.
    std::function<std::optional<bool>(const AvatarPositionEntryPoint5Request&)> entry_point5;
};
class AvatarPositionValid {
public:
    AvatarPositionValid(World& world,AvatarPositionValidServices services):world_(world),services_(std::move(services)){}
    class PreparedAuthorization {
    public:
        PreparedAuthorization()=default;
    private:
        struct Data;std::shared_ptr<const Data> data_;
        explicit PreparedAuthorization(std::shared_ptr<const Data> data):data_(std::move(data)){}
        friend class AvatarPositionValid;
    };
    // Capture the real direct5/16/46 target BEFORE source PrePositionChange
    // detaches it. The capability is restricted to this validator and dispatch.
    std::optional<PreparedAuthorization> prepare(const VmFrame&,const Vm&,const Instruction&,
        const VmDispatch&,std::int16_t actor,std::uint64_t generation);
    AvatarPositionValidResult evaluate_prepared(const PreparedAuthorization&,
        const VmFrame&,const Vm&,const Instruction&,const VmDispatch&,
        std::int16_t actor,std::uint64_t generation,const EntityPosition& candidate,
        std::uint8_t direction,std::uint8_t flags);
    // Source flags: AcceptSlots1, UserBuildableLimit2, AllowIntersection4,
    // AllAvatarsSolid8. Evaluates placement only; never publishes a position.
    // The selected source proof and actor generation remain required across callbacks.
    AvatarPositionValidResult evaluate(const VmFrame&,const Vm&,const Instruction&,const VmDispatch&,
        std::int16_t actor,std::uint64_t generation,const EntityPosition& candidate,
        std::uint8_t direction,std::uint8_t flags);
    // Exact newly-created avatar of a still-pending real opcode42 factory.
    AvatarPositionValidResult evaluate_creation(const LiveObjectCreationPlacementSource&,
        std::int16_t actor,std::uint64_t generation,const EntityPosition& candidate,
        std::uint8_t direction,std::uint8_t flags);
    AvatarPositionValidResult evaluate_lifecycle(const LiveRetiredContainerPlacementSource&,
        const EntityPosition& candidate,std::uint8_t direction,std::uint8_t flags);
    AvatarPositionValidResult evaluate_constructor(const LiveAvatarConstructorPlacementSource&,
        const EntityPosition&,std::uint8_t direction,std::uint8_t flags);
    AvatarPositionValidResult evaluate_recovery(const LiveOriginalRecoveryPlacementSource&,
        const EntityPosition& candidate,std::uint8_t direction,std::uint8_t flags);
    // Route-owned opcode45 placement requires Routing's callback-scoped proof;
    // a default or expired proof is unsupported. Opcode46 continues through
    // the same exact active-dispatch validation as evaluate().
    AvatarPositionValidResult evaluate_route(const RoutePlacementContext&,
        std::int16_t actor,std::uint64_t generation,const EntityPosition& candidate,
        std::uint8_t direction,std::uint8_t flags);
    // Source routing SetPosition uses NORTH with default placement flags.
    // As with evaluate(), candidate.direction is not the requested direction;
    // the explicit direction parameter controls PositionValid's temporary pose.
    // AttemptWalk's ghost1 passability checks use live EP5 directly, without
    // PositionValid or a temporary heading change. Only Routing mints proof.
    AvatarPositionValidResult evaluate_execution(const RouteExecutionContext&,
        std::int16_t actor,std::uint64_t generation,const EntityPosition& candidate,
        std::uint8_t direction,std::uint8_t flags);
private:
    bool authorization_matches(const PreparedAuthorization&,const VmFrame&,const Vm&,const Instruction&,
        const VmDispatch&,std::int16_t,std::uint64_t) const;
    AvatarPositionValidResult evaluate_internal(const VmFrame&,const Vm&,const Instruction&,const VmDispatch&,
        const PreparedAuthorization*,const RoutePlacementProof*,const RouteExecutionContext*,
        const LiveObjectCreationPlacementSource*,std::int16_t,std::uint64_t,
        const EntityPosition&,std::uint8_t,std::uint8_t);
    AvatarPositionValidResult evaluate_geometry(const std::function<bool()>&,
        const std::function<void(AvatarPositionEntryPoint5Request&)>&,
        std::optional<WorldEntityReference>,std::int16_t,std::uint64_t,
        const EntityPosition&,std::uint8_t,std::uint8_t);
    World& world_;AvatarPositionValidServices services_;bool executing_=false;
    std::shared_ptr<const std::uint8_t> authorization_owner_;
};
}
