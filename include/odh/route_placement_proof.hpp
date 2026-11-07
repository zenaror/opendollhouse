#pragma once
#include "odh/vm.hpp"
#include <memory>
namespace odh {
class Routing;
// Default-invalid, copyable, callback-scoped route placement authority. Only
// Routing can mint a lease; captured copies expire safely when its callback ends.
class RoutePlacementProof {
public:
    RoutePlacementProof()=default;
    bool matches(const VmFrame&,const Vm&,const Instruction&,const VmDispatch&) const noexcept;
private:
    struct State;
    std::weak_ptr<State> state_;
    explicit RoutePlacementProof(const std::shared_ptr<State>& state):state_(state){}
    friend class Routing;
};
struct RoutePlacementContext {
    const Vm* vm=nullptr;
    VmFrame frame;
    Instruction instruction{};
    VmDispatch dispatch{};
    RoutePlacementProof proof;
};
}
