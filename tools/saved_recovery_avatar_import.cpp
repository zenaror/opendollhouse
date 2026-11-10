// SPDX-License-Identifier: MPL-2.0
#include "saved_recovery_avatar_import.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace odh_host {
using namespace odh;
namespace {
bool fail(std::string& error,const char* reason){error=reason;return false;}
bool no_async(const VmSnapshot& s){return !s.pending&&s.suspended_pending.empty()&&!s.interaction_return&&
    !s.external_frame_return&&s.immediate_continuations.empty()&&!s.immediate_return;}
bool equal_frames(const std::vector<VmFrame>& a,const std::vector<VmFrame>& b){
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),vm_frame_equal);
}
std::int16_t signed_word(std::int32_t value){const auto bits=static_cast<std::uint16_t>(value);
    return static_cast<std::int16_t>(bits<32768?std::int32_t(bits):std::int32_t(bits)-65536);}
}
SavedRecoveryAvatarImport::SavedRecoveryAvatarImport(SavedRecoveryAvatarImportContext context):context_(std::move(context)){}
bool SavedRecoveryAvatarImport::current(const Prepared& p,std::string& error) const {
    if(!context_.source_still_matches||!context_.source_still_matches()||
       context_.will_reset_before_dispatch(p.binding.person)!=p.reset_before_dispatch)return fail(error,"avatar original import classification/authority expired");
    const auto* actor=context_.world->find(p.binding.person.id);
    if(!actor||actor->generation!=p.binding.person.generation||actor->dead||actor->ui_ghost||actor->kind!=EntityKind::avatar||
       p.binding.thread->dispatch_active()||!p.binding.queue->bound_to(*p.binding.thread)||
       !p.binding.controllers->bound_to(*p.binding.thread)||&p.binding.controllers->interactions()!=p.binding.queue)
        return fail(error,"avatar import lost its actual World/thread/queue identity");
    const auto now=p.binding.thread->snapshot();
    if(now.run_generation!=p.before.run_generation||now.steps!=p.before.steps||now.ticket_sequence!=p.before.ticket_sequence||
       now.status!=p.before.status||now.temps!=p.before.temps||now.temp_xl!=p.before.temp_xl||
       !no_async(now)||!equal_frames(now.stack,p.before.stack))return fail(error,"avatar execution changed after import preparation");
    for(const auto& [owner,program]:p.owners)
        if(p.binding.thread->shared_program_for_owner(owner)!=program)return fail(error,"avatar frozen resource owner changed before publication");
    const auto exact=[&](WorldEntityReference ref){
        if(!ref.id)return ref.generation==0;
        const auto source=context_.original_reference(ref.id);
        return source&&*source==ref&&context_.world->resolve_reference(ref);
    };
    for(const auto& frame:p.execution.stack)
        for(const auto role:{VmEntityRole::caller,VmEntityRole::callee,VmEntityRole::stack_object})
            if(!exact(vm_context_reference(frame.context,role)))return fail(error,"avatar saved frame lost its exact original body reference");
    for(const auto& frame:p.execution.stack)if(frame.original_import)
        for(const auto& use:frame.original_import->uses)if(!exact(use.target))return fail(error,"avatar original use metadata lost its exact original body");
    for(const auto& action:p.queue.actions)
        if(!exact({action.call.context.callee,action.target_generation})||
           (action.icon_owner&&!exact({action.icon_owner->id,action.icon_owner->generation})))
            return fail(error,"avatar saved action lost its exact original body reference");
    const auto controls=p.binding.controllers->snapshot();
    const auto& q=controls.interactions;
    if(controls.thread_interrupt||controls.sleep.job||q.sequence||!q.actions.empty()||q.active||q.wait||q.completion||
       q.resume_tick||q.interrupted||!q.suspended_active.empty()||q.actor_id!=p.binding.person.id||q.actor_generation!=p.binding.person.generation)
        return fail(error,"avatar source import requires its untouched fresh controller queue");
    const auto& prior=p.controllers_before;const auto& oldq=prior.interactions;
    const auto& sleep=controls.sleep;const auto& olds=prior.sleep;
    const bool same_actor=bool(sleep.actor)==bool(olds.actor)&&(!sleep.actor||
        (sleep.actor->id==olds.actor->id&&sleep.actor->generation==olds.actor->generation&&
         sleep.actor->run_generation==olds.actor->run_generation));
    // No source invocation may own a wait. Freeze all remaining controller
    // preimage, including no-dispatch clock changes, rather than roll the queue
    // back to preparation time while leaving Sleep at a newer lot tick.
    if(!same_actor||sleep.job||olds.job||sleep.clock!=olds.clock||sleep.last_tick!=olds.last_tick||
       sleep.interrupted!=olds.interrupted||sleep.notify_resume!=olds.notify_resume||sleep.diagnostic!=olds.diagnostic||
       controls.thread_interrupt!=prior.thread_interrupt||controls.schedule_idle_start!=prior.schedule_idle_start||
       q.actor_id!=oldq.actor_id||q.actor_generation!=oldq.actor_generation||q.run_generation!=oldq.run_generation||
       q.capacity!=oldq.capacity||q.sequence!=oldq.sequence||q.clock!=oldq.clock||q.idle_start!=oldq.idle_start||
       q.last_tick!=oldq.last_tick||q.resume_tick!=oldq.resume_tick||q.interrupted!=oldq.interrupted||
       q.diagnostic!=oldq.diagnostic||q.profile!=oldq.profile||q.next_queue_uid!=oldq.next_queue_uid)
        return fail(error,"avatar controllers changed after import preparation");
    if(!p.reset_before_dispatch){
        if(!p.continuation||!p.binding.thread->original_import_current(*p.continuation)||
           !context_.services_current||
           !p.binding.controllers->can_import_original(p.execution,p.queue))
            return fail(error,"actual avatar controllers cannot admit the original continuation");
        if(!context_.services_current(p.binding.person,error))return false;
    }else if(!p.binding.queue->can_restore_snapshot(p.queue,p.execution))return fail(error,"actual avatar queue cannot admit the prepared recovery-only execution");
    return true;
}
bool SavedRecoveryAvatarImport::prepare(const std::vector<SavedRecoveryAvatarBinding>& bindings,std::string& error){
    if(ready_||consumed_)return fail(error,"avatar source import preparation is single-use");
    if(!context_.world||!context_.types||!context_.source_still_matches||!context_.source_still_matches()||
       !context_.original_reference||!context_.resolve_owner||!context_.resolve_action||!context_.will_reset_before_dispatch||
       !context_.avatars||bindings.size()>context_.avatars)return fail(error,"avatar import lacks complete original resource/recovery authority");
    std::vector<Prepared> staged;staged.reserve(bindings.size());std::set<WorldEntityReference> people;
    std::size_t total_frames=0,total_words=0,total_actions=0;
    const auto consume=[&](std::size_t count,std::size_t& used,std::size_t limit){
        if(count>limit-used)return false;used+=count;return true;
    };
    const auto reference=[&](std::int16_t id,WorldEntityReference& out){
        if(!id){out={};return true;}
        const auto original=context_.original_reference(id);
        if(!original||original->id!=id||!original->generation||!context_.world->resolve_reference(*original))return false;
        out=*original;return true;
    };
    try{
        for(const auto& binding:bindings){
            if(!binding.thread||!binding.queue||!binding.controllers||!binding.source||
               &binding.controllers->interactions()!=binding.queue||!binding.queue->bound_to(*binding.thread)||
               !binding.controllers->bound_to(*binding.thread)||
               !binding.source->tail.person||binding.source->serialized_object_type!=2||
               binding.source->saved.object_id!=binding.person.id||binding.source->imported_generation!=binding.person.generation||
               !people.insert(binding.person).second)return fail(error,"avatar import binding/source identity is invalid or duplicate");
            const auto* actor=context_.world->find(binding.person.id);
            if(!actor||actor->generation!=binding.person.generation||!actor->definition||
               actor->definition->guid!=binding.source->saved.guid||actor->kind!=EntityKind::avatar||actor->dead||actor->ui_ghost)
                return fail(error,"avatar import does not identify its original live definition");
            Prepared p;p.binding=binding;p.binding.source=nullptr;p.before=binding.thread->snapshot();
            p.reset_before_dispatch=context_.will_reset_before_dispatch(binding.person);
            if(!p.reset_before_dispatch&&(!context_.prepare_services||!context_.services_current||!context_.commit_services))
                return fail(error,"compatible original avatar lacks its actual playback/appearance import consumers");
            p.controllers_before=binding.controllers->snapshot();
            if(binding.thread->dispatch_active()||p.before.steps||p.before.ticket_sequence||!no_async(p.before)||p.before.stack.size()>1||
               p.before.run_generation==std::numeric_limits<std::uint64_t>::max()||p.before.status==VmStatus::fault||
               std::any_of(p.before.stack.begin(),p.before.stack.end(),[](const auto& f){return f.pc||f.invocation_ticket||f.return_mode!=VmCallReturnMode::ordinary;}))
                return fail(error,"avatar import requires the actual undispatched original-load thread");
            p.source_plan=make_simitone_saved_thread_plan(*binding.source,*context_.types);
            if(!consume(p.source_plan.frames.size(),total_frames,context_.frames)||p.source_plan.frames.size()>context_.vm_limits.stack_depth)
                return fail(error,"avatar imported frame budget exceeded");
            const auto owner=[&](std::uint32_t guid)->std::optional<SavedRecoveryAvatarOwner>{
                const auto found=context_.resolve_owner(binding.person,guid);
                if(!found||!found->owner||!found->program||binding.thread->shared_program_for_owner(found->owner)!=found->program)return {};
                if(std::none_of(p.owners.begin(),p.owners.end(),[&](const auto& old){return old.first==found->owner;}))p.owners.emplace_back(found->owner,found->program);
                return found;
            };
            if(!p.reset_before_dispatch){
                // Pinned converter maps ObjectUses against the ACTUAL caller
                // instruction. No inferred wait/deadline/routing state is read.
                apply_simitone_saved_return_mapping(p.source_plan,[&](const auto& raw)->std::optional<std::uint16_t>{
                    const auto code=owner(raw.code_owner_guid);if(!code)return {};
                    const auto key=routine_key(raw.routine_id);
                    const auto bank=key.scope==RoutineScope::global?binding.thread->shared_program_for_owner(0):code->program;
                    if(!bank)return {};const auto routine=bank->routines().find(key);
                    if(routine==bank->routines().end()||raw.instruction_pointer>=routine->second.instructions.size())return {};
                    return routine->second.instructions[raw.instruction_pointer].opcode;
                });
                for(auto& raw:p.source_plan.frames)if(raw.return_state==SimitoneSavedReturnState::unresolved)
                    raw.return_state=SimitoneSavedReturnState::normal;
            }
            std::vector<VmFrame> frames;frames.reserve(p.source_plan.frames.size());
            for(const auto& raw:p.source_plan.frames){
                const auto code=owner(raw.code_owner_guid);if(!code)return fail(error,"avatar saved frame lacks its exact installed frozen code owner");
                if(raw.arguments.size()>context_.vm_limits.frame_variables||raw.locals.size()>context_.vm_limits.frame_variables||
                   !consume(raw.arguments.size(),total_words,context_.words)||!consume(raw.locals.size(),total_words,context_.words))
                    return fail(error,"avatar imported variable budget exceeded");
                VmFrame frame{};frame.key=routine_key(raw.routine_id);frame.pc=raw.instruction_pointer;
                frame.context={raw.caller,raw.callee,raw.stack_object,code->owner};frame.context.entity_references_bound=true;
                for(const auto& role:std::vector<std::pair<VmEntityRole,std::int16_t>>{{VmEntityRole::caller,raw.caller},
                        {VmEntityRole::callee,raw.callee},{VmEntityRole::stack_object,raw.stack_object}}){
                    WorldEntityReference ref;if(!reference(role.second,ref)||!vm_context_set_reference(frame.context,role.first,ref))
                        return fail(error,"avatar saved frame references an unavailable original identity");
                }
                const auto bank=frame.key.scope==RoutineScope::global?binding.thread->shared_program_for_owner(0):code->program;
                if(!bank)return fail(error,"avatar global resource bank is unavailable");
                if(frame.key.scope==RoutineScope::global&&std::none_of(p.owners.begin(),p.owners.end(),[](const auto& old){return old.first==0;}))
                    p.owners.emplace_back(0,bank);
                const auto routine=bank->routines().find(frame.key);
                if(routine!=bank->routines().end()&&(routine->second.version<0x8000||routine->second.version>0x8002))
                    return fail(error,"avatar saved frame resolves an unsupported native BHAV format");
                // These are opaque unwind frames, not reconstructed invocation
                // continuations. Raw return/primitive metadata stays in source_plan.
                frame.arguments=raw.arguments;frame.locals=raw.locals;
                if(!p.reset_before_dispatch){
                    frame.action_tree=raw.action_tree;
                    const auto kind=raw.return_state==SimitoneSavedReturnState::retry?VmOriginalReturn::retry:
                        raw.return_state==SimitoneSavedReturnState::interaction?VmOriginalReturn::interaction:VmOriginalReturn::normal;
                    frame.original_import=VmOriginalFrameState{raw.saved_primitive_state,kind};
                }
                frames.push_back(std::move(frame));
            }
            if(!p.reset_before_dispatch){
                for(const auto& use:binding.source->tail.person->object_uses){
                    if(use.stack_length<1||std::size_t(use.stack_length)>frames.size())return fail(error,"original use bank depth is outside the actual stack");
                    WorldEntityReference target;if(!reference(use.target_id,target)||!target.id)return fail(error,"original use bank target identity is unavailable");
                    frames[std::size_t(use.stack_length)-1].original_import->uses.push_back(VmOriginalUse{target,use.unknown2});
                }
                p.continuation=binding.thread->prepare_original_import(std::move(frames),p.source_plan.temp_registers,
                    context_.source_still_matches,error);
                if(!p.continuation)return false;
                p.execution=p.continuation->snapshot();
            }else{
            p.execution=p.before;p.execution.run_generation=p.before.run_generation+1;p.execution.steps=0;
            p.execution.diagnostic.clear();p.execution.pending.reset();p.execution.suspended_pending.clear();
            p.execution.interaction_return.reset();p.execution.external_frame_return.reset();
            p.execution.immediate_continuations.clear();p.execution.immediate_return.reset();
            p.execution.temps=p.source_plan.temp_registers;
            if(frames.empty()){p.execution.stack.clear();p.execution.status=VmStatus::returned_false;}
            else{
                p.execution.stack=binding.thread->prepare_dormant_frames(std::move(frames),VmSavedRoutinePolicy::defer_missing_or_pc_fault);
                p.execution.status=VmStatus::dormant;p.execution.action_tree_known=false;
            }
            }
            // Account metadata-driven zero padding using the SAME VM reader.
            for(const auto& frame:p.execution.stack)
                if(!consume(frame.arguments.size()+frame.locals.size(),total_words,context_.words))return fail(error,"avatar normalized variable budget exceeded");
            p.queue=binding.queue->snapshot();
            p.queue.run_generation=p.execution.run_generation;p.queue.sequence=0;p.queue.actions.clear();p.queue.active.reset();
            p.queue.suspended_active.clear();p.queue.wait.reset();p.queue.completion.reset();p.queue.resume_tick.reset();
            p.queue.interrupted=false;p.queue.diagnostic.clear();p.queue.profile=InteractionQueueProfile::ts1_priority;p.queue.next_queue_uid=0;
            const auto& person=*binding.source->tail.person;
            const auto action=[&](const ObjmSavedInteraction& raw)->bool{
                if(!consume(1,total_actions,context_.actions)||p.queue.actions.size()>=p.queue.capacity)return fail(error,"avatar imported queue budget exceeded");
                WorldEntityReference callee,icon;
                if(!reference(raw.target_id,callee)||!callee.id||!reference(raw.icon,icon))return fail(error,"avatar queued action references an unavailable original body");
                const auto source=context_.resolve_action(binding.person,callee,raw);
                if(!source||source->key.id!=routine_key(raw.action_tree_id).id||source->key.scope!=routine_key(raw.action_tree_id).scope||!routine_key_valid(source->key)||
                   !source->code.owner||!source->code.program||binding.thread->shared_program_for_owner(source->code.owner)!=source->code.program)
                    return fail(error,"avatar queued action lacks exact TTAB/resource ownership");
                if(std::none_of(p.owners.begin(),p.owners.end(),[&](const auto& old){return old.first==source->code.owner;}))
                    p.owners.emplace_back(source->code.owner,source->code.program);
                InteractionActionSnapshot item{};item.uid=++p.queue.sequence;item.queue_uid=static_cast<std::uint16_t>(raw.uid);
                item.target_generation=callee.generation;item.call.key=source->key;item.call.context={binding.person.id,callee.id,callee.id,source->code.owner};
                item.call.context.entity_references_bound=true;
                vm_context_set_reference(item.call.context,VmEntityRole::caller,binding.person);
                vm_context_set_reference(item.call.context,VmEntityRole::callee,callee);vm_context_set_reference(item.call.context,VmEntityRole::stack_object,callee);
                item.call.arguments.assign(raw.args.begin(),raw.args.end());item.call.exact_arguments=true;
                item.call.return_mode=VmCallReturnMode::interaction_next_tick;item.argument_mode=InteractionArgumentMode::source_explicit;
                item.priority=signed_word(raw.priority);item.mode=InteractionQueueMode::normal;
                // Pinned original-import conversion: FSOSkipPermissions and
                // the source PushHeadContinuation bit, not current TTAB flags.
                item.flags=(1U<<28)|((static_cast<std::uint32_t>(raw.flags)&2U)?(1U<<29):0U);
                item.notify_idle=(static_cast<std::uint32_t>(raw.flags)&256U)!=0;
                if(icon.id)item.icon_owner=InteractionIconOwnerSnapshot{icon.id,icon.generation};
                item.interaction_number=source->interaction_number;
                p.queue.next_queue_uid=static_cast<std::uint16_t>(item.queue_uid+1U);p.queue.actions.push_back(std::move(item));return true;
            };
            const bool active=person.active_interaction.tta_index!=-1;
            if(active&&!action(person.active_interaction))return false;
            for(const auto& raw:person.interaction_queue)if(!action(raw))return false;
            if(active){
                if(person.person_data.size()<=33)return fail(error,"native active priority bank is unavailable");
                for(std::size_t i=1;i<p.queue.actions.size();++i)
                    if(p.queue.actions[i].priority>person.person_data[33])p.queue.actions.front().notify_idle=true;
            }
            if(!p.reset_before_dispatch){
                std::vector<const VmFrame*> boundaries;
                for(const auto& frame:p.execution.stack)
                    if(frame.original_import&&frame.original_import->result==VmOriginalReturn::interaction)boundaries.push_back(&frame);
                if(boundaries.size()!=(active?std::size_t{1}:std::size_t{0}))return fail(error,"original active header lacks its exact single Interaction frame boundary");
                if(active){
                    const auto& frame=*boundaries.front();auto& header=p.queue.actions.front();
                    if(frame.key.scope!=header.call.key.scope||frame.key.id!=header.call.key.id||
                       frame.context.code_owner!=header.call.context.code_owner||frame.context.caller!=binding.person.id||
                       frame.context.callee!=header.call.context.callee||frame.context.callee_generation!=header.target_generation||
                       frame.arguments.size()<header.call.arguments.size())
                        return fail(error,"original active action/frame resource identity differs");
                    header.call.exact_arguments=frame.exact_arguments;
                    header.argument_mode=frame.exact_arguments?InteractionArgumentMode::source_explicit:InteractionArgumentMode::legacy_metadata;
                    p.queue.active=InteractionActiveSnapshot{header.uid,frame.invocation_ticket};
                }
            }
            if(!p.reset_before_dispatch&&!context_.prepare_services(binding.person,*binding.source->tail.person,p.execution,error))return false;
            if(!current(p,error))return false;
            staged.push_back(std::move(p));
        }
    }catch(const std::exception& exception){error=std::string("avatar import preparation: ")+exception.what();return false;}
    prepared_.swap(staged);ready_=true;return true;
}
SavedRecoveryAvatarImportResult SavedRecoveryAvatarImport::commit(){
    SavedRecoveryAvatarImportResult result;
    if(!ready_||consumed_){result.diagnostic="avatar source import is unprepared or already consumed";return result;}
    consumed_=true;
    try{
        for(const auto& p:prepared_)if(!current(p,result.diagnostic))return result;
        for(auto& p:prepared_){
            result.discard_candidate=true;auto& thread=*p.binding.thread;
            if(!p.reset_before_dispatch){
                if(!p.continuation||!thread.commit_original_import(*p.continuation)||
                   !p.binding.controllers->commit_original_import(p.queue,p.before.run_generation)){
                    result.diagnostic="actual original avatar VM/controller publication failed";return result;
                }
                if(!context_.commit_services(p.binding.person,thread.snapshot(),result.diagnostic))return result;
                if(thread.steps()!=0||thread.run_generation()!=p.execution.run_generation||
                   !equal_frames(thread.stack(),p.execution.stack)){
                    result.diagnostic="original avatar publication differs from admitted continuation";return result;
                }
                ++result.avatars;continue;
            }
            if(p.execution.stack.empty()){
                if(!thread.reset_execution_state()){result.diagnostic="empty native avatar thread cannot enter its recovery-only state";return result;}
                thread.temps()=p.execution.temps;
            }else thread.restore_dormant_state(p.execution.stack,p.execution.temps,VmSavedRoutinePolicy::defer_missing_or_pc_fault);
            thread.temp_xl()=p.before.temp_xl; // No native TempXL input exists; retain the real bank, never invent saved words.
            if(!p.binding.queue->restore_snapshot(p.queue)){
                result.diagnostic="actual avatar queue rejected publication after native frame hydration";return result;
            }
            if(thread.run_generation()!=p.execution.run_generation||thread.status()!=p.execution.status||
               !equal_frames(thread.stack(),p.execution.stack)){
                result.diagnostic="hydrated avatar execution differs from its prepared actual-VM admission";return result;
            }
            ++result.avatars;
        }
        result.completed=true;result.discard_candidate=false;return result;
    }catch(const std::exception& exception){result.diagnostic=std::string("avatar import publication: ")+exception.what();return result;}
}
} // namespace odh_host
