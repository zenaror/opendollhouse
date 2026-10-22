#include "odh/saved_thread_plan.hpp"

#include <limits>

namespace odh {

SimitoneSavedThreadPlan make_simitone_saved_thread_plan(
    const SavedWorldEntity& entity, const Objt& types) {
    if (entity.saved.object_id == 0 ||
        entity.saved.object_id > static_cast<std::uint16_t>(std::numeric_limits<std::int16_t>::max()))
        throw FormatError("saved thread owner ID is outside the runtime identity range");
    if (entity.common.stack.size() > 1024)
        throw FormatError("saved thread frame count exceeds plan limit");

    SimitoneSavedThreadPlan result{};
    result.entity_id = static_cast<std::int16_t>(entity.saved.object_id);
    for (std::size_t i = 0; i < entity.saved.prefix.temps.size(); ++i)
        result.temp_registers[i] = entity.saved.prefix.temps[i];

    result.frames.reserve(entity.common.stack.size());
    for (const auto& saved : entity.common.stack) {
        if (saved.code_owner_obj_type <= 0)
            throw FormatError("saved thread frame has invalid OBJT code-owner type");
        const auto owner_type = static_cast<std::uint16_t>(saved.code_owner_obj_type);
        const auto* owner = types.entry_for_type(owner_type);
        if (!owner || !owner->guid)
            throw FormatError("saved thread frame code-owner GUID is unavailable");

        SimitoneSavedFramePlan frame{};
        frame.routine_id = static_cast<std::uint16_t>(saved.tree_id);
        frame.instruction_pointer = static_cast<std::uint16_t>(saved.node_id);
        frame.stack_object = saved.stack_object_id;
        frame.saved_primitive_state = saved.primitive_state;
        frame.code_owner_objt_type = owner_type;
        frame.code_owner_guid = owner->guid;
        frame.caller = result.entity_id;
        frame.callee = result.entity_id;
        frame.arguments = saved.parameters;
        if (frame.arguments.size() < 4) frame.arguments.resize(4, 0);
        frame.locals = saved.locals;
        result.frames.push_back(std::move(frame));
    }

    if (entity.tail.person) {
        for (const auto& use : entity.tail.person->object_uses) {
            if (use.stack_length <= 0 ||
                static_cast<std::size_t>(use.stack_length) > result.frames.size() ||
                use.target_id <= 0)
                throw FormatError("saved thread ObjectUses entry does not identify a frame and target");
            const auto first = static_cast<std::size_t>(use.stack_length - 1);
            for (std::size_t i = first; i < result.frames.size(); ++i)
                result.frames[i].callee = use.target_id;
        }
    }
    return result;
}

} // namespace odh
