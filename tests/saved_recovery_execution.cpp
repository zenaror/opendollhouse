// SPDX-License-Identifier: MPL-2.0
#include "saved_recovery_execution.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/sleep.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <limits>
#include <type_traits>
#include <utility>
#include <stdexcept>
using namespace odh;
using namespace odh_host;
namespace {
void need(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
Instruction assign(std::uint8_t scope,std::int16_t value,std::uint8_t next){
    Instruction i{2,next,255,{}};i.operands={0,0,std::uint8_t(value),std::uint8_t(std::uint16_t(value)>>8),0,5,scope,7};return i;
}
Bhav phase(bool deletion=false){return {0x8002,{assign(8,31,1),assign(42,31234,2),{std::uint16_t(deletion?18:99),254,255,{}}},{},Bhav::Metadata{0,6,0,0}};}
struct Fixture {
    World world;WorldEntityReference ref;std::unique_ptr<Vm> thread;
    bool authorized=true,in_scope=false,delete_in_entry=false;unsigned scope_calls=0,entry_calls=0,main_calls=0,traces=0;
    std::function<void()> entry_effect;
    std::vector<std::int16_t> expected_arguments{7,0,0,0,0,0};
    Fixture(std::uint16_t load=4098,std::uint16_t main=4097,bool deletion=false,std::uint16_t init=4096,bool empty_roots=false,bool avatar=false,
            std::uint16_t skipped=0,bool short_table=false):delete_in_entry(deletion){
        std::vector<EntryPoint> table(27);table[0].action=init;table[1].action=main;table[2].action=load;table[3].action=4099;table[4].action=skipped;table[6].action=4098;table[9].action=4099;table[10].action=4096;
        if(short_table)table.resize(4);
        need(avatar?(world.create(8,EntityKind::avatar)&&world.define_type(8,100)&&world.define_entry_points(8,table)):
            world.create_runtime_object(8,100,{},table),"actual source entity constructor");ref={8,world.find(8)->generation};
        const Bhav empty{0x8002,{}, {},Bhav::Metadata{0,6,0,0}};
        RoutineSet bank{{routine_key(4096),empty_roots?empty:phase(deletion)},{routine_key(4098),empty_roots?empty:phase(deletion)},{routine_key(4099),phase(deletion)},
            {routine_key(4097),Bhav{0x8002,{{0,254,255,{}}},{},Bhav::Metadata{0,6,0,0}}}};
        thread=std::make_unique<Vm>(RoutineSet{},world.vm_memory(),VmLimits{},
            [this](const auto&,const auto&,const auto&,const auto&){++main_calls;return PrimitiveOutcome{PrimitiveDisposition::wait,{}};},RoutineOwners{{9000,std::move(bank)}});
        need(thread->start(routine_key(4097),{8,8,0,9000},{10,20,30,40},true)==VmStatus::running,"actual existing Main frame");
        thread->temps()[0]=7;thread->temp_xl()[0]=123456;
    }
    SavedRecoveryExecutionContext context(){
        SavedRecoveryExecutionContext c{world,ref,thread.get(),9000};
        c.authority=[this]{return authorized;};
        // This fixture owns only a VM: no host playback/routing/dialog owners.
        c.external_owners_detached=[this](auto identity,Vm& actual,CodeOwner owner){
            return identity==ref&&thread.get()==&actual&&owner==9000;};
        c.thread_for=[this](auto identity){return identity==ref?thread.get():nullptr;};
        c.owner_for=[this](auto identity)->std::optional<CodeOwner>{return identity==ref&&thread?std::optional<CodeOwner>{9000}:std::nullopt;};
        c.guarded_operation=[this](Vm& actual,WorldEntityReference identity,CodeOwner owner,const auto& operation){
            need(thread.get()==&actual&&identity==ref&&owner==9000,"guard receives the reacquired exact Thread/World owner");
            need(!in_scope,"real scope does not reenter");++scope_calls;in_scope=true;
            struct End{bool& value;~End(){value=false;}}end{in_scope};return operation();};
        c.synchronous_primitive=[this](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
            need(in_scope&&instruction.opcode==(delete_in_entry?18:99)&&frame.arguments==expected_arguments&&frame.exact_arguments,
                "actual guarded lifecycle dispatch sees its exact source argument copy");
            need(dispatch.origin!=thread.get()&&frame.context.code_owner==9000&&
                vm_context_reference(frame.context,VmEntityRole::caller)==ref&&vm_context_reference(frame.context,VmEntityRole::callee)==ref,
                "existing Thread native evaluator retains actual resource/identity binding");
            ++entry_calls;VmFrame write;write.context={8,8,0,9000};need(world.bind_context_live(write.context)&&
                world.write(write,3,0,std::int16_t(world.find(8)->object_words.at(0)+1)),"entry performs real same-World effect");
            if(entry_effect)entry_effect();
            if(delete_in_entry){need(world.mark_dead(ref),"actual lifecycle owner marks exact body dead");return PrimitiveOutcome{PrimitiveDisposition::lifecycle_deleted,{}};}
            return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
        };
        c.trace=[this](const auto&){need(in_scope,"trace observes same real lifecycle scope");++traces;};
        c.lifecycle_deleted=[this](auto identity){return world.reference_is_dead(identity);};return c;
    }
};
void native_placement9(){
    Fixture f;const auto before=f.thread->snapshot();auto context=f.context();
    SavedRecoveryExecutionRequest request;request.phase=SavedRecoveryExecutionPhase::placement9;request.stack_object=f.ref;
    const auto primitive=context.synchronous_primitive;
    context.synchronous_primitive=[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
        need(frame.context.stack_object==0&&frame.context.code_owner==9000,"native EP9 uses receiver owner and null StackObject");
        return primitive(frame,instruction,temps,dispatch);
    };
    const auto result=execute_saved_recovery_execution(context,request);
    auto expected=before;expected.temps[0]=31;expected.temp_xl[0]=31234;
    need(result.completed&&result.action==4099&&result.instructions==3&&f.entry_calls==1&&
        encode_vm_snapshot(f.thread->snapshot())==encode_vm_snapshot(expected),
        "native placement EP9 copies receiver TEMP, shares its effects, preserves parked Main and uses StackObject0");
}
void native_wall6(){
    // Native4351ab..4351b5 supplies NULL arguments and StackObject0. The
    // installed root metadata requests six words from this receiver's TEMP.
    for(const bool authorized:{false,true}){
        Fixture f;f.expected_arguments={17,23,31,47,59,71};
        std::copy(f.expected_arguments.begin(),f.expected_arguments.end(),f.thread->temps().begin());
        const auto before=f.thread->snapshot();auto context=f.context();
        SavedRecoveryExecutionRequest request;request.phase=SavedRecoveryExecutionPhase::wall6;request.stack_object=f.ref;
        const auto primitive=context.synchronous_primitive;
        context.synchronous_primitive=[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
            need(frame.context.stack_object==0&&frame.context.code_owner==9000,
                "native wall EP6 retains receiver owner and null StackObject");
            return primitive(frame,instruction,temps,dispatch);
        };
        f.authorized=authorized;const auto result=execute_saved_recovery_execution(context,request);
        auto expected=before;if(authorized){expected.temps[0]=31;expected.temp_xl[0]=31234;}
        need(result.completed==authorized&&result.effects_started==authorized&&
            f.entry_calls==unsigned(authorized)&&f.main_calls==0&&
            f.world.find(8)->object_words.at(0)==std::int16_t(authorized)&&
            encode_vm_snapshot(f.thread->snapshot())==encode_vm_snapshot(expected)&&
            (!authorized||(result.action==4098&&result.instructions==3)),
            "wall EP6 uses metadata-count target TEMP defaults, real World and shared registers without dispatching parked Main; expired source refuses");
    }
}
void native_remove10(){
    // MasterID selection belongs to RemoveFromWorld's caller. This executor
    // only borrows the actual receiver's installed EP10 and register banks.
    for(unsigned mode=0;mode<3;++mode){
        Fixture f;const auto before=f.thread->snapshot();auto context=f.context();
        SavedRecoveryExecutionRequest request;request.phase=SavedRecoveryExecutionPhase::remove10;
        request.stack_object=f.ref; // Only EP4 may use this field.
        const auto primitive=context.synchronous_primitive;
        context.synchronous_primitive=[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
            need(frame.context.stack_object==0&&frame.context.code_owner==9000,
                "native EP10 uses the receiver owner and null StackObject");
            return primitive(frame,instruction,temps,dispatch);
        };
        if(mode==1)f.authorized=false;
        if(mode==2)f.entry_effect=[&]{f.authorized=false;};
        const auto result=execute_saved_recovery_execution(context,request);
        if(mode==1){
            need(!result.completed&&!result.effects_started&&f.entry_calls==0&&f.scope_calls==0&&
                f.world.find(8)->object_words.at(0)==0&&
                encode_vm_snapshot(f.thread->snapshot())==encode_vm_snapshot(before),
                "expired EP10 source refuses before dispatch or same-World effects");
        }else{
            auto expected=before;expected.temps[0]=31;expected.temp_xl[0]=31234;
            need(result.completed==(mode==0)&&result.effects_started&&result.action==4096&&result.instructions==3&&
                f.entry_calls==1&&f.scope_calls==1&&f.main_calls==0&&f.world.find(8)->object_words.at(0)==1&&
                encode_vm_snapshot(f.thread->snapshot())==encode_vm_snapshot(expected),
                "EP10 copies target TEMP into arguments, shares registers and real World effects, keeps Main parked; expired authority cannot roll effects back");
        }
    }
}
void phases_and_restart(){
    Fixture f;auto context=f.context();const auto before=f.thread->snapshot();
    const auto encoded=encode_vm_snapshot(before);
    auto request=SavedRecoveryExecutionRequest{};
    need(preflight_saved_recovery_execution(context,request).supported&&encode_vm_snapshot(f.thread->snapshot())==encoded&&f.scope_calls==0,
        "whole-phase preflight reads actual ownership without execution/register effects");
    const auto loaded=execute_saved_recovery_execution(context,request);
    auto native_expected=before;native_expected.temps[0]=31;native_expected.temp_xl[0]=31234;
    need(loaded.completed&&loaded.instructions==3&&f.thread->temps()[0]==31&&f.thread->temp_xl()[0]==31234&&
        encode_vm_snapshot(f.thread->snapshot())==encode_vm_snapshot(native_expected)&&f.world.find(8)->object_words.at(0)==1,
        "native Load copies metadata-count TEMP arguments and shares target TEMP/XL independently of caller sharing flag");
    f.expected_arguments[0]=31;
    request.phase=SavedRecoveryExecutionPhase::cleanup3;request.share_registers=true;
    const auto cleaned=execute_saved_recovery_execution(context,request);auto after=f.thread->snapshot();
    need(cleaned.completed&&f.thread->temps()[0]==31&&f.thread->temp_xl()[0]==31234&&after.stack.size()==before.stack.size()&&
        vm_frame_equal(after.stack.front(),before.stack.front())&&after.status==before.status&&after.steps==before.steps&&after.run_generation==before.run_generation,
        "source cleanup shares both real register banks and preserves existing frames/work/run identity");
    request.phase=SavedRecoveryExecutionPhase::init0;request.share_registers=false;
    need(execute_saved_recovery_execution(context,request).completed&&f.scope_calls==3&&f.entry_calls==3&&f.traces==9,
        "Init remains separately executable and guarded; phase ordering belongs to caller");
    request.phase=SavedRecoveryExecutionPhase::restart_main;const auto calls=f.main_calls,entries=f.entry_calls;
    const auto restarted=execute_saved_recovery_execution(context,request);after=f.thread->snapshot();
    need(restarted.completed&&restarted.main_started&&after.stack.size()==1&&after.stack.front().pc==0&&after.stack.front().exact_arguments&&
        after.stack.front().arguments==std::vector<std::int16_t>({0,0,0,0})&&f.main_calls==calls&&f.entry_calls==entries&&after.steps==0&&
        after.temps[0]==31&&after.temp_xl[0]==31234&&after.run_generation==before.run_generation+2,
        "restart resets actual execution then pushes exact Main with preserved registers and no dispatch");
}
void native_defaults_and_skipped4(){
    Fixture f(4098,4097,false,4096,false,false,4098);need(f.world.create(2,EntityKind::avatar),"actual skipped-action person");
    const WorldEntityReference person{2,f.world.find(2)->generation};
    Vm person_thread({},f.world.vm_memory());person_thread.temps()[0]=991;person_thread.temps()[1]=992;
    f.thread->temps()[0]=17;f.thread->temps()[1]=23;f.thread->temps()[5]=-8;
    f.expected_arguments={17,23,0,0,0,-8};
    auto c=f.context();const auto primitive=c.synchronous_primitive;
    c.synchronous_primitive=[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
        need(vm_context_reference(frame.context,VmEntityRole::stack_object)==person&&
            frame.arguments==f.expected_arguments&&std::as_const(*f.thread).temps()[0]==31,
            "EP4 uses exact person StackObject but copies target TEMP before its shared writes");
        need(frame.arguments[0]==17&&frame.arguments[1]==23,
            "native frame arguments retain copied values after target TEMP0 changed to31");
        auto outcome=primitive(frame,instruction,temps,dispatch);
        outcome.variable_write=VmVariableWrite{8,1,777};
        return outcome;
    };
    SavedRecoveryExecutionRequest request;request.phase=SavedRecoveryExecutionPhase::skipped4;request.stack_object=person;
    const auto before=encode_vm_snapshot(f.thread->snapshot());const auto calls=f.entry_calls;
    const auto prepared=preflight_saved_recovery_execution(c,request);
    need(prepared.supported&&prepared.argument_count==6&&f.entry_calls==calls&&f.scope_calls==0&&
        encode_vm_snapshot(f.thread->snapshot())==before,"EP4 preflight reads metadata and identity without primitive dispatch");
    const auto result=execute_saved_recovery_execution(c,request);
    need(result.completed,("native EP4 failed: "+result.diagnostic).c_str());
    need(result.completed&&f.thread->temps()[0]==31&&f.thread->temps()[1]==777&&f.thread->temp_xl()[0]==31234&&
        person_thread.temps()[0]==991&&person_thread.temps()[1]==992&&
        f.thread->stack().front().arguments==std::vector<std::int16_t>({10,20,30,40}),
        "EP4 shares only target registers; person TEMP and parked action arguments do not supply entry arguments");
    request.argument_mode=SavedRecoveryArgumentMode::explicit_four_zeros;request.share_registers=false;
    f.expected_arguments={0,0,0,0};auto explicit_context=f.context();f.thread->temps()[0]=66;f.thread->temp_xl()[0]=555;
    need(execute_saved_recovery_execution(explicit_context,request).completed&&f.thread->temps()[0]==66&&f.thread->temp_xl()[0]==555,
        "explicit four-zero mode honors nonsharing independently of native default behavior");
    auto stale=request;++stale.stack_object.generation;
    const auto unchanged=encode_vm_snapshot(f.thread->snapshot());
    need(!execute_saved_recovery_execution(explicit_context,stale).effects_started&&encode_vm_snapshot(f.thread->snapshot())==unchanged,
        "stale EP4 person cannot borrow target registers or dispatch");
    const auto guard=explicit_context.guarded_operation;
    explicit_context.guarded_operation=[&](auto& thread,auto ref,auto owner,const auto& operation){
        need(f.world.remove(2)&&f.world.create(2,EntityKind::avatar),"reuse person ID between preflight and execution");
        return guard(thread,ref,owner,operation);
    };
    need(!execute_saved_recovery_execution(explicit_context,request).effects_started&&encode_vm_snapshot(f.thread->snapshot())==unchanged,
        "EP4 reacquires exact person inside guard and never follows reused ID");
    Fixture removed(4098,4097,false,4096,false,false,4098);need(removed.world.create(2,EntityKind::avatar),"EP4 person removed during callback");
    auto removed_context=removed.context();const auto removed_primitive=removed_context.synchronous_primitive;
    removed_context.synchronous_primitive=[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
        need(removed.world.remove(2),"actual EP4 callback removes its person");
        return removed_primitive(frame,instruction,temps,dispatch);
    };
    SavedRecoveryExecutionRequest removed_request;removed_request.phase=SavedRecoveryExecutionPhase::skipped4;
    removed_request.stack_object={2,removed.world.find(2)->generation};
    const auto lost=execute_saved_recovery_execution(removed_context,removed_request);
    need(!lost.completed&&lost.effects_started&&removed.thread->temps()[0]==31,
        "EP4 revalidates person after execution and does not promise rollback of completed target effects");
    Fixture short_table(4098,4097,false,4096,false,false,0,true);
    need(short_table.world.create(2,EntityKind::avatar),"known short EP4 table person");
    request.stack_object={2,short_table.world.find(2)->generation};
    const auto skipped=execute_saved_recovery_execution(short_table.context(),request);
    need(skipped.completed&&skipped.no_action&&!skipped.effects_started&&short_table.scope_calls==0,
        "known short EP4 table is a no-op, not unknown resource metadata");
}
void native_metadata_limits(){
    for(const auto count:{std::uint8_t{0},std::uint8_t{8},std::uint8_t{9}}){
        Fixture f;auto state=f.thread->snapshot();auto routines=f.thread->shared_program_for_owner(9000)->routines();
        routines.at(routine_key(4098)).metadata->arguments=count;
        auto replacement=std::make_unique<Vm>(RoutineSet{},f.world.vm_memory(),VmLimits{},VmPrimitive{},RoutineOwners{{9000,std::move(routines)}});
        need(replacement->restore_snapshot(state),"install actual metadata-specific native entry bank");f.thread=std::move(replacement);
        f.expected_arguments.assign(count,0);if(count)f.expected_arguments[0]=7;
        const auto before=encode_vm_snapshot(f.thread->snapshot());const auto prepared=preflight_saved_recovery_execution(f.context(),{});
        if(count>8)need(!prepared.supported&&!execute_saved_recovery_execution(f.context(),{}).effects_started&&
            encode_vm_snapshot(f.thread->snapshot())==before&&f.scope_calls==0,"native argument count beyond eight TEMP words refuses before effects");
        else need(prepared.supported&&prepared.argument_count==count&&execute_saved_recovery_execution(f.context(),{}).completed,
            "zero through eight metadata arguments copy exact target TEMP extent");
    }
    const std::vector<std::pair<std::uint16_t,std::uint8_t>> legacy_cases{
        {0x8000,0},{0x8001,0},{0x8001,1},{0x8001,2},{0x8001,3},{0x8001,4},{0x8001,5},{0x8001,255},{0x8002,0}};
    for(const auto [version,raw_count]:legacy_cases){
        Fixture f;auto state=f.thread->snapshot();auto routines=f.thread->shared_program_for_owner(9000)->routines();
        auto& root=routines.at(routine_key(4098));root.version=version;root.metadata.reset();root.header[1]=raw_count;
        auto replacement=std::make_unique<Vm>(RoutineSet{},f.world.vm_memory(),VmLimits{},VmPrimitive{},RoutineOwners{{9000,std::move(routines)}});
        need(replacement->restore_snapshot(state),"install actual legacy/unknown metadata entry");f.thread=std::move(replacement);
        const auto prepared=preflight_saved_recovery_execution(f.context(),{});
        if(version==0x8002)need(!prepared.supported&&!execute_saved_recovery_execution(f.context(),{}).effects_started&&f.scope_calls==0,
            "missing current-format argument metadata is unsupported before effects");
        else {
            const auto count=version==0x8000?std::size_t{4}:std::min<std::size_t>(raw_count,4);
            f.expected_arguments.assign(count,0);if(count)f.expected_arguments[0]=7;
            need(prepared.supported&&prepared.argument_count==count&&execute_saved_recovery_execution(f.context(),{}).completed,
                "native8000 defaults four; native8001 uses exact raw count clamped to four, including zero");
        }
    }
}
void preflight_and_guards(){
    Fixture f;auto c=f.context();SavedRecoveryExecutionRequest request;request.phase=SavedRecoveryExecutionPhase::restart_main;
    const auto before=encode_vm_snapshot(f.thread->snapshot());
    auto no_proof=c;no_proof.external_owners_detached={};
    need(!execute_saved_recovery_execution(no_proof,request).effects_started&&encode_vm_snapshot(f.thread->snapshot())==before,
        "VM-local idle state alone cannot authorize restart without external owner proof");
    auto held=c;held.external_owners_detached=[](auto,auto&,auto){return false;};
    need(!execute_saved_recovery_execution(held,request).effects_started&&encode_vm_snapshot(f.thread->snapshot())==before,
        "retained host owner rejects restart before register/frame mutation");
    auto changed=c;const auto original_guard=changed.guarded_operation;
    bool released=true;changed.external_owners_detached=[&](auto,auto&,auto){return released;};
    changed.guarded_operation=[&](auto& thread,auto identity,auto owner,const auto& operation){
        released=false;return original_guard(thread,identity,owner,operation);};
    need(!execute_saved_recovery_execution(changed,request).effects_started&&encode_vm_snapshot(f.thread->snapshot())==before,
        "external owner reacquired between preflight and guard blocks reset");
    auto wrong=c;wrong.owner=9001;need(!execute_saved_recovery_execution(wrong,request).completed&&encode_vm_snapshot(f.thread->snapshot())==before,"wrong actual owner rejected before reset");
    auto stale=c;++stale.entity.generation;need(!preflight_saved_recovery_execution(stale,request).supported,"stale generation cannot reach stored Thread");
    auto missing_bank=c;missing_bank.owner=7000;missing_bank.owner_for=[](auto)->std::optional<CodeOwner>{return 7000;};
    need(!preflight_saved_recovery_execution(missing_bank,request).supported&&encode_vm_snapshot(f.thread->snapshot())==before,"missing installed owner bank never triggers resolver or reset");
    unsigned resolves=0;need(f.thread->set_owner_resolver([&](auto)->std::shared_ptr<const VmProgram>{++resolves;return {};}),"actual lazy resolver installed");
    need(!preflight_saved_recovery_execution(missing_bank,request).supported&&resolves==0,"read-only missing bank proof never invokes lazy owner installation");
    Fixture no_main(4098,4999);auto no_main_context=no_main.context();request.phase=SavedRecoveryExecutionPhase::restart_main;
    const auto absent=execute_saved_recovery_execution(no_main_context,request);
    need(absent.completed&&absent.no_action&&!absent.main_started&&no_main.thread->stack().empty()&&no_main.thread->status()==VmStatus::returned_false&&
        no_main.thread->temps()[0]==7&&no_main.thread->temp_xl()[0]==123456&&no_main.main_calls==0,
        "known absent Main leaves genuine reset idle, retains registers and invents no routine");
}
void split_object_reset_sequence(){
    static_assert(!std::is_copy_constructible_v<SavedRecoveryResetAuthority>&&!std::is_move_constructible_v<SavedRecoveryResetAuthority>);
    static_assert(!std::is_copy_assignable_v<SavedRecoveryResetAuthority>&&!std::is_move_assignable_v<SavedRecoveryResetAuthority>);
    Fixture f;auto c=f.context();
    f.expected_arguments={0,0,0,0};
    SavedRecoveryExecutionRequest clear;clear.phase=SavedRecoveryExecutionPhase::reset_execution;
    SavedRecoveryExecutionRequest cleanup;cleanup.phase=SavedRecoveryExecutionPhase::cleanup3;cleanup.share_registers=true;
    cleanup.argument_mode=SavedRecoveryArgumentMode::explicit_four_zeros;
    SavedRecoveryExecutionRequest main;main.phase=SavedRecoveryExecutionPhase::start_main;
    const auto before=encode_vm_snapshot(f.thread->snapshot());
    need(!execute_saved_recovery_execution(c,main).effects_started&&encode_vm_snapshot(f.thread->snapshot())==before,
        "start-only cannot accept an old running Thread without reset authority");
    f.entry_effect=[&]{need(f.thread->stack().empty()&&!f.thread->pending()&&f.thread->status()==VmStatus::returned_false,
        "each actual Cleanup sees the cleared original Thread before Main preparation");};
    std::shared_ptr<SavedRecoveryResetAuthority> previous;
    const auto initial=f.thread->run_generation();
    for(unsigned pass=0;pass<2;++pass){
        const auto regs=f.thread->temps();const auto xl=f.thread->temp_xl();
        const auto cleared=execute_saved_recovery_execution(c,clear);
        need(cleared.completed&&cleared.reset_authority&&!cleared.main_started&&f.thread->stack().empty()&&
            f.thread->run_generation()==initial+pass*2+1&&f.thread->temps()==regs&&f.thread->temp_xl()==xl,
            "split reset retires old execution once with retained register banks");
        if(previous){main.reset_authority=previous;need(!execute_saved_recovery_execution(c,main).effects_started,
            "previous pass reset lease cannot authorize a later reset run");}
        need(execute_saved_recovery_execution(c,cleanup).completed&&f.thread->temps()[0]==31&&f.thread->temp_xl()[0]==31234,
            "Cleanup shares registers on the cleared actual Thread");
        main.reset_authority=cleared.reset_authority;
        const auto started=execute_saved_recovery_execution(c,main);
        need(started.completed&&started.main_started&&f.thread->run_generation()==initial+pass*2+2&&
            f.thread->steps()==0&&f.thread->stack().size()==1&&f.thread->stack().front().pc==0&&
            f.thread->stack().front().exact_arguments&&f.thread->stack().front().arguments==std::vector<std::int16_t>({0,0,0,0})&&
            f.thread->temps()[0]==31&&f.thread->temp_xl()[0]==31234&&f.main_calls==0,
            "split Main start adds only its start epoch and never dispatches or erases Cleanup registers");
        previous=cleared.reset_authority;
    }
    need(f.entry_calls==2,"two reset passes retain two distinct Cleanup callbacks");
    Fixture absent(4098,4999);auto absent_context=absent.context();
    const auto reset=execute_saved_recovery_execution(absent_context,clear);main.reset_authority=reset.reset_authority;
    const auto started=execute_saved_recovery_execution(absent_context,main);const auto epoch=absent.thread->run_generation();
    need(started.completed&&started.no_action&&!started.main_started&&absent.thread->stack().empty()&&
        !execute_saved_recovery_execution(absent_context,main).effects_started&&absent.thread->run_generation()==epoch,
        "absent Main consumes its lease once without another reset or epoch");
    Fixture rejected;auto denied=rejected.context();const auto guard=denied.guarded_operation;
    denied.guarded_operation=[&](auto& thread,auto identity,auto owner,const auto& operation){
        (void)guard(thread,identity,owner,operation);return false;};
    const auto failed=execute_saved_recovery_execution(denied,clear);main.reset_authority=failed.reset_authority;
    need(!failed.completed&&failed.effects_started&&!execute_saved_recovery_execution(rejected.context(),main).effects_started,
        "failed outer guard never publishes usable start authority despite completed reset effects");
    for(bool has_main:{false,true}){
        Fixture limit(4098,has_main?4097:4999);auto state=limit.thread->snapshot();
        state.run_generation=std::numeric_limits<std::uint64_t>::max()-1;
        auto replacement=std::make_unique<Vm>(RoutineSet{},limit.world.vm_memory(),VmLimits{},VmPrimitive{},
            RoutineOwners{{9000,limit.thread->shared_program_for_owner(9000)->routines()}});
        need(replacement->restore_snapshot(state),"restore genuine boundary execution for split reset budget");
        limit.thread=std::move(replacement);auto binding=limit.context();
        const auto before_limit=encode_vm_snapshot(limit.thread->snapshot());
        const auto cleared=execute_saved_recovery_execution(binding,clear);
        if(has_main)need(!cleared.effects_started&&encode_vm_snapshot(limit.thread->snapshot())==before_limit,
            "split reset reserves both clear/start epochs before effects when Main exists");
        else{
            main.reset_authority=cleared.reset_authority;
            const auto idle=execute_saved_recovery_execution(binding,main);
            need(cleared.completed&&idle.completed&&idle.no_action&&!idle.main_started&&
                limit.thread->run_generation()==std::numeric_limits<std::uint64_t>::max(),
                "no-Main reset consumes only final available epoch and idle start needs no extra capacity");
        }
    }
}
void known_absent_entry_parity(){
    for(bool empty:{false,true})for(const auto phase:{SavedRecoveryExecutionPhase::load2,SavedRecoveryExecutionPhase::init0}){
        Fixture f(empty?4098:4999,4097,false,empty?4096:4998,empty);auto c=f.context();SavedRecoveryExecutionRequest request;request.phase=phase;
        const auto before=encode_vm_snapshot(f.thread->snapshot());const auto checked=preflight_saved_recovery_execution(c,request);
        need(checked.supported&&!checked.executable&&checked.action>255&&f.scope_calls==0,
            "installed known owner admits absent/empty entry root without dispatch or preflight effects");
        VmSubroutineResult native;
        need(c.guarded_operation(*c.thread,c.entity,c.owner,[&]{native=f.thread->evaluate_entry_point_check(routine_key(checked.action),{8,8,0,9000},
            {0,0,0,0},10000,false,c.synchronous_primitive,c.trace,false);return native.started&&native.status==VmStatus::returned_false;}),
            "actual native API enters real scope and completes known absent/empty root as returned false");
        const auto result=execute_saved_recovery_execution(c,request);
        need(result.completed&&!result.no_action&&result.effects_started&&result.vm_status==native.status&&result.instructions==native.steps&&
            result.instructions==0&&f.scope_calls==2&&f.entry_calls==0&&f.traces==0&&encode_vm_snapshot(f.thread->snapshot())==before&&
            f.world.find(8)->object_words.at(0)==0,
            "Load/Init service matches actual native absent/empty semantics and scope without manufacturing code or changing owner registers");
    }
}
void released_sleep_object_restart(){
    World world;std::vector<EntryPoint> entries(27);entries[1].action=4097;
    need(world.create_runtime_object(8,100,{},entries),"real waiting object created");
    const WorldEntityReference ref{8,world.find(8)->generation};unsigned rng=0;
    Sleep sleep(world,[&]{++rng;});
    RoutineSet bank{{routine_key(4097),Bhav{0x8002,{{0,254,255,{}}},{},Bhav::Metadata{0,4,0,0}}}};
    Vm vm(RoutineSet{},world.vm_memory(),{},sleep.primitive(),RoutineOwners{{9000,std::move(bank)}});
    need(sleep.bind(vm)&&sleep.tick(1)&&vm.start(routine_key(4097),{8,8,0,9000},{20,0,0,0},true)==VmStatus::running&&
        vm.run(1)==VmStatus::waiting,"actual Sleep owns pending object instruction");
    const auto before=encode_vm_snapshot(vm.snapshot());const auto old_ticket=vm.pending()->ticket;
    bool released=false;unsigned proofs=0;
    SavedRecoveryExecutionContext c{world,ref,&vm,9000};
    c.authority=[&]{return !vm.dispatch_active();};
    c.thread_for=[&](auto r){return r==ref?&vm:nullptr;};
    c.owner_for=[&](auto r)->std::optional<CodeOwner>{return r==ref?std::optional<CodeOwner>{9000}:std::nullopt;};
    c.guarded_operation=[&](auto& actual,auto r,auto owner,const auto& operation){
        return &actual==&vm&&r==ref&&owner==9000&&operation();};
    c.external_owners_detached=[&](auto r,auto& actual,auto owner){
        ++proofs;need(vm.pending()&&vm.pending()->ticket==old_ticket,"host proof sees intact old VM ticket before reset");
        return r==ref && &actual==&vm && owner==9000 && released && !sleep.active();};
    SavedRecoveryExecutionRequest request;request.phase=SavedRecoveryExecutionPhase::restart_main;
    need(!execute_saved_recovery_execution(c,request).effects_started&&encode_vm_snapshot(vm.snapshot())==before,
        "actual unretired Sleep refuses restart with old VM unchanged");
    released=sleep.discard_for_source_recovery(ref);
    need(released&&encode_vm_snapshot(vm.snapshot())==before,"real service release leaves VM metadata intact");
    const auto restarted=execute_saved_recovery_execution(c,request);
    need(restarted.completed&&restarted.main_started&&proofs>=3&&!vm.pending()&&vm.steps()==0&&rng==0&&
        vm.stack().front().arguments==std::vector<std::int16_t>({0,0,0,0})&&sleep.rebase_after_source_recovery(ref),
        "authenticated object restart retires old metadata without completion/RNG and rebases real Sleep");
}
void pending_and_guard_protocol(){
    {
        Fixture person(4098,4097,false,4096,false,true);auto binding=person.context();
        need(person.thread->run(1)==VmStatus::waiting,"actual avatar retains pending continuation");
        SavedRecoveryExecutionRequest request;request.phase=SavedRecoveryExecutionPhase::restart_main;
        const auto before=encode_vm_snapshot(person.thread->snapshot());unsigned proofs=0;
        binding.external_owners_detached=[&](auto,auto&,auto){++proofs;return true;};
        need(!execute_saved_recovery_execution(binding,request).effects_started&&proofs==0&&
            encode_vm_snapshot(person.thread->snapshot())==before,
            "avatar still requires ordered unwind even if a host would claim external release");
    }
    Fixture f;auto c=f.context();need(f.thread->run(1)==VmStatus::waiting&&f.thread->pending(),"actual Main yields a pending native service");
    c.external_owners_detached=[](auto,auto&,auto){return false;}; // This fixture's waiting service still owns its ticket.
    const auto before=f.thread->snapshot();const auto bytes=encode_vm_snapshot(before);
    SavedRecoveryExecutionRequest restart;restart.phase=SavedRecoveryExecutionPhase::restart_main;
    const auto denied=execute_saved_recovery_execution(c,restart);
    need(!denied.completed&&!denied.effects_started&&f.scope_calls==0&&encode_vm_snapshot(f.thread->snapshot())==bytes,
        "real pending service rejects restart before guard/reset/ticket destruction");
    SavedRecoveryExecutionRequest cleanup;cleanup.phase=SavedRecoveryExecutionPhase::cleanup3;cleanup.share_registers=true;
    need(execute_saved_recovery_execution(c,cleanup).completed,"cleanup can evaluate while preserving parked Main pending ownership");
    auto expected=before;expected.temps[0]=31;expected.temp_xl[0]=31234;
    need(encode_vm_snapshot(f.thread->snapshot())==encode_vm_snapshot(expected),"cleanup leaves all frames/pending/continuations/tickets intact except explicitly shared TEMP/XL");
    for(unsigned mode=0;mode<3;++mode){Fixture actual;auto binding=actual.context();const auto guard=binding.guarded_operation;
        if(mode==0)binding.guarded_operation=[](auto&,auto,auto,const auto&){return true;};
        if(mode==1)binding.guarded_operation=[guard](auto& thread,auto identity,auto owner,const auto& op){
            return guard(thread,identity,owner,[&]{const bool first=op(),second=op();return first&&!second;});};
        if(mode==2)binding.guarded_operation=[guard](auto& thread,auto identity,auto owner,const auto& op){
            guard(thread,identity,owner,op);return false;};
        const auto result=execute_saved_recovery_execution(binding,{});
        need(!result.completed&&actual.entry_calls==(mode?1U:0U)&&result.effects_started==(mode!=0),
            "forged guard success, repeated operation or rejected completed guard cannot report lifecycle completion");
        need(actual.world.find(8)->object_words.at(0)==std::int16_t(mode?1:0),"guard failure never pretends completed World effects rolled back");
    }
    Fixture expiry;auto binding=expiry.context();std::function<bool()> escaped;const auto guard=binding.guarded_operation;
    binding.guarded_operation=[&](auto& thread,auto identity,auto owner,const auto& op){escaped=op;return guard(thread,identity,owner,op);};
    need(execute_saved_recovery_execution(binding,{}).completed,"one actual operation completes before callback lease expires");
    expiry.thread.reset();need(escaped&&!escaped(),"expired guarded operation cannot touch a destroyed Thread or replay effects");
}
void thread_replaced_before_guard(){
    Fixture f;auto binding=f.context();const auto original=binding.thread_for;unsigned lookups=0;
    binding.thread_for=[&](WorldEntityReference identity)->Vm*{
        ++lookups;
        if(lookups==3){f.thread.reset();return nullptr;}
        return original(identity);
    };
    const auto result=execute_saved_recovery_execution(binding,{});
    need(!result.completed&&!result.effects_started&&f.scope_calls==0&&f.entry_calls==0&&
        !f.thread&&f.world.find(8)->object_words.at(0)==0,
        "Thread retired after preflight is reacquired and rejected before lifecycle scope or World effects");
}
void owner_replaced_before_guard(){
    Fixture f;auto binding=f.context();const auto original=binding.owner_for;unsigned lookups=0;
    binding.owner_for=[&](WorldEntityReference identity)->std::optional<CodeOwner>{
        ++lookups;if(lookups==3)return 9001;return original(identity);
    };
    const auto result=execute_saved_recovery_execution(binding,{});
    need(!result.completed&&!result.effects_started&&f.scope_calls==0&&f.entry_calls==0&&
        f.world.find(8)->object_words.at(0)==0,
        "owner changed after preflight rejects before lifecycle scope or World effects");
}
void active_child_ownership(){
    for(bool immediate:{false,true}){
        Fixture f;auto binding=f.context();VmCall call;call.key=routine_key(4096);call.context=f.thread->stack().front().context;
        call.arguments={0,0,0,0};call.exact_arguments=true;
        if(immediate)need(f.thread->inject_immediate(call).has_value(),"actual native immediate child acquired");
        else{need(f.thread->run(1)==VmStatus::waiting&&f.thread->pending(),"actual native external service parent wait");
            need(f.thread->call_pending(f.thread->pending()->ticket,call),"actual native service pushes external child");}
        const auto child=f.thread->stack().back().return_mode;
        need(child==(immediate?VmCallReturnMode::immediate:VmCallReturnMode::external_frame_next_tick)&&!f.thread->pending(),
            "actual child is active before its return or another wait");
        SavedRecoveryExecutionRequest restart;restart.phase=SavedRecoveryExecutionPhase::restart_main;
        binding.external_owners_detached=[](auto,auto&,auto){return false;}; // Child service has not released ownership.
        const auto before=encode_vm_snapshot(f.thread->snapshot());const auto denied=execute_saved_recovery_execution(binding,restart);
        need(!denied.completed&&!denied.effects_started&&f.scope_calls==0&&encode_vm_snapshot(f.thread->snapshot())==before,
            "actual external/immediate child and suspended ownership reject reset before effects");
    }
}
void lifecycle_deletion_and_reentry(){
    Fixture f;auto context=f.context();SavedRecoveryExecutionRequest cleanup;cleanup.phase=SavedRecoveryExecutionPhase::cleanup3;cleanup.share_registers=true;
    bool nested_rejected=false;f.entry_effect=[&]{SavedRecoveryExecutionRequest restart;restart.phase=SavedRecoveryExecutionPhase::restart_main;
        const auto nested=execute_saved_recovery_execution(context,restart);nested_rejected=!nested.completed&&!nested.effects_started;};
    const auto generation=f.thread->run_generation();need(execute_saved_recovery_execution(context,cleanup).completed&&nested_rejected&&
        f.thread->run_generation()==generation&&f.thread->stack().size()==1,"active same-Thread evaluation loan cannot reenter restart or clear parked Main");
    Fixture deleted(4098,4097,true);auto binding=deleted.context();const auto guard=binding.guarded_operation;
    binding.guarded_operation=[&](auto& thread,auto identity,auto owner,const auto& op){
        const bool done=guard(thread,identity,owner,op);deleted.thread.reset();return done;};
    SavedRecoveryExecutionRequest load;load.allow_lifecycle_deletion=true;
    const auto result=execute_saved_recovery_execution(binding,load);
    need(result.completed&&result.lifecycle_deleted&&deleted.world.reference_is_dead(deleted.ref)&&!deleted.thread,
        "typed Load deletion plus exact body proof completes without dereferencing a Thread freed after guard unwind");
    need(!preflight_saved_recovery_execution(binding,load).supported,"stale cached Thread pointer rejects via reacquisition before dereference");
    Fixture unproved(4098,4097,true);auto invalid=unproved.context();invalid.lifecycle_deleted=[](auto){return false;};
    const auto refused=execute_saved_recovery_execution(invalid,load);
    need(!refused.completed&&refused.effects_started&&unproved.world.reference_is_dead(unproved.ref),
        "typed VM deletion stop does not fabricate exact lifecycle identity proof or rollback effects");
}
}
int main(){try{native_placement9();native_remove10();native_wall6();phases_and_restart();native_defaults_and_skipped4();native_metadata_limits();preflight_and_guards();split_object_reset_sequence();known_absent_entry_parity();released_sleep_object_restart();pending_and_guard_protocol();active_child_ownership();thread_replaced_before_guard();owner_replaced_before_guard();lifecycle_deletion_and_reentry();
    std::cout<<"saved recovery actual Thread execution checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
