#include "odh/object_startup.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::Bhav set_value(std::uint16_t value) {
    odh::Instruction assign{2, 254, 255,
        {0, 0, static_cast<std::uint8_t>(value & 255),
         static_cast<std::uint8_t>(value >> 8), 0, 5, 0, 7}};
    return {0x8002, {assign}, {}, odh::Bhav::Metadata{0, 4, 4, 0}};
}
}

int main() {
    try {
        constexpr std::int16_t object_id = 37;
        constexpr odh::CodeOwner owner = 0x80000002U;
        int writes = 0;
        odh::VmMemory memory;
        memory.write = [&](const odh::VmFrame& frame, std::uint8_t scope,
                           std::int16_t index, std::int16_t value) {
            require(scope == 0 && index == 0 && value == 321,
                    "Init writes through the object World context");
            require(frame.context.caller == object_id && frame.context.callee == object_id &&
                        frame.context.stack_object == 0 && frame.context.code_owner == owner,
                    "Init uses the object's identity and independent resource owner");
            require(frame.exact_arguments&&frame.arguments == std::vector<std::int16_t>({0, 0, 0, 0}),
                    "source null Init is normalized to the same exact four zeros as EP8");
            ++writes;
            return true;
        };
        auto shared = std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
        odh::RoutineOwners owners{{owner, {{odh::routine_key(4100), set_value(321)}}}};
        odh::Vm vm(shared, odh::ShareVmProgramTag{}, memory, odh::VmLimits{}, {},
                   std::move(owners));

        odh::EntryPointTable table{0, 0, std::vector<odh::EntryPoint>(9, {0, 0}), 0,
                                   odh::EntryPointSource::objf};
        table.entries[0].action = 4100;
        const auto started = odh::run_object_startup(vm, object_id, owner, table);
        require(started.completed && started.steps == 1 && writes == 1,
                "object Init completes before any later entry point");
        require(started.stages.size() == 2 && started.stages[0].phase == "init" &&
                    started.stages[0].status == odh::VmStatus::returned_true &&
                    started.stages[1].phase == "dynamic_multitile" &&
                    started.stages[1].status == odh::VmStatus::ready &&
                    started.stages[1].routine == 0,
                "entry 8 is skipped when Food has no action there");
        require(vm.stack().empty(), "object startup leaves Main for the scheduler");
        auto wide_init=set_value(321);wide_init.metadata->arguments=9;
        auto wide_dynamic=set_value(321);wide_dynamic.metadata->arguments=11;
        odh::Vm wide(shared,odh::ShareVmProgramTag{},memory,odh::VmLimits{},{},
            odh::RoutineOwners{{owner,{{odh::routine_key(4100),wide_init},{odh::routine_key(4101),wide_dynamic}}}});
        auto wide_table=table;wide_table.entries[8].action=4101;const auto previous_writes=writes;
        const auto wide_started=odh::run_object_startup(wide,object_id,owner,wide_table);
        require(wide_started.completed&&wide_started.steps==2&&writes==previous_writes+2,
            "source Init and EP8 do not pad arguments to wider BHAV metadata");

        {
            odh::Bhav defaults{0x8002,{}, {},odh::Bhav::Metadata{0,0,0,0}};
            for(std::uint16_t n=0;n<4;++n)defaults.instructions.push_back({2,static_cast<std::uint8_t>(n==3?254:n+1),255,
                {static_cast<std::uint8_t>(n),0,0,0,0,2,9,7}}); // Param n ==0
            auto staged=table;staged.entries[0].action=4150;
            odh::Vm actual(shared,odh::ShareVmProgramTag{},memory,{}, {},odh::RoutineOwners{{owner,{{odh::routine_key(4150),defaults}}}});
            const auto available=odh::run_object_startup(actual,object_id,owner,staged);
            require(available.completed&&available.steps==4&&available.stages[0].status==odh::VmStatus::returned_true,
                "Init can read default Params0..3 even when its BHAV declares zero arguments");
            auto outside=defaults;outside.metadata->arguments=9;outside.instructions={{2,254,255,{4,0,0,0,0,2,9,7}}};
            odh::Vm bounded(shared,odh::ShareVmProgramTag{},memory,{}, {},odh::RoutineOwners{{owner,{{odh::routine_key(4150),outside}}}});
            const auto rejected=odh::run_object_startup(bounded,object_id,owner,staged);
            require(!rejected.completed&&rejected.stages[0].status==odh::VmStatus::fault&&bounded.stack().empty(),
                "Init Param4 is outside the exact four defaults despite wider BHAV metadata");
            auto explicit_check=defaults;explicit_check.metadata->arguments=9;
            explicit_check.instructions={{99,254,255,{}}};
            odh::Vm explicit_owner(shared,odh::ShareVmProgramTag{},memory,{}, {},odh::RoutineOwners{{owner,{{odh::routine_key(4150),explicit_check}}}});
            const auto explicit_result=explicit_owner.evaluate_entry_point_check(odh::routine_key(4150),{object_id,object_id,0,owner},
                {23,-4},10,false,[&](const odh::VmFrame& frame,const auto&,const auto&,const auto&){
                    require(frame.exact_arguments&&frame.arguments==std::vector<std::int16_t>({23,-4}),
                        "explicit entry-point arguments retain values and length without null/default normalization");
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                });
            require(explicit_result.started&&explicit_result.status==odh::VmStatus::returned_true,
                "explicit source argument arrays remain distinct from default-null construction entries");
        }
        {
            auto staged=table;staged.entries[0].action=4160;staged.entries[8].action=4161;
            const odh::RoutineOwners code{{owner,{
                {odh::routine_key(4160),{0x8002,{{18,254,255,{}}},{},odh::Bhav::Metadata{0,0,0,0}}},
                {odh::routine_key(4161),set_value(321)}}}};
            odh::Vm actual(shared,odh::ShareVmProgramTag{},memory,{}, {},code);
            bool deleted=false;
            const auto before=actual.snapshot();
            const auto remove=[&](const auto&,const auto&,const auto&,const auto& dispatch){
                require(dispatch.check_mode,"lifecycle deletion executes on the constructor's disposable check");
                deleted=true;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::lifecycle_deleted,{}};};
            const auto stopped=odh::run_object_startup(actual,object_id,owner,staged,10,{},false,remove,[&]{return deleted;});
            require(stopped.completed&&stopped.self_deleted&&stopped.stages.size()==1&&stopped.steps==1&&
                stopped.stages[0].status==odh::VmStatus::ready&&actual.stack().empty()&&actual.status()==before.status,
                "typed self-deletion completes distinctly without EP8 or Main and preserves the real Thread");
            odh::Vm unproved(shared,odh::ShareVmProgramTag{},memory,{}, {},code);
            const auto missing_proof=odh::run_object_startup(unproved,object_id,owner,staged,10,{},false,remove,[]{return false;});
            require(!missing_proof.completed&&!missing_proof.self_deleted,
                "typed lifecycle stop alone cannot substitute for the removed entity identity proof");
            odh::Vm faulted(shared,odh::ShareVmProgramTag{},memory,{}, {},code);
            const auto bad=odh::run_object_startup(faulted,object_id,owner,staged,10,{},false,
                [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"unported effect"};},
                []{return true;});
            require(!bad.completed&&!bad.self_deleted&&bad.stages[0].status==odh::VmStatus::fault,
                "a dead-identity predicate never converts an ordinary unsupported fault into lifecycle success");
        }
        for(const bool sharing:{false,true}){
            odh::Vm actual(shared,odh::ShareVmProgramTag{},memory,{}, {},odh::RoutineOwners{{owner,{
                {odh::routine_key(4200),{0x8002,{{2,1,255,{0,0,57,48,0,5,42,7}},{99,254,255,{}}},{},odh::Bhav::Metadata{0,9,0,0}}},
                {odh::routine_key(4201),{0x8002,{{100,254,255,{}}},{},odh::Bhav::Metadata{0,11,0,0}}},
                {odh::routine_key(4202),{0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,9,0,0}}}}}});
            actual.temps()[0]=7;actual.temp_xl()[0]=90000;unsigned phases=0,traces=0;
            const auto before=actual.snapshot();auto staged=table;
            staged.entries[0].action=4200;staged.entries[8].action=4201;
            const auto synchronous=[&](const odh::VmFrame& frame,const odh::Instruction& op,
                const odh::Ts1Temps& temps,const odh::VmDispatch& dispatch)->odh::PrimitiveOutcome{
                ++phases;bool parent_guarded=false;
                try{actual.reset_execution_state();}catch(const std::logic_error&){parent_guarded=true;}
                require(parent_guarded,"disposable startup cannot mutate its actual Thread execution state");
                require(dispatch.check_mode&&dispatch.origin!=&actual&&!frame.action_tree,
                    "Init and EP8 execute on authentic disposable nonaction checks");
                require(temps[0]==(op.opcode==100&&sharing?19:7),"EP8 seeds the actual new object's register buffer");
                require(frame.arguments==std::vector<std::int16_t>(4,0),
                    "Init null normalization and EP8 explicit array both retain exactly four words despite wide metadata");
                odh::PrimitiveOutcome out{odh::PrimitiveDisposition::goto_true,{}};
                out.temp_write=odh::VmTempWrite{0,19};return out;
            };
            const auto result=odh::run_object_startup(actual,object_id,owner,staged,10,
                [&](const auto&,const auto&){++traces;},sharing,synchronous);
            if(!(result.completed&&result.steps==3&&phases==2&&traces==3&&actual.stack().empty()&&
                actual.steps()==before.steps&&actual.status()==before.status&&actual.temps()[0]==(sharing?19:7)&&
                actual.temp_xl()[0]==(sharing?12345:90000)))
                throw std::runtime_error("scratch stage diagnostic share="+std::to_string(sharing)+
                    " completed="+std::to_string(result.completed)+" steps="+std::to_string(result.steps)+
                    " phases="+std::to_string(phases)+" traces="+std::to_string(traces)+
                    " temp="+std::to_string(actual.temps()[0])+" xl="+std::to_string(actual.temp_xl()[0])+" "+result.diagnostic);
            require(result.completed&&result.steps==3&&phases==2&&traces==3&&actual.stack().empty()&&
                actual.steps()==before.steps&&actual.status()==before.status&&
                actual.temps()[0]==(sharing?19:7)&&actual.temp_xl()[0]==(sharing?12345:90000),
                "scratch stages preserve real Thread readiness and publish only scheduler-shared register effects");
            require(actual.start_after_reset(odh::routine_key(4202),{object_id,object_id,0,owner},{0,0,0,0},true)==odh::VmStatus::running&&
                actual.temps()[0]==(sharing?19:7)&&actual.stack().front().exact_arguments&&actual.stack().front().arguments==std::vector<std::int16_t>(4,0)&&actual.steps()==0,
                "Main normalizes source null arguments to four zeros and preserves constructor buffers without execution");
        }
        {
            odh::RoutineSet recursive;
            for(std::uint16_t id=4300;id<4305;++id)recursive.emplace(odh::routine_key(id),odh::Bhav{0x8002,
                {{static_cast<std::uint16_t>(id+1),254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}});
            recursive.emplace(odh::routine_key(4305),odh::Bhav{0x8002,
                {{2,1,255,{0,0,210,4,0,5,42,7}},{99,254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}});
            for(const bool sharing:{false,true})for(const std::size_t capacity:{std::size_t{64},std::size_t{3}}){
                odh::VmLimits limits;limits.stack_depth=capacity;
                odh::Vm actual(shared,odh::ShareVmProgramTag{},memory,limits,{},odh::RoutineOwners{{owner,recursive}});
                actual.temps()[0]=7;actual.temp_xl()[0]=8;
                auto staged=table;staged.entries[0].action=4300;unsigned calls=0;
                const auto result=odh::run_object_startup(actual,object_id,owner,staged,20,{},sharing,
                    [&](const auto&,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
                        ++calls;odh::PrimitiveOutcome out{odh::PrimitiveDisposition::goto_true,{}};
                        out.temp_write=odh::VmTempWrite{0,19};return out;
                    });
                require(actual.stack().empty()&&actual.status()==odh::VmStatus::ready&&actual.steps()==0,
                    "deep EP check preserves actual Thread readiness without dispatching Main");
                if(capacity>5)require(result.completed&&result.steps==7&&calls==1&&
                    actual.temps()[0]==(sharing?19:7)&&actual.temp_xl()[0]==(sharing?1234:8),
                    "source constructor List capacity5 does not limit legitimate deeper EP checks in alias or clone mode");
                else require(!result.completed&&result.stages.front().status==odh::VmStatus::fault&&calls==0&&
                    result.diagnostic.find("stack depth limit")!=std::string::npos&&actual.temps()[0]==7&&actual.temp_xl()[0]==8,
                    "an explicit low native owner stack budget still rejects before deep leaf effects");
            }
        }
        {
            odh::Vm actual(shared,odh::ShareVmProgramTag{},memory,{}, {},odh::RoutineOwners{{owner,{
                {odh::routine_key(4400),{0x8002,{{2,1,255,{0,0,88,0,0,5,8,7}},{99,254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}}}}}});
            auto staged=table;staged.entries[0].action=4400;
            const auto result=odh::run_object_startup(actual,object_id,owner,staged,10,{},true,
                [](const auto&,const auto&,const auto&,const auto&){
                    odh::PrimitiveOutcome out{odh::PrimitiveDisposition::wait,{}};out.temp_write=odh::VmTempWrite{0,88};return out;
                });
            require(!result.completed&&result.stages.front().status==odh::VmStatus::fault&&!actual.pending()&&actual.stack().empty()&&actual.temps()[0]==88,
                "Init rejects async completion while preserving earlier scheduler-shared register effects");
        }
        {
            auto staged=table;staged.entries[0].action=4500;staged.entries[8].action=4501;
            odh::Vm actual(shared,odh::ShareVmProgramTag{},memory,{}, {},odh::RoutineOwners{{owner,{
                {odh::routine_key(4501),{0x8002,{},{},odh::Bhav::Metadata{0,4,0,0}}}}}});
            const auto result=odh::run_object_startup(actual,object_id,owner,staged);
            require(result.completed&&result.steps==0&&result.stages[0].status==odh::VmStatus::returned_false&&
                result.stages[1].status==odh::VmStatus::returned_false&&actual.status()==odh::VmStatus::ready,
                "authenticated absent and empty entry points return source false without aborting startup");
            odh::Vm unknown(shared,odh::ShareVmProgramTag{},memory);
            const auto unsupported=odh::run_object_startup(unknown,object_id,owner,staged);
            require(!unsupported.completed&&unsupported.stages[0].status==odh::VmStatus::fault,
                "unknown owner bank cannot be replaced with an invented false entry-point result");
        }
        bool missing_table_rejected = false;
        try {
            odh::EntryPointTable short_table{0, 0, {{0, 4100}}, 0,
                                              odh::EntryPointSource::objf};
            (void)odh::run_object_startup(vm, object_id, owner, short_table);
        } catch (const std::invalid_argument&) { missing_table_rejected = true; }
        require(missing_table_rejected, "incomplete entry table is rejected");

        std::cout << "PASS object startup contract (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
