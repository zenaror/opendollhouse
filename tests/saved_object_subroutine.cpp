#include "saved_object_subroutine.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
odh::Bhav tree(std::uint8_t branch){
    return {0x8002,{{1,branch,branch,{}}},{},odh::Bhav::Metadata{0,4,0,0}};
}
bool same_context(const odh::VmContext& a,const odh::VmContext& b){return odh::vm_context_equal(a,b);}
bool same_pending(const std::optional<odh::VmPending>& a,const std::optional<odh::VmPending>& b){
    if(a.has_value()!=b.has_value())return false;if(!a)return true;
    const auto& x=a->instruction;const auto& y=b->instruction;
    return a->ticket==b->ticket&&same_context(a->context,b->context)&&x.step==y.step&&x.depth==y.depth&&
        x.key.scope==y.key.scope&&x.key.id==y.key.id&&x.pc==y.pc&&x.code_owner==y.code_owner&&
        x.instruction.opcode==y.instruction.opcode&&x.instruction.operands==y.instruction.operands&&
        x.instruction.on_true==y.instruction.on_true&&x.instruction.on_false==y.instruction.on_false;
}
template<class T> bool same_return(const std::optional<T>& a,const std::optional<T>& b){
    return a.has_value()==b.has_value()&&(!a||(a->ticket==b->ticket&&a->result==b->result&&
        a->code_owner==b->code_owner&&a->run_generation==b->run_generation));
}
bool same_execution(const odh::VmSnapshot& a,const odh::VmSnapshot& b){
    if(a.status!=b.status||a.diagnostic!=b.diagnostic||a.steps!=b.steps||a.run_generation!=b.run_generation||
       a.ticket_sequence!=b.ticket_sequence||a.stack.size()!=b.stack.size()||!same_pending(a.pending,b.pending)||
       a.suspended_pending.size()!=b.suspended_pending.size()||a.immediate_continuations.size()!=b.immediate_continuations.size()||
       !same_return(a.interaction_return,b.interaction_return)||!same_return(a.external_frame_return,b.external_frame_return)||
       !same_return(a.immediate_return,b.immediate_return))return false;
    for(std::size_t n=0;n<a.stack.size();++n){const auto& x=a.stack[n];const auto& y=b.stack[n];
        if(x.key.scope!=y.key.scope||x.key.id!=y.key.id||x.pc!=y.pc||!same_context(x.context,y.context)||
           x.arguments!=y.arguments||x.locals!=y.locals||x.return_mode!=y.return_mode||
           x.invocation_ticket!=y.invocation_ticket||x.exact_arguments!=y.exact_arguments||x.action_tree!=y.action_tree)return false;}
    for(std::size_t n=0;n<a.suspended_pending.size();++n)
        if(!same_pending(a.suspended_pending[n],b.suspended_pending[n]))return false;
    for(std::size_t n=0;n<a.immediate_continuations.size();++n){const auto& x=a.immediate_continuations[n];const auto& y=b.immediate_continuations[n];
        if(x.ticket!=y.ticket||x.parent_depth!=y.parent_depth||x.parent_status!=y.parent_status||
           !same_pending(x.parent_pending,y.parent_pending))return false;}
    return true;
}
odh::Bhav named_routine(std::initializer_list<odh::Instruction> instructions,unsigned args=4){
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,static_cast<std::uint8_t>(args),0,0}};}
odh::Instruction set_temp(unsigned index,unsigned value){return {2,254,255,
    {static_cast<std::uint8_t>(index),0,static_cast<std::uint8_t>(value),0,0,5,8,7}};}
