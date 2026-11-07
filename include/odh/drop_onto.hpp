#pragma once

#include "odh/playback.hpp"
#include "odh/world.hpp"
#include <functional>
#include <optional>
#include <string>

namespace odh {

// Native, transactionally published subset of pinned TS1 DropOnto (opcode 43).
// It moves an existing item from Caller slot to StackObject slot. It does not
// consult SLOT resources; container slot indices are the instruction operands.
class DropOnto {
public:
    struct CarrierController {
        Playback* playback=nullptr;
        std::uint64_t actor_generation=0;
        std::uint64_t vm_generation=0;
    };
    using CarrierResolver=std::function<std::optional<CarrierController>(std::int16_t)>;

    struct SlotTransferResult {bool completed=false,placed=false;std::string diagnostic;};
    // Optional source lifecycle consumer. Receives the actual authenticated
    // opcode43 dispatch and frozen World move, never a fabricated frame.
    // Failure can follow durable source Pre/Post effects; no rollback is implied.
    using SlotTransfer=std::function<SlotTransferResult(const VmFrame&,const Instruction&,
        const VmDispatch&,const PreparedSlotMove&)>;
    explicit DropOnto(World& world,CarrierResolver carriers={},SlotTransfer transfer={})
        :world_(world),carriers_(std::move(carriers)),transfer_(std::move(transfer)){}
    DropOnto(const DropOnto&)=delete;
    DropOnto& operator=(const DropOnto&)=delete;
    bool bind(const Vm& vm);
    VmPrimitive primitive();

private:
    World& world_;
    CarrierResolver carriers_;
    SlotTransfer transfer_;
    const Vm* vm_=nullptr;
    bool executing_=false;
};

} // namespace odh
