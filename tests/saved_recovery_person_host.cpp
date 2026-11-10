// SPDX-License-Identifier: MPL-2.0
#include "saved_recovery_person_host.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace odh;using namespace odh_host;
using Phase=SavedRecoveryPhase;
void require(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
RoutineSet code(){return {{routine_key(4096),Bhav{0x8002,{{98,254,255,{}}},{},Bhav::Metadata{0,4,0,0}}},
    {routine_key(4097),Bhav{0x8002,{{99,254,255,{}}},{},Bhav::Metadata{0,4,0,0}}}};}
struct Fixture {
    World world;EntityThreadControllers controls{world,1};Vm vm;
    SavedWorldEntity source{};Objt types{};WorldEntityReference person{},target{};
    bool lease=true,owners=true,recover=true,entry4_admitted=true;
    unsigned resolutions=0,dispatches=0,cleanup=0,skipped=0,detach=0;std::optional<std::uint64_t> service_run;
    Fixture():vm({},world.vm_memory(),{},[&](const auto&,const auto&,const auto&,const auto&){
        ++dispatches;return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};},{{77,code()},{88,code()}}){
        std::array<std::int16_t,68> words{};words[3]=-1;
        require(world.create_saved_entity(1,EntityKind::avatar,1001,{},std::vector<std::int16_t>(88),
            std::vector<std::int16_t>(16),words,{},1),"original person body");
        require(world.create_runtime_object(2,1002,{},std::vector<EntryPoint>(11))&&
            world.define_entry_points(1,std::vector<EntryPoint>(11))&&world.declare_object_word(2,62,1),"actual referenced object");
        person={1,world.find(1)->generation};target={2,world.find(2)->generation};
        require(world.set_suppress_referenced_reset(person,false)&&world.set_suppress_referenced_reset(target,false),"known native reset policies");
        VmContext main{1,1,0,77};require(world.bind_context_live(main)&&vm.start(routine_key(4096),main)==VmStatus::running&&controls.bind(vm),"actual undispatched controller graph");
        vm.temps().fill(45);vm.temp_xl().fill(123456);
        source.saved.object_id=1;source.saved.guid=1001;source.serialized_object_type=2;source.imported_generation=person.generation;
        source.saved.prefix.temps={1,2,3,4,5,6,7,8};source.tail.person.emplace();source.tail.person->person_data.resize(88);
        source.tail.person->active_interaction.tta_index=0;source.tail.person->last_interaction.tta_index=-1;
        ObjmStackFrame frame{};frame.tree_id=4096;frame.node_id=9;frame.stack_object_id=2;frame.code_owner_obj_type=1;
        source.common.stack.push_back(frame);source.tail.person->object_uses.push_back({2,1,1});
        auto& active=source.tail.person->active_interaction;active.target_id=2;active.action_tree_id=4097;active.uid=17;
        active.args={11,12,13,14};auto queued=active;queued.uid=18;queued.icon=2;source.tail.person->interaction_queue.push_back(queued);
        types.entries.resize(1);types.entries[0].guid=1001;
    }
    SavedRecoveryAvatarBinding binding(){return {person,&vm,&controls.interactions(),&controls,&source};}
    SavedRecoveryPersonHostContext context(){
        SavedRecoveryPersonHostContext c;auto& a=c.avatar_import;auto& p=c.person;
        a.world=p.world=&world;a.types=&types;
        a.source_still_matches=p.source_still_matches=[this]{return lease;};
        a.original_reference=[this](std::int16_t id)->std::optional<WorldEntityReference>{if(id==1)return person;if(id==2)return target;return {};};
        a.resolve_owner=[this](auto ref,std::uint32_t guid)->std::optional<SavedRecoveryAvatarOwner>{
            ++resolutions;if(ref!=person||guid!=1001)return {};return SavedRecoveryAvatarOwner{77,vm.shared_program_for_owner(77)};};
        a.resolve_action=[this](auto ref,auto other,const auto& raw)->std::optional<SavedRecoveryAvatarActionSource>{
            ++resolutions;if(ref!=person||other!=target)return {};
            return SavedRecoveryAvatarActionSource{routine_key(raw.action_tree_id),{88,vm.shared_program_for_owner(88)},raw.tta_index};};
        a.will_reset_before_dispatch=[this](auto){return recover;};
        a.prepare_services=[this](auto ref,const ObjmPersonState& raw,const VmSnapshot& proposed,std::string& error){
            if(ref!=person||!world.is_live_reference(ref)||!raw.animation.bytes.empty()||!raw.carry_animation.bytes.empty()||proposed.run_generation!=vm.run_generation()+1||proposed.steps){error="synthetic service preflight rejected";return false;}
            service_run=vm.run_generation();return true;};
        a.services_current=[this](auto ref,std::string&){return owners&&lease&&ref==person&&service_run&&*service_run==vm.run_generation();};
        a.commit_services=[this](auto ref,const VmSnapshot& proposed,std::string&){return owners&&lease&&ref==person&&service_run&&proposed.run_generation==*service_run+1&&proposed.run_generation==vm.run_generation();};
        c.binding_still_matches=[this](auto ref,const Vm* thread,const InteractionQueue* queue,const EntityThreadControllers* bundle){
            return owners&&ref==person&&thread==&vm&&queue==&controls.interactions()&&bundle==&controls;};
        p.supports_phase=[](auto,auto,std::string&){return true;};
        p.execute_phase=[this](auto ref,Phase phase,std::string&){
            require(ref==person,"lifecycle exact original person");
            if(phase==Phase::person_unwind){++detach;require(!controls.sleep().active(),"host service already released controller wait");return true;}
            if(phase==Phase::restart_main){VmContext context{1,1,0,77};return world.bind_context_live(context)&&vm.reset_execution_state()&&
                vm.start_after_reset(routine_key(4096),context,std::vector<std::int16_t>(4),true)==VmStatus::running;}
            if(phase==Phase::person_cleanup){
                const auto result=vm.evaluate_entry_point_check(routine_key(4097),{1,1,0,77},std::vector<std::int16_t>(4),32,true,
                    [this](const auto&,const auto&,const auto&,const auto&){++cleanup;PrimitiveOutcome result{PrimitiveDisposition::goto_true,{}};
                        result.variable_write=VmVariableWrite{8,0,71};return result;});
                return result.started&&result.status==VmStatus::returned_true;
            }
            // Fixture owns no external graphics or spatial services.
            return true;
        };
        p.supports_entry4=[this](auto other,auto ref,std::string&){return entry4_admitted&&other==target&&ref==person;};
        p.skipped_entry4=[this](auto other,auto ref,std::string&){
            require(other==target&&ref==person,"EP4 actual target/person roles");++skipped;
            VmFrame frame;frame.context.caller=target.id;return world.write(frame,3,25,91);
        };
        p.update_footprint=[this](auto ref,std::string&){return world.is_live_reference(ref);};return c;
    }
};
void full_cohort_cycle(){
    Fixture f;SavedRecoveryPersonHost host(f.context());std::string error;const auto old=f.vm.snapshot();
    require(host.prepare({f.binding()},error),error);const auto resolutions=f.resolutions;
    require(!host.ready()&&!host.dispatch_ready()&&f.vm.run_generation()==old.run_generation&&f.vm.stack().front().pc==0&&
        f.controls.interactions().snapshot().actions.empty(),"prepare freezes resources without hydrating or dispatching");
    // Host owns its source copy; external diagnostic projections are not its
    // continuation metadata after admission.
    f.source.common.stack.front().node_id=123;f.source.tail.person->interaction_queue.clear();
    const auto committed=host.commit();require(committed.completed&&!committed.discard_candidate&&host.ready()&&!host.dispatch_ready(),committed.diagnostic);
    require(f.vm.stack().front().pc==9&&f.controls.interactions().snapshot().actions.size()==2&&f.vm.temps()[0]==1&&
        f.vm.temp_xl()[0]==123456&&!f.dispatches,"same actual graph hydrated with frozen native source and retained XL");
    unsigned recursive=0;
    for(const auto phase:{Phase::person_unwind,Phase::person_visuals,Phase::person_placement,Phase::restart_main,Phase::person_cleanup,Phase::person_actions}){
        SavedRecoveryCall call{phase,f.person,f.person,true,{}};
        call.reset_referenced=[&](auto ref,std::string&){require(ref==f.target,"exact referenced original object");++recursive;return true;};
        require(host.supports(call,error)&&host.execute(call,error),error);
        if(phase!=Phase::person_actions)require(!host.dispatch_ready(),"opaque original thread cannot dispatch before complete reset");
    }
    require(host.dispatch_ready()&&f.detach==1&&recursive==1&&f.cleanup==1&&f.skipped==1&&f.resolutions==resolutions&&
        f.world.find(2)->object_words.at(62)==0&&f.world.find(2)->object_words.at(25)==91&&
        f.controls.interactions().snapshot().actions.empty()&&f.vm.stack().size()==1&&f.vm.stack().front().pc==0&&
        f.vm.temps()[0]==71&&f.vm.temp_xl()[0]==123456&&!f.dispatches,"real unwind, queue removal, Main/EP3 and target EP4 complete without resource I/O or Main dispatch");
    require(!host.commit().completed,"person host hydration is single-use");
}
void compatible_notifier_preserves_continuation(){
    Fixture f;f.recover=false;f.source.common.stack.front().node_id=0;
    f.source.tail.person->active_interaction.tta_index=-1;f.source.tail.person->object_uses.clear();
    SavedRecoveryPersonHost host(f.context());std::string error;
    require(host.prepare({f.binding()},error)&&host.commit().completed&&host.dispatch_ready(),error);
    const auto before=f.vm.snapshot();const auto motive=f.world.find(1)->motives;
    SavedRecoveryCall notification{Phase::notify_person,f.person,f.target,false,{}};
    notification.reset_referenced=[](auto,std::string&){return false;};
    require(host.supports(notification,error)&&host.execute(notification,error),error);
    require(host.dispatch_ready()&&f.detach==0&&f.cleanup==0&&f.vm.run_generation()==before.run_generation&&
        f.vm.stack().size()==before.stack.size()&&vm_frame_equal(f.vm.stack().front(),before.stack.front())&&
        f.world.find(1)->motives==motive&&f.skipped==1&&f.controls.interactions().snapshot().actions.empty(),
        "compatible person remains in real notifications: queued EP4 removal never invents a full Reset");
}

