#pragma once
#include "odh/world.hpp"
#include "odh/route.hpp"

namespace odh {
struct RouteMovement {std::uint64_t tick;std::int16_t entity;EntityPosition position;};
// Initial controlled route adapter. One stable VM per controller; all referenced
// objects must outlive calls. This is not the complete TS1 routing policy.
class Routing {
public:
    Routing(World& world,RouteGrid& grid,const SlotTable& globals):world_(world),grid_(grid),globals_(globals) {}
    Routing(const Routing&)=delete;
    Routing& operator=(const Routing&)=delete;
    bool bind(Vm&);
    VmPrimitive primitive();
    bool tick(std::uint64_t tick,const std::function<void(const RouteMovement&)>& moved={});
    bool active() const {return job_.has_value();}
    const std::string& diagnostic() const {return diagnostic_;}
private:
    struct Job {
        std::uint64_t ticket,actor_generation,target_generation;
        std::int16_t actor,target;
        EntityPosition expected,target_position;
        std::vector<RoutePoint> path;
        std::size_t next=1;
        std::int16_t entry_flags;
        std::uint8_t facing;
    };
    PrimitiveOutcome request(const VmFrame&,const Instruction&,const VmDispatch&);
    bool finish(bool result,const std::string& reason);
    World& world_;
    RouteGrid& grid_;
    const SlotTable& globals_;
    Vm* vm_=nullptr;
    std::optional<Job> job_;
    std::optional<std::uint64_t> last_tick_;
    std::string diagnostic_;
    bool executing_=false;
};
} // namespace odh
