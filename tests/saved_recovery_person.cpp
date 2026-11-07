// SPDX-License-Identifier: MPL-2.0
#include "saved_recovery_person.hpp"
#include "saved_recovery_execution.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace odh;
using namespace odh_host;
using Phase=SavedRecoveryPhase;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
Bhav bhav(std::initializer_list<Instruction> instructions){return {0x8002,instructions,{},Bhav::Metadata{0,4,0,0}};}
struct Fixture {
    World world;
    unsigned rng=0;
    EntityThreadControllers controllers{world,1,[this]{++rng;}};
    Vm thread;
    SavedWorldEntity source{};
    WorldEntityReference person{},target{};
    std::vector<Phase> phases;
    unsigned cleanup=0,init=0;
    bool lease=true;
    Fixture(bool target_ep4=false,bool sleep_main=false):thread({{routine_key(4096),sleep_main?bhav({{0,254,255,{}}}):bhav({{98,254,255,{}}})},
            {routine_key(4097),bhav({{99,254,255,{}}})}},world.vm_memory(),{},
            [this,sleep_main](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
                if(sleep_main&&instruction.opcode==0)return controllers.sleep_primitive()(frame,instruction,temps,dispatch);
                if(instruction.opcode==99)++cleanup;else ++init;
                return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            }){
        std::array<std::int16_t,68> words{};words[1]=2;words[3]=-1;
        require(world.create_saved_entity(1,EntityKind::avatar,1001,{},std::vector<std::int16_t>(88,77),
            std::vector<std::int16_t>(16,55),words,{},3),"saved person");
        std::vector<EntryPoint> target_entries(target_ep4?5:0);
        if(target_ep4)target_entries[4].action=4096;
        require(world.create_runtime_object(2,1002,{},target_entries),"notification target");
        person={1,world.find(1)->generation};target={2,world.find(2)->generation};
        require(world.define_entry_points(1,std::vector<EntryPoint>(11))&&controllers.bind(thread),"exact execution bindings");
        source.saved.object_id=1;source.saved.guid=1001;source.serialized_object_type=2;
        source.imported_generation=person.generation;source.tail.person.emplace();
        source.tail.person->active_interaction.tta_index=-1;
        source.tail.person->last_interaction.tta_index=-1;
        thread.temps().fill(45);thread.temp_xl().fill(123456);
    }
    SavedRecoveryPersonContext context(){
        SavedRecoveryPersonContext result;result.world=&world;result.source_still_matches=[this]{return lease;};
        std::map<std::int16_t,WorldEntityReference> originals;
        for(const auto& entity:world.snapshot().entities)originals.emplace(entity.id,WorldEntityReference{entity.id,entity.generation});
        originals[1]=person;originals[2]=target;
        result.original_reference=[originals](std::int16_t id)->std::optional<WorldEntityReference>{
            const auto found=originals.find(id);return found==originals.end()?std::nullopt:std::optional<WorldEntityReference>{found->second};
        };
        result.suppress_referenced_reset=[](auto)->std::optional<bool>{return false;};
        result.supports_phase=[](auto,auto,std::string&){return true;};
        result.execute_phase=[this](auto id,Phase phase,std::string&){
            require(id==person,"phase exact identity");phases.push_back(phase);
            if(phase==Phase::restart_main)return thread.reset_execution_state()&&
                thread.start_after_reset(routine_key(4096),{1,1,0,0},std::vector<std::int16_t>(4),true)==VmStatus::running;
            if(phase==Phase::person_cleanup){
                const auto done=thread.evaluate_entry_point_check(routine_key(4097),{1,1,0,0},std::vector<std::int16_t>(4),100,true,
                    [&](const auto&,const auto& instruction,const auto&,const auto&){
                        require(instruction.opcode==99,"only human EP3 executes synchronously");++cleanup;
                        return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
                    });
                return done.started&&(done.status==VmStatus::returned_true||done.status==VmStatus::returned_false);
            }
            // Fixture has no visual playback, routing job or containment.
            return true;
        };
        result.remove_action=[this](auto,std::uint64_t uid,bool active,std::string&){return controllers.interactions().source_recovery_remove(uid,active);};
        result.set_active_icon=[this](auto,std::uint64_t uid,auto icon,std::string&){return controllers.interactions().source_recovery_set_action_icon(uid,icon);};
        result.pop_frames=[this](auto,std::size_t depth,std::string&){return thread.source_recovery_pop_to_depth(depth);};
        result.supports_entry4=[](auto,auto,std::string&){return true;};
        result.skipped_entry4=[](auto,auto,std::string& error){error="fixture has no queued actions";return false;};
        result.update_footprint=[](auto,std::string&){return true;};return result;
    }
    SavedRecoveryPersonBinding binding(){return {person,&thread,&controllers.interactions(),&controllers,&source};}
    void restore_native_actions(const std::vector<std::int16_t>& targets,
            const std::array<std::int16_t,4>& args={}){
        auto& queue=controllers.interactions();auto saved=queue.snapshot();
        saved.run_generation=thread.run_generation();saved.sequence=targets.size();saved.next_queue_uid=static_cast<std::uint16_t>(targets.size());
        for(std::size_t i=0;i<targets.size();++i){
            const auto* target_body=world.find(targets[i]);require(target_body,"native action target exists");
            InteractionActionSnapshot action{};action.uid=i+1;action.queue_uid=static_cast<std::uint16_t>(i);
            action.target_generation=target_body->generation;action.call.key=routine_key(4097);
            action.call.context={1,targets[i],targets[i],0};require(world.bind_context_live(action.call.context),"bind native action references");
            action.call.arguments.assign(args.begin(),args.end());action.call.return_mode=VmCallReturnMode::interaction_next_tick;
            action.call.exact_arguments=true;action.argument_mode=InteractionArgumentMode::source_explicit;
            saved.actions.push_back(std::move(action));
        }
        require(queue.restore_snapshot(saved),"restore actual original-import actions into fresh native queue");
    }
};
void start_existing_sleep(Fixture& f){
    f.restore_native_actions({2});
    const auto action=f.controllers.interactions().snapshot().actions.front();
    ObjmSavedInteraction raw_action{};raw_action.uid=action.queue_uid;raw_action.tta_index=0;
    raw_action.target_id=2;raw_action.action_tree_id=4097;
    f.source.tail.person->interaction_queue.push_back(raw_action);
    require(f.thread.start(routine_key(4096),{1,1,0,0},{20,0,0,0},true)==VmStatus::running&&
        f.controllers.interactions().source_recovery_rebase(),"prepare actual waiting person and its queue epoch");
    require(f.controllers.advance(1)&&f.thread.run(1)==VmStatus::waiting&&f.controllers.sleep().active(),
        "actual Sleep owns original VM pending ticket");
    for(std::uint64_t tick=2;tick<=4;++tick)require(f.controllers.advance(tick),"advance original bundle clock without completing wait");
    require(f.controllers.notify()&&f.controllers.thread_interrupted()&&f.rng==0,"real Notify latches old wait without running it");
    const auto& frame=f.thread.stack().front();ObjmStackFrame raw{};
    raw.tree_id=4096;raw.node_id=frame.pc;raw.parameter_count=static_cast<std::uint8_t>(frame.arguments.size());
    raw.parameters=frame.arguments;raw.local_count=static_cast<std::uint8_t>(frame.locals.size());raw.locals=frame.locals;
    f.source.common.stack.push_back(std::move(raw));
}
void controller_bundle_recovery(){
    Fixture f(false,true);start_existing_sleep(f);
    const auto before=f.controllers.snapshot();const auto vm_before=f.thread.snapshot();const auto old_ticket=f.thread.pending()->ticket;
    auto context=f.context();const auto execute=context.execute_phase;bool detached_before_callback=false,headers_at_cleanup=false;
    context.execute_phase=[&](auto person,Phase phase,std::string& error){
        if(phase==Phase::person_unwind){
            const auto state=f.controllers.snapshot();
            detached_before_callback=!state.sleep.job&&!state.interactions.wait&&f.thread.pending()&&
                f.thread.pending()->ticket==old_ticket&&state.sleep.clock==4&&state.interactions.clock==4&&
                state.interactions.actions.front().uid==before.interactions.actions.front().uid;
            require(detached_before_callback,"bundle releases real waits before external detach callback without touching VM tickets/headers");
        }
        if(phase==Phase::person_cleanup){
            const auto state=f.controllers.snapshot();
            headers_at_cleanup=state.interactions.run_generation==f.thread.run_generation()&&state.sleep.actor&&
                state.sleep.actor->run_generation==f.thread.run_generation()&&state.sleep.clock==4&&state.interactions.clock==4&&
                state.schedule_idle_start==std::optional<std::uint64_t>{4}&&!state.thread_interrupt&&
                state.interactions.actions.front().uid==before.interactions.actions.front().uid;
            require(headers_at_cleanup,"service rebases both real controllers before EP3 while preserving native queue headers");
        }
        return execute(person,phase,error);
    };
    SavedRecoveryPersonService service(std::move(context));std::string error;
    require(service.prepare({f.binding()},error),error);
    for(const auto phase:{Phase::person_unwind,Phase::person_visuals,Phase::person_placement,Phase::restart_main,Phase::person_cleanup})
        require(service.execute({phase,f.person,f.person,true,{}},error),error);
    const auto after=f.controllers.snapshot();
    require(detached_before_callback&&headers_at_cleanup&&f.cleanup==1&&f.init==0&&f.rng==0&&
        after.sleep.clock==before.sleep.clock&&after.sleep.last_tick==before.sleep.last_tick&&
        after.interactions.clock==before.interactions.clock&&after.interactions.last_tick==before.interactions.last_tick&&
        f.thread.steps()==0&&f.thread.run_generation()>vm_before.run_generation&&f.thread.stack().size()==1&&
        f.thread.stack().front().pc==0&&!f.thread.pending(),
        "same bundle preserves clock/RNG and prepares rather than dispatches Main across recovery");
    require(f.controllers.advance(5)&&f.rng==0,"rebased bundle accepts the consecutive next clock boundary");
}
void controller_bundle_recovery_failures(){
    {
        Fixture f;EntityThreadControllers split(f.world,1);
        Vm other({{routine_key(4096),bhav({{0,254,255,{}}})}},f.world.vm_memory(),{},split.sleep_primitive());
        require(split.sleep().bind(other)&&split.interactions().bind(f.thread)&&split.sleep().tick(1)&&
            other.start(routine_key(4096),{1,1,0,0},{20,0,0,0},true)==VmStatus::running&&other.run(1)==VmStatus::waiting,
            "legitimately bind split Sleep and queue to different VMs");
        auto binding=f.binding();binding.controllers=&split;binding.queue=&split.interactions();
        SavedRecoveryPersonService service(f.context());std::string error;
        require(!service.prepare({binding},error)&&split.sleep().active()&&other.pending()&&f.phases.empty(),
            "mismatched bundle refuses admission without releasing another VM's actual Sleep job");
    }
    for(bool lose_identity:{false,true}){
        Fixture f(false,true);start_existing_sleep(f);const auto before=f.controllers.snapshot();const auto ticket=f.thread.pending()->ticket;
        auto context=f.context();context.execute_phase=[&](auto,Phase phase,std::string& error){
            require(phase==Phase::person_unwind&&!f.controllers.sleep().active()&&f.thread.pending()->ticket==ticket,
                "negative callback starts after real wait retirement and before VM bookkeeping mutation");
            if(lose_identity){require(f.world.remove(1)&&f.world.create(1,EntityKind::avatar),"replace person during external callback");return true;}
            require(f.world.set_person_data(1,6,99),"retain completed external callback effect");error="partial external detach failed";return false;
        };
        SavedRecoveryPersonService service(std::move(context));std::string error;
        require(service.prepare({f.binding()},error),error);
        require(!service.execute({Phase::person_unwind,f.person,f.person,true,{}},error)&&
            !f.controllers.sleep().active()&&f.thread.pending()&&f.thread.pending()->ticket==ticket&&
            f.controllers.sleep().clock()==before.sleep.clock&&f.rng==0,
            "identity loss or partial callback rejects without rollback/completion or invented rebase");
        if(!lose_identity)require(f.world.find(1)->person_data[6]==99,"partial external effect is not represented as rolled back");
    }
    {
        Fixture f(false,true);start_existing_sleep(f);f.source.tail.person->object_uses.push_back({2,1,0});
        require(f.world.declare_object_word(2,62,1),"pending-pop use ownership");
        auto context=f.context();bool pop_attempted=false;
        context.pop_frames=[&](auto,std::size_t depth,std::string&){pop_attempted=true;return f.thread.source_recovery_pop_to_depth(depth);};
        SavedRecoveryPersonService service(std::move(context));std::string error;
        require(service.prepare({f.binding()},error),error);const auto ticket=f.thread.pending()->ticket;
        require(!service.execute({Phase::person_unwind,f.person,f.person,true,{}},error)&&pop_attempted&&
            !f.controllers.sleep().active()&&f.thread.pending()->ticket==ticket&&f.thread.stack().size()==1&&f.rng==0,
            "retiring real bundle waits does not fabricate support for popping VM pending continuations");
    }
}
void self_reset(){
    Fixture f;std::string error;SavedRecoveryPersonService service(f.context());
    require(service.prepare({f.binding()},error),error);
    const auto motives=f.world.find(1)->motives;
    for(const auto p:{Phase::person_unwind,Phase::person_visuals,Phase::person_placement,
            Phase::restart_main,Phase::person_cleanup,Phase::person_actions})
        require(service.execute({p,f.person,f.person,true,{}},error),error);
    require(f.cleanup==1&&f.init==0,"human EP3 executes, Main is only stacked and Init never executes");
    require(f.thread.stack().size()==1&&f.thread.stack().front().key.id==4096,"same actual VM stacks Main");
    require(f.thread.temps()[0]==45&&f.thread.temp_xl()[0]==123456&&f.world.find(1)->motives==motives,
        "person reset preserves TEMP/XL and motives");
    require(f.world.find(1)->person_data[6]==77&&f.world.find(1)->person_data[60]==77,
        "personality and unrelated person words survive directed cleanup");
    require(f.world.find(1)->person_data[22]==0&&f.world.find(1)->person_data[33]==0,
        "directed visual and active-action fields are cleared");
}
void notification_selection(){
    for(bool active_icon:{false,true}){
        Fixture f;if(active_icon)f.source.tail.person->active_interaction.icon=2; // TTA=-1 does not invalidate the icon pointer.
        std::string error;SavedRecoveryPersonService service(f.context());
        require(service.prepare({f.binding()},error),error);
        require(service.execute({Phase::notify_person,f.person,f.target,false,{}},error),error);
        require(f.cleanup==(active_icon?1U:0U),"raw active icon triggers reset even without valid TTA; unrelated target does not");
    }
    {
        Fixture f;
        auto& queue=f.controllers.interactions();
        f.restore_native_actions({2});
        require(f.thread.stack().empty()&&!queue.active(),"native active header exists before first Main/ticket dispatch");
        const auto action=queue.snapshot().actions.front();auto& active=f.source.tail.person->active_interaction;
        require(queue.source_recovery_set_action_icon(action.uid,f.target)&&queue.snapshot().actions.front().icon_owner&&
            queue.snapshot().actions.front().icon_owner->generation==f.target.generation&&
            queue.source_recovery_set_action_icon(action.uid,{}),
            "exact native header UID updates and clears IconOwner without a queue.active ticket or Main frame");
        active.tta_index=0;active.uid=action.queue_uid;active.target_id=2;active.icon=0;active.action_tree_id=4097;
        auto context=f.context();
        context.remove_action=[&](auto person,std::uint64_t uid,bool,std::string&){
            require(person==f.person,"native active removal person identity");
            return queue.source_recovery_remove(uid,true);
        };
        std::string error;SavedRecoveryPersonService service(std::move(context));
        require(service.prepare({f.binding()},error),error);
        require(service.execute({Phase::notify_person,f.person,f.target,false,{}},error),error);
        require(f.cleanup==0&&f.phases.empty()&&queue.size()==0,
            "active target with null IconOwner and no uses clears action without full reset or invented icon");
    }
}
void notification_policy_selection(){
    for(bool active_use:{false,true}){
        Fixture f;std::string error;
        if(active_use){
            require(f.thread.start(routine_key(4096),{1,1,0,0})==VmStatus::running&&
                f.controllers.interactions().source_recovery_rebase(),"actual use execution epoch");
            ObjmStackFrame raw{};raw.tree_id=4096;raw.node_id=0;f.source.common.stack.push_back(raw);
            f.source.tail.person->object_uses.push_back({2,1,0});
            require(f.world.declare_object_word(2,62,1),"actual use ownership count");
        }else f.source.tail.person->active_interaction.icon=2;
        auto context=f.context();unsigned target_policy_reads=0;
        context.suppress_referenced_reset=[&](auto ref)->std::optional<bool>{
            if(ref==f.target){++target_policy_reads;return {};}
            return false;
        };
        SavedRecoveryPersonService service(std::move(context));require(service.prepare({f.binding()},error),error);
        const auto frames=f.thread.stack().size(),queue=f.controllers.interactions().size();
        const SavedRecoveryCall call{Phase::notify_person,f.person,f.target,false,{}};
        require(!service.supports(call,error)&&!service.execute(call,error)&&target_policy_reads==2&&f.phases.empty()&&
            f.thread.stack().size()==frames&&f.controllers.interactions().size()==queue&&f.cleanup==0&&
            (!active_use||f.world.find(2)->object_words.at(62)==1),
            "active icon/use requires known recursive policy before any notification effects");
    }
    {
        Fixture f;f.source.tail.person->last_interaction.target_id=2;f.source.tail.person->last_interaction.icon=2;
        auto context=f.context();unsigned target_policy_reads=0;
        context.suppress_referenced_reset=[&](auto ref)->std::optional<bool>{if(ref==f.target){++target_policy_reads;return {};}return false;};
        SavedRecoveryPersonService service(std::move(context));std::string error;require(service.prepare({f.binding()},error),error);
        const SavedRecoveryCall call{Phase::notify_person,f.person,f.target,false,{}};
        require(service.supports(call,error)&&service.execute(call,error)&&target_policy_reads==0&&f.phases.empty(),
            "last-only notification does not demand or invent recursive-reset policy");
    }
    for(bool icon_initially_set:{false,true}){
        Fixture f;f.restore_native_actions({2});auto& queue=f.controllers.interactions();const auto action=queue.snapshot().actions.front();
        auto& active=f.source.tail.person->active_interaction;active.tta_index=0;active.uid=action.queue_uid;
        active.target_id=2;active.action_tree_id=4097;active.icon=icon_initially_set?2:0;
        require(queue.source_recovery_set_action_icon(action.uid,icon_initially_set?f.target:WorldEntityReference{}),"initial actual active icon");
        auto context=f.context();context.suppress_referenced_reset=[&](auto ref)->std::optional<bool>{return ref==f.target?std::nullopt:std::optional<bool>{false};};
        SavedRecoveryPersonService service(std::move(context));std::string error;require(service.prepare({f.binding()},error),error);
        const SavedRecoveryCall call{Phase::notify_person,f.person,f.target,false,{}};
        require(service.supports(call,error)==!icon_initially_set,"preflight observes initial actual icon");
        require(queue.source_recovery_set_action_icon(action.uid,icon_initially_set?WorldEntityReference{}:f.target),"change real icon after preflight");
        require(service.supports(call,error)==icon_initially_set,"preflight rereads current icon rather than source copy");
        require(service.execute(call,error)==icon_initially_set&&f.phases.empty()&&f.cleanup==0&&
            queue.size()==(icon_initially_set?0U:1U),"execution rechecks full-unwind branch after queue changes without unknown-policy effects");
    }
    {
        Fixture f;auto context=f.context();context.suppress_referenced_reset=[](auto)->std::optional<bool>{return {};};
        SavedRecoveryPersonService service(std::move(context));std::string error;
        require(!service.prepare({f.binding()},error)&&f.phases.empty(),"own full-reset policy still required during person admission");
    }
}
void admission(){
    {
        Fixture f;auto context=f.context();context.pop_frames={};SavedRecoveryPersonService service(std::move(context));std::string error;
        require(!service.prepare({f.binding()},error)&&f.phases.empty(),"missing actual consumer rejects before effects");
    }
    {
        Fixture f;SavedRecoveryPersonService service(f.context());std::string error;
        require(service.prepare({f.binding()},error),error);f.lease=false;
        require(!service.execute({Phase::notify_person,f.person,f.target,false,{}},error)&&f.phases.empty(),"expired source lease rejects before effects");
    }
    {
        Fixture f;f.source.tail.person->object_uses.push_back({2,1,1});SavedRecoveryPersonService service(f.context());std::string error;
        require(!service.prepare({f.binding()},error)&&f.phases.empty(),"native use cannot be reconstructed from a fabricated Main stack");
    }
    {
        Fixture f;SavedRecoveryPersonService service(f.context());std::string error;
        require(service.prepare({f.binding()},error),error);f.thread.start(routine_key(4096),{1,1,2,0});
        require(!service.execute({Phase::notify_person,f.person,f.target,false,{}},error),"untracked execution cannot retain old native use metadata");
    }
    {
        Fixture f;f.source.tail.person->active_interaction.icon=2;
        require(f.world.remove(2)&&f.world.create_runtime_object(2,2002,{},{}),"replace original saved action target before preparation");
        SavedRecoveryPersonService service(f.context());std::string error;
        require(!service.prepare({f.binding()},error)&&f.phases.empty(),"serialized target ID cannot adopt a replacement generation during preparation");
    }
}
void retained_queued_target_is_not_silent_noop(){
    Fixture f;require(f.thread.start(routine_key(4096),{1,1,0,0})==VmStatus::running,"person for retained target event");
    f.restore_native_actions({2},{11,12,13,14});
    const auto queued=f.controllers.interactions().snapshot().actions.front();
    ObjmSavedInteraction raw{};raw.uid=queued.queue_uid;raw.tta_index=0;raw.target_id=2;
    raw.action_tree_id=4097;raw.args={11,12,13,14};f.source.tail.person->interaction_queue.push_back(raw);
    auto context=f.context();bool called=false;
    context.remove_action=[&](auto,std::uint64_t uid,bool active,std::string&){
        return !active&&f.controllers.interactions().source_recovery_remove(uid,false)&&f.world.remove_retaining_body(f.target);
    };
    context.skipped_entry4=[&](auto target,auto person,std::string& error){
        called=target==f.target&&person==f.person&&f.world.reference_is_dead(target);
        error="fixture cannot authorize retained-target EP4";return false;
    };
    std::string error;SavedRecoveryPersonService service(std::move(context));
    require(service.prepare({f.binding()},error),error);
    require(!service.execute({Phase::notify_person,f.person,f.target,false,{}},error)&&called&&
        !f.controllers.interactions().contains_uid(queued.uid),
        "retained target reaches real EP4 authority after removal and refuses explicitly instead of being skipped");
}
void queued_drop_rescans_actual_queue(){
    Fixture f(true);require(f.world.create_runtime_object(3,1003,{},{}),"queued survivor target");
    require(f.thread.start(routine_key(4096),{1,1,0,0})==VmStatus::running,"actual Main for callback PushInteraction");
    f.thread.temps().fill(45); // Seed the running person after start clears its registers.
    auto& queue=f.controllers.interactions();
    queue.set_queue_skipped_handler([](const auto&)->bool{throw std::runtime_error("ordinary cancellation EP4 must not run during source recovery removal");});
    const auto enqueue=[&](std::int16_t id){return queue.enqueue_ts1(routine_key(4097),{1,id,id,0},
        {11,12,13,14},1,InteractionQueueMode::normal,0,false,true);};
    f.restore_native_actions({2,3},{11,12,13,14});
    const auto initial=queue.snapshot();
    for(const auto& action:initial.actions){
        ObjmSavedInteraction raw{};raw.uid=action.queue_uid;raw.tta_index=0;
        raw.target_id=action.call.context.callee;raw.action_tree_id=4097;raw.args={11,12,13,14};
        f.source.tail.person->interaction_queue.push_back(raw);
    }
    auto context=f.context();unsigned dropped=0,target_policy_reads=0;
    context.suppress_referenced_reset=[&](auto ref)->std::optional<bool>{
        if(ref==f.target){++target_policy_reads;return {};}
        return false;
    };
    context.remove_action=[&](auto,std::uint64_t uid,bool active,std::string&){
        require(!active,"queued-drop does not pretend to cancel active execution");
        return queue.source_recovery_remove(uid,false);
    };
    std::unique_ptr<Vm> target_vm;
    const VmPrimitive target_primitive=[&](const VmFrame& frame,const Instruction& instruction,const auto& temps,const auto&){
        if(instruction.opcode==100)return PrimitiveOutcome{PrimitiveDisposition::wait,{}};
        require(instruction.opcode==98&&frame.arguments==std::vector<std::int16_t>({std::int16_t(51+dropped*10),52,53,54})&&
            vm_context_reference(frame.context,VmEntityRole::caller)==f.target&&
            vm_context_reference(frame.context,VmEntityRole::callee)==f.target&&
            vm_context_reference(frame.context,VmEntityRole::stack_object)==f.person,
            "actual target EP4 copies target TEMP with exact target/person roles, never queue args or person TEMP");
        const auto live_queue=queue.snapshot();
        require(std::none_of(live_queue.actions.begin(),live_queue.actions.end(),
            [](const auto& action){return action.call.context.callee==2;}),"matching action is absent before its EP4 callback");
        ++dropped;
        if(dropped==1)require(enqueue(2),"EP4 adds another real matching queue action");
        PrimitiveOutcome outcome{PrimitiveDisposition::goto_true,{}};
        outcome.variable_write=VmVariableWrite{8,0,std::int16_t(temps[0]+10)};return outcome;
    };
    target_vm=std::make_unique<Vm>(RoutineSet{},f.world.vm_memory(),VmLimits{},target_primitive,
        RoutineOwners{{9002,{{routine_key(4096),bhav({{98,254,255,{}}})},
                          {routine_key(4097),bhav({{100,254,255,{}}})}}}});
    require(target_vm->start(routine_key(4097),{2,2,0,9002},{99,98,97,96},true)==VmStatus::running&&
        target_vm->run(1)==VmStatus::waiting,"actual object Main stays parked during EP4");
    target_vm->temps()={51,52,53,54,55,56,57,58};const auto target_before=target_vm->snapshot();
    SavedRecoveryExecutionContext execution{f.world,f.target,target_vm.get(),9002};
    execution.authority=[&]{return f.lease;};
    execution.thread_for=[&](auto ref){return ref==f.target?target_vm.get():nullptr;};
    execution.owner_for=[&](auto ref)->std::optional<CodeOwner>{return ref==f.target?std::optional<CodeOwner>{9002}:std::nullopt;};
    execution.guarded_operation=[&](Vm& actual,auto ref,auto owner,const auto& operation){
        return &actual==target_vm.get()&&ref==f.target&&owner==9002&&operation();};
    execution.synchronous_primitive=target_primitive;
    SavedRecoveryExecutionRequest event;event.phase=SavedRecoveryExecutionPhase::skipped4;event.stack_object=f.person;
    context.supports_entry4=[&](auto callee,auto actor,std::string& error){
        if(callee!=f.target)return true; // Exact fixture target3 has an explicitly empty entry table.
        if(actor!=f.person)return false;
        const auto checked=preflight_saved_recovery_execution(execution,event);error=checked.diagnostic;return checked.supported;
    };
    context.skipped_entry4=[&](auto callee,auto actor,std::string& error){
        require(callee==f.target&&actor==f.person,"person service routes EP4 to the target owner");
        const auto result=execute_saved_recovery_execution(execution,event);error=result.diagnostic;return result.completed;
    };
    std::string error;SavedRecoveryPersonService service(std::move(context));
    require(service.prepare({f.binding()},error),error);
    require(dropped==0&&target_vm->temps()[0]==51,"whole person preflight never executes EP4 or mutates target TEMP");
    require(service.execute({Phase::notify_person,f.person,f.target,false,{}},error),error);
    const auto after=queue.snapshot();
    require(dropped==2&&after.actions.size()==1&&after.actions.front().uid==initial.actions.back().uid&&
        after.actions.front().call.arguments==std::vector<std::int16_t>({11,12,13,14})&&f.phases.empty()&&
        target_vm->temps()[0]==71&&target_vm->pending()->ticket==target_before.pending->ticket&&
        target_vm->steps()==target_before.steps&&target_vm->run_generation()==target_before.run_generation&&f.thread.temps()[0]==45&&target_policy_reads==0,
        "remove-before-EP4 rescans callback changes, preserves survivor identity/arguments and avoids full reset");
}
void native_use_unwind(){
    for(bool duplicate:{false,true}){
        Fixture f;
        require(f.thread.start(routine_key(4096),{1,1,0,0})==VmStatus::running,"actual original stack fixture");
        require(f.controllers.interactions().source_recovery_rebase(),"bind actual native use queue epoch");
        f.thread.temps().fill(45);f.thread.temp_xl().fill(123456);
        ObjmStackFrame raw{};raw.tree_id=4096;raw.node_id=0;
        // Actual BHAV arguments require four zero locals, while the serialized
        // old routine has none. Admit only this metadata-derived zero tail.
        f.source.common.stack.push_back(raw);f.source.tail.person->object_uses.push_back({2,1,1});
        if(duplicate)f.source.tail.person->object_uses.push_back({2,1,0});
        require(f.world.declare_object_word(2,62,2),"native raw UseCount bank");
        auto context=f.context();context.pop_frames=[&](auto id,std::size_t depth,std::string&){
            require(id==f.person,"pop actual person execution owner");return f.thread.source_recovery_pop_to_depth(depth);
        };
        SavedRecoveryPersonService service(std::move(context));std::string error;
        if(duplicate){
            require(!service.prepare({f.binding()},error)&&f.thread.stack().size()==1&&f.world.find(2)->object_words.at(62)==2,
                "unproved equal native use boundaries reject before frame/UseCount effects");continue;
        }
        require(service.prepare({f.binding()},error),error);unsigned resets=0;
        SavedRecoveryCall call{Phase::person_unwind,f.person,f.person,true,{}};
        call.reset_referenced=[&](auto referenced,std::string&){
            require(referenced==f.target&&f.thread.stack().empty()&&f.world.find(2)->object_words.at(62)==1,
                "native frame pop and UseCount release precede recursive referenced-object recovery");++resets;return true;
        };
        require(service.execute(call,error),error);
        require(resets==1&&f.thread.temps()[0]==45&&f.thread.temp_xl()[0]==123456,
            "actual use unwind preserves registers and invokes one referenced reset");
    }
}
void local_padding_rejection(){
    for(const bool nonzero:{false,true}){
        Fixture f;VmFrame frame{};frame.key=routine_key(4096);frame.context={1,1,0,0};
        require(f.world.bind_context_live(frame.context),"bind padded original frame");
        frame.locals=nonzero?std::vector<std::int16_t>{0,0,0,1}:std::vector<std::int16_t>(5,0);
        f.thread.restore_dormant_state({frame},{});
        require(f.controllers.interactions().source_recovery_rebase(),"bind padded native queue epoch");
        ObjmStackFrame raw{};raw.tree_id=4096;raw.node_id=0;f.source.common.stack.push_back(raw);
        f.source.tail.person->object_uses.push_back({2,1,0});
        SavedRecoveryPersonService service(f.context());std::string error;
        require(!service.prepare({f.binding()},error),
            "nonzero tail and arbitrary zero extent cannot masquerade as metadata padding");
    }
}
}
int main(){try{controller_bundle_recovery();controller_bundle_recovery_failures();self_reset();notification_selection();notification_policy_selection();admission();retained_queued_target_is_not_silent_noop();queued_drop_rescans_actual_queue();native_use_unwind();local_padding_rejection();std::cout<<"saved person recovery adapter passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
