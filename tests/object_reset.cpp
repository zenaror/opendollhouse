#include "odh/object_reset.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/world.hpp"
#include <iostream>
#include <algorithm>
#ifdef ODH_VM_HOST_THREAD_TEST
#include <thread>
#endif
#include <memory>
#include <stdexcept>

namespace {
int checks=0;
void require(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
constexpr odh::CodeOwner owner=0x80000008U;
constexpr std::int16_t id=17;
odh::RoutineKey key(std::uint16_t value){return odh::routine_key(value);}
odh::Bhav routine(std::initializer_list<odh::Instruction> code){return {0x8002,code,{},odh::Bhav::Metadata{0,4,4,0}};}
odh::EntryPointTable table(){odh::EntryPointTable result{};result.entries.resize(9);
    result.entries[0].action=4101;result.entries[1].action=4100;result.entries[3].action=4097;result.entries[8].action=4101;return result;}
}
void active_reset_contract(){
    using namespace odh;
    const auto shared=std::make_shared<const VmProgram>(RoutineSet{});
    const auto entries=[] {auto t=table();t.entries[3].action=0;return t;}();
    // VMThread.HandleResult keeps its dispatched frame object: ordinary branch
    // updates that old frame, whereas FE/FF pop the CURRENT entity stack.
    for(const unsigned destination:{1U,253U,254U,255U}){
        Vm* active=nullptr;std::optional<VmInstructionLease> saved;
        unsigned new_main_calls=0;
        const VmPrimitive primitive=[&](const VmFrame& frame,const Instruction& instruction,const Ts1Temps&,const VmDispatch& dispatch){
            if(instruction.opcode==101){++new_main_calls;return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};}
            require(instruction.opcode==99,"reset fixture dispatches old primitive");
            auto lease=Vm::active_instruction_lease(frame);require(lease&&lease->origin()==active&&lease->matches(frame,instruction,dispatch),"actual callback mints a pointer-bound lease");
            saved=lease;const VmFrame copied=frame;
#ifdef ODH_VM_HOST_THREAD_TEST
            bool foreign_rejected=false;
            std::thread other([&]{foreign_rejected=!lease->valid()&&!lease->origin()&&!lease->call_commit_active()&&
                !Vm::active_instruction_lease(frame)&&!active->instruction_lease(frame);});
            other.join();require(foreign_rejected&&lease->valid(),"live instruction lease is local to its actual executing thread");
#endif
            require(!active->instruction_lease(copied)&&!Vm::active_instruction_lease(copied)&&!lease->matches(copied,instruction,dispatch),"identical copied frame cannot mint or use instruction authority");
            auto wrong=dispatch;++wrong.ticket;
            require(!lease->matches(frame,instruction,wrong),"wrong ticket cannot borrow actual frame lease");
            Vm check(RoutineSet{});
            const auto result=run_object_reset(*lease,check,*active,id,owner,entries,10,{},
                { [&]{return active->source_reset_lifecycle_active();},[&]{return active->source_reset_lifecycle_active();} });
            require(result.completed&&result.main_started&&lease->valid()&&lease->detached()&&lease->matches(frame,instruction,dispatch),"Reset retains the actual dispatched frame and narrow callback capability");
            require(frame.key.id==4096&&frame.arguments[0]==73&&active->stack().size()==1&&active->stack()[0].key.id==4100&&active->stack()[0].arguments[0]==0,"old frame and new Main have separate storage");
            require(!active->source_reset_lifecycle_active(),"target mutation hook capability expires before primitive continues");
            require(active->references_entity(WorldEntityReference{88,900}),"retained old-generation participant remains a strong reference");
            const auto refs=active->strong_entity_references();
            require(std::find(refs.begin(),refs.end(),WorldEntityReference{88,900})!=refs.end(),"retained participant is enumerated for generation pinning");
            PrimitiveOutcome outcome{PrimitiveDisposition::goto_true,{}};
            outcome.write=VmFrameWrite{VmFrameBank::arguments,0,91};return outcome;
        };
        Vm thread(shared,ShareVmProgramTag{}, {},{},primitive,RoutineOwners{{owner,{
            {key(4096),routine({{99,static_cast<std::uint8_t>(destination),1,{}},{102,254,255,{}}})},
            {key(4100),routine({{101,254,255,{}}})}}}});active=&thread;
        VmContext context{id,id,88,owner};context.entity_references_bound=true;
        context.caller_generation=40;context.callee_generation=40;context.stack_object_generation=900;
        thread.start(key(4096),context,{73});
        const auto status=thread.run(1,[&](const VmTrace&){
            require(!Vm::active_instruction_lease(thread.stack().back()),"trace cannot mint instruction authority");
        });
        require(saved&&!saved->valid()&&!saved->call_commit_active(),"callback authority expires after result unwind");
        require(new_main_calls==0&&thread.last_run_instruction_count()==1,"one outer instruction counted across Reset without dispatching Main");
        if(destination<254){
            require(status==VmStatus::budget_exhausted&&thread.stack().size()==1&&thread.stack()[0].pc==0&&thread.stack()[0].arguments[0]==0,"ordinary and FD fallback update old frame only");
            require(thread.run(1)==VmStatus::returned_true&&new_main_calls==1,"new Main runs on its actual later dispatch");
            require(thread.run(4)==VmStatus::returned_true&&thread.last_run_instruction_count()==0,"no-work run reports zero newly dispatched instructions");
        }else require(status==(destination==254?VmStatus::returned_true:VmStatus::returned_false)&&thread.stack().empty(),"FE/FF result pops the new current Main");
        require(!thread.references_entity(WorldEntityReference{88,900}),"retained generation releases after instruction/result unwind");
    }

