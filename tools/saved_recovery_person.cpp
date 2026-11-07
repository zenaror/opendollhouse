// SPDX-License-Identifier: MPL-2.0
#include "saved_recovery_person.hpp"
#include <algorithm>
#include <limits>

namespace odh_host {
using namespace odh;
namespace {
using Phase=SavedRecoveryPhase;
bool fail(std::string& error,const char* text){error=text;return false;}
WorldEntityReference target_of(const InteractionActionSnapshot& a){
    return {a.call.context.callee,a.target_generation};
}
WorldEntityReference icon_of(const InteractionActionSnapshot& a){
    return a.icon_owner?WorldEntityReference{a.icon_owner->id,a.icon_owner->generation}:WorldEntityReference{};
}
bool person_phase(Phase p){
    return p==Phase::person_unwind||p==Phase::person_visuals||p==Phase::person_placement||
        p==Phase::restart_main||p==Phase::person_cleanup||p==Phase::person_actions;
}
}
SavedRecoveryPersonService::SavedRecoveryPersonService(SavedRecoveryPersonContext context):context_(std::move(context)){}
bool SavedRecoveryPersonService::charge(std::string& error){
    if(operations_>=context_.operations)return fail(error,"person recovery operation budget exceeded");
    ++operations_;return true;
}
bool SavedRecoveryPersonService::identity_current(const State& s,std::string& error) const {
    if(!context_.source_still_matches||!context_.source_still_matches())return fail(error,"original person recovery lease expired");
    const auto* e=context_.world->resolve_reference(s.binding.person);
    if(!context_.world->is_live_reference(s.binding.person)||!e||e->dead||e->ui_ghost||e->kind!=EntityKind::avatar)
        return fail(error,"person recovery lost its exact live avatar");
    if(!s.binding.thread||!s.binding.queue||!s.binding.controllers||s.binding.thread->dispatch_active()||
       !s.binding.controllers->bound_to(*s.binding.thread)||
       &s.binding.controllers->interactions()!=s.binding.queue||!s.binding.queue->bound_to(*s.binding.thread))
        return fail(error,"person recovery thread is executing or queue ownership changed");
    return true;
}
bool SavedRecoveryPersonService::current(State& s,std::string& error) const {
    if(!identity_current(s,error))return false;
    const auto queue=s.binding.queue->snapshot();
    if(queue.actor_id!=s.binding.person.id||queue.actor_generation!=s.binding.person.generation||queue.run_generation!=s.run)
        return fail(error,"person recovery queue lost its actual thread epoch");
    if(s.active_uid&&std::none_of(queue.actions.begin(),queue.actions.end(),[&](const auto& action){return action.uid==*s.active_uid;}))
        return fail(error,"native active action was removed outside tracked person recovery");
    const auto& frames=s.binding.thread->stack();
    if(s.binding.thread->run_generation()!=s.run||frames.size()!=s.frames.size()||
       !std::equal(frames.begin(),frames.end(),s.frames.begin(),vm_frame_equal))
        return fail(error,"person use metadata cannot follow untracked execution/frame changes");
    return true;
}
bool SavedRecoveryPersonService::prepare(const std::vector<SavedRecoveryPersonBinding>& bindings,std::string& error){
    if(prepared_)return fail(error,"person recovery already prepared");
    if(!context_.world||!context_.source_still_matches||!context_.source_still_matches()||!context_.original_reference||
       !context_.suppress_referenced_reset||!context_.supports_phase||!context_.execute_phase||
       !context_.remove_action||!context_.set_active_icon||!context_.pop_frames||
       !context_.supports_entry4||!context_.skipped_entry4||!context_.update_footprint||!context_.operations||bindings.size()>4096)
        return fail(error,"person recovery lacks actual native lifecycle consumers");
    std::map<WorldEntityReference,State> staged;
    std::size_t retained=0;
    const auto reference=[&](std::int16_t id,WorldEntityReference& out){
        if(!id){out={};return true;}
        const auto exact=context_.original_reference(id);
        if(!exact||exact->id!=id||!exact->generation||!context_.world->resolve_reference(*exact))return false;
        out=*exact;return true;
    };
    for(const auto& b:bindings){
        if(!b.thread||!b.queue||!b.controllers||!b.source||!b.source->tail.person||
           b.source->saved.object_id!=b.person.id||b.source->imported_generation!=b.person.generation||
           b.thread->dispatch_active()||!b.queue->bound_to(*b.thread)||!b.controllers->bound_to(*b.thread)||
           &b.controllers->interactions()!=b.queue)
            return fail(error,"person recovery source/thread/queue identity is unavailable");
        State s;s.binding=b;s.binding.source=nullptr;s.run=b.thread->run_generation();s.frames=b.thread->stack();
        const auto* live=context_.world->find(b.person.id);
        if(!live||live->generation!=b.person.generation||live->dead||live->ui_ghost||
           live->kind!=EntityKind::avatar||live->person_data.size()<74||!live->entry_points||!live->definition||
           b.source->saved.guid!=live->definition->guid||b.source->serialized_object_type!=2)
            return fail(error,"person recovery requires exact live person banks and entry points");
        const auto& source=*b.source->tail.person;
        for(const auto count:{source.object_uses.size(),source.interaction_queue.size(),s.frames.size()}){
            if(count>4096||count>65536-retained)return fail(error,"person recovery retained metadata budget exceeded");
            retained+=count;
        }
        std::size_t previous=0;
        for(const auto& use:source.object_uses){
            WorldEntityReference target;
            // One native AfterPop consumes one boundary. Equal-depth nested
            // records need additional original evidence; decline before effects.
            if(use.stack_length<1||std::size_t(use.stack_length)<=previous||std::size_t(use.stack_length)>s.frames.size()||
               !reference(use.target_id,target)||!target.id)
                return fail(error,"person source object-use boundary or target is unavailable");
            previous=std::size_t(use.stack_length);s.uses.push_back({target,previous,use.unknown2});
        }
        if(!s.uses.empty()){
            if(s.frames.size()!=b.source->common.stack.size())return fail(error,"native use records require the actual imported person stack");
            for(std::size_t i=0;i<s.frames.size();++i){const auto& raw=b.source->common.stack[i];const auto& frame=s.frames[i];
                if(frame.key.id!=std::uint16_t(raw.tree_id)||frame.pc!=std::uint16_t(raw.node_id)||frame.locals.size()<raw.locals.size()||
                   !std::equal(raw.locals.begin(),raw.locals.end(),frame.locals.begin()))
                    return fail(error,"person execution no longer matches original use records");
                if(frame.locals.size()!=raw.locals.size()){
                    const auto bank=b.thread->shared_program_for_owner(frame.key.scope==RoutineScope::global?0:frame.context.code_owner);
                    if(!bank)return fail(error,"person local padding lacks its actual resource bank");
                    const auto found=bank->routines().find(frame.key);
                    if(found==bank->routines().end())return fail(error,"person local padding lacks current BHAV metadata");
                    const auto& routine=found->second;auto metadata=routine.metadata;
                    if(!metadata&&(routine.version==0x8000||routine.version==0x8001))metadata=Bhav::Metadata{0,4,0,0};
                    if(!metadata||frame.locals.size()!=std::max(raw.locals.size(),std::max(std::size_t(metadata->locals),std::size_t(metadata->arguments)))||
                       std::any_of(frame.locals.begin()+raw.locals.size(),frame.locals.end(),[](auto word){return word!=0;}))
                        return fail(error,"person locals contain unproved padding or changed source values");
                }
            }
        }
        if(!reference(source.active_interaction.target_id,s.active_target)||!reference(source.active_interaction.icon,s.active_icon)||
           !reference(source.last_interaction.target_id,s.last_target)||!reference(source.last_interaction.icon,s.last_icon))
            return fail(error,"person active/last native action points to an unavailable identity");
        const auto queue=b.queue->snapshot();
        const bool has_active=source.active_interaction.tta_index!=-1;
        if(queue.actor_id!=b.person.id||queue.actor_generation!=b.person.generation||queue.run_generation!=s.run||
           !queue.suspended_active.empty()||queue.actions.size()!=source.interaction_queue.size()+std::size_t(has_active))
            return fail(error,"person live queue differs from original active/queued actions");
        const auto matches=[&](const InteractionActionSnapshot& action,const ObjmSavedInteraction& raw){
            WorldEntityReference target,icon;
            return reference(raw.target_id,target)&&reference(raw.icon,icon)&&action.queue_uid==std::uint16_t(raw.uid)&&
                target_of(action)==target&&icon_of(action)==icon&&action.call.key.id==std::uint16_t(raw.action_tree_id)&&
                action.call.arguments.size()==4&&std::equal(raw.args.begin(),raw.args.end(),action.call.arguments.begin());
        };
        std::size_t offset=0;
        if(has_active){if(!matches(queue.actions[0],source.active_interaction))return fail(error,"person active action metadata changed");
            s.active_uid=queue.actions[0].uid;offset=1;}
        if(queue.active&&(!s.active_uid||queue.active->uid!=*s.active_uid))return fail(error,"person queue has an unproved active invocation");
        for(std::size_t i=0;i<source.interaction_queue.size();++i)
            if(!matches(queue.actions[i+offset],source.interaction_queue[i]))return fail(error,"person queued native action order changed");
        for(const auto& action:queue.actions)
            if(!context_.supports_entry4(target_of(action),b.person,error))return false;
        for(const auto p:{Phase::person_unwind,Phase::person_visuals,Phase::person_placement,Phase::restart_main,Phase::person_cleanup})
            if(!context_.supports_phase(b.person,p,error))return false;
        if(!context_.suppress_referenced_reset(b.person))return fail(error,"person internal recursive-reset policy is unknown");
        if(!staged.emplace(b.person,std::move(s)).second)return fail(error,"duplicate person recovery binding");
    }
    people_.swap(staged);prepared_=true;return true;
}
bool SavedRecoveryPersonService::supports(const SavedRecoveryCall& call,std::string& error) const {
    if(!prepared_||!people_.count(call.entity))return fail(error,"person recovery identity was not preflighted");
    if(call.phase!=Phase::notify_person&&!person_phase(call.phase))return fail(error,"operation is not a native person recovery phase");
    if(call.phase==Phase::notify_person){
        if(resetting_.count(call.entity))return true;
        auto s=people_.at(call.entity);
        if(!current(s,error))return false;
        // Native45e8fd consumes target+114 bit12 only in its full unwind
        // branch. Queued/last-action notification alone does not reset uses.
        // The queue can change its active IconOwner since prepare(); consult
        // that real header, not the serialized icon copied into State.
        auto icon=s.active_icon;
        if(s.active_uid){const auto queue=s.binding.queue->snapshot();
            const auto action=std::find_if(queue.actions.begin(),queue.actions.end(),[&](const auto& a){return a.uid==*s.active_uid;});
            if(action==queue.actions.end())return fail(error,"native active action was removed outside person recovery");
            icon=icon_of(*action);
        }
        const bool full=icon==call.target||std::any_of(s.uses.begin(),s.uses.end(),[&](const auto& use){return use.target==call.target;});
        if(full&&!context_.suppress_referenced_reset(call.target))
            return fail(error,"notification target internal recursive-reset policy is unknown");
    }
    return true;
}
bool SavedRecoveryPersonService::phase(State& s,Phase p,std::string& error){
    if(!current(s,error))return false;
    const auto before=s.binding.queue->snapshot();
    if(!charge(error))return false;
    if(p==Phase::person_unwind&&!s.binding.controllers->discard_waits_for_source_recovery())
        return fail(error,"person recovery could not release its actual controller waits");
    if(!context_.execute_phase(s.binding.person,p,error)||!identity_current(s,error))return false;
    if(p==Phase::restart_main&&!s.binding.controllers->rebase_after_source_recovery())
        return fail(error,"person recovery could not rebase its actual controller bundle");
    if(p==Phase::person_unwind||p==Phase::restart_main){
        const auto after=s.binding.queue->snapshot();
        if(before.actions.size()!=after.actions.size()||!std::equal(before.actions.begin(),before.actions.end(),after.actions.begin(),
                [](const auto& a,const auto& b){return a.uid==b.uid;}))
            return fail(error,"service detach/Main restart erased native queue actions before their EP4 notifications");
    }
    if(p==Phase::person_unwind)return current(s,error); // service detach must not erase native uses/frames
    s.frames=s.binding.thread->stack();s.run=s.binding.thread->run_generation();
    if(!current(s,error))return false;
    if(p==Phase::person_visuals){
        for(const auto index:{22,23,30})if(!context_.world->set_person_data(s.binding.person.id,index,0))return false;
        if(!context_.world->clear_bound_appearances(s.binding.person.id))return false;
    }
    return true;
}
bool SavedRecoveryPersonService::clear_active(State& s,std::string& error){
    if(s.active_uid){
        const auto queue=s.binding.queue->snapshot();
        if(queue.active&&(!s.active_uid||queue.active->uid!=*s.active_uid))
            return fail(error,"queued-drop encountered an untracked active native action");
        if(std::any_of(queue.actions.begin(),queue.actions.end(),[&](const auto& a){return a.uid==*s.active_uid;}))
            if(!charge(error)||!context_.remove_action(s.binding.person,*s.active_uid,true,error))return false;
        if(s.binding.queue->contains_uid(*s.active_uid))return fail(error,"native active action removal did not reach the actual queue");
    }
    const auto* e=context_.world->find(s.binding.person.id);if(!e)return fail(error,"person disappeared while clearing its active action");
    const bool extended=e->person_data[73]!=0;
    if(s.active_target.id)for(const auto index:{38,39})if(!context_.world->set_person_data(e->id,index,0))return false;
    for(const auto index:{33,71,72,73})if(!context_.world->set_person_data(e->id,index,0))return false;
    if(!context_.world->clear_motive_changes(e->id)||(extended&&!context_.update_footprint(s.binding.person,error)))return false;
    s.active_uid.reset();s.active_target={};s.active_icon={};
    if(context_.tracker&&!context_.tracker->observe(s.binding.queue->snapshot()))
        return fail(error,"player request tracker rejected native active-action release");
    return true;
}
bool SavedRecoveryPersonService::unwind(State& s,const SavedRecoveryCall& call,std::string& error){
    if(!current(s,error))return false;
    const auto policy=context_.suppress_referenced_reset(call.target.id?call.target:s.binding.person);
    if(!policy)return fail(error,"notification target internal recursive-reset policy is unknown");
    if(!phase(s,Phase::person_unwind,error))return false;
    while(!s.uses.empty()){
        const auto use=s.uses.back();
        while(s.binding.thread->stack().size()>=use.depth){
            const auto depth=s.binding.thread->stack().size()-1;
            if(!charge(error)||!context_.pop_frames(s.binding.person,depth,error)||s.binding.thread->stack().size()!=depth)
                return fail(error,"person native frame pop did not reach its exact boundary");
            s.frames=s.binding.thread->stack();s.run=s.binding.thread->run_generation();
            if(s.binding.thread->stack().size()<s.uses.back().depth){
                const auto crossed=s.uses.back();
                if(depth==crossed.depth-1)for(const auto& candidate:s.uses)if(candidate.flags){
                    if(s.active_uid&&!context_.set_active_icon(s.binding.person,*s.active_uid,candidate.target,error))return false;
                    s.active_icon=candidate.target;
                }
                const auto* target=context_.world->resolve_reference(crossed.target);
                if(!target)return fail(error,"unwound native use target body was released");
                const auto found=target->object_words.find(62);
                if(found==target->object_words.end())return fail(error,"unwound target has no native UseCount bank");
                VmFrame frame{};frame.context={crossed.target.id,crossed.target.id,0,0};frame.context.entity_references_bound=true;
                if(!vm_context_set_reference(frame.context,VmEntityRole::caller,crossed.target)||
                   !vm_context_set_reference(frame.context,VmEntityRole::callee,crossed.target)||
                   !context_.world->write(frame,3,62,static_cast<std::int16_t>(std::uint16_t(found->second)-1U)))
                    return fail(error,"cannot decrement native use ownership");
                s.uses.pop_back();
                if(s.uses.empty()&&!clear_active(s,error))return false;
            }
        }
        if(!s.uses.empty()&&s.uses.back().depth>=use.depth)return fail(error,"person use boundary was not consumed by native pop");
        if(!*policy){
            if(!call.reset_referenced)return fail(error,"person use unwind lacks recursive object recovery");
            if(!charge(error)||!call.reset_referenced(use.target,error))return false;
        }
        if(!current(s,error))return false;
    }
    return true;
}
bool SavedRecoveryPersonService::discard_queued(State& s,std::optional<WorldEntityReference> target,std::string& error){
    for(;;){
        const auto queue=s.binding.queue->snapshot();
        if(queue.active&&(!s.active_uid||queue.active->uid!=*s.active_uid))
            return fail(error,"queued-drop encountered an untracked active native action");
        const auto found=std::find_if(queue.actions.begin(),queue.actions.end(),[&](const auto& a){
            return (!s.active_uid||a.uid!=*s.active_uid)&&(!target||target_of(a)==*target||icon_of(a)==*target);
        });
        if(found==queue.actions.end())return true;
        if(found->call.arguments.size()!=4)return fail(error,"queued-drop action lacks four native arguments");
        const auto action=*found;
        if(!context_.supports_entry4(target_of(action),s.binding.person,error))return false;
        if(!charge(error)||!context_.remove_action(s.binding.person,action.uid,false,error))return false;
        if(s.binding.queue->contains_uid(action.uid))return fail(error,"queued-drop did not remove its actual action before EP4");
        const auto callee=target_of(action);const auto* live=context_.world->resolve_reference(callee);
        if(!live)return fail(error,"queued-drop lost the exact EP4 target body after removal");
        // The original tests pointer presence, not Dead. The actual callback
        // must execute the retained target or explicitly refuse that profile.
        if(!charge(error)||!context_.skipped_entry4(callee,s.binding.person,error))return false;
        if(context_.tracker&&!context_.tracker->observe(s.binding.queue->snapshot()))return fail(error,"player request tracker rejected native queued-drop state");
        if(!current(s,error))return false; // callback use/frame changes must be tracked, never guessed
    }
}
bool SavedRecoveryPersonService::notify(State& s,const SavedRecoveryCall& call,std::string& error){
    if(resetting_.count(s.binding.person))return true;
    if(!current(s,error))return false;
    resetting_.insert(s.binding.person);
    struct Guard{std::set<WorldEntityReference>& values;WorldEntityReference ref;~Guard(){values.erase(ref);}}guard{resetting_,s.binding.person};
    if(s.active_uid){const auto queue=s.binding.queue->snapshot();
        const auto action=std::find_if(queue.actions.begin(),queue.actions.end(),[&](const auto& a){return a.uid==*s.active_uid;});
        if(action==queue.actions.end())return fail(error,"native active action was removed outside person recovery");
        s.active_icon=icon_of(*action);s.active_target=target_of(*action);
    }
    const bool full=s.active_icon==call.target||std::any_of(s.uses.begin(),s.uses.end(),[&](const auto& use){return use.target==call.target;});
    if(full){
        if(!unwind(s,call,error))return false;
        for(const auto p:{Phase::person_visuals,Phase::person_placement,Phase::restart_main,Phase::person_cleanup})
            if(!phase(s,p,error))return false;
        for(const auto index:{0,40,59})if(!context_.world->set_person_data(s.binding.person.id,index,0))return false;
        for(const auto index:{34,9})if(!context_.world->declare_object_word(s.binding.person.id,index,0))return false;
        if(!clear_active(s,error))return false;
    }
    if(!discard_queued(s,call.target,error))return false;
    if(s.last_target==call.target){s.last_target={};s.last_icon={};}else if(s.last_icon==call.target)s.last_icon={};
    if(s.active_target==call.target){if(!clear_active(s,error))return false;}
    else if(s.active_icon==call.target){
        if(s.active_uid&&!context_.set_active_icon(s.binding.person,*s.active_uid,{},error))return false;
        s.active_icon={};
    }
    if(context_.tracker&&!context_.tracker->observe(s.binding.queue->snapshot()))
        return fail(error,"player request tracker rejected person notification state");
    return true;
}
bool SavedRecoveryPersonService::execute(const SavedRecoveryCall& call,std::string& error){
    if(!supports(call,error))return false;
    auto& s=people_.at(call.entity);
    if(call.phase==Phase::notify_person)return notify(s,call,error);
    if(!current(s,error))return false;
    if(call.phase==Phase::person_unwind){resetting_.insert(call.entity);return unwind(s,call,error);}
    if(call.phase==Phase::person_actions){
        if(!clear_active(s,error)||!discard_queued(s,{},error))return false;
        resetting_.erase(call.entity);return true;
    }
    if(!phase(s,call.phase,error))return false;
    if(context_.tracker&&!context_.tracker->observe(s.binding.queue->snapshot()))return fail(error,"player request tracker rejected person recovery state");
    return true;
}
} // namespace odh_host
