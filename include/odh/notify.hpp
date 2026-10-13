#pragma once
#include "odh/world.hpp"
namespace odh {
// Sink returns true only when a real registered object thread was interrupted
// and rescheduled. An absent sink means this world has no object-thread backend.
using IdleNotifySink=std::function<bool(std::int16_t)>;
VmPrimitive notify_idle_primitive(World&,IdleNotifySink={},
    std::function<void(std::int16_t,const std::string&)> observed={});
}