    {
        std::uint64_t current_generation=700;unsigned resolutions=0;
        VmMemory memory;memory.resolve_live_entity=[&](std::int16_t object)->std::optional<WorldEntityReference>{
            ++resolutions;return object==id?std::optional<WorldEntityReference>{{id,current_generation}}:std::nullopt;
        };
        const RoutineOwners owners{{owner,{{key(4097),routine({{100,254,255,{}}})},
            {key(4100),routine({{101,254,255,{}}})}}}};
        Vm thread(shared,ShareVmProgramTag{},memory,{}, {},owners);
        Vm cleanup(shared,ShareVmProgramTag{},memory,{}, {},owners);
        const auto result=run_object_reset(cleanup,thread,id,owner,table(),10,
            [&](const VmFrame& frame,const Instruction&,const Ts1Temps&,const VmDispatch&){
                require(frame.context.entity_references_bound&&frame.context.caller_generation==700&&frame.context.callee_generation==700,"Cleanup enters with captured exact target body");
                current_generation=701;return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            });
        require(result.completed&&result.main_started&&thread.stack().size()==1&&
            thread.stack()[0].context.entity_references_bound&&thread.stack()[0].context.caller_generation==700&&
            thread.stack()[0].context.callee_generation==700&&resolutions==2,
            "Cleanup numeric-ID reuse never redirects new Main onto the replacement generation");
        memory.resolve_live_entity=[](std::int16_t)->std::optional<WorldEntityReference>{return std::nullopt;};
        Vm absent(shared,ShareVmProgramTag{},memory,{}, {},owners);Vm absent_check(RoutineSet{});
        const auto missing=run_object_reset(absent_check,absent,id,owner,entries);
        require(!missing.completed&&absent.run_generation()==0&&absent.stack().empty(),"known absent target cannot gain Reset mutation authority through a numeric ID");
    }

