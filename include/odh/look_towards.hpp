#pragma once

#include "odh/world.hpp"

namespace odh {

// TS1 mode-0 head target bookkeeping and source camera-mode no-op. Body modes
// execute through the caller's bound Routing controller.
VmPrimitive look_towards_primitive(World&);

}
