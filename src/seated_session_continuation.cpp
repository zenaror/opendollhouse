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
bool canonical_relative_path(const std::string& value) {
    if(value.empty()||value.size()>4096||value.front()=='/'||value.back()=='/'||
       value.find('\\')!=std::string::npos||
       value.find(':')!=std::string::npos||value.find('\0')!=std::string::npos)return false;
    std::size_t begin=0;
    while(begin<value.size()){
        const auto end=value.find('/',begin);
        const auto part=value.substr(begin,end==std::string::npos?end:end-begin);
        if(part.empty()||part=="."||part=="..")return false;
        if(end==std::string::npos)break;
        begin=end+1;
    }
    return true;
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
    if(routines.empty()||routines.size()>768)throw FormatError("seated action owner exceeds routine limit");
    Sha256 hash;static constexpr char domain[]="ODH-SEATED-ACTION-CLOSURE-V1";
    hash.update(domain,sizeof(domain));digest_u16(hash,static_cast<std::uint16_t>(routines.size()));
    std::size_t instructions=0;
    for(const auto& item:routines) {
        const auto& key=item.first;const auto& bhav=item.second;
        if((key.scope!=RoutineScope::private_object&&key.scope!=RoutineScope::semiglobal)||
           routine_key(key.id).scope!=key.scope||bhav.version!=0x8002||!bhav.metadata||
           bhav.instructions.size()>32768-instructions)
            throw FormatError("seated action owner is not bounded private/semiglobal BHAV 8002");
        instructions+=bhav.instructions.size();digest_u16(hash,key.id);digest_u16(hash,bhav.version);
        hash.update(bhav.header.data(),bhav.header.size());
        const auto& m=*bhav.metadata;hash.update(&m.type,1);hash.update(&m.arguments,1);
        digest_u16(hash,m.locals);digest_u16(hash,m.tree_version);
        digest_u16(hash,static_cast<std::uint16_t>(bhav.instructions.size()));
        for(const auto& ins:bhav.instructions) {
            digest_u16(hash,ins.opcode);hash.update(&ins.on_true,1);hash.update(&ins.on_false,1);
            hash.update(ins.operands.data(),ins.operands.size());
            const auto called=routine_key(ins.opcode);
            if((called.scope==RoutineScope::private_object||called.scope==RoutineScope::semiglobal)&&
               !routines.count(called))
                throw FormatError("seated action owner has an unresolved private/semiglobal call");
        }
    }
    return hash.finish();
}

std::array<std::uint8_t,32> session_loaded_bank_fingerprint(const RoutineSet& routines) {
    // This is identity, not an executable-closure verifier. Match the native
    // owner constructor's key rules; unused trees must not create new startup
    // failures. The map keyspace bounds routine count, while vector lengths
    // are serialized without narrowing and SHA-256 checks its own length bound.
    static_assert(sizeof(std::size_t)<=sizeof(std::uint64_t),"loaded bank length must fit uint64");
    const bool named=std::any_of(routines.begin(),routines.end(),[](const auto& entry){
        return entry.first.scope==RoutineScope::named_private||entry.first.scope==RoutineScope::named_semiglobal;
    });
    Sha256 hash;static constexpr char legacy_domain[]="ODH-LOADED-CODE-BANK-V1";
    static constexpr char named_domain[]="ODH-LOADED-CODE-BANK-V2";
    const auto length=[&](std::size_t value){
        const auto count=static_cast<std::uint64_t>(value);
        std::array<std::uint8_t,8> bytes{};
        for(unsigned n=0;n<8;++n)bytes[n]=static_cast<std::uint8_t>(count>>(n*8));
        hash.update(bytes.data(),bytes.size());
    };
    if(named)hash.update(named_domain,sizeof(named_domain));
    else hash.update(legacy_domain,sizeof(legacy_domain));
    length(routines.size());
    for(const auto& item:routines){
        const auto& key=item.first;const auto& bhav=item.second;
        if(key.scope==RoutineScope::global||!routine_key_valid(key))
            throw FormatError("loaded owner bank has invalid native owner routine keys");
        if(named){const auto scope=static_cast<std::uint8_t>(key.scope);hash.update(&scope,1);}
        digest_u16(hash,key.id);digest_u16(hash,bhav.version);
        hash.update(bhav.header.data(),bhav.header.size());
        const std::uint8_t metadata_present=bhav.metadata?1:0;hash.update(&metadata_present,1);
        if(bhav.metadata){const auto& m=*bhav.metadata;
            hash.update(&m.type,1);hash.update(&m.arguments,1);digest_u16(hash,m.locals);digest_u16(hash,m.tree_version);}
        length(bhav.instructions.size());
        for(const auto& ins:bhav.instructions){
            digest_u16(hash,ins.opcode);hash.update(&ins.on_true,1);hash.update(&ins.on_false,1);
            hash.update(ins.operands.data(),ins.operands.size());
        }
    }
    return hash.finish();
}

