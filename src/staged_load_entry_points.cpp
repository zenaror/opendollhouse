#include "odh/staged_load_entry_points.hpp"
#include "odh/entity_next.hpp"
#include "odh/build_buy_permission.hpp"
#include <algorithm>
#include <set>

namespace odh {

StagedLoadEntryPointResult stage_saved_load_entry_points(
    const WorldSnapshot& source, const std::vector<std::int16_t>& entity_order,
    const std::map<std::int16_t, CodeOwner>& code_owners,
    const std::map<std::int16_t, bool>& ghost_images,
    const std::map<std::int16_t, std::array<std::int16_t, 20>>& temporary_registers,
    const RoutineSet& globals, const RoutineOwners& owners,
    std::uint64_t instruction_budget, WorldLimits world_limits,
    VmMemory supplemental_memory, VmPrimitive supplemental_primitive,
    SoundResolver sound_resolver, const GeometricRoomMap* room_map,
    std::optional<std::uint64_t> random_seed,bool initial_build_buy_enabled) {
    StagedLoadEntryPointResult result;
    std::vector<SoundRequest> pending_sound_events;
    std::size_t unresolved_sound_events=0;
    std::size_t cancelled_sound_events=0;
    std::map<std::uint16_t,std::int16_t> pending_room_scores;
    std::size_t pending_room_score_refreshes=0;
    std::optional<RandomState> staged_random;
    if(random_seed)staged_random.emplace(*random_seed);
    BuildBuyPermission build_buy;
    build_buy.restore({initial_build_buy_enabled});
    std::vector<bool> pending_build_buy_events;
    auto reject = [&](std::string reason, std::int16_t entity = 0,
                      std::uint16_t action = 0, VmStatus status = VmStatus::ready) {
        result.snapshot.reset();
        result.build_buy_enabled.reset();
        result.deferred_build_buy_events.clear();
        result.discarded_sound_events=pending_sound_events.size();
        pending_sound_events.clear();
        result.unresolved_sound_events=unresolved_sound_events;
        result.discarded_room_score_refreshes=pending_room_score_refreshes;
        pending_room_scores.clear();
        result.failed_entity = entity;
        result.failed_action = action;
        result.vm_status = status;
        result.reason = std::move(reason);
        return result;
    };

    if (entity_order.size() != source.entities.size())
        return reject("entity order does not cover the complete World");
    if (ghost_images.size() != source.entities.size())
        return reject("GhostImage state is not known for every entity");
    if (temporary_registers.size() != source.entities.size())
        return reject("saved temporary registers are not known for every entity");
    std::set<std::int16_t> seen;
    for (const auto id : entity_order) {
        if (!seen.insert(id).second) return reject("entity order contains a duplicate ID", id);
        const auto found = std::find_if(source.entities.begin(), source.entities.end(),
            [id](const Entity& entity) { return entity.id == id; });
        if (found == source.entities.end()) return reject("entity order contains an unknown ID", id);
        if (!ghost_images.count(id)) return reject("GhostImage state is unknown for entity", id);
        if (!temporary_registers.count(id)) return reject("saved temporary registers are unknown for entity", id);
    }

    World candidate(world_limits, source.attribute_policy);
    if (!candidate.restore_snapshot(source)) return reject("source World exceeds candidate limits");
    const auto memory_for = [&supplemental_memory](World& world) {
        auto memory = world.vm_memory();
        // The provider reads immutable Work.iff only; the staged VM supplies
        // its own exact per-entity Temp0/Temp1 through the typed callback.
        memory.job_data=supplemental_memory.job_data;
        if (supplemental_memory.read) {
            const auto world_read = memory.read;
            const auto extra_read = supplemental_memory.read;
            memory.read = [world_read, extra_read](const VmFrame& frame, std::uint8_t scope,
                                                  std::int16_t index) {
                if (const auto value = extra_read(frame, scope, index)) return value;
                return world_read ? world_read(frame, scope, index) : std::optional<std::int16_t>{};
            };
        }
        if (supplemental_memory.write) {
            const auto world_write = memory.write;
            const auto extra_write = supplemental_memory.write;
            memory.write = [world_write, extra_write](const VmFrame& frame, std::uint8_t scope,
                std::int16_t index, std::int16_t value) {
                return extra_write(frame, scope, index, value) ||
                       (world_write && world_write(frame, scope, index, value));
            };
        }
        if (supplemental_memory.room_by_temp0) {
            const auto world_room = memory.room_by_temp0;
            const auto extra_room = supplemental_memory.room_by_temp0;
            memory.room_by_temp0 = [world_room, extra_room](const VmFrame& frame,
                std::int16_t temp0, std::int16_t index) {
                if (const auto value = extra_room(frame, temp0, index)) return value;
                return world_room ? world_room(frame, temp0, index) : std::optional<std::int16_t>{};
            };
        }
        return memory;
    };
    // Profile only the Load-time services whose behavior is established by
    // pinned Simitone restore path. LoadingGameScreen uses InitVMConfig(true):
    // Generic1 is registered and must never be blanket-approved. Opcode10 is
    // unregistered; VMThread.ExecuteInstruction explicitly branches true for a
    // null registration. This proves reference control flow, not tutorial UI.
    // Generic EntryPoint dispatch supplies a null
    // StackObject, so NotifyOutOfIdle (49) has no thread to interrupt and
    // returns true. Refresh(Caller, graphic) only invalidates a
    // render cache, which this unpublished World candidate does not own.
    // Area-contribution refresh uses the resolved room map and live RoomImpact
    // values. Other Refresh variants and unknown operations still fail closed.
    const VmPrimitive load_profile_primitive = [](const VmFrame&, const Instruction& instruction,
        const std::array<std::int16_t,20>&, const VmDispatch&) {
        if(instruction.opcode==10)
            return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
        if(instruction.opcode==49)
            return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
        if(instruction.opcode==7&&instruction.operands[0]==0&&instruction.operands[1]==0&&
           instruction.operands[2]==0&&instruction.operands[3]==0)
            return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
        return PrimitiveOutcome{};
    };
    const auto primitive_for = [&](World& world,BuildBuyPermission& permission,
                                   std::vector<bool>& permission_events) {
        const auto permission_handler=build_buy_permission_primitive(permission,
            [&permission_events](bool enabled){permission_events.push_back(enabled);});
        VmPrimitive random_handler;
        if(staged_random)random_handler=random_primitive(*staged_random,memory_for(world));
        const auto sound_handler=sound_primitive(world,sound_resolver,{},
            [&pending_sound_events,&unresolved_sound_events](const SoundRequest& request,
                                                             const std::string& status){
                if(status=="skipped_no_backend")pending_sound_events.push_back(request);
                else if(status=="missing_resource"||status=="missing_owner")++unresolved_sound_events;
            });
        const auto entity_next_handler=entity_next_primitive(world);
        return VmPrimitive{[load_profile_primitive,supplemental_primitive,random_handler,sound_handler,
                            entity_next_handler,permission_handler,room_map,&pending_room_scores,
                            &pending_room_score_refreshes,&pending_sound_events,
                            &cancelled_sound_events,&world](
            const VmFrame& frame,const Instruction& instruction,const std::array<std::int16_t,20>& temps,
            const VmDispatch& dispatch) {
            if(instruction.opcode==1){
                // Only the implemented shared TS1 architecture modes are
                // available here. Unknown Generic1 modes keep their effects
                // explicitly unsupported, even with a supplemental RNG handler.
                return permission_handler(frame,instruction,temps,dispatch);
            }
            if(instruction.opcode==8&&random_handler)
                return random_handler(frame,instruction,temps,dispatch);
            if(instruction.opcode==8&&supplemental_primitive)
                return supplemental_primitive(frame,instruction,temps,dispatch);
            if(instruction.opcode==23)return sound_handler(frame,instruction,temps,dispatch);
            if(instruction.opcode==48){
                const auto source=instruction.operands[0]==1?frame.context.stack_object:frame.context.caller;
                const auto first=pending_sound_events.size();
                pending_sound_events.erase(std::remove_if(pending_sound_events.begin(),pending_sound_events.end(),
                    [source](const SoundRequest& request){return request.source==source;}),pending_sound_events.end());
                cancelled_sound_events+=first-pending_sound_events.size();
                return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            }
            if(instruction.opcode==31&&(instruction.operands[4]&127U)==4U)
                return entity_next_handler(frame,instruction,temps,dispatch);
            if(instruction.opcode==7&&instruction.operands[2]==2&&instruction.operands[3]==0){
                if(instruction.operands[0]>1||instruction.operands[1]!=0)
                    return PrimitiveOutcome{PrimitiveDisposition::unsupported,"Refresh area target is invalid"};
                if(!room_map)
                    return PrimitiveOutcome{PrimitiveDisposition::unsupported,"Refresh area requires a resolved room map"};
                const auto target=instruction.operands[0]==0?frame.context.caller:frame.context.stack_object;
                const auto* target_entity=world.find(target);
                if(!target_entity)
                    return PrimitiveOutcome{PrimitiveDisposition::unsupported,"Refresh area target entity is absent"};
                if(!target_entity->position)
                    return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
                const auto snapshot=world.snapshot();
                std::vector<RoomScoreContribution> contributions;
                contributions.reserve(snapshot.entities.size());
                for(const auto& entity:snapshot.entities){
                    if(!entity.position)continue;
                    const auto impact=entity.object_words.find(7);
                    contributions.push_back({entity.position->x,entity.position->y,entity.position->level,
                        impact==entity.object_words.end()?std::int16_t{0}:impact->second});
                }
                const auto update=calculate_room_score(*room_map,contributions,
                    target_entity->position->x,target_entity->position->y,target_entity->position->level);
                if(!update)
                    return PrimitiveOutcome{PrimitiveDisposition::unsupported,"Refresh area room score inputs are unresolved"};
                if(update->updated){
                    pending_room_scores[update->light_base_room]=update->score;
                    ++pending_room_score_refreshes;
                }
                return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            }
            return load_profile_primitive(frame,instruction,temps,dispatch);
        }};
    };
    const auto candidate_primitive=primitive_for(candidate,build_buy,pending_build_buy_events);
    Vm vm(globals, memory_for(candidate), {}, candidate_primitive, owners);

    for (const auto id : entity_order) {
        const auto* entity = candidate.find(id);
        if (!entity || !entity->entry_points)
            return reject("entity has no resolved EntryPoint table", id);
        if (entity->entry_points->size() <= 2)
            return reject("entity EntryPoint table has no Load slot", id);
        const auto action = entity->entry_points->at(2).action;
        if (action <= 255) {
            result.completed_entities.push_back(id);
            continue;
        }
        const auto owner = code_owners.find(id);
        if (owner == code_owners.end() || owner->second == 0 || !owners.count(owner->second))
            return reject("executable Load action has no exact resource CodeOwner", id, action);
        const auto root_key = routine_key(action);
        const auto& root_bank = root_key.scope == RoutineScope::global
            ? globals : owners.at(owner->second);
        if (!root_bank.count(root_key)) {
            // Simitone's generic immediate EntryPoint dispatch returns false
            // when GetRoutineWithOwner cannot resolve the root action.
            result.missing_root_actions.emplace_back(id, action);
            result.completed_entities.push_back(id);
            continue;
        }
        if (result.instructions >= instruction_budget)
            return reject("aggregate instruction budget exhausted", id, action,
                          VmStatus::budget_exhausted);

        const auto remaining = instruction_budget - result.instructions;
        VmStatus status;
        std::string diagnostic;
        std::optional<VmTrace> last_instruction;
        const auto trace=[&last_instruction](const VmTrace& event){last_instruction=event;};
        if (ghost_images.at(id)) {
            World sandbox(world_limits, source.attribute_policy);
            if (!sandbox.restore_snapshot(candidate.snapshot()))
                return reject("cannot create isolated GhostImage sandbox", id, action);
            auto sandbox_permission=build_buy;
            std::vector<bool> discarded_permission_events;
            const auto sandbox_primitive=primitive_for(sandbox,sandbox_permission,discarded_permission_events);
            Vm sandbox_vm(globals, memory_for(sandbox), {}, sandbox_primitive, owners);
            const VmContext context{id, id, 0, owner->second};
            const auto started = sandbox_vm.start(routine_key(action), context, {0, 0, 0, 0},true);
            if (started == VmStatus::running) sandbox_vm.temps() = temporary_registers.at(id);
            status = started == VmStatus::running ? sandbox_vm.run(remaining,trace) : started;
            result.instructions += sandbox_vm.steps();
            diagnostic = sandbox_vm.diagnostic();
        } else {
            const VmContext context{id, id, 0, owner->second};
            const auto started = vm.start(routine_key(action), context, {0, 0, 0, 0},true);
            if (started == VmStatus::running) vm.temps() = temporary_registers.at(id);
            status = started == VmStatus::running ? vm.run(remaining,trace) : started;
            result.instructions += vm.steps();
            diagnostic = vm.diagnostic();
        }
        if (status != VmStatus::returned_true && status != VmStatus::returned_false){
            if(last_instruction){
                result.failed_routine=last_instruction->key.id;
                result.failed_pc=last_instruction->pc;
                result.failed_opcode=last_instruction->instruction.opcode;
            }
            return reject("Load action did not complete synchronously: " + diagnostic,
                          id, action, status);
        }
        result.completed_entities.push_back(id);
    }

    result.snapshot = candidate.snapshot();
    result.deferred_sound_events=std::move(pending_sound_events);
    result.unresolved_sound_events=unresolved_sound_events;
    result.cancelled_sound_events=cancelled_sound_events;
    result.room_scores=std::move(pending_room_scores);
    if(staged_random)result.random_seed=staged_random->seed();
    result.build_buy_enabled=build_buy.enabled();
    result.deferred_build_buy_events=std::move(pending_build_buy_events);
    result.reason = "all Load actions completed synchronously in the unpublished candidate";
    return result;
}

} // namespace odh
