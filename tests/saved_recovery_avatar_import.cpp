// SPDX-License-Identifier: MPL-2.0
#include "saved_recovery_avatar_import.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/random.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace odh;
using namespace odh_host;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
RoutineSet code(){return {{routine_key(4096),Bhav{0x8002,{{98,254,255,{}}},{},Bhav::Metadata{0,4,2,0}}},
    {routine_key(4097),Bhav{0x8002,{{99,254,255,{}}},{},Bhav::Metadata{0,4,0,0}}}};}
struct Fixture {
    World world;
    RandomState random{12345};unsigned random_reads=0;
    EntityThreadControllers controls{world,1,[this]{++random_reads;random.next(1);}};
    unsigned dispatched=0;
    Vm vm;
    SavedWorldEntity source{};
    Objt types{};
    WorldEntityReference actor{},target{};
    bool lease=true,recover=true;std::optional<std::uint64_t> service_run;unsigned service_commits=0;
    explicit Fixture(RoutineSet resources=code(),bool bind_before_start=false):vm({},world.vm_memory(),{},[this](const auto&,const auto&,const auto&,const auto&){
        ++dispatched;return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};},{{77,resources},{88,resources}}){
        std::array<std::int16_t,68> words{};words[1]=2;words[3]=-1;
        require(world.create_saved_entity(1,EntityKind::avatar,1001,{},std::vector<std::int16_t>(88),
            std::vector<std::int16_t>(16),words,{},3),"create original avatar");
        require(world.create_runtime_object(2,1002,{},{}),"create original action target");
        actor={1,world.find(1)->generation};target={2,world.find(2)->generation};
        VmContext context{1,1,0,77};require(world.bind_context_live(context),"bind constructor Main context");
        if(bind_before_start)require(controls.bind(vm),"bind controllers before actual Main.start");
        require(vm.start(routine_key(4096),context)==VmStatus::running,"undispatched constructor Main");
        if(!bind_before_start)require(controls.bind(vm),"bind actual avatar controllers");
        vm.temps().fill(45);vm.temp_xl().fill(123456);
        source.saved.object_id=1;source.saved.guid=1001;source.serialized_object_type=2;
        source.imported_generation=actor.generation;source.saved.prefix.temps={1,2,3,4,5,6,7,8};
        source.tail.person.emplace();source.tail.person->person_data.resize(88);
        source.tail.person->active_interaction.tta_index=0;
        source.tail.person->last_interaction.tta_index=-1;
        ObjmStackFrame frame{};frame.tree_id=4096;frame.node_id=9;frame.stack_object_id=2;
        frame.parameters={31};frame.locals={41};frame.primitive_state=777;frame.code_owner_obj_type=1;
        source.common.stack.push_back(frame);source.tail.person->object_uses.push_back({2,1,1});
        types.entries.resize(1);types.entries[0].guid=1001;
        ObjmSavedInteraction active{};active.target_id=2;active.icon=0;active.tta_index=0;
        active.uid=65535;active.action_tree_id=4097;active.args={11,12,13,14};
        source.tail.person->active_interaction=active;
        auto queued=active;queued.uid=0;queued.icon=2;queued.args={21,22,23,24};queued.priority=2;queued.flags=256;
        source.tail.person->interaction_queue.push_back(queued);
    }
    SavedRecoveryAvatarBinding binding(){return {actor,&vm,&controls.interactions(),&controls,&source};}
    SavedRecoveryAvatarImportContext context(){
        SavedRecoveryAvatarImportContext c;c.world=&world;c.types=&types;c.source_still_matches=[this]{return lease;};
        const auto original_actor=actor,original_target=target;
        c.original_reference=[original_actor,original_target](std::int16_t id)->std::optional<WorldEntityReference>{
            if(id==1)return original_actor;if(id==2)return original_target;return {};};
        c.resolve_owner=[this](auto,std::uint32_t guid)->std::optional<SavedRecoveryAvatarOwner>{
            if(guid!=1001)return {};return SavedRecoveryAvatarOwner{77,vm.shared_program_for_owner(77)};};
        c.resolve_action=[this](auto,auto,const ObjmSavedInteraction& raw)->std::optional<SavedRecoveryAvatarActionSource>{
            return SavedRecoveryAvatarActionSource{routine_key(raw.action_tree_id),{88,vm.shared_program_for_owner(88)},raw.tta_index};};
        c.will_reset_before_dispatch=[this](auto){return recover;};
        c.prepare_services=[this](auto ref,const ObjmPersonState& person,const VmSnapshot& proposed,std::string& error){
            if(ref!=actor||!world.is_live_reference(ref)||!person.animation.bytes.empty()||!person.carry_animation.bytes.empty()||
               proposed.run_generation!=vm.run_generation()+1||proposed.steps||proposed.pending){error="synthetic empty-clip service preflight rejected";return false;}
            service_run=vm.run_generation();return true;};
        c.services_current=[this](auto ref,std::string& error){const bool valid=lease&&ref==actor&&world.is_live_reference(ref)&&service_run&&*service_run==vm.run_generation()&&!vm.steps();
            if(!valid)error="synthetic service preimage changed";return valid;};
        c.commit_services=[this](auto ref,const VmSnapshot& proposed,std::string& error){const bool valid=ref==actor&&world.is_live_reference(ref)&&service_run&&proposed.run_generation==*service_run+1&&
                vm.run_generation()==proposed.run_generation&&!vm.steps()&&!proposed.pending;
            if(!valid){error="synthetic service commit rejected";return false;}++service_commits;return true;};
        return c;
    }
};
void hydration(){
    Fixture f;const auto run=f.vm.run_generation();std::string error;
    SavedRecoveryAvatarImport importer(f.context());require(importer.prepare({f.binding()},error),error.c_str());
    require(f.vm.run_generation()==run&&f.vm.stack().front().pc==0&&f.controls.interactions().snapshot().actions.empty(),
        "whole-cohort preparation never mutates actual VM or queue");
    const auto result=importer.commit();require(result.completed&&!result.discard_candidate&&result.avatars==1,result.diagnostic.c_str());
    require(f.dispatched==0&&f.vm.status()==VmStatus::dormant&&f.vm.run_generation()==run+1,
        "source frame hydration executes neither old nor new Main");
    const auto& frame=f.vm.stack().front();
    require(frame.pc==9&&frame.context.code_owner==77&&frame.context.callee==2&&frame.context.callee_generation==f.target.generation&&
        frame.arguments==std::vector<std::int16_t>({31,0,0,0})&&frame.locals==std::vector<std::int16_t>({41,0,0,0})&&
        frame.return_mode==VmCallReturnMode::ordinary&&!frame.invocation_ticket,
        "raw PC/prefix and exact original owner preserved with actual metadata zero padding and no invented ticket");
    require(f.vm.temps()[0]==1&&f.vm.temps()[7]==8&&f.vm.temps()[8]==0&&f.vm.temp_xl()[0]==123456,
        "source TEMP hydrated and existing real XL retained");
    const auto q=f.controls.interactions().snapshot();
    require(q.actions.size()==2&&!q.active&&q.sequence==2&&q.actions[0].uid==1&&q.actions[1].uid==2&&
        q.actions[0].queue_uid==65535&&q.actions[1].queue_uid==0&&!q.actions[0].icon_owner&&
        q.actions[1].icon_owner->generation==f.target.generation&&q.actions[0].notify_idle&&q.actions[1].notify_idle&&
        q.actions[0].call.arguments==std::vector<std::int16_t>({11,12,13,14})&&q.actions[1].call.context.code_owner==88,
        "native active header stays pending with original public UIDs/args and exact nullable IconOwner");
    require(!importer.commit().completed,"publication is single-use");
}
void compatible_frames_and_queue(){
    Fixture f;f.recover=false;f.source.common.stack.front().node_id=0;
    f.source.tail.person->object_uses.clear();f.source.tail.person->active_interaction.tta_index=-1;
    const auto motives=f.world.find(1)->motives;const auto run=f.vm.run_generation();
    SavedRecoveryAvatarImport importer(f.context());std::string error;
    require(importer.prepare({f.binding()},error),error.c_str());
    require(f.vm.steps()==0&&f.vm.run_generation()==run,"compatible preparation has no dispatch or publication");
    const auto result=importer.commit();require(result.completed&&!result.discard_candidate,result.diagnostic.c_str());
    const auto& frame=f.vm.stack().front();const auto controls=f.controls.snapshot();
    require(f.vm.status()==VmStatus::running&&f.vm.steps()==0&&f.vm.run_generation()==run+1&&
        frame.pc==0&&frame.original_import&&frame.original_import->primitive_state==777&&
        frame.original_import->result==VmOriginalReturn::normal&&frame.arguments[0]==31&&frame.locals[0]==41&&
        controls.interactions.actions.size()==1&&!controls.interactions.active&&
        controls.interactions.actions.front().queue_uid==0&&f.world.find(1)->motives==motives&&!f.dispatched&&f.service_commits==1,
        "compatible source keeps raw frame/register/action metadata in the same actual graph without Reset");
    require(!controls.sleep.job&&(!controls.sleep.actor||controls.sleep.actor->run_generation==f.vm.run_generation()),
        "actual Sleep owner follows the imported Thread epoch without an invented job or actor");
}
void bind_before_start_admission(){
    const auto compatible=[](Fixture& f){
        f.recover=false;f.source.common.stack.front().node_id=0;
        f.source.tail.person->object_uses.clear();f.source.tail.person->active_interaction.tta_index=-1;
    };
    {
        Fixture f(code(),true);compatible(f);const auto before=encode_vm_snapshot(f.vm.snapshot());
        require(f.controls.snapshot().interactions.run_generation+1==f.vm.run_generation(),
            "reproduce host order: first bind precedes Main.start by one epoch");
        SavedRecoveryAvatarImport importer(f.context());std::string error;
        require(!importer.prepare({f.binding()},error),"missing start admission rejects before publication");
        require(encode_vm_snapshot(f.vm.snapshot())==before&&f.vm.steps()==0&&!f.dispatched&&
            f.controls.snapshot().interactions.run_generation+1==f.vm.run_generation()&&!f.service_commits,
            "missing admission cannot partly publish VM or controllers");
    }
    {
        Fixture f(code(),true);compatible(f);const auto before=encode_vm_snapshot(f.vm.snapshot());
        const auto seed=f.random.seed(),run=f.vm.run_generation();const auto motives=f.world.find(1)->motives;
        require(f.controls.admit_undispatched_start(),"admit one actual undispatched Main.start");
        const auto admitted=f.controls.snapshot();
        require(admitted.interactions.run_generation==run&&encode_vm_snapshot(f.vm.snapshot())==before&&
            !admitted.sleep.actor&&!admitted.sleep.job&&!admitted.sleep.last_tick&&!admitted.sleep.clock&&
            !admitted.interactions.last_tick&&!admitted.interactions.clock&&!admitted.interactions.sequence&&
            admitted.interactions.actions.empty()&&!admitted.interactions.active&&!admitted.interactions.wait&&
            !admitted.thread_interrupt&&!admitted.schedule_idle_start&&f.random.seed()==seed&&!f.random_reads&&!f.dispatched,
            "admission publishes only queue epoch, not clocks/steps/RNG/waits or World motives");
        require(!f.controls.admit_undispatched_start(),"start admission is not replayable");
        SavedRecoveryAvatarImport importer(f.context());std::string error;
        require(importer.prepare({f.binding()},error),error.c_str());
        const auto result=importer.commit();require(result.completed&&!result.discard_candidate,result.diagnostic.c_str());
        const auto final=f.controls.snapshot();
        require(f.vm.run_generation()==run+1&&final.interactions.run_generation==run+1&&f.vm.steps()==0&&
            f.vm.stack().front().original_import&&f.vm.stack().front().original_import->primitive_state==777&&
            final.interactions.actions.size()==1&&final.interactions.actions.front().queue_uid==0&&
            !final.interactions.active&&!final.sleep.job&&!final.sleep.clock&&!final.interactions.clock&&
            f.world.find(1)->motives==motives&&f.random.seed()==seed&&!f.random_reads&&!f.dispatched&&f.service_commits==1,
            "real compatible import retains fresh Sleep and publishes exact source frames/queue without Reset");
    }
    {
        Fixture f(code(),true);const auto context=f.vm.stack().front().context;
        require(f.vm.start(routine_key(4096),context)==VmStatus::running,"second real start without dispatch");
        const auto before=encode_vm_snapshot(f.vm.snapshot());
        require(!f.controls.admit_undispatched_start()&&encode_vm_snapshot(f.vm.snapshot())==before,
            "cannot rebase over more than the single bound Main.start");
    }
    {
        Fixture f(code(),true);require(f.controls.observe(),"observe the actual controller owner");
        require(!f.controls.admit_undispatched_start(),"observed controllers cannot regain freshness");
    }
    {
        Fixture f;require(!f.controls.admit_undispatched_start(),"already current first-bind epoch needs no start admission");
    }
    {
        Fixture f(code(),true);const auto before=encode_vm_snapshot(f.vm.snapshot());
        const auto epoch=f.controls.snapshot().interactions.run_generation;auto stale=f.vm.snapshot();
        ++stale.stack.front().context.caller_generation;
        require(!f.vm.restore_snapshot(stale),"ordinary restore rejects a stale caller generation before admission");
        require(encode_vm_snapshot(f.vm.snapshot())==before&&
            f.controls.snapshot().interactions.run_generation==epoch&&!f.random_reads&&!f.dispatched,
            "stale caller rejection preserves the actual Main and bound controller epoch");
    }
    {
        Fixture f(code(),true);const auto before=encode_vm_snapshot(f.vm.snapshot());
        const auto epoch=f.controls.snapshot().interactions.run_generation;auto stale=f.vm.snapshot();
        ++stale.stack.front().context.callee_generation;
        require(!f.vm.restore_snapshot(stale),"ordinary restore rejects a stale callee generation before admission");
        require(encode_vm_snapshot(f.vm.snapshot())==before&&
            f.controls.snapshot().interactions.run_generation==epoch&&!f.random_reads&&!f.dispatched,
            "stale callee rejection preserves the actual Main and bound controller epoch");
    }
    {
        Fixture f(code(),true);compatible(f);
        require(f.controls.join_at(0),"existing birth snapshot consumes restoration freshness");
        require(!f.controls.admit_undispatched_start(),"join_at cannot manufacture an untouched import owner");
        SavedRecoveryAvatarImport importer(f.context());std::string error;
        require(!importer.prepare({f.binding()},error),"birth snapshot is not a replacement for typed start admission");
    }
}
void compatible_source_returns(){
    for(const bool interaction:{false,true}){
        auto resources=code();resources.at(routine_key(4096)).instructions.front().opcode=interaction?17:27;
        Fixture f(resources);f.recover=false;auto& person=*f.source.tail.person;
        auto parent=f.source.common.stack.front();parent.node_id=0;parent.primitive_state=123;
        auto child=parent;child.tree_id=4097;child.primitive_state=456;
        f.source.common.stack={parent,child};person.object_uses={{2,2,1}};
        person.interaction_queue.clear();if(!interaction)person.active_interaction.tta_index=-1;
        auto context=f.context();context.resolve_action=[&](auto,auto,const ObjmSavedInteraction& raw)->std::optional<SavedRecoveryAvatarActionSource>{
            return SavedRecoveryAvatarActionSource{routine_key(raw.action_tree_id),{77,f.vm.shared_program_for_owner(77)},raw.tta_index};};
        SavedRecoveryAvatarImport importer(std::move(context));std::string error;
        require(importer.prepare({f.binding()},error),error.c_str());const auto result=importer.commit();require(result.completed,result.diagnostic.c_str());
        const auto source=f.vm.snapshot();const auto saved=f.controls.snapshot();
        require(source.steps==0&&!source.pending&&source.suspended_pending.empty()&&source.action_tree_known&&
            source.stack.back().original_import->primitive_state==456&&
            source.stack.back().original_import->uses.size()==1&&source.stack.back().original_import->uses.front().target==f.target&&
            source.stack.back().original_import->result==(interaction?VmOriginalReturn::interaction:VmOriginalReturn::retry),
            "source return mapping preserves raw primitive metadata and invents no native wait or work");
        require(interaction?(saved.interactions.active&&source.ticket_sequence==1&&
            saved.interactions.active->ticket==source.stack.back().invocation_ticket):
            (!saved.interactions.active&&source.ticket_sequence==0&&!source.stack.back().invocation_ticket),
            "only the authenticated active Interaction reserves an actual VM ticket");
        const auto bytes=encode_vm_snapshot(source);require(bytes[0]==9,"original continuation selects explicit VM codec9");
        Vm reopened({},f.world.vm_memory(),{},[&](const auto&,const auto&,const auto&,const auto&){
            return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};},{{77,resources},{88,resources}});
        require(reopened.restore_snapshot(decode_vm_snapshot(bytes)),"cold restores source return state without invoking original recovery");
        EntityThreadControllers cold(f.world,1);require(cold.bind(reopened)&&cold.restore_snapshot(saved),"cold actual controllers accept the saved original queue and epoch");
        reopened.run(1);
        require(reopened.stack().size()==1&&reopened.stack().front().pc==0,
            "original child return does not branch the parent's saved primitive");
        if(interaction){
            require(reopened.status()==VmStatus::yielded&&cold.observe()&&cold.interactions().snapshot().actions.empty()&&
                cold.interactions().snapshot().completion.has_value(),"source Interaction closes exactly its active header and yields");
        }else require(reopened.status()==VmStatus::budget_exhausted&&!reopened.pending(),"source Retry reexecutes its actual parent without a fabricated suspended routing job");
    }
}
void ordinary_restore_cannot_import_annotations(){
    Fixture f;VmFrame frame{};frame.key=routine_key(4096);frame.context={1,1,0,77};
    frame.original_import=VmOriginalFrameState{0,VmOriginalReturn::retry};
    bool rejected=false;try{f.vm.prepare_dormant_frames({frame});}catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&f.vm.steps()==0,"ordinary dormant API cannot bypass typed original-return admission");
}

