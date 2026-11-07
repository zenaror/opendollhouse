#pragma once
#include "odh/world.hpp"
namespace odh {
// Sink returns true only when a real registered object thread was interrupted
// and rescheduled. An absent sink means this world has no object-thread backend.
// Dispatch-bearing calls require their actual query capability; hosts can also
// require it for every call. The raw-ID sink is reached only for the exact
// indexed StackObject generation, never a replacement of a retained/nil role.
// A strong reference without its body, or a retained/dead body without an exact
// Thread backend, is unavailable rather than evidence of a null source Thread.
using IdleNotifySink=std::function<bool(std::int16_t)>;
VmPrimitive notify_idle_primitive(World&,IdleNotifySink={},
    std::function<void(std::int16_t,const std::string&)> observed={},
    bool require_source_query=false);
}
