#include "odh/seated_session_continuation.hpp"
#include "odh/sha256.hpp"

#include <algorithm>
#include <set>

namespace odh {
namespace {
bool nonzero(const std::array<std::uint8_t,32>& value) {
    return std::any_of(value.begin(),value.end(),[](auto byte){return byte!=0;});
}
bool valid_text(const std::string& value,bool allow_empty) {
    return (allow_empty||!value.empty())&&value.size()<=4096&&value.find('\0')==std::string::npos;
}
const Entity& entity(const SessionBundle& bundle,std::int16_t id) {
    const auto found=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
        [&](const auto& value){return value.id==id;});
    if(found==bundle.world.entities.end())throw FormatError("seated continuation entity is absent");
    return *found;
}
void digest_u16(Sha256& hash,std::uint16_t value) {
    const std::array<std::uint8_t,2> bytes{{static_cast<std::uint8_t>(value),static_cast<std::uint8_t>(value>>8)}};
    hash.update(bytes.data(),bytes.size());
}
}

std::array<std::uint8_t,32> session_action_routine_fingerprint(const RoutineSet& routines) {
    if(routines.empty()||routines.size()>256)throw FormatError("seated action closure exceeds routine limit");
    Sha256 hash;static constexpr char domain[]="ODH-SEATED-ACTION-CLOSURE-V1";
    hash.update(domain,sizeof(domain));digest_u16(hash,static_cast<std::uint16_t>(routines.size()));
    std::size_t instructions=0;
    for(const auto& item:routines) {
        const auto& key=item.first;const auto& bhav=item.second;
        if(key.scope!=RoutineScope::private_object||routine_key(key.id).scope!=key.scope||
           bhav.version!=0x8002||!bhav.metadata||bhav.instructions.size()>16384-instructions)
            throw FormatError("seated action closure is not bounded private BHAV 8002");
        instructions+=bhav.instructions.size();digest_u16(hash,key.id);digest_u16(hash,bhav.version);
        hash.update(bhav.header.data(),bhav.header.size());
        const auto& m=*bhav.metadata;hash.update(&m.type,1);hash.update(&m.arguments,1);
        digest_u16(hash,m.locals);digest_u16(hash,m.tree_version);
        digest_u16(hash,static_cast<std::uint16_t>(bhav.instructions.size()));
        for(const auto& ins:bhav.instructions) {
            digest_u16(hash,ins.opcode);hash.update(&ins.on_true,1);hash.update(&ins.on_false,1);
            hash.update(ins.operands.data(),ins.operands.size());
            if(ins.opcode>=4096&&ins.opcode<8192&&!routines.count(routine_key(ins.opcode)))
                throw FormatError("seated action closure has an unresolved private call");
        }
    }
    return hash.finish();
}

