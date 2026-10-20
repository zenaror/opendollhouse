#pragma once
#include "odh/world.hpp"
#include "odh/route.hpp"
#include <cstddef>
#include <vector>

namespace odh {
struct RouteMovement {std::uint64_t tick;std::int16_t entity;EntityPosition position;};
// Enumerates the bounded offset lattice used by the pinned FreeSO
// VMSlotParser for ranged type-3 SLOTs. This is only candidate generation:
// it does not filter by distance, room, direction, floor, walls, occupancy,
// scoring, or establish original The Sims 1 semantics.
std::vector<RoutePoint> enumerate_reference_slot_range_lattice(
    const SlotTable&,std::size_t type3_index,std::size_t candidate_limit=4096);
// Applies only the pinned FreeSO proximity band (including its +/-0.5
// tolerance and FSOSquare distance mode) to that lattice. Room, direction,
// geometry, occupancy, score/RNG and original-game semantics remain unknown.
std::vector<RoutePoint> filter_reference_slot_range_proximity(
    const SlotTable&,std::size_t type3_index,const std::vector<RoutePoint>& offsets);
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