    // CheckAction runs on a fresh check VM; Reset changes the real entity layer
    // and returning the check must not restore the old parked Main.
    {
        Vm* active=nullptr;unsigned checks_seen=0;std::optional<VmInstructionLease> parent_lease;
        const VmPrimitive primitive=[&](const VmFrame& frame,const Instruction& instruction,const Ts1Temps&,const VmDispatch&){
            if(instruction.opcode==101)return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            parent_lease=active->instruction_lease(frame);
            const auto result=active->evaluate_check(key(4200),frame.context,{},10,
                [&](const VmFrame& check_frame,const Instruction&,const Ts1Temps&,const VmDispatch& dispatch){
                    ++checks_seen;auto source=Vm::active_instruction_lease(check_frame);
                    require(source&&source->origin()!=active&&dispatch.origin==source->origin()&&dispatch.check_mode&&parent_lease->valid(),"fresh source_check inherits exact parent callback lineage");
                    auto* check_vm=const_cast<Vm*>(dispatch.origin);check_vm->temps()[3]=51;check_vm->temp_xl()[1]=62;
                    Vm cleanup(RoutineSet{});const auto reset=run_object_reset(*source,cleanup,*active,id,owner,entries);
                    require(reset.completed&&!active->stack().empty()&&active->stack()[0].key.id==4100,"linked check can Reset ancestor entity execution");
                    return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
                });
            if(!result.started||result.status!=VmStatus::returned_true||active->stack().empty())
                throw std::runtime_error("fresh check result status="+std::string(vm_status_name(result.status))+" stack="+
                    std::to_string(active->stack().size())+" detail="+result.diagnostic);
            require(active->stack()[0].key.id==4100&&active->temps()[3]==51&&active->temp_xl()[1]==62,
                "fresh check shares TEMP/XL without resurrecting retired Main");
            return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
        };
        Vm thread(shared,ShareVmProgramTag{}, {},{},primitive,RoutineOwners{{owner,{
            {key(4096),routine({{99,1,255,{}},{102,254,255,{}}})},
            {key(4100),routine({{101,254,255,{}}})},{key(4200),routine({{100,254,255,{}}})}}}});active=&thread;
        thread.start(key(4096),{id,id,0,owner});
        require(thread.run(1)==VmStatus::budget_exhausted&&checks_seen==1&&thread.stack()[0].key.id==4100,"check result continues against current execution layer");
        require(parent_lease&&!parent_lease->valid(),"parent lease expires after nested check unwinds");
        Vm arbitrary({{key(4200),routine({{100,254,255,{}}})}});arbitrary.start(key(4200));
        const auto root=arbitrary.run_synchronous_check(2,[&](const VmFrame& f,const auto&,const auto&,const auto&){
            require(!Vm::active_instruction_lease(f),"arbitrary query/check root has no lifecycle authority");return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};});
        require(root.status==VmStatus::returned_true,"query root may still run ordinary read-only check");
    }
    // Source AttemptPush may return a retained candidate after CheckAction Reset.
    // Admit it to the NEW Main, with a commit-only proof; old argument outputs
    // must never leak onto that Main. No missing queue header is reconstructed.
    for(const bool as_call:{false,true}){
        Vm* active=nullptr;unsigned child_effects=0;bool committed=false;
        std::optional<VmInstructionLease> saved;const VmPrimitive primitive=[&](const VmFrame& frame,const Instruction& instruction,const Ts1Temps&,const VmDispatch& dispatch){
            if(instruction.opcode==102){++child_effects;return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};}
            if(instruction.opcode==101)return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            auto source=active->instruction_lease(frame);require(source.has_value(),"queue callback owns actual instruction lease");saved=source;
            Vm check(RoutineSet{});require(run_object_reset(*source,check,*active,id,owner,entries).completed,"queue fixture resets current execution");
            PrimitiveOutcome outcome{as_call?PrimitiveDisposition::call:PrimitiveDisposition::continue_next_tick,{}};
            outcome.write=VmFrameWrite{VmFrameBank::arguments,0,222};
            if(as_call){
                outcome.call=VmCall{key(4200),{id,id,0,owner},{},VmCallReturnMode::interaction_next_tick};
                require(active->prepare_instruction_call(*source,frame,instruction,dispatch,*outcome.call),"retained actual source can preflight immutable call resources after Reset");
                const auto copy=frame;
                require(!active->prepare_instruction_call(*source,copy,instruction,dispatch,*outcome.call),"call resource preflight rejects copied source frames");
                outcome.detached_call=source->prepare_detached_call(*outcome.call);
                require(outcome.detached_call.has_value(),"detached source produces exact new-stack call continuation");
                const auto* actual=&frame;const auto lease=*source;const auto operation=instruction;const auto token=dispatch;
                outcome.call_commit=[&,actual,lease,operation,token]{
                    require(!lease.valid()&&lease.call_commit_active(),"commit phase does not reopen handler lifecycle authority");
                    require(lease.matches_call_commit(*actual,operation,token,1),"commit authenticates old instruction and admitted new parent depth");
                    require(active->stack().size()==2&&active->stack()[0].key.id==4100&&active->stack()[0].arguments[0]==0,"detached child attaches to new Main without old argument publication");
                    committed=true;return true;
                };
            }
            return outcome;
        };
        Vm thread(shared,ShareVmProgramTag{}, {},{},primitive,RoutineOwners{{owner,{
            {key(4096),routine({{99,1,255,{}},{103,254,255,{}}})},
            {key(4100),routine({{101,254,255,{}}})},{key(4200),routine({{102,254,255,{}}})}}}});active=&thread;
        thread.start(key(4096),{id,id,0,owner},{11});
        require(thread.run(as_call?2:1)==(as_call?VmStatus::yielded:VmStatus::next_tick),"detached call/nexttick use current scheduling without old pending wait");
        require(!thread.pending()&&thread.stack().size()==1&&thread.stack()[0].key.id==4100&&thread.stack()[0].pc==0&&thread.stack()[0].arguments[0]==0,"current Main remains separate from discarded source outputs");
        require(saved&&!saved->valid()&&!saved->call_commit_active()&&child_effects==(as_call?1U:0U)&&committed==as_call,"detached child effects happen before ordinary queue acknowledgment");
    }

    // Linked Cleanup is a real synchronous source child. Reentering base Reset
    // while InReset is set performs only the avatar override effects.
    {
        Vm* active=nullptr;unsigned clears=0,avatar_effects=0,cleanup_calls=0;
        const auto owners=RoutineOwners{{owner,{
            {key(4096),routine({{99,1,255,{}},{103,254,255,{}}})},
            {key(4097),routine({{100,254,255,{}}})},{key(4100),routine({{101,254,255,{}}})}}}};
        const VmPrimitive primitive=[&](const VmFrame& frame,const Instruction&,const Ts1Temps&,const VmDispatch&){
            auto source=active->instruction_lease(frame);require(source.has_value(),"Cleanup fixture source is actual parent");
            Vm cleanup(shared,ShareVmProgramTag{}, {},{}, {},owners);
            ObjectResetHooks hooks{[&]{++clears;return true;},[&]{++avatar_effects;return true;}};
            const auto outer=run_object_reset(*source,cleanup,*active,id,owner,table(),10,
                [&](const VmFrame& cf,const Instruction&,const Ts1Temps&,const VmDispatch& d){
                    ++cleanup_calls;auto nested_source=Vm::active_instruction_lease(cf);
                    require(active->source_reset_queue_mutation_active()&&!active->source_reset_lifecycle_active(),"Cleanup has exact target queue mutation scope without hook-stage capability");
                    require(nested_source&&nested_source->origin()==d.origin,"Cleanup gets linked setter/handler lifecycle authority");
                    Vm nested(RoutineSet{});const auto result=run_object_reset(*nested_source,nested,*active,id,owner,table(),10,{},hooks);
                    require(result.completed&&result.already_resetting&&!result.main_started&&nested.run_generation()==0,"same InReset skips base clear/Cleanup/Main");
                    return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
                },hooks);
            require(!active->source_reset_queue_mutation_active(),"Cleanup queue capability expires before primitive resumes");
            require(outer.completed&&clears==1&&cleanup_calls==1&&avatar_effects==2,"nested InReset retains base record through nested avatar effects");
            return PrimitiveOutcome{PrimitiveDisposition::continue_next_tick,{}};
        };
        Vm thread(shared,ShareVmProgramTag{}, {},{},primitive,owners);active=&thread;
        thread.start(key(4096),{id,id,0,owner});require(thread.run(1)==VmStatus::next_tick,"source Cleanup reentry completes against current Main");
    }
    // A real child scope can reach its ancestor, but an unrelated constructor
    // does not gain that ancestor's authority merely by being executing.
    {
        Vm* active=nullptr;bool saw_child=false;
        const RoutineOwners owners{{owner,{{key(4096),routine({{99,1,255,{}},{103,254,255,{}}})},
            {key(4100),routine({{101,254,255,{}}})},{key(4200),routine({{100,254,255,{}}})}}}};
        const VmPrimitive primitive=[&](const VmFrame& frame,const Instruction&,const Ts1Temps&,const VmDispatch&){
            auto parent=active->instruction_lease(frame);require(parent.has_value(),"constructor fixture parent lease");
            {
                Vm unrelated(shared,ShareVmProgramTag{}, {},{},[&](const VmFrame& cf,const Instruction&,const Ts1Temps&,const VmDispatch&){
                    auto lease=Vm::active_instruction_lease(cf);require(lease.has_value(),"ordinary unrelated instruction has its own scope");
                    Vm cleanup(RoutineSet{});const auto refused=run_object_reset(*lease,cleanup,*active,id,owner,entries);
                    require(!refused.completed&&active->stack()[0].key.id==4096,"unrelated executing VM cannot Reset active ancestor without a child link");
                    return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
                },owners);
                unrelated.start(key(4200),{id,id,0,owner});
                require(unrelated.run(1,[&](const VmTrace&){require(!parent->valid(),"trace masks even parent lease while unrelated VM executes");})==VmStatus::returned_true,"unrelated VM ordinary effects remain executable");
            }
            Vm linked(shared,ShareVmProgramTag{}, {},{},[&](const VmFrame& cf,const Instruction&,const Ts1Temps&,const VmDispatch&){
                saw_child=true;auto lease=Vm::active_instruction_lease(cf);require(lease&&parent->valid(),"explicit ordinary child keeps ancestor chain live");
                Vm cleanup(RoutineSet{});require(run_object_reset(*lease,cleanup,*active,id,owner,entries).completed,"constructor can Reset explicitly linked ancestor");
                return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            },owners);
            auto scope=linked.authorize_instruction_child(*parent);require(scope&&scope->valid(),"fresh linked constructor admission");
            require(!scope->authenticated_reset_of(*active),"construction link cannot fabricate a typed service retirement receipt");
            linked.start(key(4200),{id,id,0,owner});require(linked.run(1)==VmStatus::returned_true&&scope->valid(),"child effects preserve active parent scope");
            require(!scope->authenticated_reset_of(*active),"ordinary construction Reset does not mint unrelated parked-service retirement proof");
            return PrimitiveOutcome{PrimitiveDisposition::continue_next_tick,{}};
        };
        Vm thread(shared,ShareVmProgramTag{}, {},{},primitive,owners);active=&thread;
        thread.start(key(4096),{id,id,0,owner});require(thread.run(1)==VmStatus::next_tick&&saw_child,"linked ordinary child runs before source result");
    }
    // A failed observer follows already-published base effects; it must neither
    // claim completion nor roll the old stack back into execution.
    {
        Vm* active=nullptr;const VmPrimitive primitive=[&](const VmFrame& frame,const Instruction&,const Ts1Temps&,const VmDispatch&){
            const auto source=active->instruction_lease(frame);Vm check(RoutineSet{});
            const auto result=run_object_reset(*source,check,*active,id,owner,entries,10,{},
                {[]{return false;},{}});
            require(!result.completed&&!result.main_started&&!result.diagnostic.empty()&&active->stack().empty()&&source->detached(),"observer failure retains source partial clear and reports failure");
            return PrimitiveOutcome{PrimitiveDisposition::source_error,"fixture observer failure"};
        };
        Vm thread(shared,ShareVmProgramTag{}, {},{},primitive,RoutineOwners{{owner,{
            {key(4096),routine({{99,1,255,{}},{103,254,255,{}}})},{key(4100),routine({{101,254,255,{}}})}}}});active=&thread;
        thread.start(key(4096),{id,id,0,owner});require(thread.run(1)==VmStatus::fault&&thread.stack().empty(),"failed lifecycle observer is not converted into a successful branch");
    }


    {
        Vm* active=nullptr;unsigned writes=0;
        VmMemory memory;memory.expression_write=[&](const VmFrame& frame,std::uint8_t scope,std::int16_t index,std::int16_t value)->std::optional<bool>{
            ++writes;require(scope==0&&index==0&&value==1,"expression fixture reaches exact source setter");
            const auto lease=Vm::active_instruction_lease(frame);const auto copied=frame;
            require(lease&&lease->origin()==active&&!Vm::active_instruction_lease(copied),"expression setter receives actual-frame lifecycle lease only");
            Vm check(RoutineSet{});require(run_object_reset(*lease,check,*active,id,owner,entries).completed,"generic Expression setter can perform authenticated Reset");
            require(lease->valid()&&lease->detached()&&frame.context.stack_object==88,"setter retains original context across Reset");return true;
        };
        const Instruction assignment{2,1,255,{0,0,1,0,0,5,0,7}};
        Vm thread(shared,ShareVmProgramTag{},memory,{}, {},RoutineOwners{{owner,{
            {key(4096),routine({assignment,{103,254,255,{}}})},{key(4100),routine({{101,254,255,{}}})}}}});active=&thread;
        thread.start(key(4096),{id,id,88,owner});require(thread.run(1)==VmStatus::budget_exhausted&&writes==1&&
            thread.stack().size()==1&&thread.stack()[0].key.id==4100&&thread.stack()[0].context.stack_object==0&&
            thread.stack()[0].pc==0,"Expression branch after setter Reset never modifies the new Main context/PC");
    }
    {
        Vm* active=nullptr;unsigned completed=0;const VmPrimitive primitive=[&](const VmFrame& frame,const Instruction&,const Ts1Temps&,const VmDispatch&){
            const auto source=active->instruction_lease(frame);require(source.has_value(),"bounded Reset fixture owns actual scope");
            for(unsigned n=0;n<33;++n){
                Vm check(RoutineSet{});const auto reset=run_object_reset(*source,check,*active,id,owner,entries);
                if(n<32){require(reset.completed,"bounded source Reset admits each allowed action");++completed;}
                else require(!reset.completed&&!reset.diagnostic.empty()&&source->valid(),"source instruction Reset action cap rejects further side effects explicitly");
            }
            return PrimitiveOutcome{PrimitiveDisposition::continue_next_tick,{}};
        };
        Vm thread(shared,ShareVmProgramTag{}, {},{},primitive,RoutineOwners{{owner,{
            {key(4096),routine({{99,1,255,{}},{103,254,255,{}}})},{key(4100),routine({{101,254,255,{}}})}}}});active=&thread;
        thread.start(key(4096),{id,id,0,owner});require(thread.run(1)==VmStatus::next_tick&&completed==32&&
            thread.last_run_instruction_count()==1&&thread.stack().size()==1,"Reset action cap is instruction-scoped and does not corrupt retained/new stacks");
    }

    // Multiple Reset calls in one primitive retain all displaced generations.
    // InReset ends at base return, before avatar override effects invoke Reset.
    {
        Vm* active=nullptr;unsigned clear_count=0,avatar_count=0;const VmPrimitive primitive=[&](const VmFrame& frame,const Instruction&,const Ts1Temps&,const VmDispatch&){
            const auto source=active->instruction_lease(frame);require(source.has_value(),"multiple Reset owns source lease");
            ObjectResetHooks hooks;hooks.after_stack_cleared=[&]{++clear_count;return true;};
            hooks.before_return_avatar_effects=[&]{
                ++avatar_count;
                if(avatar_count==1){Vm nested(RoutineSet{});const auto result=run_object_reset(*source,nested,*active,id,owner,entries,10,{},hooks);
                    require(result.completed&&!result.already_resetting,"avatar effects after base return begin a new base Reset");}
                return true;
            };
            Vm check(RoutineSet{});require(run_object_reset(*source,check,*active,id,owner,entries,10,{},hooks).completed,"outer active Reset completes");
            require(clear_count==2&&avatar_count==2&&source->valid()&&frame.key.id==4096,"all old stacks remain alive through repeated Reset");
            return PrimitiveOutcome{PrimitiveDisposition::continue_next_tick,{}};
        };
        Vm thread(shared,ShareVmProgramTag{}, {},{},primitive,RoutineOwners{{owner,{
            {key(4096),routine({{99,1,255,{}},{103,254,255,{}}})},{key(4100),routine({{101,254,255,{}}})}}}});active=&thread;
        thread.start(key(4096),{id,id,0,owner});require(thread.run(1)==VmStatus::next_tick&&clear_count==2,"multiple Reset schedules current Main only once per source result");
    }
}

