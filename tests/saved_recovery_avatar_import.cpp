// SPDX-License-Identifier: MPL-2.0
#include "saved_recovery_avatar_import.hpp"
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
    EntityThreadControllers controls{world,1};
    unsigned dispatched=0;
    Vm vm;
    SavedWorldEntity source{};
    Objt types{};
    WorldEntityReference actor{},target{};
    bool lease=true,recover=true;
    Fixture():vm({},world.vm_memory(),{},[this](const auto&,const auto&,const auto&,const auto&){
        ++dispatched;return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};},{{77,code()},{88,code()}}){
        std::array<std::int16_t,68> words{};words[1]=2;words[3]=-1;
        require(world.create_saved_entity(1,EntityKind::avatar,1001,{},std::vector<std::int16_t>(88),
            std::vector<std::int16_t>(16),words,{},3),"create original avatar");
        require(world.create_runtime_object(2,1002,{},{}),"create original action target");
        actor={1,world.find(1)->generation};target={2,world.find(2)->generation};
        VmContext context{1,1,0,77};require(world.bind_context_live(context),"bind constructor Main context");
        require(vm.start(routine_key(4096),context)==VmStatus::running,"undispatched constructor Main");
        require(controls.bind(vm),"bind actual avatar controllers");
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
        c.will_reset_before_dispatch=[this](auto){return recover;};return c;
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
int main(){try{hydration();rejected_profiles();stale_and_empty();original_identity_and_cohort();split_controller_ownership();controller_preimage();std::cout<<"saved avatar execution hydration passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