void named_target_parked_entity_thread_rejects(){
    const auto main=odh::routine_key(4096),check_key=odh::routine_key(4400),named=odh::named_routine_key(42,false);
    odh::Vm* actual=nullptr;unsigned effects=0;SavedObjectSubroutineReport report;odh::PrimitiveOutcome rejected;
    odh::VmPrimitive check;
    check=[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch)->odh::PrimitiveOutcome{
        if(instruction.opcode==99){++effects;return {odh::PrimitiveDisposition::goto_true,{}};}
        rejected=run_saved_named_tree(*actual,actual,{frame,instruction,dispatch,1,77,named,1},temps,report,check,8);
        return rejected;
    };
    odh::Vm vm({{main,named_routine({{200,254,255,{}}})},{check_key,named_routine({{28,254,255,{0,0,0,0,1,1,0,0}}})}}, {}, {},
        [&](const auto& frame,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
            auto context=frame.context;context.callee=3;
            const auto result=actual->evaluate_check(check_key,context,{},16,check);
            require(result.started&&result.status==odh::VmStatus::fault&&rejected.disposition==odh::PrimitiveDisposition::unsupported&&
                rejected.diagnostic.find("parked-thread execution bridge is unavailable")!=std::string::npos&&effects==0&&
                actual->entity_thread_frames().back().context.callee==2&&actual->temps()[4]==55,
                "named destination1 targeting parked actual Entity.Thread rejects before retargeting or executing a temporary Check frame");
            return {odh::PrimitiveDisposition::goto_true,{}};
        },{{77,{{named,named_routine({{99,254,255,{}}})}}}});actual=&vm;
    vm.start(main,{1,2,1});vm.temps()[4]=55;
    require(vm.run(1)==odh::VmStatus::returned_true&&effects==0&&vm.temps()[4]==55,
        "unsupported logical-thread target bridge leaves actual Main and registers intact");
}
void explicit_named_resource_identity(){
    const auto main=odh::routine_key(4096),action=odh::routine_key(4100);
    for(const auto raw:{std::uint16_t{10},std::uint16_t{4096},std::uint16_t{8192},std::uint16_t{65535}})
    for(const bool linked:{false,true})for(const std::uint8_t destination:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{2}}){
        const auto named=odh::named_routine_key(raw,linked);
        std::map<std::int16_t,std::uint64_t> generations{{1,11},{2,22},{3,33},{4,44},{5,55},{6,66}};
        odh::VmMemory memory;memory.resolve_live_entity=[&](auto id)->std::optional<odh::WorldEntityReference>{
            const auto found=generations.find(id);return found==generations.end()?std::nullopt:std::optional<odh::WorldEntityReference>{{id,found->second}};
        };
        const odh::RoutineOwners owners{{77,{{named,named_routine({{99,254,255,{}}},6)}}}};
        const odh::RoutineSet target_code{{main,named_routine({{0,254,255,{}}})}};
        odh::Vm target(target_code,memory,{},[](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};},owners);
        target.start(main,{4,5,6});target.run(1);target.temps()[0]=9;
        odh::VmFrame address;require(target.effective_memory().write(address,12,7,22),"target Thread owns an existing advertisement override");
        const auto before=target.snapshot();unsigned calls=0;SavedObjectSubroutineReport report;odh::Vm* caller=nullptr;
        const auto checked=[&](const odh::VmFrame& frame,const odh::Instruction& op,const odh::Ts1Temps& temps,const odh::VmDispatch& dispatch)->odh::PrimitiveOutcome{
            ++calls;const bool in_target=destination==1;
            require(op.opcode==99&&frame.key.scope==named.scope&&frame.key.id==raw&&frame.context.code_owner==77&&
                frame.context.caller==(in_target?4:1)&&frame.context.caller_generation==(in_target?44:11)&&
                frame.context.callee==(destination==2?2:3)&&frame.context.callee_generation==(destination==2?22:33)&&
                frame.context.stack_object==3&&frame.context.stack_object_generation==33&&
                frame.arguments==std::vector<std::int16_t>{-1,19,0,0,-1,-1}&&
                frame.action_tree==!in_target&&dispatch.check_mode==(destination!=2)&&temps[0]==(in_target?9:-1),
                "direct named entry preserves raw source ID, physical bank, exact pointers, previous ActionTree and distinct register owner");
            const auto* execution=dispatch.origin;
            require(execution->synchronous_queue_empty()==(destination!=2)&&execution->effective_memory().read(frame,12,7)==(in_target?22:55)&&
                execution->effective_memory().write(frame,12,7,66),"RunInMyStack preserves real Thread advertisement dictionary instead of fresh EvaluateCheck memory");
            return {odh::PrimitiveDisposition::goto_true,{}};
        };
        odh::RoutineSet caller_code{{main,named_routine({{200,254,255,{}}})},
            {action,named_routine({{28,254,255,{0,0,0,0,1,destination,0,0}}})},
            {odh::routine_key(10),named_routine({{98,254,255,{}}})}};
        odh::Vm actual(caller_code,memory,{},[&](const auto& frame,const auto& op,const auto& temps,const auto& dispatch)->odh::PrimitiveOutcome{
            if(op.opcode==200)return {odh::PrimitiveDisposition::call,{}, {},odh::VmCall{action,frame.context,{},odh::VmCallReturnMode::ordinary,false,true}};
            if(op.opcode==99)return checked(frame,op,temps,dispatch);
            require(op.opcode==28,"named low-ID body must never be redirected to the global numeric bank");
            return run_saved_named_tree(*caller,&target,{frame,op,dispatch,3,77,named,destination},temps,report,checked,8);
        },owners);caller=&actual;
        actual.start(main,{1,2,3});actual.temps()[0]=-1;actual.temps()[1]=19;
        require(actual.effective_memory().write(address,12,7,55)&&actual.run(1)==odh::VmStatus::budget_exhausted,"enter a genuine accepted action ancestor before named dispatch");
        if(destination!=1)generations[3]=333; // Existing SO pointer must not bind a new live generation.
        const auto status=actual.run(4);const auto after=target.snapshot();
        auto expected_parked=before;
        if(destination==1)++expected_parked.ticket_sequence; // One actual named opcode99 dispatch on this Thread.
        const auto& advertisements=destination==1?target.advertisement_changes():actual.advertisement_changes();
        const auto advertised=advertisements&&advertisements->count((1U<<16)|7)?static_cast<int>(advertisements->at((1U<<16)|7)):-99999;
        if(status!=odh::VmStatus::returned_true||calls!=1||!same_execution(expected_parked,after)||advertised!=66)
            throw std::runtime_error("named identity diagnostic id="+std::to_string(raw)+" linked="+std::to_string(linked)+
                " destination="+std::to_string(destination)+" status="+odh::vm_status_name(status)+
                " calls="+std::to_string(calls)+" parked_equal="+std::to_string(same_execution(expected_parked,after))+
                " target_ticket_before="+std::to_string(before.ticket_sequence)+" target_ticket_after="+std::to_string(after.ticket_sequence)+
                " target_steps_before="+std::to_string(before.steps)+" target_steps_after="+std::to_string(after.steps)+
                " target_run_before="+std::to_string(before.run_generation)+" target_run_after="+std::to_string(after.run_generation)+
                " pending_before="+std::to_string(before.pending?before.pending->ticket:0)+" pending_after="+std::to_string(after.pending?after.pending->ticket:0)+
                " advertisement="+std::to_string(advertised)+" report="+odh::vm_status_name(report.result.status)+":"+report.result.diagnostic+" caller="+actual.diagnostic());
        require(status==odh::VmStatus::returned_true&&calls==1&&
            after.ticket_sequence==before.ticket_sequence+(destination==1?1U:0U)&&
            same_execution(expected_parked,after)&&advertisements->at((1U<<16)|7)==66,
            "all named physical namespaces and execution destinations retain source effects without changing parked execution");
    }
    {
        const auto first=odh::named_routine_key(10,false),second=odh::named_routine_key(42,true);
        const odh::RoutineOwners owners{{77,{{first,named_routine({{28,254,255,{0,0,0,0,1,2,0,0}}})},
            {second,named_routine({{99,254,255,{}}})}}}};
        odh::Vm* actual=nullptr;unsigned calls=0;SavedObjectSubroutineReport report;odh::VmPrimitive handler;
        handler=[&](const auto& frame,const auto& op,const auto& temps,const auto& dispatch)->odh::PrimitiveOutcome{
            if(op.opcode==200)return {odh::PrimitiveDisposition::call,{}, {},odh::VmCall{action,frame.context,{},odh::VmCallReturnMode::ordinary,false,true}};
            if(op.opcode==99){++calls;require(dispatch.check_mode&&frame.action_tree&&frame.key.scope==second.scope&&
                actual->synchronous_queue_empty(),"destination2 remains an ordinary synchronous child inside real RunInMyStack");
                return {odh::PrimitiveDisposition::goto_true,{}};}
            return run_saved_named_tree(*actual,nullptr,{frame,op,dispatch,3,77,op.operands[5]==2?second:first,op.operands[5]},temps,report,handler,8);
        };
        odh::Vm vm({{main,named_routine({{200,254,255,{}}})},{action,named_routine({{28,254,255,{0,0,0,0,1,0,0,0}}})}}, {}, {},handler,owners);actual=&vm;
        vm.start(main,{1,2,3});
        require(vm.run(2)==odh::VmStatus::returned_true&&calls==1,"nested named destination2 uses the same synchronous interpreter without asynchronous service admission");
    }
    for(const std::uint8_t destination:{std::uint8_t{0},std::uint8_t{2}}){
        const auto empty_key=odh::named_routine_key(42,false);odh::Vm* actual=nullptr;SavedObjectSubroutineReport report;
        odh::Vm vm({{main,named_routine({{28,254,255,{0,0,0,0,1,destination,0,0}}})}}, {}, {},
            [&](const auto& frame,const auto& op,const auto& temps,const auto& dispatch){
                return run_saved_named_tree(*actual,nullptr,{frame,op,dispatch,3,77,empty_key,destination},temps,report,{},3);
            },{{77,{{empty_key,named_routine({})}}}});actual=&vm;vm.start(main,{1,2,3});
        require(vm.run(3)==(destination==2?odh::VmStatus::budget_exhausted:odh::VmStatus::returned_false)&&
            (destination!=2||(vm.stack().size()==1&&vm.stack().back().pc==0&&!vm.pending())),
            "known empty named routine preserves source destination2 CONTINUE or synchronous false semantics");
    }
    const auto named=odh::named_routine_key(10,false);
    odh::Vm unknown(odh::RoutineSet{});require(!unknown.run_named_in_stack(named,{1,1,1,99},{},8).started,
        "unknown named owner is unsupported rather than a fabricated false lookup");
    odh::Vm empty({}, {}, {},{},{{77,{{named,named_routine({{99,254,255,{}}})}}}});
    const auto no_parent=empty.run_named_in_stack(named,{1,1,1,77},{},8);
    require(no_parent.started&&no_parent.status==odh::VmStatus::returned_false&&empty.stack().empty(),
        "empty executor has no source predecessor and therefore does not execute a named child");
}
void named_tree_modes(){
    const auto main=odh::routine_key(4096),named=odh::routine_key(8193);
    constexpr odh::CodeOwner owner=77;
    for(const auto destination:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{2},std::uint8_t{3},std::uint8_t{255}})
        for(const auto result:{false,true})for(unsigned state=0;state<(destination==0||destination==2?1U:9U);++state){
        const bool in_target=destination!=0&&destination!=2;
        const odh::Instruction source{28,1,2,{0,0,0,0,1,destination,0,0}};
        const auto bank=odh::RoutineOwners{{owner,{{named,named_routine({{99,254,255,{}}},6)}}}};
        odh::Ts1Temps source_temps{};source_temps[0]=42;source_temps[1]=17;source_temps[2]=-1;source_temps[3]=9;
        unsigned effects=0;std::int16_t world_value=0;SavedObjectSubroutineReport report;
        odh::VmMemory memory;memory.write=[&](const auto&,auto,auto,std::int16_t value){world_value=value;return true;};
        const odh::RoutineSet target_program{
            {main,named_routine({{45,1,1,{}},set_temp(6,19)})},
            {odh::routine_key(4097),named_routine({{0,254,255,{}}})},
            {odh::routine_key(4100),named_routine({{1,254,255,{}}})},
            {odh::routine_key(4101),named_routine({{0,254,255,{}}})},
            {odh::routine_key(4102),named_routine({{98,1,1,{}},{1,254,255,{}}})}};
        const auto target_primitive=[](const auto&,const odh::Instruction& instruction,const auto&,const auto&){
            return odh::PrimitiveOutcome{instruction.opcode==98?odh::PrimitiveDisposition::goto_true_next_tick:
                instruction.opcode==0||instruction.opcode==45?odh::PrimitiveDisposition::wait:
                odh::PrimitiveDisposition::goto_true,{}};};
        odh::Vm target(target_program,memory,{},target_primitive,bank);
        if(state==1){odh::Ts1Temps temps{};temps[0]=8;
            target.restore_dormant_state({{main,0,{91,92,93,0},{},{}}},temps);}
        if(state>=2){target.start(main,{91,92,93,0});
            if(state>=3&&state<=6)target.run(1);}
        if(state==4){target.start(odh::routine_key(4102),{91,92,93,0});
            require(target.run(1)==odh::VmStatus::next_tick,"target reaches authored next-tick boundary");}
        if(state==5||state==6){const auto route=target.pending()->ticket;
            require(target.call_pending(route,{odh::routine_key(4097),{91,92,93,0},{}})&&target.run(1)==odh::VmStatus::waiting,
                "target has an actual external child and suspended parent route");
            if(state==5){require(target.complete(target.pending()->ticket,true)&&target.status()==odh::VmStatus::yielded,
                "target external child yields its real parent return");}
            else {const auto immediate=target.inject_immediate({odh::routine_key(4101),{91,95,96,0},{}},[](const auto&,const auto&){return true;});
                require(immediate&&target.run(1)==odh::VmStatus::waiting,"target has parked external and immediate parent waits");}}
        if(state==7)target.start(odh::routine_key(4999),{91,92,93,0});
        if(state==8){target.start(odh::routine_key(4100),{91,92,93,0});target.run(1);}
        target.temps()[0]=8;target.temps()[1]=7;target.temp_xl()[1]=29;
        const auto before=target.snapshot();const bool executed=!in_target||!before.stack.empty();
        const auto check=[&](const odh::VmFrame& frame,const auto&,const odh::Ts1Temps& temps,const odh::VmDispatch& dispatch){
            ++effects;
            const auto expected_caller=in_target?(before.stack.empty()?207:before.stack.back().context.caller):71;
            require(frame.context.caller==expected_caller&&frame.context.callee==(destination==2?72:207)&&
                frame.context.stack_object==207&&frame.context.code_owner==owner&&
                frame.arguments==std::vector<std::int16_t>{42,17,-1,9,-1,-1}&&dispatch.check_mode==(destination!=2)&&
                temps[0]==(in_target?8:42),"named destinations preserve source-specific caller/callee/register and argument rules");
            auto* vm=const_cast<odh::Vm*>(dispatch.origin);vm->temps()[4]=55;vm->temp_xl()[1]=1234;
            require(memory.write(frame,0,0,7),"named tree publishes real World effect");
            return odh::PrimitiveOutcome{result?odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::goto_false,{}};
        };
        odh::Vm* caller_ptr=nullptr;
        const auto primitive=[&](const odh::VmFrame& frame,const odh::Instruction& instruction,const odh::Ts1Temps& temps,const odh::VmDispatch& dispatch){
            if(instruction.opcode==99)return check(frame,instruction,temps,dispatch);
            auto wrong=dispatch;++wrong.ticket;SavedObjectSubroutineReport invalid;
            const SavedNamedTreeRequest forged{frame,instruction,wrong,207,owner,named,destination};
            require(run_saved_named_tree(*caller_ptr,&target,forged,temps,invalid,check,8).disposition==odh::PrimitiveDisposition::unsupported&&
                effects==0,"forged source ticket rejects before target execution or shared effects");
            return run_saved_named_tree(*caller_ptr,&target,{frame,instruction,dispatch,207,owner,named,destination},temps,report,check,8);
        };
        odh::Vm caller({{main,named_routine({source,set_temp(6,11),set_temp(6,22)})}},memory,{},primitive,bank);caller_ptr=&caller;
        caller.start(main,{71,72,207,0});caller.temps()=source_temps;caller.temp_xl()[1]=39;
        require(caller.run(8)==odh::VmStatus::returned_true&&caller.temps()[6]==(executed&&result?11:22)&&effects==(executed?1U:0U)&&world_value==(executed?7:0),
            "all named-tree destinations and authored target-destination aliases return the real branch");
        const auto after=target.snapshot();auto expected_parked=before;
        if(in_target&&executed)++expected_parked.ticket_sequence; // Exactly one opcode99, even on authored false.
        require(after.ticket_sequence==before.ticket_sequence+(in_target&&executed?1U:0U)&&
            same_execution(expected_parked,after)&&target.temps()[4]==(in_target&&executed?55:0)&&target.temp_xl()[1]==(in_target&&executed?1234:29)&&
            caller.temps()[4]==(!in_target&&executed?55:0)&&caller.temp_xl()[1]==(!in_target&&executed?1234:39),
            "target execution roster survives empty/dormant/pending/next-tick/yielded/mixed states while only actual check registers are shared");
    }
}
void named_tree_continuations(){
    const auto main=odh::routine_key(4096),child=odh::routine_key(4097),immediate=odh::routine_key(4101),named=odh::routine_key(8193);
    for(const auto destination:{std::uint8_t{0},std::uint8_t{2}})for(const auto result:{false,true}){
        const odh::Instruction source{28,1,2,{0,0,0,0,0,destination,0,0}};
        const odh::RoutineSet program{{main,named_routine({{45,254,255,{}}})},
            {child,named_routine({{0,254,255,{}}})},
            {immediate,named_routine({source,set_temp(6,11),set_temp(6,22)})}};
        const odh::RoutineOwners owners{{77,{{named,named_routine({{99,254,255,{}}},6)}}}};
        odh::Vm* active=nullptr;SavedObjectSubroutineReport report;unsigned checks=0;
        const auto synchronous=[&](const auto& frame,const auto&,const auto&,const auto& dispatch){
            ++checks;require(dispatch.check_mode&&frame.context.caller==71&&frame.context.callee==207&&
                frame.arguments==std::vector<std::int16_t>{42,0,0,0,-1,-1},"in-dispatch named check uses actual source context and arguments");
            return odh::PrimitiveOutcome{result?odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::goto_false,{}};};
        const odh::VmPrimitive primitive=[&](const auto& frame,const odh::Instruction& instruction,const auto& temps,const auto& dispatch){
            if(instruction.opcode==28)return run_saved_named_tree(*active,active,{frame,instruction,dispatch,207,77,named,destination},temps,report,synchronous,8);
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
        odh::Vm vm(program,{}, {},primitive,owners);active=&vm;
        vm.start(main,{71,72,207,0});require(vm.run(1)==odh::VmStatus::waiting,"prepare route parent");
        const auto route=vm.pending()->ticket;
        require(vm.call_pending(route,{child,{71,72,207,0},{}})&&vm.run(1)==odh::VmStatus::waiting,"prepare actual external child");
        const auto sleep=vm.pending()->ticket;
        const auto invocation=vm.inject_immediate({immediate,{71,72,207,0},{}},[](const auto&,const auto&){return true;});
        require(invocation.has_value(),"inject named action over both parked services");vm.temps()[0]=42;
        const auto status=vm.run(destination==2?2:1);
        require(vm.suspended_pending().size()==1&&vm.suspended_pending()[0].ticket==route&&
            vm.immediate_continuations().size()==1&&vm.immediate_continuations()[0].parent_pending->ticket==sleep,
            "named dispatch retains external and immediate parent service identities");
        if(destination==2){
            if(status!=odh::VmStatus::waiting||!vm.pending()||vm.pending()->instruction.instruction.opcode!=99||checks!=0||
                vm.stack().back().arguments!=std::vector<std::int16_t>{42,0,0,0,-1,-1}||
                vm.stack().back().context.callee!=72||vm.stack().back().context.stack_object!=207){
                std::cerr<<"named async status="<<odh::vm_status_name(status)<<" diagnostic="<<vm.diagnostic()<<" checks="<<checks
                    <<" depth="<<vm.stack().size()<<" context="<<vm.stack().back().context.caller<<","<<vm.stack().back().context.callee
                    <<","<<vm.stack().back().context.stack_object<<" args=";
                for(const auto value:vm.stack().back().arguments)std::cerr<<value<<",";std::cerr<<"\n";
            }
            require(status==odh::VmStatus::waiting&&vm.pending()->instruction.instruction.opcode==99&&checks==0&&
                vm.stack().back().context.callee==72&&vm.stack().back().context.stack_object==207&&
                vm.stack().back().arguments==std::vector<std::int16_t>{42,0,0,0,-1,-1},
                "destination two is an ordinary asynchronous child, not a synchronous check");}
        else require(status==odh::VmStatus::budget_exhausted&&checks==1&&report.result.started,
            "caller check executes inside mixed immediate and external parents");
        odh::Vm fresh(program,{}, {},primitive,owners);active=&fresh;
        require(fresh.restore_snapshot(vm.snapshot()),"fresh VM restores named child and complete parent ancestry");
        if(destination==2)require(fresh.complete(fresh.pending()->ticket,result),"ordinary named child returns actual branch");
        require(fresh.run(2)==odh::VmStatus::yielded&&fresh.immediate_return()&&fresh.immediate_return()->ticket==*invocation&&
            fresh.temps()[6]==(result?11:22)&&fresh.resume_immediate(*invocation)&&fresh.pending()->ticket==sleep&&
            fresh.suspended_pending().front().ticket==route,"named result resumes exact original raw28 branch and parked services after fresh restore");
    }
}
void named_tree_nonreturns(){
    const auto main=odh::routine_key(4096),named=odh::routine_key(8193);
    for(const auto destination:{std::uint8_t{0},std::uint8_t{1}})for(unsigned mode=0;mode<3;++mode){
        const odh::Instruction source{28,254,255,{0,0,0,0,0,destination,0,0}};
        const odh::RoutineSet caller_program{{main,named_routine({source})}};
        const odh::RoutineOwners owners{{77,{{named,named_routine({{99,static_cast<std::uint8_t>(mode==0?0:254),255,{}}})}}}};
        odh::Vm target({{main,named_routine({{0,254,255,{}}})}}, {}, {},{},owners);
        target.start(main,{91,92,93,0});target.temps()[0]=9;const auto before=target.snapshot();
        odh::Vm* active=nullptr;unsigned effects=0;int world=0;SavedObjectSubroutineReport report;
        const auto check=[&](const auto&,const auto&,const auto&,const odh::VmDispatch& dispatch){
            ++effects;world=7;const_cast<odh::Vm*>(dispatch.origin)->temps()[4]=55;
            return odh::PrimitiveOutcome{mode==0?odh::PrimitiveDisposition::goto_true:
                mode==1?odh::PrimitiveDisposition::wait:odh::PrimitiveDisposition::unsupported,"test nonreturn"};};
        odh::Vm caller(caller_program,{}, {},[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
            return run_saved_named_tree(*active,&target,{frame,instruction,dispatch,207,77,named,destination},temps,report,check,3);
        },owners);active=&caller;caller.start(main,{71,72,207,0});
        const auto status=caller.run(1);const auto after=target.snapshot();auto expected_parked=before;
        const auto dispatches=mode==0?3U:1U; // Three budgeted opcode99 loops, or one rejected wait/unsupported callback.
        if(destination==1)expected_parked.ticket_sequence+=dispatches;
        require(status==odh::VmStatus::fault&&report.result.started&&
            report.result.status==(mode==0?odh::VmStatus::budget_exhausted:odh::VmStatus::fault)&&world==7&&
            effects==dispatches&&after.ticket_sequence==before.ticket_sequence+(destination==1?dispatches:0U)&&
            same_execution(expected_parked,after)&&
            (destination==0?caller.temps()[4]:target.temps()[4])==55,
            "named nonreturn never becomes false/success and preserves actual World/register effects and target execution");
    }
}

}
int main(){try{
    named_target_parked_entity_thread_rejects();
    explicit_named_resource_identity();
    named_tree_modes();
    named_tree_continuations();
    named_tree_nonreturns();
    constexpr odh::CodeOwner owner=2933422533U;
    const auto main=odh::routine_key(4096),child=odh::routine_key(4097);
    std::array<std::int16_t,20> caller_temps{};caller_temps[0]=42;caller_temps[1]=17;
    for(const auto status:{odh::VmStatus::returned_true,odh::VmStatus::returned_false,
                          odh::VmStatus::budget_exhausted,odh::VmStatus::fault,odh::VmStatus::waiting}){
        const auto branch=static_cast<std::uint8_t>(status==odh::VmStatus::returned_false?255:
            status==odh::VmStatus::budget_exhausted?0:254);
        odh::Vm target({}, {}, {}, [&](const auto& frame,const auto&,const auto& temps,const auto&){
            require(frame.context.caller==71&&frame.context.callee==207&&frame.context.stack_object==207&&
                frame.context.code_owner==owner,"callee uses top saved Main caller and exact target owner");
            require(frame.arguments[0]==42&&frame.arguments[1]==17&&temps[0]==9,
                "caller Temps become arguments while target Temps remain its own");
            if(status==odh::VmStatus::fault)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"synthetic fault"};
            if(status==odh::VmStatus::waiting)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
            odh::PrimitiveOutcome outcome{odh::PrimitiveDisposition::goto_true,{}};
            outcome.temp_write=odh::VmTempWrite{1,99};
            return outcome;
        },{{owner,{{main,tree(254)},{child,tree(branch)}}}});
        std::array<std::int16_t,20> target_temps{};target_temps[0]=9;
        target.restore_dormant_state({{main,0,{70,72,73,owner},{1},{2}},
            {main,0,{71,74,75,owner},{3},{4}}},target_temps);
        const auto before=target.snapshot();
        SavedObjectSubroutineReport report;
        const auto outcome=run_saved_object_subroutine(target,207,owner,child,caller_temps,report,3);
        require(report.result.started&&report.result.status==status,"subroutine records real execution status");
        const auto expected=status==odh::VmStatus::returned_true?odh::PrimitiveDisposition::goto_true:
            status==odh::VmStatus::returned_false?odh::PrimitiveDisposition::goto_false:odh::PrimitiveDisposition::unsupported;
        require(outcome.disposition==expected,"only completed true/false become a parent branch");
        require(report.trace.size()==report.result.steps&&report.result.steps==(status==odh::VmStatus::budget_exhausted?3:1),
            "bounded trace and step count include every executed instruction");
        auto expected_temps=target_temps;
        if(status!=odh::VmStatus::fault&&status!=odh::VmStatus::waiting)expected_temps[1]=99;
        require(report.temps_before==target_temps&&report.temps_after==expected_temps&&caller_temps[1]==17,
            "report retains target register writes without publishing them to the calling thread");
        require(target.status()==odh::VmStatus::dormant&&target.steps()==0&&!target.pending()&&target.stack().size()==2&&
            target.stack().back().context.stack_object==75&&target.stack().back().arguments==before.stack.back().arguments&&
            target.stack().back().locals==before.stack.back().locals&&target.temps()==expected_temps,
            "true false budget fault and wait all preserve dormant Main and retain the target registers");
    }
    odh::Vm empty({});SavedObjectSubroutineReport rejected;
    require(run_saved_object_subroutine(empty,207,owner,child,caller_temps,rejected).disposition==
        odh::PrimitiveDisposition::unsupported&&!rejected.result.started,
        "missing saved Main cannot be presented as a completed false result");
    for(const auto budget:{1U,3U}){
        std::int16_t world_attribute=0;
        odh::VmMemory memory;
        memory.read=[&](const auto&,auto,auto)->std::optional<std::int16_t>{return world_attribute;};
        memory.write=[&](const auto&,auto,auto,std::int16_t value){world_attribute=value;return true;};
        auto effect=tree(254);
        effect.instructions={{2,1,253,{0,0,7,0,0,5,0,7}},{49,254,255,{}}};
        odh::Vm target({},memory,{},[](const auto&,const auto&,const auto&,const auto&){
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"unscheduled NotifyOutOfIdle"};
        },{{owner,{{main,tree(254)},{child,effect}}}});
        target.restore_dormant_state({{main,0,{207,207,0,owner},{},{}}},{});
        SavedObjectSubroutineReport report;
        const auto outcome=run_saved_object_subroutine(target,207,owner,child,caller_temps,report,budget);
        require(outcome.disposition==odh::PrimitiveDisposition::unsupported&&world_attribute==7&&
            report.result.status==(budget==1?odh::VmStatus::budget_exhausted:odh::VmStatus::fault)&&
            target.status()==odh::VmStatus::dormant&&target.steps()==0,
            "external writes persist across budget/fault while the parent receives no false success");
    }
    auto closure=tree(254);
    closure.instructions[0].opcode=4098;
    require(saved_phone_update_closure(closure,4097),"audited private dependency is admitted");
    for(const auto opcode:{280,4099,8192,0,28}){
        closure.instructions[0].opcode=static_cast<std::uint16_t>(opcode);
        require(!saved_phone_update_closure(closure,4097),"unresolved children and foreign primitives are rejected");
    }
    closure.instructions[0].opcode=26;
    require(saved_phone_update_closure(closure,4098),"relation-only child is admitted");
    closure.instructions[0].opcode=4097;
    require(!saved_phone_update_closure(closure,4098),"recursive child closure is rejected");
    std::cout<<"saved object subroutine context/returns/budget/fault/wait passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
