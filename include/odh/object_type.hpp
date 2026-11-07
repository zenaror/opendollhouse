#pragma once
#include "odh/world.hpp"
namespace odh {
// Decimal opcode32 (0x20). Identity must be bound from explicit resource data;
// A known absent object returns the reference's ERROR, never a false type
// match. Unknown identity or unavailable operand scope remains unsupported.
// StackObjectID uses the exact frame reference, including a retained body;
// other operand scopes select a live entity by the resulting numeric ID.
// A positive bound generation that cannot resolve is an ownership failure,
// not a known null pointer and not an authored ERROR result.
VmPrimitive object_type_primitive(const World&);
}