void validate_seated_session_continuation(const SessionBundle& bundle) {
    if(!bundle.seated_continuation)throw FormatError("seated continuation metadata is absent");
    const auto& c=*bundle.seated_continuation;
    if(c.actor_id<=0||c.actor_id!=bundle.host.runtime_avatar_id||c.target_id<=0||c.actor_id==c.target_id||
       !c.actor_generation||!c.target_generation||!c.target_guid||!c.tree_table_id||
       c.action!=4097||c.stand_entry_point!=27||c.stand_action!=4102||
       !valid_text(c.source,false)||!valid_text(c.asset,true)||
       !nonzero(c.sit_routine_fingerprint)||!nonzero(c.stand_routine_fingerprint)||
       c.code_owners.size()>1||!c.completion.uid||!c.completion.ticket||!c.completion.result||
       c.completion.tick>=bundle.host.tick||bundle.runtime_entities.size()!=1||bundle.scheduler.faulted)
        throw FormatError("unsupported seated continuation identity or boundary");
    const auto& actor=entity(bundle,c.actor_id);const auto& target=entity(bundle,c.target_id);
    if(actor.kind!=EntityKind::avatar||actor.generation!=c.actor_generation||!actor.definition||
       actor.definition->guid!=bundle.host.resident_guid||actor.person_data.empty()||actor.person_data[0]!=1||
       !actor.attachment||actor.attachment->container!=c.target_id||actor.attachment->slot!=c.target_slot||
       !actor.attachment->positioned||!actor.position||!actor.static_footprint||actor.ui_ghost||
       target.kind!=EntityKind::object||target.generation!=c.target_generation||!target.definition||
       target.definition->guid!=c.target_guid||c.target_slot>=target.slots.size()||
       target.slots[c.target_slot]!=c.actor_id||!target.position||target.attachment||
       target.position->x!=actor.position->x||target.position->y!=actor.position->y||
       target.position->level!=actor.position->level||!target.entry_points||target.entry_points->size()<=27||
       (*target.entry_points)[27].condition!=c.stand_condition||(*target.entry_points)[27].action!=c.stand_action)
        throw FormatError("seated continuation posture, target or reciprocal slot is stale");
    const auto& state=bundle.runtime_entities.front();const auto& queue=state.controllers.interactions;
    if(state.id!=c.actor_id||queue.actor_id!=c.actor_id||queue.actor_generation!=c.actor_generation||
       queue.run_generation!=state.vm.run_generation)
        throw FormatError("seated continuation controller actor/run identity is stale");
    if(state.vm.status==VmStatus::fault||state.vm.status==VmStatus::dormant||state.vm.status==VmStatus::ready)
        throw FormatError("seated continuation VM is faulted, dormant or not started");
    if(!queue.actions.empty()||queue.active)
        throw FormatError("seated continuation interaction queue still has an action");
    if(queue.resume_tick||state.vm.interaction_return)
        throw FormatError("seated continuation Sit has not crossed its next-tick completion boundary");
    if(queue.interrupted||!queue.diagnostic.empty())
        throw FormatError("seated continuation interaction queue has an interrupt or diagnostic");
    if(!queue.completion||queue.completion->uid!=c.completion.uid||queue.completion->ticket!=c.completion.ticket||
       queue.completion->tick!=c.completion.tick||queue.completion->result!=c.completion.result)
        throw FormatError("seated continuation completion differs from the idle queue");
    if(state.vm.external_frame_return)
        throw FormatError("seated continuation has an external frame return");
    const bool main_animation_wait=state.playback.state&&state.playback.state->wait;
    if(main_animation_wait) {
        if(!state.vm.pending||state.vm.pending->instruction.instruction.opcode!=44||
           state.vm.pending->context.caller!=c.actor_id||
           state.vm.pending->context.code_owner!=static_cast<CodeOwner>(c.actor_id)||
           state.playback.state->resource.code_owner!=static_cast<CodeOwner>(c.actor_id))
            throw FormatError("seated continuation animation wait is not owned by the resident Main");
        // Reuse the complete Playback/VM contract, including ticket, resource
        // context, VM/frame generations, opcode operands, frame output bank,
        // event index/expected value and blocking animation mode.
        validate_session_playback_links(state,actor,bundle.host);
    }
    // At this boundary Main may be sleeping or IdleForInput, never inside an
    // object action, route or portal. A current Main-owned animation wait is
    // permitted only after the completed Sit has left the interaction stack.
    std::set<CodeOwner> referenced;
    const auto owner=[&](CodeOwner value){if(value!=static_cast<CodeOwner>(c.actor_id))referenced.insert(value);};
    for(const auto& frame:state.vm.stack) {
        if(frame.return_mode!=VmCallReturnMode::ordinary||frame.context.caller!=c.actor_id||
           frame.context.code_owner!=static_cast<CodeOwner>(c.actor_id))
            throw FormatError("seated continuation VM is still inside an action or external frame");
    }
    const auto pending=[&](const VmPending& value,bool animation){
        if(value.context.caller!=c.actor_id||value.context.code_owner!=static_cast<CodeOwner>(c.actor_id)||
           (value.instruction.instruction.opcode!=0&&value.instruction.instruction.opcode!=17&&
            !(animation&&value.instruction.instruction.opcode==44)))
            throw FormatError("seated continuation has a pending action, route or animation");
    };
    if(state.vm.pending)pending(*state.vm.pending,main_animation_wait);
    for(const auto& value:state.vm.suspended_pending)pending(value,false);
    if(state.playback.state)owner(state.playback.state->resource.code_owner);
    if(state.playback.carry)owner(state.playback.carry->code_owner);
    std::set<CodeOwner> declared;
    for(const auto& binding:c.code_owners) {
        if(binding.owner<0x80000000U||binding.source!=c.source||binding.asset!=c.asset||
           binding.root_routine!=c.action||binding.routine_fingerprint!=c.sit_routine_fingerprint||
           !declared.insert(binding.owner).second)
            throw FormatError("seated continuation code owner has mismatched resource provenance");
    }
    if(referenced!=declared)throw FormatError("seated continuation dynamic owner roster is incomplete or excessive");
}

RoutineOwners resolve_seated_session_owners(const SessionBundle& bundle,
    const DefinitionCandidate& candidate,const std::vector<EntryPoint>& entry_points,
    const SessionActionRoutineLoader& load) {
    validate_seated_session_continuation(bundle);const auto& c=*bundle.seated_continuation;
    if(!load||candidate.source!=c.source||candidate.asset!=c.asset||candidate.chunk_id!=c.definition_chunk_id||
       candidate.definition.guid!=c.target_guid||candidate.definition.tree_table_id!=c.tree_table_id||
       entry_points.size()<=27||entry_points[27].condition!=c.stand_condition||entry_points[27].action!=c.stand_action)
        throw FormatError("seated continuation exact DefinitionCandidate or Stand EntryPoint changed");
    auto sit=load(candidate,c.action);auto stand=load(candidate,c.stand_action);
    if(!sit.count(routine_key(c.action))||!stand.count(routine_key(c.stand_action))||
       session_action_routine_fingerprint(sit)!=c.sit_routine_fingerprint||
       session_action_routine_fingerprint(stand)!=c.stand_routine_fingerprint)
        throw FormatError("seated continuation exact Sit or Stand routine identity changed");
    RoutineOwners result;
    for(const auto& binding:c.code_owners)result.emplace(binding.owner,sit);
    return result;
}
} // namespace odh
