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
    // Missing selected SLOT is distinct from an owned empty SLOT resource.
    // Pinned FreeSO7e76 VMEntity235 / VMGameObject.Init127 permits null Slots;
    // such an object has zero containment slots (TotalSlots231-235).
    std::optional<SlotTable> slots;
    EntryPointTable entry_points;
    ObjectConstructionProfile construction;
    RoutineSet private_routines;
    std::optional<std::string> semiglobal_name;
    // Supplied by the authenticated group plan, never inferred by the
    // per-piece allocator. Direct creation of a subpart leaves this absent.
    std::optional<std::uint32_t> master_guid;
};

struct ObjectGroupConstructionPart {
    const DefinitionCandidate* candidate = nullptr;
    std::int16_t offset_x=0,offset_y=0,offset_level=0;
    bool lead=false;
};
struct ObjectGroupConstructionPlan {
    std::uint32_t definition_guid=0;
    bool multitile=false;
    std::size_t base_index=0;
    std::vector<ObjectGroupConstructionPart> parts; // Exact IFF chunk order.
};
// Expand only an abstract master. Authenticate every selected subpart against
// the same immutable IFF/index and preserve source order, signed offsets and
// BaseObject selection. A directly selected subpart remains a singleton.
ObjectGroupConstructionPlan plan_object_group_construction(const View&,
    const DefinitionCandidate&,const DefinitionIndex&,std::size_t member_limit=64);

enum class ObjectRuntimeCreateStatus { created, unsupported_profile, world_rejected };

// `resource` must be the exact IFF asset named by `candidate`. OBJD, OBJf,
// SLOT and private BHAVs are resolved only inside it; no archive precedence or
// fallback is applied. OBJD SlotID must be complete, but an absent selected SLOT
// is valid and remains nullopt, with zero containment slots. Present SLOTs are
// parsed strictly. Missing/duplicate required chunks and identity mismatch fail
// with FormatError. Global and semiglobal BHAV code is not loaded here.
ObjectRuntimeResources read_object_runtime_resources(
    const View& resource, const DefinitionCandidate& candidate,
    const ObjectRuntimeResourceLimits& = {}, bool selected_subpart = false);

// Publishes the resource-derived entity banks and entry points atomically.
// This still does not bind BHAV owners, execute Init/EP8, or schedule Main.
ObjectRuntimeCreateStatus materialize_runtime_object(
    World&, std::int16_t id, const ObjectRuntimeResources&);

} // namespace odh
