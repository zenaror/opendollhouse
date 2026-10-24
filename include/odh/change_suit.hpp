#pragma once

#include "odh/vm.hpp"
#include "odh/animation.hpp"
#include "odh/world.hpp"
#include <functional>
#include <optional>
#include <string>

namespace odh {
using Ts1ObjectSuitLookup = std::function<std::optional<std::string>(CodeOwner,std::uint16_t)>;
// Strict TS1 ChangeSuitOrAccessory profile for Object-scope `.apr` bindings.
// Object string resources are supplied by the exact BHAV code owner.
VmPrimitive ts1_change_suit_or_accessory(World&,Ts1ObjectSuitLookup);
}
