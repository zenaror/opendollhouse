#pragma once
#include "odh/world.hpp"
namespace odh {
struct EntityNext {
    VmContext context;
    std::uint32_t guid;
    std::int16_t current;
    std::optional<std::int16_t> result;
    bool success;
    std::uint8_t scope;
    std::int16_t index;
    std::uint8_t mode=4;
};
// Bounded ODH SetToNext (opcode31) profile: mode0 any entity, mode1 avatar,
// mode2 object, mode3 next member of an explicit saved multitile group, mode4
// exact definition GUID, mode8 next avatar/object on the cursor's tile, mode9
// next entity in adjacent tiles around the object selected by the frame local,
// and mode12 next avatar sharing PersonData[61] (FAMI chunk ID), with wrap. Modes
// 0/1/2 include unknown definitions and ignore GUID/Local operand bytes. Mode3
// wraps within its group. Other modes stop without wrap.
// World must
// outlive the primitive/VM. Callbacks are owned copies; objects they capture
// must also outlive it. Memory callbacks and observer must not throw or reenter
// the VM; a rejected external write must leave its backend unchanged.
// Unlike FreeSO's ignored SetVariable bool, ODH rejects unavailable writes.
// Observer runs only after a completed false/true dispatch; native VM outputs
// are staged and published by Vm after the primitive returns.
VmPrimitive entity_next_primitive(const World&,VmMemory memory={},
    std::function<void(const EntityNext&)> observed={},
    std::vector<std::vector<std::int16_t>> multitile_groups={});
}
