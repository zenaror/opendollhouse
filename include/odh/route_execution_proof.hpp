#pragma once

#include "odh/route.hpp"
#include "odh/world.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace odh {

class Routing;

enum class RouteExecutionPhase : std::uint8_t { attempt_walk, set_position };

// Default-invalid, callback-scoped authority minted only by Routing for an
// authenticated source45/source27 operation or its own pending route tick.
class RouteExecutionProof {
public:
    RouteExecutionProof()=default;
    bool matches(const VmFrame&,const Vm&,const Instruction&,const VmDispatch&,
        RouteExecutionPhase) const noexcept;
    bool matches_after_restore(const VmFrame&,const Vm&,const Instruction&,const VmDispatch&,
        RouteExecutionPhase) const noexcept;
private:
    struct State;
    std::weak_ptr<State> state_;
    explicit RouteExecutionProof(const std::shared_ptr<State>& state):state_(state){}
    friend class Routing;
};

struct RouteExecutionContext {
    const Vm* vm=nullptr;
    VmFrame frame;
    Instruction instruction{};
    VmDispatch dispatch{};
    RouteExecutionPhase phase=RouteExecutionPhase::attempt_walk;
    RouteExecutionProof proof;
    std::int16_t actor=0,target=0;
    std::uint64_t actor_generation=0,target_generation=0;
    EntityPosition actor_position{},target_position{};
    RoutePoint goal{};
    std::size_t candidate_index=0;
};

} // namespace odh