void validate_seated_session_continuation(const SessionBundle& bundle) {
    if(!bundle.seated_continuation)throw FormatError("seated continuation metadata is absent");
    if(!bundle.household_roster.empty()||!bundle.household_seated_continuations.empty())
        throw FormatError("singular seated validator requires the legacy resident profile");
    validate_seated_session_continuation(bundle,*bundle.seated_continuation);
}

const SessionSeatedContinuation* find_seated_session_continuation(const SessionBundle& bundle,std::int16_t id) {
    if(bundle.seated_continuation&&bundle.seated_continuation->actor_id==id)return &*bundle.seated_continuation;
    const auto found=std::find_if(bundle.household_seated_continuations.begin(),bundle.household_seated_continuations.end(),
        [&](const auto& value){return value.actor_id==id;});
    return found==bundle.household_seated_continuations.end()?nullptr:&*found;
}

void validate_generic_session_posture(const SessionBundle& bundle) {
    if(!bundle.generic_execution)throw FormatError("generic posture requires GEXP execution authority");
    const auto validate=[&](const SessionRuntimeEntity& state){
        const auto& actor=entity(bundle,state.id);
        const auto& queue=state.controllers.interactions;
        // This additional posture contract concerns physical containment.
        // Ordinary unbound GEXP actors retain the existing codec's complete
        // execution validation, including minimal source-independent fixtures.
        if(!actor.attachment)return;
        if(actor.kind!=EntityKind::avatar||!actor.generation||!actor.definition||actor.person_data.empty()||
           queue.actor_id!=actor.id||queue.actor_generation!=actor.generation||queue.run_generation!=state.vm.run_generation)
            throw FormatError("generic posture actor/controller identity is stale");
        // Posture is source-writable state. An actual container is independently
        // authoritative during intermediate EP26 instructions as well as after
        // the route returns; no completion record is manufactured here.
        // VMRoutingFrame.STAND_FUNC explicitly accepts Posture.Sitting with
        // no Container and resets it when routing next runs. Preserve that
        // writable intermediate source state rather than inventing a seat.
        const auto& attachment=*actor.attachment;
        const auto& parent=entity(bundle,attachment.container);
        if(parent.id==actor.id||!parent.generation||!parent.definition||!attachment.positioned||
           attachment.slot>=parent.slots.size()||parent.slots[attachment.slot]!=actor.id||
           !actor.position||!parent.position||actor.position->x!=parent.position->x||
           actor.position->y!=parent.position->y||actor.position->level!=parent.position->level)
            throw FormatError("generic posture physical containment is nonreciprocal or stale");
        const auto known_owner=[&](CodeOwner owner){
            if(owner==0||owner==static_cast<CodeOwner>(actor.id)||std::any_of(state.resource_owners.begin(),state.resource_owners.end(),
                [&](const auto& bank){return bank.owner==owner&&bank.resource_guid&&nonzero(bank.routine_fingerprint);}))return true;
            // DYNO retains its existing exact route/resource/VM validation in
            // the bundle codec; it is not a GEXP bank alias. Physical seating
            // must not erase a legitimate already-owned Go Here ancestry.
            if(!state.route&&(!state.route_stack||state.route_stack->jobs.empty()))return false;
            return std::any_of(state.dynamic_action_owners.begin(),state.dynamic_action_owners.end(),[&](const auto& bank){
                const auto target=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
                    [&](const auto& value){return value.id==bank.target_id;});
                return bank.owner==owner&&nonzero(bank.routine_fingerprint)&&target!=bundle.world.entities.end()&&
                    target->generation==bank.target_generation&&target->definition&&target->definition->guid==bank.target_guid;
            });
        };
        for(const auto& frame:state.vm.stack)if(!known_owner(frame.context.code_owner))
            throw FormatError("generic posture frame has no installed resource bank");
        for(const auto& action:queue.actions)if(!known_owner(action.call.context.code_owner))
            throw FormatError("generic posture queued action has no installed resource bank");
        if(state.playback.carry&&!known_owner(state.playback.carry->code_owner))
            throw FormatError("generic posture carry has no installed resource bank");
        const auto route=[&](const auto& job){
            if(!job.external_frame_sitting)return;
            const auto* call=job.external_frame_call?&*job.external_frame_call:nullptr;
            if(!job.external_frame_active||!job.selected_sitting_object||!call||
               call->context.caller!=actor.id||call->context.callee!=*job.selected_sitting_object||
               call->context.stack_object!=*job.selected_sitting_object||!known_owner(call->context.code_owner))
                throw FormatError("generic chair continuation has no exact installed EP26 call");
            const auto& chair=entity(bundle,*job.selected_sitting_object);
            if(!chair.entry_points||chair.entry_points->size()<=26||call->key.id!=chair.entry_points->at(26).action)
                throw FormatError("generic chair continuation differs from actual EP26");
            if(job.source_choices){const auto& selected=job.source_choices->selected;
                if(!selected.chair||selected.chair->id!=chair.id||selected.chair->generation!=chair.generation)
                    throw FormatError("generic chair continuation identity is stale");}
        };
        if(state.route_stack)for(const auto& job:state.route_stack->jobs)route(job);
        // A local route cannot own an external child; its complete validation
        // remains in the ordinary RouteStack/VM codec, including return ABI.
    };
    for(const auto& state:bundle.runtime_entities)validate(state);
    for(const auto& npc:bundle.npc_avatars)validate(npc.runtime);
}

