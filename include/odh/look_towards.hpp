#pragma once

#include "odh/world.hpp"

namespace odh {

// Supported TS1 mode-0 head target bookkeeping. Body rotation/routing modes
// remain unsupported until a lot-turn scheduler is connected.
VmPrimitive look_towards_primitive(World&);

}
