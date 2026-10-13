#pragma once
#include "odh/world.hpp"
namespace odh {
// Decimal opcode32 (0x20). Identity must be bound from explicit resource data;
// missing object/type/scope is unsupported, never an invented false match.
VmPrimitive object_type_primitive(World&);
}
