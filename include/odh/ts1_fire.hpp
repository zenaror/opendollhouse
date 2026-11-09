#pragma once
#include "odh/world.hpp"
#include "odh/ts1_simulation.hpp"

namespace odh {
// VMBurn's installed TS1 Fire definition, not a synthetic replacement object.
constexpr std::uint32_t ts1_fire_guid=0x24c95f99U;
struct Ts1FireServices {
    std::uint32_t width=0,height=0;
    std::uint8_t stories=2;
    // Exact current source authority. Each external service may run source
    // Init/Reset and must preserve the caller's pointer/generation authority.
    std::function<bool()> source_current;
    // Actual handler/setter lifetime survives legitimate synchronous Reset.
    std::optional<VmInstructionLease> instruction_lease;
    std::function<std::optional<std::uint32_t>(std::uint32_t)> random;
    std::function<std::optional<bool>(WorldEntityReference)> caller_in_pool;
    // Read-only body Position, including an explicit canonical OOW sentinel.
    // nullopt means unavailable, not permission to invent a level/coordinate.
    std::function<std::optional<EntityPosition>(WorldEntityReference)> position;
    // Source ID-sorted tile query and actual physical-group member order.
    // Empty is proven empty; nullopt is incomplete evidence.
    std::function<std::optional<std::vector<WorldEntityReference>>(const EntityPosition&)> objects_at;
    std::function<std::optional<std::vector<WorldEntityReference>>(WorldEntityReference)> group;
    // VMEntity.IsInUse(multitile=true), occupied flags plus AVATAR Callees only.
    std::function<std::optional<bool>(WorldEntityReference)> in_use;
    // General SetValue(Flags), including avatar Reset on either Burning-bit
    // transition. It must preserve prior writes on lifecycle failure.
    std::function<bool(WorldEntityReference,std::int16_t,std::string&)> set_flags;
    // Real shared constructor: installed Fire, centered tile, NORTH, original
    // Init/Main and native resource provenance. True means the source attempt
    // completed (including normal null/placement refusal); false is unsupported.
    std::function<bool(const EntityPosition&,std::string&)> create;
};
// Actual opcode9 with its instruction lease, ordinary or an explicitly
// authenticated source-check child. The frame's StackObject may retain
// its exact dead body for read-only Position; affected objects remain live.
// RNG, pool test, threshold reduction, OOW and subsequent effects retain the
// reference order. Failure never rolls back preceding source effects.
PrimitiveOutcome execute_ts1_burn(const World&,Ts1Clock&,const VmFrame&,
    const Instruction&,const VmDispatch&,const Ts1FireServices&);
} // namespace odh
