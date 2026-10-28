#include "odh/object_runtime_resources.hpp"

namespace odh {
namespace {
struct SelectedChunks {
    std::optional<View> objd;
    std::optional<View> objf;
    std::optional<View> slots;
    std::optional<View> glob;
    std::size_t glob_count = 0;
};
}

ObjectRuntimeResources read_object_runtime_resources(
    const View& resource, const DefinitionCandidate& candidate,
    const ObjectRuntimeResourceLimits& limits) {
    SelectedChunks selected;
    iff_chunks(resource, [&](const Chunk& chunk) {
        if (chunk.type == "OBJD" && chunk.id == candidate.chunk_id) {
            if (selected.objd) throw FormatError("duplicate selected OBJD chunk");
            selected.objd = chunk.payload;
        } else if (chunk.type == "OBJf" && chunk.id == candidate.chunk_id) {
            if (selected.objf) throw FormatError("duplicate selected OBJf chunk");
            selected.objf = chunk.payload;
        } else if (chunk.type == "GLOB") {
            if (++selected.glob_count > 1)
                throw FormatError("multiple GLOB links in selected object resource");
            selected.glob = chunk.payload;
        }
    }, limits.formats);
    if (!selected.objd) throw FormatError("selected OBJD chunk is absent from exact resource");

    auto definition = read_object_definition(*selected.objd);
    const auto& indexed = candidate.definition;
    if (definition.version != indexed.version || definition.guid != indexed.guid ||
        definition.master_id != indexed.master_id || definition.sub_index != indexed.sub_index ||
        definition.build_mode_type != indexed.build_mode_type)
        throw FormatError("selected OBJD identity differs from the definition index");
    if (!definition.slot_table_id)
        throw FormatError("selected OBJD has no complete SlotID field");

    // The selected SLOT and all private BHAVs are collected in the same pass
    // only after the definition has established the exact SLOT chunk ID.
    std::map<RoutineKey, Bhav> private_routines;
    std::size_t instruction_count = 0;
    iff_chunks(resource, [&](const Chunk& chunk) {
        if (chunk.type == "SLOT" && chunk.id == *definition.slot_table_id) {
            if (selected.slots) throw FormatError("duplicate selected SLOT chunk");
            selected.slots = chunk.payload;
        }
        if (chunk.type != "BHAV") return;
        const auto key = routine_key(chunk.id);
        if (key.scope != RoutineScope::private_object) return;
        if (private_routines.size() >= limits.private_routines)
            throw FormatError("private BHAV count exceeds object resource budget");
        if (chunk.payload.size() > limits.routine_payload_bytes)
            throw FormatError("private BHAV payload exceeds object resource budget");
        auto routine = read_bhav(chunk.payload);
        if (routine.instructions.size() > limits.private_instructions - instruction_count)
            throw FormatError("private BHAV instruction count exceeds object resource budget");
        instruction_count += routine.instructions.size();
        if (!private_routines.emplace(key, std::move(routine)).second)
            throw FormatError("duplicate private BHAV ID in exact object resource");
    }, limits.formats);
    if (!selected.slots) throw FormatError("OBJD SlotID is absent from exact object resource");

    const auto functions = read_objd_functions(*selected.objd);
    EntryPointTable entry_points = functions.uses_function_table != 0
        ? (selected.objf ? read_objf(*selected.objf, limits.entry_points)
                         : throw FormatError("OBJD requires a missing OBJf function table"))
        : read_objd_entry_points(*selected.objd);
    auto slots = read_slot(*selected.slots, limits.formats);
    auto construction = object_construction_profile(definition, slots);

    std::optional<std::string> semiglobal_name;
    if (selected.glob) semiglobal_name = read_glob(*selected.glob).name;

    return {std::move(definition), std::move(slots), std::move(entry_points),
            construction, std::move(private_routines), std::move(semiglobal_name)};
}

ObjectRuntimeCreateStatus materialize_runtime_object(
    World& world, std::int16_t id, const ObjectRuntimeResources& resources) {
    if (resources.construction.status != ObjectConstructionStatus::ready)
        return ObjectRuntimeCreateStatus::unsupported_profile;
    const EntityLayout layout{resources.construction.layout.attributes, 0,
                              resources.construction.layout.containment_slots, 0};
    return world.create_runtime_object(id, resources.definition.guid, layout,
        resources.entry_points.entries)
        ? ObjectRuntimeCreateStatus::created
        : ObjectRuntimeCreateStatus::world_rejected;
}

} // namespace odh
