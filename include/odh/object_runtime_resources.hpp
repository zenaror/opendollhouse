#pragma once

#include "odh/definition_index.hpp"
#include "odh/entry_points.hpp"
#include "odh/object_construction.hpp"
#include "odh/vm.hpp"
#include "odh/world.hpp"

namespace odh {

struct ObjectRuntimeResourceLimits {
    Limits formats{};
    EntryPointLimits entry_points{};
    std::size_t private_routines = 4096;
    std::size_t private_instructions = 262144;
    std::uint64_t routine_payload_bytes = 1024 * 1024;
};

// Owned runtime inputs extracted from one already selected IFF resource.
// The semiglobal name is only the raw local GLOB link; callers must resolve
// that exact bank separately before executing code that needs it.
struct ObjectRuntimeResources {
    ObjectDefinition definition;
    SlotTable slots;
    EntryPointTable entry_points;
    ObjectConstructionProfile construction;
    RoutineSet private_routines;
    std::optional<std::string> semiglobal_name;
};

enum class ObjectRuntimeCreateStatus { created, unsupported_profile, world_rejected };

// `resource` must be the exact IFF asset named by `candidate`. OBJD, OBJf,
// SLOT and private BHAVs are resolved only inside it; no archive precedence or
// fallback is applied. Missing/duplicate required chunks and identity mismatch
// fail with FormatError. Global and semiglobal BHAV code is not loaded here.
ObjectRuntimeResources read_object_runtime_resources(
    const View& resource, const DefinitionCandidate& candidate,
    const ObjectRuntimeResourceLimits& = {});

// Publishes the resource-derived entity banks and entry points atomically.
// This still does not bind BHAV owners, execute Init/EP8, or schedule Main.
ObjectRuntimeCreateStatus materialize_runtime_object(
    World&, std::int16_t id, const ObjectRuntimeResources&);

} // namespace odh
