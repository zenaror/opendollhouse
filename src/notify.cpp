#include "odh/notify.hpp"
namespace odh {
VmPrimitive notify_idle_primitive(World& world,IdleNotifySink sink,
    std::function<void(std::int16_t,const std::string&)> observed) {
    return [&world,sink=std::move(sink),observed=std::move(observed)]
        (const VmFrame& frame,const Instruction& i,const auto&,const auto&)->PrimitiveOutcome {
        if(i.opcode!=49)return {};
        const auto id=frame.context.stack_object;
        const std::string status=!world.find(id)?"missing_object":sink&&sink(id)?"notified":"no_thread";
        if(observed)observed(id,status);
        // Fixed reference returns true even if StackObject or its Thread is null.
        return {PrimitiveDisposition::goto_true,{}};
    };
}
}