void validate_seated_session_continuations(const SessionBundle& bundle) {
    if(bundle.seated_continuation){
        if(!bundle.household_roster.empty()||!bundle.household_seated_continuations.empty())
            throw FormatError("legacy seated continuation cannot coexist with a household profile");
        validate_seated_session_continuation(bundle);
    }
    if(!bundle.household_seated_continuations.empty()&&bundle.household_roster.empty())
        throw FormatError("household seated continuation requires HOST v2 roster");
    std::int16_t previous=0;
    for(const auto& c:bundle.household_seated_continuations){
        if(c.actor_id<=previous)throw FormatError("household seated actor roster is duplicated or unordered");
        validate_seated_session_continuation(bundle,c);previous=c.actor_id;
    }
    if(bundle.generic_execution){validate_generic_session_posture(bundle);return;}
    if(!bundle.household_roster.empty())for(const auto& state:bundle.runtime_entities){
        const auto& actor=entity(bundle,state.id);
        const bool seated=!actor.person_data.empty()&&actor.person_data[0]==1;
        const bool declared=find_seated_session_continuation(bundle,state.id)!=nullptr;
        if(seated!=declared||(!declared&&actor.attachment))
            throw FormatError("household seated actor coverage is incomplete or stale");
    }
}

void validate_seated_session_continuation(const SessionBundle& bundle,const SessionSeatedContinuation& c) {
    const bool household=!bundle.household_roster.empty();
    const auto member=std::find_if(bundle.household_roster.begin(),bundle.household_roster.end(),
        [&](const auto& value){return value.id==c.actor_id;});
    const auto sit_scope=routine_key(c.action).scope;
    const auto stand_scope=routine_key(c.stand_action).scope;
    const bool private_profile=c.action==4097&&c.stand_action==4102&&
        sit_scope==RoutineScope::private_object&&stand_scope==RoutineScope::private_object&&
        !c.linked_semiglobal;
    const bool linked_profile=sit_scope==RoutineScope::semiglobal&&stand_scope==RoutineScope::semiglobal&&
        c.linked_semiglobal&&valid_text(c.linked_semiglobal->name,false)&&
        c.linked_semiglobal->name.find_first_of("/\\:")==std::string::npos&&
        c.linked_semiglobal->name.find('.')==std::string::npos&&
        canonical_relative_path(c.linked_semiglobal->archive)&&
        c.linked_semiglobal->entry==c.linked_semiglobal->name+".iff"&&
        c.linked_semiglobal->entry.find('/')==std::string::npos;
    if(c.actor_id<=0||(!household&&c.actor_id!=bundle.host.runtime_avatar_id)||
       (household&&(member==bundle.household_roster.end()||member->generation!=c.actor_generation))||
       c.target_id<=0||c.actor_id==c.target_id||
       !c.actor_generation||!c.target_generation||!c.target_guid||!c.tree_table_id||
       (!private_profile&&!linked_profile)||c.stand_entry_point!=27||
       !valid_text(c.source,false)||!valid_text(c.asset,true)||
       !nonzero(c.sit_routine_fingerprint)||!nonzero(c.stand_routine_fingerprint)||
       c.code_owners.size()>1||!c.completion.uid||!c.completion.ticket||!c.completion.result||
       c.completion.tick>=bundle.host.tick||(!household&&bundle.runtime_entities.size()!=1)||
       (household&&(!bundle.generic_execution||!c.code_owners.empty()))||bundle.scheduler.faulted)
        throw FormatError("unsupported seated continuation identity or boundary");
    const auto& actor=entity(bundle,c.actor_id);const auto& target=entity(bundle,c.target_id);
    if(actor.kind!=EntityKind::avatar||actor.generation!=c.actor_generation||!actor.definition||
       actor.definition->guid!=(household?member->guid:bundle.host.resident_guid)||actor.person_data.empty()||actor.person_data[0]!=1||
       !actor.attachment||actor.attachment->container!=c.target_id||actor.attachment->slot!=c.target_slot||
       !actor.attachment->positioned||!actor.position||!actor.static_footprint||actor.ui_ghost||
       target.kind!=EntityKind::object||target.generation!=c.target_generation||!target.definition||
       target.definition->guid!=c.target_guid||c.target_slot>=target.slots.size()||
       target.slots[c.target_slot]!=c.actor_id||!target.position||target.attachment||
       target.position->x!=actor.position->x||target.position->y!=actor.position->y||
       target.position->level!=actor.position->level||!target.entry_points||target.entry_points->size()<=27||
       (*target.entry_points)[27].condition!=c.stand_condition||(*target.entry_points)[27].action!=c.stand_action)
        throw FormatError("seated continuation posture, target or reciprocal slot is stale");
    const auto found=std::find_if(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
        [&](const auto& value){return value.id==c.actor_id;});
    if(found==bundle.runtime_entities.end())throw FormatError("seated resident has no runtime execution state");
    const auto& state=*found;const auto& queue=state.controllers.interactions;
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
    std::set<CodeOwner> declared;
    for(const auto& binding:c.code_owners) {
        if(binding.owner<0x80000000U||binding.source!=c.source||binding.asset!=c.asset||
           binding.root_routine!=c.action||binding.routine_fingerprint!=c.sit_routine_fingerprint||
           !declared.insert(binding.owner).second)
            throw FormatError("seated continuation code owner has mismatched resource provenance");
    }
    // Household GEXP already carries exact full loaded banks. SEAT separately
    // proves Sit/Stand closures without reinstalling a smaller legacy closure.
    std::set<CodeOwner> resource_declared;
    if(household)for(const auto& binding:state.resource_owners){
        if(binding.resource_guid==c.target_guid&&binding.source==c.source&&binding.asset==c.asset&&
           binding.definition_chunk_id==c.definition_chunk_id&&binding.tree_table_id==c.tree_table_id&&
           binding.linked_semiglobal==c.linked_semiglobal)
            resource_declared.insert(binding.owner);
    }
    const auto allowed_owner=[&](CodeOwner value){
        return value==static_cast<CodeOwner>(c.actor_id)||declared.count(value)!=0||resource_declared.count(value)!=0;
    };
    const bool main_animation_wait=state.playback.state&&state.playback.state->wait;
    if(main_animation_wait) {
        if(!state.vm.pending||state.vm.pending->instruction.instruction.opcode!=44||
           state.vm.pending->context.caller!=c.actor_id||
           !allowed_owner(state.vm.pending->context.code_owner)||
           state.playback.state->resource.code_owner!=state.vm.pending->context.code_owner)
            throw FormatError("seated continuation animation wait is not owned by the declared Sit resource");
        if(state.vm.pending->context.code_owner!=static_cast<CodeOwner>(c.actor_id)&&
           ((state.vm.pending->context.callee!=c.actor_id&&state.vm.pending->context.callee!=c.target_id)||
            state.vm.pending->context.stack_object!=c.target_id))
            throw FormatError("seated continuation animation wait has a foreign target context");
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
    if(state.vm.stack.empty()||state.vm.stack.front().context.code_owner!=static_cast<CodeOwner>(c.actor_id))
        throw FormatError("seated continuation root frame is not the resident Main");
    const auto context_matches_owner=[&](const VmContext& context){
        if(context.caller!=c.actor_id||!allowed_owner(context.code_owner))return false;
        if(context.code_owner!=static_cast<CodeOwner>(c.actor_id))
            return (context.callee==c.actor_id||context.callee==c.target_id)&&
                context.stack_object==c.target_id;
        return true;
    };
    for(const auto& frame:state.vm.stack) {
        if(frame.return_mode!=VmCallReturnMode::ordinary||!context_matches_owner(frame.context))
            throw FormatError("seated continuation VM is still inside an action or foreign frame");
        owner(frame.context.code_owner);
    }
    const auto pending=[&](const VmPending& value,bool animation){
        if(!context_matches_owner(value.context)||!value.instruction.depth||
           value.instruction.depth>state.vm.stack.size()||
           (value.instruction.instruction.opcode!=0&&value.instruction.instruction.opcode!=17&&
            !(animation&&value.instruction.instruction.opcode==44)))
            throw FormatError("seated continuation has a pending action, route or foreign animation");
        const auto& frame=state.vm.stack[value.instruction.depth-1];
        if(!vm_context_equal(frame.context,value.context))
            throw FormatError("seated continuation pending instruction does not match its owning frame");
        if(value.context.code_owner!=static_cast<CodeOwner>(c.actor_id)&&
           (!animation||value.instruction.instruction.opcode!=44))
            throw FormatError("seated continuation Sit owner is not waiting on its matching animation");
        owner(value.context.code_owner);
    };
    if(state.vm.pending)pending(*state.vm.pending,main_animation_wait);
    for(const auto& value:state.vm.suspended_pending)pending(value,false);
    if(state.playback.state)owner(state.playback.state->resource.code_owner);
    if(state.playback.carry)owner(state.playback.carry->code_owner);
    for(const auto owner:resource_declared)referenced.erase(owner);
    if(referenced!=declared)throw FormatError("seated continuation dynamic owner roster is incomplete or excessive");
}

RoutineOwners resolve_seated_session_owners(const SessionBundle& bundle,
    const DefinitionCandidate& candidate,const std::vector<EntryPoint>& entry_points,
    const SessionActionRoutineLoader& load) {
    validate_seated_session_continuation(bundle);
    return resolve_seated_session_owners(bundle,*bundle.seated_continuation,candidate,entry_points,load);
}

RoutineOwners resolve_seated_session_owners(const SessionBundle& bundle,const SessionSeatedContinuation& c,
    const DefinitionCandidate& candidate,const std::vector<EntryPoint>& entry_points,
    const SessionActionRoutineLoader& load) {
    validate_seated_session_continuation(bundle,c);
    if(!load||candidate.source!=c.source||candidate.asset!=c.asset||candidate.chunk_id!=c.definition_chunk_id||
       candidate.definition.guid!=c.target_guid||candidate.definition.tree_table_id!=c.tree_table_id||
       entry_points.size()<=27||entry_points[27].condition!=c.stand_condition||entry_points[27].action!=c.stand_action)
        throw FormatError("seated continuation exact DefinitionCandidate or Stand EntryPoint changed");
    auto sit=load(candidate,c.action);auto stand=load(candidate,c.stand_action);
    if(sit.linked_semiglobal!=c.linked_semiglobal||stand.linked_semiglobal!=c.linked_semiglobal||
       !sit.routines.count(routine_key(c.action))||!stand.routines.count(routine_key(c.stand_action))||
       session_action_routine_fingerprint(sit.routines)!=c.sit_routine_fingerprint||
       session_action_routine_fingerprint(stand.routines)!=c.stand_routine_fingerprint)
        throw FormatError("seated continuation exact Sit or Stand routine identity changed");
    RoutineOwners result;
    for(const auto& binding:c.code_owners)result.emplace(binding.owner,sit.routines);
    return result;
}
} // namespace odh