void failures(){
    for(unsigned scenario=0;scenario<4;++scenario){
        Fixture f;auto context=f.context();if(scenario==0)f.recover=false;
        if(scenario==1)f.owners=false;
        if(scenario==2)context.avatar_import.resolve_owner=[](auto,auto)->std::optional<SavedRecoveryAvatarOwner>{return {};};
        if(scenario==3){std::array<std::int16_t,68> words{};require(f.world.create_saved_entity(3,EntityKind::avatar,1003,{},
            std::vector<std::int16_t>(88),{},words,{},0),"non-resident notifiable original person");}
        SavedRecoveryPersonHost host(std::move(context));std::string error;const auto run=f.vm.run_generation();
        require(!host.prepare({f.binding()},error)&&!error.empty()&&!host.ready()&&!host.dispatch_ready()&&
            f.vm.run_generation()==run&&f.controls.interactions().snapshot().actions.empty(),"unsupported compatible/reset proof, owners or omitted person reject without effects");
    }
    {Fixture f;SavedRecoveryPersonHost host(f.context());std::string error;require(host.prepare({f.binding()},error),error);
        const auto run=f.vm.run_generation();f.lease=false;const auto result=host.commit();
        require(!result.completed&&!result.discard_candidate&&f.vm.run_generation()==run,"expired lease rejects before hydration");}
    {Fixture f;SavedRecoveryPersonHost host(f.context());std::string error;require(host.prepare({f.binding()},error),error);
        f.entry4_admitted=false;const auto result=host.commit();require(!result.completed&&result.discard_candidate&&
            f.vm.stack().front().pc==9&&!host.ready()&&!host.dispatch_ready(),"post-hydration consumer refusal requires discarding the private candidate with effects retained");}
    {Fixture f;SavedRecoveryPersonHost host(f.context());std::string error;require(host.prepare({f.binding()},error)&&host.commit().completed,error);
        require(!host.execute({Phase::person_actions,f.person,f.person,true,{}},error)&&!host.dispatch_ready()&&
            f.controls.interactions().snapshot().actions.size()==2,"premature actions phase cannot release dispatch/save guard");}
    {Fixture f;SavedRecoveryPersonHost host(f.context());std::string error;require(host.prepare({f.binding()},error)&&host.commit().completed,error);
        f.owners=false;require(!host.execute({Phase::person_unwind,f.person,f.person,true,{}},error)&&f.detach==0,
            "actual registry loss rejects before dereferencing or detaching borrowed services");}
}
}
int main(){try{full_cohort_cycle();compatible_notifier_preserves_continuation();failures();std::cout<<"saved person host integration passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
