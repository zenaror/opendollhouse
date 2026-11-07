#pragma once

#include "odh/object_definition.hpp"

namespace odh {

// Structural sizes needed to allocate an ordinary runtime object. This is
// deliberately not a factory: it does not create a World entity, bind code,
// or run the object's initialization routines.
struct ObjectConstructionLayout {
    std::size_t attributes = 0;
    std::size_t containment_slots = 0;
};

struct ObjectConstructionLimits {
    std::size_t attributes = 4096;
    std::size_t containment_slots = 4096;
};

enum class ObjectConstructionStatus {
    ready,
    unsupported_multitile,
    missing_runtime_fields,
    resource_limit_exceeded
};

struct ObjectConstructionProfile {
    ObjectConstructionStatus status;
    ObjectConstructionLayout layout;
};

// Derives the World bank sizes from OBJD and the SLOT table resolved through
// OBJD.SlotID. SLOT type 0 records are the object's containment slots; other
// types are retained for routing/rendering and are not allocated in this bank.
// Callers must resolve the SLOT resource belonging to this definition.
ObjectConstructionProfile object_construction_profile(
    const ObjectDefinition&, const SlotTable&, const ObjectConstructionLimits& = {},
    bool selected_subpart = false);

} // namespace odh