int main(){try {
    active_reset_contract();
    using namespace odh;
    World world;require(world.create(id,EntityKind::object,{1}) && world.declare_object_word(id,12,333),"private reset World");
    const auto shared=std::make_shared<const VmProgram>(RoutineSet{});
    for(const bool returns_false:{false,true}) {
        unsigned main_calls=0,cleanup_calls=0,external_calls=0;
        const VmPrimitive main_handler=[&](const auto&,const auto& instruction,const auto&,const auto&){
            if(instruction.opcode==99){++external_calls;return PrimitiveOutcome{PrimitiveDisposition::wait,{}};}
            require(instruction.opcode==101,"Init/dynamic trap must never execute");++main_calls;
            return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};};
        Vm thread(shared,ShareVmProgramTag{},world.vm_memory(),{},main_handler,
            RoutineOwners{{owner,{{key(4096),routine({{99,254,255,{}}})},{key(4100),routine({{101,254,255,{}}})},
                {key(4101),routine({{102,254,255,{}}})}}}});
        require(thread.start(key(4096),{id,id,44,owner})==VmStatus::running && thread.run(1)==VmStatus::waiting && thread.pending(),"old Main wait fixture");
        thread.temps()[2]=71;thread.temp_xl()[1]=72;
        Vm check(shared,ShareVmProgramTag{},world.vm_memory(),{},main_handler,
            RoutineOwners{{owner,{{key(4097),routine({{100,1,255,{}},
                {2,2,255,{1,0,99,0,0,5,42,7}},
                {103,static_cast<std::uint8_t>(returns_false?255:254),255,{}}})}}}});
        const auto generation=thread.run_generation();
        const auto result=run_object_reset(check,thread,id,owner,table(),10,
            [&](const VmFrame& frame,const Instruction& instruction,const Ts1Temps& temps,const VmDispatch& dispatch){
                ++cleanup_calls;
                require(dispatch.check_mode && frame.context.caller==id && frame.context.callee==id &&
                    frame.context.stack_object==0 && frame.context.code_owner==owner &&
                    frame.arguments==std::vector<std::int16_t>({0,0,0,0}),"Cleanup has source self context and four zero args");
                require(thread.stack().empty() && !thread.pending() && main_calls==0,"old execution is cleared before synchronous Cleanup");
                if(instruction.opcode==100){
                    require(temps[2]==71 && check.temp_xl()[1]==72,"Cleanup clones retained registers before effects");
                    require(world.write(frame,0,0,76),"Cleanup World write belongs to private candidate");
                    PrimitiveOutcome answer{PrimitiveDisposition::goto_true,{}};answer.temp_write=VmTempWrite{2,88};return answer;
                }
                require(instruction.opcode==103 && temps[2]==88 && check.temp_xl()[1]==99,"Cleanup local TEMP and XL effects persist across its instructions");
                return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            });
        require(result.completed && result.main_started && result.steps==3 && result.stages.size()==2 &&
            result.stages[0].phase=="cleanup" && result.stages[0].status==(returns_false?VmStatus::returned_false:VmStatus::returned_true) &&
            result.stages[1].phase=="main" && result.stages[1].status==VmStatus::running,
            "Cleanup true/false completes before Main is only started");
        require(cleanup_calls==2 && main_calls==0 && external_calls==1 && thread.steps()==0 &&
            thread.status()==VmStatus::running && thread.stack().size()==1 && thread.stack()[0].key.id==4100 &&
            thread.stack()[0].pc==0 && thread.stack()[0].context.stack_object==0 &&
            thread.stack()[0].arguments==std::vector<std::int16_t>({0,0,0,0}) && !thread.pending() &&
            thread.run_generation()>generation && thread.temps()[2]==88 && thread.temp_xl()[1]==99 &&
            world.find(id)->attributes[0]==76 && world.find(id)->object_words.at(12)==333,
            "Main preparation preserves shared Cleanup registers, durable words and explicit World effects without dispatch");
        require(thread.run(1)==VmStatus::returned_true && main_calls==1,"host may explicitly dispatch prepared Main later");
    }
    const auto make_thread=[&]{return std::make_unique<Vm>(shared,ShareVmProgramTag{},world.vm_memory(),VmLimits{},VmPrimitive{},
        RoutineOwners{{owner,{{key(4100),routine({{101,254,255,{}}})}}}});};
    for(const std::uint16_t absent:{std::uint16_t{0},std::uint16_t{255},std::uint16_t{4999}}) {
        auto thread=make_thread();thread->temps()[1]=456;thread->temp_xl()[0]=789;
        Vm check(RoutineSet{});auto entries=table();entries.entries[3].action=absent;entries.entries[1].action=absent;
        const auto result=run_object_reset(check,*thread,id,owner,entries);
        require(result.completed && !result.main_started && result.steps==0 && thread->stack().empty() && !thread->pending() &&
            thread->temps()[1]==456 && thread->temp_xl()[0]==789 && check.run_generation()==0,
            "missing, <=255 and unavailable routines skip without claiming Main started or erasing registers");
    }
    {
        auto thread=make_thread();Vm check(RoutineSet{});auto entries=table();entries.entries[3].action=4999;
        const auto result=run_object_reset(check,*thread,id,owner,entries,0);
        require(result.completed && result.main_started && result.steps==0 && thread->steps()==0,"unavailable Cleanup skips without spending budget; installed Main starts only");
    }
    for(const unsigned failure:{0U,1U,2U}) {
        auto thread=make_thread();thread->temps()[2]=71;
        Vm check(shared,ShareVmProgramTag{},world.vm_memory(),{}, {},RoutineOwners{{owner,{{key(4097),routine({{100,static_cast<std::uint8_t>(failure==2?0:254),255,{}}})}}}});
        const auto result=run_object_reset(check,*thread,id,owner,table(),2,
            [&](const auto&,const auto&,const auto&,const auto&){
                PrimitiveOutcome outcome{failure==0?PrimitiveDisposition::wait:failure==1?
                    PrimitiveDisposition::unsupported:PrimitiveDisposition::goto_true,"fixture failure"};
                if(failure==2)outcome.temp_write=VmTempWrite{2,999};return outcome;});
        require(!result.completed && !result.main_started && !result.diagnostic.empty() && !check.pending() &&
            thread->stack().empty() && !thread->pending() &&
            result.stages[0].status==(failure==2?VmStatus::budget_exhausted:VmStatus::fault),
            "async, fault and budget failure never start Main or create asynchronous pending state");
        if(failure==2)require(thread->temps()[2]==999,"failed Cleanup register effects are retained, not presented as rollback");
    }
    {
        auto thread=make_thread();Vm check(shared,ShareVmProgramTag{},world.vm_memory(),{}, {},
            RoutineOwners{{owner,{{key(4097),routine({{100,254,255,{}}})}}}});
        const auto before=encode_vm_snapshot(thread->snapshot());
        require(!run_object_reset(check,*thread,id,owner,table(),0).completed &&
            encode_vm_snapshot(thread->snapshot())==before && check.run_generation()==0,"zero budget rejects before clearing old execution");
        require(!run_object_reset(*thread,*thread,id,owner,table()).completed &&
            encode_vm_snapshot(thread->snapshot())==before,"aliased check and entity VM rejected before effects");
        check.start(key(4097),{id,id,0,owner});
        require(!run_object_reset(check,*thread,id,owner,table()).completed &&
            encode_vm_snapshot(thread->snapshot())==before,"nonfresh cleanup check rejected before reset publication");
    }
    {
        Vm* running=nullptr;bool refused=false;Vm check(shared,ShareVmProgramTag{},world.vm_memory(),{}, {},
            RoutineOwners{{owner,{{key(4097),routine({{100,254,255,{}}})}}}});
        const VmPrimitive nested=[&](const auto&,const auto&,const auto&,const auto&){
            const auto result=run_object_reset(check,*running,id,owner,table());refused=!result.completed;
            require(check.status()==VmStatus::ready && check.run_generation()==0,"reentrant reset leaves fresh check unstarted");
            return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};};
        Vm thread(shared,ShareVmProgramTag{},world.vm_memory(),{},nested,
            RoutineOwners{{owner,{{key(4100),routine({{101,254,255,{}}})}}}});running=&thread;
        require(thread.start(key(4100),{id,id,0,owner})==VmStatus::running &&
            thread.run(1)==VmStatus::returned_true && refused,"runtime reset refuses instruction reentry while original Main completes normally");
    }
    {
        auto thread=make_thread();unsigned recursive_calls=0;
        const RoutineOwners owners{{owner,{{key(4097),routine({{100,254,255,{}}})}}}};
        Vm check(shared,ShareVmProgramTag{},world.vm_memory(),{}, {},owners);
        Vm recursive_check(shared,ShareVmProgramTag{},world.vm_memory(),{}, {},owners);
        const auto result=run_object_reset(check,*thread,id,owner,table(),10,
            [&](const auto&,const auto&,const auto&,const auto&){
                ++recursive_calls;const auto before=encode_vm_snapshot(thread->snapshot());
                const auto nested=run_object_reset(recursive_check,*thread,id,owner,table());
                require(!nested.completed && recursive_check.run_generation()==0 &&
                    encode_vm_snapshot(thread->snapshot())==before,"Cleanup cannot recursively reset the same owner through another fresh check VM");
                return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};});
        require(result.completed && result.main_started && recursive_calls==1,"outer reset completes after harmless recursion refusal");
    }
    std::cout<<"PASS object reset contract ("<<checks<<" checks)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
