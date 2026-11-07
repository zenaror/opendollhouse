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
    const ObjectRuntimeResourceLimits& limits, bool selected_subpart) {
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

    const auto functions = read_objd_functions(*selected.objd);
    EntryPointTable entry_points = functions.uses_function_table != 0
        ? (selected.objf ? read_objf(*selected.objf, limits.entry_points)
                         : throw FormatError("OBJD requires a missing OBJf function table"))
        : read_objd_entry_points(*selected.objd);
    std::optional<SlotTable> slots;
    if(selected.slots)slots=read_slot(*selected.slots, limits.formats);
    // GameObjectResource.Get<SLOT> returns null when absent; VMGameObject.Init
    // leaves Contained null and TotalSlots returns0. This local counting input
    // is not published as if a SLOT resource had been present.
    const SlotTable absent_slots{};
    auto construction = object_construction_profile(definition, slots?*slots:absent_slots,{},selected_subpart);

    std::optional<std::string> semiglobal_name;
    if (selected.glob) semiglobal_name = read_glob(*selected.glob).name;

    return {std::move(definition), std::move(slots), std::move(entry_points),
            construction, std::move(private_routines), std::move(semiglobal_name)};
}

ObjectGroupConstructionPlan plan_object_group_construction(const View& resource,
    const DefinitionCandidate& requested,const DefinitionIndex& index,std::size_t member_limit) {
    if(!member_limit||member_limit>4096||index.unique(requested.definition.guid)!=&requested)
        throw FormatError("object group requires an exact indexed definition and bounded member limit");
    ObjectGroupConstructionPlan plan;plan.definition_guid=requested.definition.guid;
    plan.multitile=requested.definition.master_id!=0&&requested.definition.sub_index==-1;
    std::size_t requested_count=0;
    std::optional<ObjectGroupConstructionPart> selected_single;
    const auto signed_byte=[](std::uint16_t value){
        const auto byte=static_cast<std::uint8_t>(value);
        return static_cast<std::int16_t>(byte<128?byte:static_cast<int>(byte)-256);
    };
    iff_chunks(resource,[&](const Chunk& chunk){
        if(chunk.type!="OBJD")return;
        const auto definition=read_object_definition(chunk.payload);
        if(chunk.id==requested.chunk_id){
            ++requested_count;
            if(definition.guid!=requested.definition.guid||definition.master_id!=requested.definition.master_id||
               definition.sub_index!=requested.definition.sub_index)
                throw FormatError("object group selected definition differs from its immutable index");
            if(!plan.multitile){
                if(!definition.level_offset||chunk.payload.size()<128)
                    throw FormatError("singleton group offset or lead metadata is incomplete");
                const auto bits=static_cast<std::uint16_t>(definition.sub_index);
                selected_single=ObjectGroupConstructionPart{&requested,
                    static_cast<std::int16_t>(signed_byte(bits>>8)*16),
                    static_cast<std::int16_t>(signed_byte(bits)*16),signed_byte(*definition.level_offset),chunk.payload.le16(126)>0};
            }
        }
        if(!plan.multitile||definition.master_id!=requested.definition.master_id||definition.sub_index==-1)return;
        if(plan.parts.size()>=member_limit)throw FormatError("object group member budget exceeded");
        const auto* candidate=index.unique(definition.guid);
        if(!candidate||candidate->source!=requested.source||candidate->asset!=requested.asset||candidate->chunk_id!=chunk.id||
           candidate->definition.master_id!=definition.master_id||candidate->definition.sub_index!=definition.sub_index)
            throw FormatError("object group subpart is missing, ambiguous or belongs to another IFF");
        if(!definition.level_offset||chunk.payload.size()<128)
            throw FormatError("object group offset or lead metadata is incomplete");
        for(const auto& part:plan.parts)if(part.candidate==candidate)
            throw FormatError("object group repeats a subpart definition");
        const auto bits=static_cast<std::uint16_t>(definition.sub_index);
        plan.parts.push_back({candidate,static_cast<std::int16_t>(signed_byte(bits>>8)*16),
            static_cast<std::int16_t>(signed_byte(bits)*16),signed_byte(*definition.level_offset),chunk.payload.le16(126)>0});
    });
    if(requested_count!=1)throw FormatError("object group requested OBJD is missing or duplicated");
    if(!plan.multitile){plan.parts.push_back(*selected_single);return plan;}
    if(plan.parts.empty())throw FormatError("object group master has no represented subparts");
    for(std::size_t n=0;n<plan.parts.size();++n)if(plan.parts[n].lead){plan.base_index=n;return plan;}
    for(std::size_t n=0;n<plan.parts.size();++n){const auto& p=plan.parts[n];
        if(!p.offset_x&&!p.offset_y&&!p.offset_level){plan.base_index=n;break;}}
    return plan;
}

ObjectRuntimeCreateStatus materialize_runtime_object(
    World& world, std::int16_t id, const ObjectRuntimeResources& resources) {
    if (resources.construction.status != ObjectConstructionStatus::ready)
        return ObjectRuntimeCreateStatus::unsupported_profile;
    const EntityLayout layout{resources.construction.layout.attributes, 0,
                              resources.construction.layout.containment_slots, 0};
    return world.create_runtime_object(id, resources.definition.guid, layout,
        resources.entry_points.entries,resources.master_guid)
        ? ObjectRuntimeCreateStatus::created
        : ObjectRuntimeCreateStatus::world_rejected;
}

} // namespace odh