void missing_matching_consumers_reject_before_publication(){
    Fixture f;f.recover=false;f.source.common.stack.front().node_id=0;
    f.source.tail.person->active_interaction.tta_index=-1;f.source.tail.person->object_uses.clear();
    auto context=f.context();context.prepare_services={};SavedRecoveryAvatarImport importer(std::move(context));std::string error;
    const auto run=f.vm.run_generation();require(!importer.prepare({f.binding()},error)&&!error.empty()&&
        f.vm.run_generation()==run&&!f.dispatched&&!f.service_commits&&f.controls.interactions().snapshot().actions.empty(),
        "matching cannot succeed by omitting the actual ConvertAvatar service");
}

void original_import_cannot_recover_a_fault(){
    Fixture f;auto frame=f.vm.stack().front();frame.original_import=VmOriginalFrameState{0,VmOriginalReturn::normal};
    require(f.vm.start(routine_key(4999),frame.context)==VmStatus::fault,"produce missing Main fault without dispatch");
    const auto before=f.vm.snapshot();std::string error;
    require(!f.vm.prepare_original_import({frame},before.temps,[]{return true;},error)&&
        f.vm.status()==VmStatus::fault&&f.vm.run_generation()==before.run_generation&&f.vm.steps()==0,
        "typed original import cannot erase an existing runtime fault even before the first step");
}
void matching_service_publication_guards(){
    for(const bool fail_commit:{false,true}){
        Fixture f;f.recover=false;f.source.common.stack.front().node_id=0;
        f.source.tail.person->active_interaction.tta_index=-1;f.source.tail.person->object_uses.clear();
        auto context=f.context();
        if(fail_commit)context.commit_services=[](auto,const auto&,std::string& error){error="actual service publication refused";return false;};
        SavedRecoveryAvatarImport importer(std::move(context));std::string error;
        require(importer.prepare({f.binding()},error),error.c_str());const auto run=f.vm.run_generation();
        if(!fail_commit)f.service_run.reset();
        const auto result=importer.commit();
        require(!result.completed&&result.discard_candidate==fail_commit&&
            result.diagnostic==(fail_commit?"actual service publication refused":"synthetic service preimage changed")&&
            f.vm.run_generation()==run+(fail_commit?1:0)&&!f.dispatched&&!f.service_commits,
            "expired real service proof prevents publication; post-VM refusal requires whole candidate discard with exact cause");
    }
}
void repeated_source_use_maps_one_boundary(){
    auto resources=code();resources.at(routine_key(4096)).instructions.front().opcode=17;
    Fixture f(resources);f.recover=false;auto parent=f.source.common.stack.front();parent.node_id=0;
    auto child=parent;child.tree_id=4097;f.source.common.stack={parent,child};
    f.source.tail.person->object_uses={{2,2,1},{2,2,1}};f.source.tail.person->interaction_queue.clear();
    auto context=f.context();context.resolve_action=[&](auto,auto,const ObjmSavedInteraction& raw)->std::optional<SavedRecoveryAvatarActionSource>{
        return SavedRecoveryAvatarActionSource{routine_key(raw.action_tree_id),{77,f.vm.shared_program_for_owner(77)},raw.tta_index};};
    SavedRecoveryAvatarImport importer(std::move(context));std::string error;
    require(importer.prepare({f.binding()},error),error.c_str());require(importer.commit().completed,"repeated source use import");
    require(f.vm.snapshot().ticket_sequence==1&&f.vm.stack().back().original_import->uses.size()==2,
        "duplicate ObjectUses map one effective boundary while raw ordered records remain preserved");
}

