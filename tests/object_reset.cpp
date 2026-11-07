#include "odh/object_reset.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/world.hpp"
#include <iostream>
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
int main(){try {
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