void rejected_profiles(){
    for(unsigned scenario=0;scenario<5;++scenario){
        Fixture f;auto c=f.context();const auto run=f.vm.run_generation();
        if(scenario==0)f.recover=false;
        if(scenario==1)c.resolve_owner=[](auto,auto)->std::optional<SavedRecoveryAvatarOwner>{return {};};
        if(scenario==2)f.source.common.stack.front().stack_object_id=99;
        if(scenario==3)c.words=1;
        if(scenario==4)c.resolve_action=[](auto,auto,const auto&)->std::optional<SavedRecoveryAvatarActionSource>{return {};};
        SavedRecoveryAvatarImport importer(std::move(c));std::string error;
        require(!importer.prepare({f.binding()},error)&&!error.empty(),"unsupported recovery profile must fail preparation");
        require(f.vm.run_generation()==run&&f.vm.stack().front().pc==0&&f.controls.interactions().snapshot().actions.empty()&&!f.dispatched,
            "rejected source profile changes neither actual execution nor queue");
    }
}
void stale_and_empty(){
    Fixture f;std::string error;SavedRecoveryAvatarImport importer(f.context());
    require(importer.prepare({f.binding()},error),error.c_str());f.lease=false;
    const auto result=importer.commit();require(!result.completed&&!result.discard_candidate&&f.vm.stack().front().pc==0,
        "expired source authority rejected before publication");
    Fixture empty;empty.source.common.stack.clear();empty.source.tail.person->object_uses.clear();
    empty.source.tail.person->active_interaction.tta_index=-1;empty.source.tail.person->interaction_queue.clear();
    SavedRecoveryAvatarImport empty_import(empty.context());require(empty_import.prepare({empty.binding()},error),error.c_str());
    require(empty_import.commit().completed&&empty.vm.stack().empty()&&empty.vm.status()==VmStatus::returned_false&&
        empty.vm.temps()[0]==1&&empty.vm.temp_xl()[0]==123456&&!empty.dispatched,
        "empty source stack imports as empty on the actual VM without fabricating Main");
}
void original_identity_and_cohort(){
    Fixture f;auto context=f.context();
    require(f.world.remove(2)&&f.world.create_runtime_object(2,1002,{},{}),"reuse serialized target ID with a different body");
    require(f.world.find(2)->generation!=f.target.generation,"target body generation changed");
    SavedRecoveryAvatarImport importer(std::move(context));std::string error;
    require(!importer.prepare({f.binding()},error)&&f.vm.stack().front().pc==0,
        "old raw target ID never binds to a new generation");
    Fixture duplicate;SavedRecoveryAvatarImport cohort(duplicate.context());
    require(!cohort.prepare({duplicate.binding(),duplicate.binding()},error)&&duplicate.vm.stack().front().pc==0&&
        duplicate.controls.interactions().snapshot().actions.empty(),
        "later cohort rejection leaves earlier prepared avatar unpublished");
}
void split_controller_ownership(){
    Fixture f;Vm other({},f.world.vm_memory());other.temps()[6]=987;
    EntityThreadControllers split(f.world,1);
    require(split.sleep().bind(other)&&split.interactions().bind(f.vm),"construct split Sleep and queue owners through public bindings");
    require(!split.bound_to(f.vm)&&!split.bound_to(other),"a controller bundle requires both services to share the same VM");
    auto binding=f.binding();binding.controllers=&split;binding.queue=&split.interactions();
    auto context=f.context();unsigned resources=0;const auto resolve=context.resolve_owner;
    context.resolve_owner=[&](auto actor,auto guid){++resources;return resolve(actor,guid);};
    const auto before=f.vm.snapshot();const auto controls=split.snapshot();
    SavedRecoveryAvatarImport importer(std::move(context));std::string error;
    require(!importer.prepare({binding},error)&&!error.empty()&&!resources,
        "split controller owners reject before resource preparation or publication");
    require(!importer.commit().completed&&f.vm.run_generation()==before.run_generation&&
        f.vm.stack().front().pc==before.stack.front().pc&&other.temps()[6]==987&&
        split.snapshot().sleep.clock==controls.sleep.clock&&split.interactions().snapshot().actions.empty()&&!f.dispatched,
        "rejected split bundle preserves both execution owners and their actual services");
}
void controller_preimage(){
    Fixture f;std::string error;SavedRecoveryAvatarImport importer(f.context());
    require(importer.prepare({f.binding()},error),error.c_str());
    const auto before=f.vm.snapshot();
    require(f.controls.advance(1),"advance actual controller clocks without dispatching BHAV instructions");
    const auto clocks=f.controls.snapshot();
    require(f.vm.steps()==before.steps&&f.vm.run_generation()==before.run_generation&&f.vm.stack().front().pc==0&&
        clocks.sleep.clock==1&&clocks.interactions.clock==1&&!f.dispatched,
        "clock-only transition changes actual controllers but leaves execution unchanged");
    const auto result=importer.commit();const auto after=f.controls.snapshot();
    require(!result.completed&&!result.discard_candidate&&f.vm.run_generation()==before.run_generation&&
        f.vm.stack().front().pc==0&&after.sleep.clock==clocks.sleep.clock&&after.interactions.clock==clocks.interactions.clock&&
        after.interactions.actions.empty(),"stale controller preimage rejects publication without rolling back clocks or mutating execution");
}
}
int main(){try{hydration();compatible_frames_and_queue();bind_before_start_admission();compatible_source_returns();ordinary_restore_cannot_import_annotations();missing_matching_consumers_reject_before_publication();original_import_cannot_recover_a_fault();matching_service_publication_guards();repeated_source_use_maps_one_boundary();rejected_profiles();stale_and_empty();original_identity_and_cohort();split_controller_ownership();controller_preimage();std::cout<<"saved avatar execution hydration passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
