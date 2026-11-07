#include "odh/vm.hpp"

#include <iostream>
#include <limits>
#include <sstream>
#include <map>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
odh::Instruction expr(unsigned op, int lhs, int rhs, unsigned ls = 8, unsigned rs = 7,
                      unsigned yes = 254, unsigned no = 255) {
    odh::Instruction i{2, static_cast<std::uint8_t>(yes), static_cast<std::uint8_t>(no), {}};
    i.operands = {static_cast<std::uint8_t>(lhs & 255), static_cast<std::uint8_t>((lhs >> 8) & 255),
                  static_cast<std::uint8_t>(rhs & 255), static_cast<std::uint8_t>((rhs >> 8) & 255),
                  0, static_cast<std::uint8_t>(op), static_cast<std::uint8_t>(ls), static_cast<std::uint8_t>(rs)};
    return i;
}
odh::Bhav routine(std::initializer_list<odh::Instruction> instructions, unsigned locals = 0,
                  unsigned args = 4) {
    return {0x8002, instructions, {}, odh::Bhav::Metadata{0, static_cast<std::uint8_t>(args),
                                       static_cast<std::uint16_t>(locals), 0}};
}
odh::RoutineKey key(unsigned id = 4096) { return odh::routine_key(static_cast<std::uint16_t>(id)); }
odh::Vm machine(odh::Bhav r) { return odh::Vm({{key(), std::move(r)}}); }
odh::Instruction call(unsigned id, unsigned yes = 254, unsigned no = 255) {
    return {static_cast<std::uint16_t>(id), static_cast<std::uint8_t>(yes),
            static_cast<std::uint8_t>(no), {255,255,255,255,255,255,255,255}};
}
odh::Vm output_machine(odh::PrimitiveOutcome outcome) {
    return odh::Vm({{key(), routine({{1,1,2,{}},expr(2,1,1,7,7),expr(2,1,2,7,7)},1)}},
        {}, {}, [outcome](const auto&,const auto&,const auto&,const auto&) { return outcome; });
}
void rejects_outputs(odh::PrimitiveOutcome outcome) {
    auto vm = output_machine(outcome);
    vm.start(key(),{11,22,33},{17});
    for (std::size_t i=0;i<vm.temps().size();++i) vm.temps()[i]=static_cast<std::int16_t>(100+i);
    const auto before=vm.temps();
    const auto arguments=vm.stack()[0].arguments;
    const auto locals=vm.stack()[0].locals;
    require(vm.run(1)==odh::VmStatus::fault,"invalid paired output faults");
    require(vm.temps()==before,"invalid paired output preserves every temp");
    require(vm.stack().size()==1 && vm.stack()[0].pc==0 &&
            vm.stack()[0].arguments==arguments && vm.stack()[0].locals==locals,
            "invalid paired output preserves frame and branch");
    require(vm.stack()[0].context.caller==11 && vm.stack()[0].context.callee==22 &&
            vm.stack()[0].context.stack_object==33 && vm.stack()[0].context.code_owner==0,
            "invalid paired output preserves all context identities");
    require(!vm.pending(),"invalid paired output does not publish wait or child");
}
void storage_liveness_proof() {
    constexpr std::int16_t retired=185;
    const auto wait=odh::Instruction{0,254,253,{0,0,0,0,0,0,0,0}};
    const auto transparent=[](const auto&,const auto& instruction){return instruction.opcode==0;};
    const auto make=[&](odh::Bhav root){return odh::Vm(
        {{key(),std::move(root)},{key(280),routine({wait})}}, {}, {},
        [](const auto&,const auto& instruction,const auto&,const auto&){
            return instruction.opcode==0?odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}}:
                odh::PrimitiveOutcome{};});};
    const auto start=[&](odh::Vm& vm,odh::VmContext context=odh::VmContext{203,203,185}){
        require(vm.start(key(),context,{100})==odh::VmStatus::running&&vm.run(2)==odh::VmStatus::waiting,
            "source-like nested idle waits with old StackObject reference");};
    auto source=make(routine({call(280,1,253),expr(5,0,0,10,7)}));start(source);
    const auto before=source.snapshot();unsigned audits=0;
    require(!source.can_release_entity_storage(retired),"pending service requires explicit host transparency audit");
    require(source.can_release_entity_storage(retired,[&](const auto& frame,const auto& instruction){
        ++audits;return frame.context.caller==203&&instruction.opcode==0;}),
        "nested pending child returns before parent overwrites StackObject without reading storage");
    const auto after=source.snapshot();
    require(audits==1&&source.references_entity(retired)&&before.status==after.status&&
        before.steps==after.steps&&before.run_generation==after.run_generation&&
        before.ticket_sequence==after.ticket_sequence&&before.temps==after.temps&&before.temp_xl==after.temp_xl&&
        before.stack.size()==after.stack.size()&&before.pending->ticket==after.pending->ticket&&
        after.pending->context.stack_object==retired&&after.stack[0].context.stack_object==retired&&
        after.stack[1].context.stack_object==retired&&after.stack[0].pc==before.stack[0].pc&&
        after.stack[1].pc==before.stack[1].pc&&after.stack[1].arguments==before.stack[1].arguments,
        "proof leaves IDs, frames, pending ticket, counters and registers intact");
    require(!source.can_release_entity_storage(retired,transparent,2)&&
        source.can_release_entity_storage(retired,transparent,3),"shared node budget counts child wait, parent call and overwrite");
    require(!source.can_release_entity_storage(0,transparent)&&
        source.can_release_entity_storage(99,{},0),"invalid identities reject and absent references require no CFG budget");
    require(!source.can_release_entity_storage(retired,[](const auto&,const auto&){return false;})&&
        !source.can_release_entity_storage(retired,[](const auto&,const auto&)->bool{throw std::runtime_error("audit");}),
        "denied and throwing host audits fail closed");
    require(!source.can_release_entity_storage(retired,[&](const auto&,const auto&){
        (void)source.can_release_entity_storage(retired,transparent);return true;}),"recursive proof rejects the outer audit too");
    require(!source.can_release_entity_storage(retired,[&](const auto&,const auto&){
        source.start(key());return true;})&&source.run_generation()==before.run_generation&&
        source.pending()->ticket==before.pending->ticket,"mutating callback reentry is refused before execution state changes");
    require(!source.can_release_entity_storage(retired,[&](const auto&,const auto&){
        source.temps()[0]=8;return true;})&&source.snapshot().temps==before.temps,
        "mutable register access is unavailable to the audit");
    require(source.complete(source.pending()->ticket,true)&&source.run(8)==odh::VmStatus::returned_true,
        "only ordinary future execution performs the authored overwrite");
    for(const auto literal:{-1,65,185,32767}){
        auto value=make(routine({call(280,1,255),expr(5,0,0,8,10,2),expr(5,0,literal,10,7)}));start(value);
        require(value.can_release_entity_storage(retired,transparent),
            "raw StackObjectID copy is a value and any literal assignment releases the old bound storage");
    }
    auto raw_rebind=make(routine({call(280,1,255),expr(5,0,0,10,10)}));start(raw_rebind);
    require(raw_rebind.can_release_entity_storage(retired,transparent),
        "raw-ID reassignment performs a fresh binding without reading old entity storage");
    for(const auto scope:{0U,1U,3U,4U,19U,21U,26U,35U}){
        auto data=make(routine({call(280,1,255),expr(5,0,0,8,scope,2),expr(5,0,0,10,7)}));start(data);
        require(!data.can_release_entity_storage(retired,transparent),"unknown or entity data footprints pin storage");
    }
    auto lhs=make(routine({call(280,1,255),expr(5,0,7,1,7,2),expr(5,0,0,10,7)}));start(lhs);
    require(!lhs.can_release_entity_storage(retired,transparent),"entity data writes also pin storage");
    auto unknown=make(routine({call(280,1,255),{99,2,255,{}},expr(5,0,0,10,7)}));start(unknown);
    require(!unknown.can_release_entity_storage(retired,transparent),"future primitive is not approved by pending-only audit");
    auto future_call=make(routine({call(280,1,255),call(280,2,255),expr(5,0,0,10,7)}));start(future_call);
    require(!future_call.can_release_entity_storage(retired,transparent),"future ordinary calls remain unsupported by bounded proof");
    auto loop=make(routine({call(280,1,255),expr(2,0,0,8,7,1,2),expr(5,0,0,10,7)}));start(loop);
    require(!loop.can_release_entity_storage(retired,transparent),"CFG cycles fail closed even with an overwrite exit");
    auto branches=make(routine({call(280,1,255),expr(2,1,1,7,7,2,3),
        expr(5,0,0,10,7),{99,254,255,{}}}));start(branches);
    require(!branches.can_release_entity_storage(retired,transparent),
        "proof does not evaluate constants to skip an unsafe comparison branch");
    auto fd=make(routine({call(280,253,1),expr(5,0,0,8,1)}));start(fd);
    require(!fd.can_release_entity_storage(retired,transparent),"FD follows native counterpart fallback rather than pretending to terminate");
    for(const auto context:{odh::VmContext{retired,203,retired},odh::VmContext{203,retired,retired}}){
        auto identity=make(routine({call(280,1,255),expr(5,0,0,10,7)}));start(identity,context);
        require(!identity.can_release_entity_storage(retired,transparent),"Caller and Callee references always pin storage");
    }
    odh::Vm missing({{key(),routine({expr(5,0,0,10,7)})}}, {}, {}, {},
        {{99,{{key(),routine({expr(5,0,0,10,7)})}}}});
    unsigned resolutions=0;
    require(missing.set_owner_resolver([&](auto)->std::shared_ptr<const odh::VmProgram>{++resolutions;return {};}),
        "bind resolver which storage proof must never invoke");
    missing.restore_dormant_state({{key(4097),0,{203,203,retired,99},{},{}}},{},
        odh::VmSavedRoutinePolicy::defer_missing_or_pc_fault);
    require(missing.activate_dormant_state()&&!missing.can_release_entity_storage(retired,transparent)&&resolutions==0,
        "missing installed routine cannot authorize retirement or trigger owner resolution");
    auto dormant=make(routine({expr(5,0,0,10,7)}));
    dormant.restore_dormant_state({{key(),0,{203,203,retired},{},{}}},{});
    require(!dormant.can_release_entity_storage(retired,transparent)&&dormant.activate_dormant_state()&&
        dormant.can_release_entity_storage(retired),"dormant execution cannot authorize storage release before activation");
    const auto fault_state=[&]{auto state=dormant.snapshot();state.status=odh::VmStatus::fault;
        state.diagnostic="fixture fault";return state;}();
    auto faulted=make(routine({expr(5,0,0,10,7)}));
    require(faulted.restore_snapshot(fault_state)&&!faulted.can_release_entity_storage(retired),
        "faulted execution pins even when its current instruction would overwrite StackObject");
    odh::Vm mixed({{key(),routine({call(280,1,255),expr(5,0,0,10,7)})},
        {key(280),routine({expr(5,0,0,10,7,1),wait})}}, {}, {},
        [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
    require(mixed.start(key(),{203,203,retired})==odh::VmStatus::running&&mixed.run(3)==odh::VmStatus::waiting&&
        mixed.stack()[0].context.stack_object==retired&&mixed.stack()[1].context.stack_object==0&&
        !mixed.can_release_entity_storage(retired,transparent),"mixed child bindings cannot hide future opaque child work");
    // A real external invocation is constructed through the public service API.
    odh::Vm external({{key(),routine({{45,254,255,{}}})},{key(280),routine({wait})}}, {}, {},
        [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
    require(external.start(key(),{203,203,retired})==odh::VmStatus::running&&external.run(1)==odh::VmStatus::waiting&&
        external.call_pending(external.pending()->ticket,{key(280),{203,203,0},{100}})&&external.run(1)==odh::VmStatus::waiting&&
        !external.can_release_entity_storage(retired,transparent),"suspended or external frame references cannot use the ordinary return proof");
    // Matching BHAV ID alone does not make a primitive-produced call an ordinary source subroutine.
    odh::Vm primitive_child({{key(),routine({{1,1,255,{}},expr(5,0,0,10,7)})},{key(280),routine({wait})}}, {}, {},
        [&](const auto& frame,const auto& instruction,const auto&,const auto&){
            if(instruction.opcode==1)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::call,{}, {},
                odh::VmCall{key(280),frame.context,{100}}};
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
    require(primitive_child.start(key(),{203,203,retired})==odh::VmStatus::running&&
        primitive_child.run(2)==odh::VmStatus::waiting&&!primitive_child.can_release_entity_storage(retired,transparent),
        "arbitrary primitive child calls do not inherit ordinary subroutine transparency");
}
void service_callback_completion(){
    const odh::Instruction create{42,1,2,{}};
    const auto program=[&]{return odh::RoutineSet{
        {key(),routine({create,expr(5,5,11),expr(5,5,22)})},
        {key(321),routine({{99,254,255,{}}},2,6)}};};
    const auto owners=[] {return odh::RoutineOwners{{77,{
        {key(4097),routine({{99,254,255,{}}},2,6)},
        {key(8193),routine({{99,254,255,{}}},2,6)}}}};};
    const odh::VmContext parent{1,9,2,0};
    for(const auto callback_key:{key(4097),key(8193),key(321)})for(const auto result:{false,true}){
        unsigned creates=0,callbacks=0;
        const auto primitive=[&](const odh::VmFrame& frame,const odh::Instruction& instruction,const odh::Ts1Temps& temps,const auto&){
            if(instruction.opcode==42){++creates;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};}
            ++callbacks;
            require(instruction.opcode==99&&frame.context.caller==parent.caller&&frame.context.callee==parent.callee&&
                frame.context.stack_object==parent.stack_object&&frame.context.code_owner==77&&
                frame.arguments==std::vector<std::int16_t>{123,0,0,0,-1,-1}&&!frame.exact_arguments&&temps[0]==123,
                "callback retains parent identities, exact resource owner and source argument extension after Temp0 commit");
            return odh::PrimitiveOutcome{result?odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::goto_false,{}};
        };
        odh::Vm vm(program(),{}, {},primitive,owners());
        require(vm.start(key(),parent)==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::waiting,
            "source Create waits before callback completion");
        const auto ticket=vm.pending()->ticket,steps=vm.steps();
        const odh::VmCall callback{callback_key,{1,9,2,77},{123,0,0,0}};
        require(vm.complete_call(ticket,callback,123)&&vm.status()==odh::VmStatus::running&&!vm.pending()&&
            vm.stack().size()==2&&vm.stack()[0].pc==0&&vm.stack()[1].return_mode==odh::VmCallReturnMode::ordinary&&
            vm.steps()==steps&&vm.snapshot().ticket_sequence==ticket&&!vm.complete_call(ticket,callback,124),
            "matching completion consumes only its wait and pushes one ordinary child without advancing parent or issuing another ticket");
        const auto saved=vm.snapshot();odh::Vm fresh(program(),{}, {},primitive,owners());
        require(fresh.restore_snapshot(saved)&&fresh.run(2)==odh::VmStatus::returned_true&&
            fresh.temps()[5]==(result?11:22)&&fresh.temps()[0]==123&&creates==1&&callbacks==1&&
            fresh.steps()==steps+2&&!fresh.interaction_return()&&!fresh.external_frame_return()&&!fresh.immediate_return(),
            "fresh callback snapshot returns through the original Create true/false branch in the same execution slice");
    }
    {
        unsigned creates=0,callbacks=0;
        const auto primitive=[&](const auto&,const odh::Instruction& instruction,const auto&,const auto&){
            if(instruction.opcode==42)++creates;else ++callbacks;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
        };
        odh::Vm vm(program(),{}, {},primitive,owners());vm.start(key(),parent);vm.run(1);
        const auto original=vm.pending()->ticket;
        const odh::VmCall callback{key(4097),{1,9,2,77},{44,0,0,0}};
        require(vm.complete_call(original,callback,44)&&vm.run(1)==odh::VmStatus::waiting&&
            vm.pending()->ticket>original&&vm.stack()[0].pc==0,"callback may wait on its own service without redispatching Create");
        const auto child=vm.pending()->ticket;
        const auto saved=vm.snapshot();odh::Vm restored(program(),{}, {},primitive,owners());
        require(restored.restore_snapshot(saved)&&!restored.complete_call(original,callback,45)&&
            restored.pending()->ticket==child&&restored.temps()[0]==44&&restored.complete(child,false)&&
            restored.run(1)==odh::VmStatus::returned_true&&restored.temps()[5]==22&&creates==1&&callbacks==1,
            "fresh waiting callback resumes only its own ticket and returns false through the protected Create branch");
    }
    {
        const auto primitive=[](const auto&,const odh::Instruction& instruction,const auto&,const auto&){
            return odh::PrimitiveOutcome{instruction.opcode==42?odh::PrimitiveDisposition::wait:
                odh::PrimitiveDisposition::goto_true_next_tick,{}};
        };
        odh::Vm vm(program(),{}, {},primitive,owners());vm.start(key(),parent);vm.run(1);
        const auto ticket=vm.pending()->ticket;
        require(vm.complete_call(ticket,{key(4097),{1,9,2,77},{9,0,0,0}},9)&&vm.run(3)==odh::VmStatus::next_tick&&
            vm.stack().size()==1&&vm.stack().back().pc==1&&vm.temps()[5]==0,
            "only an authored callback next-tick outcome delays its already selected parent continuation");
        odh::Vm fresh(program(),{}, {},primitive,owners());
        require(fresh.restore_snapshot(vm.snapshot())&&fresh.resume_next_tick()&&fresh.run(1)==odh::VmStatus::returned_true&&
            fresh.temps()[5]==11,"callback next-tick state restores without creating another service or callback tick");
    }
    {
        const auto immediate_program=[&]{return odh::RoutineSet{
            {key(),routine({{0,1,2,{}},expr(5,5,11),expr(5,5,22)})},
            {key(4100),routine({create,expr(5,6,31),expr(5,6,32)})},
            {key(4097),routine({{99,254,255,{}}},0,6)}};};
        const auto primitive=[](const auto&,const odh::Instruction& instruction,const auto&,const auto&){
            return odh::PrimitiveOutcome{instruction.opcode==99?odh::PrimitiveDisposition::goto_false:odh::PrimitiveDisposition::wait,{}};};
        odh::Vm vm(immediate_program(),{}, {},primitive);vm.start(key(),parent);vm.run(1);
        const auto sleep=vm.pending()->ticket;
        const auto immediate=vm.inject_immediate({key(4100),parent,{}},[](const auto&,const auto&){return true;});
        require(immediate&&vm.run(1)==odh::VmStatus::waiting,"immediate action parks parent Sleep and reaches Create");
        const auto create_ticket=vm.pending()->ticket;
        require(vm.complete_call(create_ticket,{key(4097),parent,{71,0,0,0}},71)&&
            vm.immediate_continuations().front().parent_pending->ticket==sleep,
            "ordinary callback completion preserves an enclosing immediate parent's original service");
        odh::Vm fresh(immediate_program(),{}, {},primitive);
        require(fresh.restore_snapshot(vm.snapshot())&&fresh.run(2)==odh::VmStatus::yielded&&fresh.immediate_return()&&
            fresh.immediate_return()->ticket==*immediate&&fresh.temps()[6]==32&&fresh.temps()[5]==0&&
            fresh.resume_immediate(*immediate)&&fresh.pending()->ticket==sleep&&fresh.complete(sleep,true)&&
            fresh.run(1)==odh::VmStatus::returned_true&&fresh.temps()[5]==11,
            "callback result branches only its immediate Create parent; ACK then restores independent Sleep continuation");
    }
    const auto frame_equal=[](const odh::VmFrame& a,const odh::VmFrame& b){
        return a.key.scope==b.key.scope&&a.key.id==b.key.id&&a.pc==b.pc&&a.arguments==b.arguments&&a.locals==b.locals&&
            a.context.caller==b.context.caller&&a.context.callee==b.context.callee&&a.context.stack_object==b.context.stack_object&&
            a.context.code_owner==b.context.code_owner&&a.return_mode==b.return_mode&&a.invocation_ticket==b.invocation_ticket&&
            a.exact_arguments==b.exact_arguments;};
    for(unsigned invalid=0;invalid<16;++invalid){
        auto bank=owners();auto limits=odh::VmLimits{};
        if(invalid==11)bank.at(77).at(key(4097))=routine({{99,3,3,{}}});
        if(invalid==12)bank.at(77).at(key(4097))=routine({});
        if(invalid==13)bank.at(77).at(key(4097)).metadata.reset();
        if(invalid==14)limits.stack_depth=1;
        if(invalid==15)limits.frame_variables=4;
        const auto primitive=[](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
        odh::Vm vm(program(),{},limits,primitive,std::move(bank));unsigned resolutions=0;
        require(vm.set_owner_resolver([&](auto)->std::shared_ptr<const odh::VmProgram>{++resolutions;return {};}),
            "install observation-only resolver before invalid callback cases");
        vm.start(key(),parent);vm.run(1);vm.temps()[0]=19;vm.temp_xl()[1]=29;
        auto ticket=vm.pending()->ticket;odh::VmCall callback{key(4097),{1,9,2,77},{123,0,0,0}};
        if(invalid==0)++ticket;
        if(invalid==1)callback.context.caller++;
        if(invalid==2)callback.context.callee++;
        if(invalid==3)callback.context.stack_object++;
        if(invalid==4)callback.context.code_owner=999;
        if(invalid==5)callback.key=key(4999);
        if(invalid==6)callback.arguments.pop_back();
        if(invalid==7)callback.arguments.push_back(7);
        if(invalid==8)callback.exact_arguments=true;
        if(invalid==9)callback.return_mode=odh::VmCallReturnMode::interaction_next_tick;
        if(invalid==10)callback.key.scope=odh::RoutineScope::semiglobal;
        const auto before=vm.snapshot();
        require(!vm.complete_call(ticket,callback,123),"invalid callback/ticket/resource/extent/branch/budget cannot consume a service wait");
        const auto after=vm.snapshot();
        require(after.status==before.status&&after.diagnostic==before.diagnostic&&after.steps==before.steps&&
            after.ticket_sequence==before.ticket_sequence&&after.run_generation==before.run_generation&&
            after.temps==before.temps&&after.temp_xl==before.temp_xl&&after.pending&&before.pending&&
            after.pending->ticket==before.pending->ticket&&after.pending->instruction.step==before.pending->instruction.step&&
            after.pending->instruction.depth==before.pending->instruction.depth&&after.pending->instruction.pc==before.pending->instruction.pc&&
            after.pending->instruction.instruction.operands==before.pending->instruction.instruction.operands&&
            after.stack.size()==before.stack.size()&&frame_equal(after.stack.front(),before.stack.front())&&
            after.suspended_pending.empty()&&after.immediate_continuations.empty()&&!after.interaction_return&&
            !after.external_frame_return&&!after.immediate_return&&resolutions==0&&!vm.shared_program_for_owner(999),
            "rejected callback leaves pending/frame/registers/counters/continuations/owner set unchanged");
    }
    {
        auto bank=owners();bank.at(77).at(key(4097))=routine({expr(5,7,9,8,7,0,0)},0,6);
        const auto primitive=[](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
        odh::Vm vm(program(),{}, {},primitive,bank);vm.start(key(),parent);vm.run(1);
        require(vm.complete_call(vm.pending()->ticket,{key(4097),{1,9,2,77},{71,0,0,0}},71)&&
            vm.run(3)==odh::VmStatus::budget_exhausted&&vm.stack().size()==2&&vm.stack()[0].pc==0&&vm.temps()[0]==71,
            "callback ordinary instruction budget stops a loop without pretending Create has returned");
        odh::Vm fresh(program(),{}, {},primitive,bank);
        require(fresh.restore_snapshot(vm.snapshot())&&fresh.run(1)==odh::VmStatus::budget_exhausted&&fresh.stack()[0].pc==0,
            "budget-exhausted callback snapshot remains a live child after fresh restore");
    }
}
void immediate_frames() {
    const odh::Instruction sleep{0,1,2,{0,0,0,0,0,0,0,0}};
    const auto program=[&]{return odh::RoutineSet{
        {key(),routine({sleep,expr(5,8,11),expr(5,8,22)})},
        {key(4101),routine({expr(5,0,73,8,7,1),expr(5,0,123,42,7,2),
            {42,3,4,{}},expr(5,1,91),expr(5,1,-91,8,7,255,255)})},
        {key(4102),routine({expr(5,3,17,8,7,255,255)})},
        {key(4200),routine({expr(5,2,55,8,7,1),expr(5,1,76,42,7,2),{99,254,255,{}}})}};};
    unsigned effects=0;
    const auto primitive=[](const auto&,const auto&,const auto&,const auto&){
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
    const auto audit=[](const auto& pending,const auto& frame){
        return pending.instruction.instruction.opcode==0&&pending.context.caller==frame.context.caller;};
    const odh::VmCall action{key(4101),{1,9,9},{}};
    const odh::VmCall nested{key(4102),{1,10,10},{}};
    odh::Vm live(program(),{}, {},primitive);
    require(live.start(key(),{1,1,1},{100})==odh::VmStatus::running&&live.run(1)==odh::VmStatus::waiting,
        "prepare parent Sleep for immediate queue injection");
    const auto parent=live.snapshot();const auto parent_ticket=live.pending()->ticket;
    require(!live.can_inject_immediate(action)&&!live.inject_immediate(action),
        "generic pending primitive cannot be preempted without its host audit");
    const auto invocation=live.inject_immediate(action,audit);
    require(invocation&&live.status()==odh::VmStatus::running&&!live.pending()&&
        live.immediate_continuations().size()==1&&live.immediate_continuations()[0].parent_pending->ticket==parent_ticket&&
        live.steps()==parent.steps&&live.run_generation()==parent.run_generation&&live.stack()[0].pc==0&&
        live.stack()[0].arguments==parent.stack[0].arguments&&live.stack()[1].return_mode==odh::VmCallReturnMode::immediate,
        "injection parks full parent wait and raw PC without executing or branching it");
    require(!live.can_release_entity_storage(9)&&live.run(8)==odh::VmStatus::waiting&&
        live.pending()->instruction.instruction.opcode==42&&live.temps()[0]==73&&live.temp_xl()[0]==123,
        "immediate child owns asynchronous Create wait and shared thread registers");
    const auto child_ticket=live.pending()->ticket;
    require(!live.complete(parent_ticket,true)&&!live.retry_pending(parent_ticket)&&!live.update_pending(parent_ticket,{odh::VmFrameBank::arguments,0,0}),
        "parked parent controller cannot complete or mutate the child's active pending service");
    const auto before_check=live.snapshot();
    const auto check=live.evaluate_check_at_safe_point(key(4200),{1,9,9},{},8,
        [&](const auto&,const auto&,const auto&,const auto&){++effects;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};});
    require(check.started&&check.status==odh::VmStatus::returned_true&&check.steps==3&&effects==1&&
        live.temps()[2]==55&&live.temp_xl()[1]==76&&live.status()==before_check.status&&
        live.steps()==before_check.steps&&live.snapshot().ticket_sequence==before_check.ticket_sequence&&
        live.run_generation()==before_check.run_generation&&live.pending()->ticket==child_ticket&&
        live.stack()[0].pc==before_check.stack[0].pc&&live.stack()[1].pc==before_check.stack[1].pc&&
        live.immediate_continuations()[0].parent_pending->ticket==parent_ticket,
        "disposable boundary Check shares registers/effects while preserving immediate and parent execution");
    const auto false_check=live.evaluate_check_at_safe_point(key(4102),{1,9,9},{},1);
    require(false_check.started&&false_check.status==odh::VmStatus::returned_false&&
        live.pending()->ticket==child_ticket&&live.stack()[1].pc==2,
        "false boundary Check never branches suspended Main or immediate action");
    const auto guarded_check=live.evaluate_check_at_safe_point(key(4200),{1,9,9},{},8,
        [&](const auto&,const auto&,const auto&,const auto&){live.start(key());
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};});
    require(guarded_check.started&&guarded_check.status==odh::VmStatus::fault&&
        live.run_generation()==before_check.run_generation&&live.steps()==before_check.steps&&
        live.pending()->ticket==child_ticket,"disposable Check cannot reenter parent execution");
    const auto inflight=live.snapshot();
    odh::Vm restored(program(),{}, {},primitive);
    require(restored.restore_snapshot(inflight)&&restored.immediate_continuations()[0].parent_pending->ticket==parent_ticket,
        "fresh VM restores immediate child Create wait and parked parent Sleep independently");
    for(unsigned mutation=0;mutation<7;++mutation){
        auto bad=inflight;
        if(mutation==0)bad.immediate_continuations[0].parent_depth=0;
        if(mutation==1)++bad.immediate_continuations[0].ticket;
        if(mutation==2)bad.immediate_continuations[0].parent_status=odh::VmStatus::next_tick;
        if(mutation==3)bad.immediate_continuations[0].parent_pending.reset();
        if(mutation==4)++bad.immediate_continuations[0].parent_pending->context.stack_object;
        if(mutation==5)bad.immediate_continuations.clear();
        if(mutation==6)bad.stack.back().context.caller=2;
        odh::Vm rejected(program(),{}, {},primitive);
        require(!rejected.restore_snapshot(bad)&&rejected.status()==odh::VmStatus::ready&&rejected.stack().empty(),
            "malformed immediate linkage rejects atomically before publication");
    }
    const auto any_pending=[](const auto&,const auto&){return true;};
    const auto second=live.inject_immediate(nested,any_pending);
    require(second&&*second>child_ticket&&live.immediate_continuations().size()==2&&live.run(1)==odh::VmStatus::yielded&&
        live.immediate_return()&&!live.immediate_return()->result&&live.immediate_return()->ticket==*second&&
        !live.pending()&&live.stack().size()==2&&live.stack()[1].pc==2,
        "nested immediate false return yields diagnostic result without selecting suspended Create branch");
    require(!live.inject_immediate(nested),"unobserved immediate return blocks another queue injection");
    const auto returned=live.snapshot();odh::Vm returned_copy(program(),{}, {},primitive);
    require(returned_copy.restore_snapshot(returned)&&!live.resume_immediate(*second+1)&&
        live.resume_immediate(*second)&&!live.resume_immediate(*second)&&
        live.status()==odh::VmStatus::waiting&&live.pending()->ticket==child_ticket&&live.stack()[1].pc==2,
        "queue ACK restores exact nested parent pending and rejects stale duplicate tickets");
    auto stale_return=returned;++stale_return.immediate_return->run_generation;
    odh::Vm stale(program(),{}, {},primitive);
    require(!stale.restore_snapshot(stale_return),"immediate completion from another VM run cannot restore");
    require(live.complete(child_ticket,true)&&live.run(1)==odh::VmStatus::yielded&&
        live.immediate_return()->ticket==*invocation&&live.immediate_return()->result&&live.stack().size()==1&&
        live.stack()[0].pc==0&&live.temps()[1]==91&&live.temps()[8]==0,
        "outer immediate result never selects a parent Sleep branch");
    odh::Vm final_copy(program(),{}, {},primitive);
    require(final_copy.restore_snapshot(live.snapshot())&&final_copy.resume_immediate(*invocation)&&
        final_copy.pending()->ticket==parent_ticket&&final_copy.stack()[0].arguments[0]==100&&
        final_copy.status()==odh::VmStatus::waiting&&final_copy.stack()[0].pc==0,
        "returned immediate snapshot restores unchanged parent counter and PC at ACK");
    require(live.resume_immediate(*invocation)&&live.pending()->ticket==parent_ticket&&
        live.complete(parent_ticket,true)&&live.run(1)==odh::VmStatus::returned_true&&live.temps()[8]==11,
        "only original parent pending completion takes its authored branch");
    odh::Vm fresh(program(),{}, {},primitive);fresh.start(key(),{1,1,1});
    require(fresh.inject_immediate(nested).has_value()&&fresh.steps()==0,
        "immediate injection before first Main instruction does not invent dispatch work");
    odh::Vm fresh_copy(program(),{}, {},primitive);
    require(fresh_copy.restore_snapshot(fresh.snapshot()),"zero-work immediate frame snapshot has a valid internal invocation ticket");
    const auto stable=fresh.snapshot();
    auto wrong=nested;wrong.context.caller=2;
    require(!fresh.can_inject_immediate(wrong)&&!fresh.inject_immediate(wrong)&&
        fresh.snapshot().ticket_sequence==stable.ticket_sequence&&fresh.stack().size()==stable.stack.size(),
        "different thread caller rejects injection without changing queue ticket or frames");
    wrong=nested;wrong.return_mode=odh::VmCallReturnMode::immediate;
    require(!fresh.inject_immediate(wrong),"external injection takes an ordinary action payload rather than a pretagged protected call");
    wrong=nested;wrong.context.code_owner=77;
    require(!fresh.inject_immediate(wrong),"missing installed owner rejects before any resolver or execution");
    odh::Vm limited(program(),{}, {1,4096,64},primitive);limited.start(key(),{1,1,1});
    require(!limited.inject_immediate(nested)&&limited.stack().size()==1&&limited.snapshot().ticket_sequence==0,
        "frame-cap rejection leaves parent and invocation sequence unchanged");
    odh::Vm exhausted(program(),{}, {},primitive);auto exhausted_state=limited.snapshot();
    exhausted_state.ticket_sequence=std::numeric_limits<std::uint64_t>::max();
    require(exhausted.restore_snapshot(exhausted_state)&&!exhausted.inject_immediate(nested)&&
        exhausted.snapshot().ticket_sequence==exhausted_state.ticket_sequence,"injection ticket overflow rejects atomically");
    odh::Vm audited(program(),{}, {},primitive);audited.start(key(),{1,1,1},{100});audited.run(1);
    const auto audited_before=audited.snapshot();
    require(!audited.inject_immediate(nested,[&](const auto&,const auto&){audited.start(key());return true;})&&
        audited.run_generation()==audited_before.run_generation&&audited.pending()->ticket==audited_before.pending->ticket,
        "pending transparency audit cannot mutate or reset the parent VM");
    require(fresh.reset_execution_state()&&!fresh.immediate_return()&&fresh.immediate_continuations().empty()&&
        !fresh.resume_immediate(stable.immediate_continuations[0].ticket),
        "explicit reset clears protected immediate state");
    // An existing ordinary action is a protected parent, not an external-route frame.
    odh::Vm ordinary({{key(),routine({{17,254,255,{}}})},
        {key(4103),routine({{0,254,255,{}}})},{key(4102),routine({expr(5,3,17,8,7,255,255)})}}, {}, {},
        [&](const auto& frame,const auto& instruction,const auto&,const auto&){
            if(instruction.opcode==17)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::call,{}, {},
                odh::VmCall{key(4103),frame.context,{},odh::VmCallReturnMode::interaction_next_tick}};
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
    require(ordinary.start(key(),{1,1,1})==odh::VmStatus::running&&ordinary.run(1)==odh::VmStatus::budget_exhausted&&
        ordinary.stack()[1].return_mode==odh::VmCallReturnMode::interaction_next_tick,
        "normal action is active before immediate preemption");
    const auto normal_ticket=ordinary.stack()[1].invocation_ticket;
    const auto over_action=ordinary.inject_immediate(nested);
    require(over_action&&ordinary.run(1)==odh::VmStatus::yielded&&ordinary.resume_immediate(*over_action)&&
        ordinary.status()==odh::VmStatus::budget_exhausted&&ordinary.stack()[1].pc==0&&ordinary.run(1)==odh::VmStatus::waiting,
        "immediate return restores original normal action budget status and PC");
    const auto normal_sleep=ordinary.pending()->ticket;
    const auto over_wait=ordinary.inject_immediate(nested,audit);
    require(over_wait&&ordinary.run(1)==odh::VmStatus::yielded,"immediate also preempts normal action Sleep");
    odh::Vm ordinary_copy({{key(),routine({{17,254,255,{}}})},{key(4103),routine({{0,254,255,{}}})},
        {key(4102),routine({expr(5,3,17,8,7,255,255)})}}, {}, {},primitive);
    require(ordinary_copy.restore_snapshot(ordinary.snapshot())&&ordinary.resume_immediate(*over_wait)&&
        ordinary.pending()->ticket==normal_sleep&&ordinary.complete(normal_sleep,true)&&
        ordinary.interaction_return()&&ordinary.interaction_return()->ticket==normal_ticket&&
        ordinary.resume_interaction(normal_ticket)&&ordinary.stack()[0].pc==0,
        "normal interaction return protocol remains independent after immediate stack restore");
}
void immediate_external_ancestry() {
    const auto program=[](){return odh::RoutineSet{
        {key(),routine({{27,254,255,{}}})},
        {key(4100),routine({{0,254,255,{}}})},
        {key(4101),routine({expr(5,1,77)})},
        {key(4102),routine({{45,254,255,{}}})}};};
    const odh::VmPrimitive wait=[](const auto&,const auto&,const auto&,const auto&){
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
    odh::Vm live(program(),{}, {},wait);live.start(key(),{1,1,1});
    require(live.run(1)==odh::VmStatus::waiting,"portal ancestry fixture waits on original route");
    const auto route=live.pending()->ticket;
    require(live.call_pending(route,{key(4100),{1,9,9},{}})&&live.run(1)==odh::VmStatus::waiting,
        "portal BHAV owns an exact suspended route pending and active Sleep child");
    const auto sleep=live.pending()->ticket;
    const auto immediate=live.inject_immediate({key(4102),{1,9,9},{}},[](const auto&,const auto&){return true;});
    require(immediate&&live.suspended_pending().size()==1&&live.run(1)==odh::VmStatus::waiting,
        "immediate action may cover authenticated external portal ancestry and request its own route");
    const auto child_route=live.pending()->ticket;
    require(live.call_pending(child_route,{key(4101),{1,9,9},{}}),
        "immediate child route can itself enter an external BHAV without dropping older suspended route");
    const auto saved=live.snapshot();odh::Vm restored(program(),{}, {},wait);
    require(restored.restore_snapshot(saved),"mixed immediate and portal stack restores every original pending owner and ticket");
    for(unsigned kind=0;kind<3;++kind){
        auto broken=saved;
        if(kind==0)++broken.suspended_pending[0].ticket;
        if(kind==1)++broken.immediate_continuations[0].parent_depth;
        if(kind==2)++broken.immediate_continuations[0].parent_pending->context.code_owner;
        odh::Vm rejected(program(),{}, {},wait);
        require(!rejected.restore_snapshot(broken),"forged mixed portal/immediate ancestry is rejected before publication");
    }
    for(auto* vm:{&live,&restored}){
        require(vm->run(4)==odh::VmStatus::yielded&&vm->external_frame_return()&&
            vm->external_frame_return()->ticket==child_route&&vm->resume_external_frame(child_route)&&
            vm->complete(child_route,true)&&vm->run(4)==odh::VmStatus::yielded&&vm->immediate_return()&&
            vm->immediate_return()->ticket==*immediate&&vm->resume_immediate(*immediate)&&
            vm->pending()->ticket==sleep&&vm->complete(sleep,true)&&vm->run(4)==odh::VmStatus::yielded&&
            vm->external_frame_return()->ticket==route&&vm->resume_external_frame(route)&&vm->complete(route,true)&&
            vm->run(4)==odh::VmStatus::returned_true&&vm->temps()[1]==77,
            "mixed child portal immediate parent portal and root route unwind in exact source order");
    }
}
void primitive_immediate_frames() {
    const auto program=[] {return odh::RoutineSet{
        {key(),routine({{17,1,2,{}},expr(5,8,11),expr(5,8,22)})},
        {key(4101),routine({expr(5,3,17,8,7,255,255)})}};};
    bool queued=true;unsigned commits=0;
    odh::Vm live(program(),{}, {},[&](const auto& frame,const auto&,const auto&,const auto&){
        if(!queued)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        odh::PrimitiveOutcome outcome{odh::PrimitiveDisposition::call,{},
            odh::VmFrameWrite{odh::VmFrameBank::arguments,0,77},
            odh::VmCall{key(4101),frame.context,{},odh::VmCallReturnMode::immediate}};
        outcome.call_commit=[&]{++commits;queued=false;return true;};return outcome;
    });
    require(live.start(key(),{1,1,9},{5})==odh::VmStatus::running&&live.run(1)==odh::VmStatus::budget_exhausted&&
        commits==1&&live.stack().size()==2&&live.stack()[0].pc==0&&live.stack()[0].arguments[0]==77&&
        live.immediate_continuations().size()==1&&live.immediate_continuations()[0].parent_depth==1,
        "IdleForInput primitive can atomically publish immediate child and source parent output");
    const auto ticket=live.stack()[1].invocation_ticket;
    require(live.run(1)==odh::VmStatus::yielded&&live.immediate_return()&&!live.immediate_return()->result&&
        live.stack()[0].pc==0&&live.resume_immediate(ticket)&&live.status()==odh::VmStatus::running&&
        live.run(2)==odh::VmStatus::returned_true&&live.temps()[8]==11,
        "primitive immediate false result resumes parent instruction without choosing its false branch or next-tick mode");
    for(const bool throws:{false,true}){
        odh::Vm rejected(program(),{}, {},[&](const auto& frame,const auto&,const auto&,const auto&){
            odh::PrimitiveOutcome outcome{odh::PrimitiveDisposition::call,{},
                odh::VmFrameWrite{odh::VmFrameBank::locals,6,99,true},
                odh::VmCall{key(4101),frame.context,{},odh::VmCallReturnMode::immediate}};
            outcome.call_commit=[&]()->bool{if(throws)throw std::runtime_error("commit failure");return false;};
            return outcome;
        });
        rejected.start(key(),{1,1,9},{5});const auto before=rejected.snapshot();bool raised=false;
        try{(void)rejected.run(1);}catch(const std::runtime_error&){raised=true;}
        require(raised==throws&&rejected.status()==odh::VmStatus::fault&&rejected.stack().size()==1&&
            rejected.stack()[0].pc==0&&rejected.stack()[0].locals==before.stack[0].locals&&
            rejected.stack()[0].arguments==before.stack[0].arguments&&rejected.immediate_continuations().empty()&&
            !rejected.immediate_return(),"failed immediate commit rolls back child, continuation and growable output");
    }
    const odh::Instruction sleep{0,254,255,{}};
    const odh::RoutineSet nested_program{
        {key(),routine({sleep})},
        {key(4100),routine({{17,1,255,{}},expr(5,4,90)})},
        {key(4101),routine({expr(5,3,17,8,7,255,255)})},
        {key(4200),routine({expr(5,2,55)})},
        {key(4300),routine({{88,1,255,{}},expr(5,5,12)})}};
    bool inner_queued=true;odh::Vm* bound=nullptr;unsigned checks_inside=0;
    odh::Vm nested(nested_program,{}, {},[&](const auto& frame,const auto& instruction,const auto&,const auto&){
        if(instruction.opcode==0)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
        if(instruction.opcode==88){
            const auto count=bound->immediate_continuations().size();
            const auto pending=bound->immediate_continuations().front().parent_pending->ticket;
            const auto check=bound->evaluate_check(key(4200),frame.context,{},4);
            require(check.started&&check.status==odh::VmStatus::returned_true&&
                bound->immediate_continuations().size()==count&&
                bound->immediate_continuations().front().parent_pending->ticket==pending,
                "in-dispatch Check restores exact immediate barrier and parked parent service");
            ++checks_inside;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        }
        if(!inner_queued)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        odh::PrimitiveOutcome outcome{odh::PrimitiveDisposition::call,{}, {},
            odh::VmCall{key(4101),frame.context,{},odh::VmCallReturnMode::immediate}};
        outcome.call_commit=[&]{inner_queued=false;return true;};return outcome;
    });bound=&nested;
    nested.start(key(),{1,1,1},{100});nested.run(1);const auto parent_ticket=nested.pending()->ticket;
    const auto audit=[](const auto& pending,const auto&){return pending.instruction.instruction.opcode==0;};
    const auto outer=nested.inject_immediate({key(4100),{1,9,9},{}},audit);
    require(outer&&nested.run(1)==odh::VmStatus::budget_exhausted&&nested.immediate_continuations().size()==2,
        "inner AllowPush immediate action can nest under an externally injected immediate");
    const auto inner=nested.stack().back().invocation_ticket;
    require(nested.run(1)==odh::VmStatus::yielded&&nested.resume_immediate(inner)&&
        nested.run(2)==odh::VmStatus::yielded&&nested.immediate_return()->ticket==*outer&&
        nested.resume_immediate(*outer)&&nested.pending()->ticket==parent_ticket&&nested.stack()[0].pc==0,
        "nested primitive immediate ACKs preserve original Sleep and reexecute only their own parent PCs");
    const auto checked=nested.inject_immediate({key(4300),{1,9,9},{}},audit);
    const auto before_steps=nested.steps();
    require(checked&&nested.run(2)==odh::VmStatus::yielded&&checks_inside==1&&nested.temps()[2]==55&&
        nested.steps()==before_steps+2&&nested.resume_immediate(*checked)&&nested.pending()->ticket==parent_ticket,
        "immediate action Check shares Temps under its own budget without charging parent VM work");
}
void exact_check_argument_extents() {
    const auto programs=[](){return odh::RoutineSet{{key(),routine({{90,254,255,{}}})},
        {key(4097),routine({{91,254,255,{}}},0,9)}};};
    for(const bool exact:{false,true})for(const auto& args:std::vector<std::vector<std::int16_t>>{{},{0,0,0,0},{7,-8}}){
        const auto expected=exact?args.size():std::size_t{9};
        std::size_t observed=99;bool observed_exact=false;
        const odh::VmPrimitive inspect=[&](const auto& frame,const auto&,const auto&,const auto&){
            observed=frame.arguments.size();observed_exact=frame.exact_arguments;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        };
        odh::Vm external(programs());external.start(key(),{1,2,2},{42});const auto parent=external.snapshot();
        const auto checked=external.evaluate_check_at_safe_point(key(4097),{1,2,2},args,8,inspect,exact);
        require(checked.started&&checked.status==odh::VmStatus::returned_true&&observed==expected&&observed_exact==exact&&
            external.stack().front().arguments==parent.stack.front().arguments&&external.stack().front().pc==parent.stack.front().pc&&
            external.steps()==parent.steps&&external.run_generation()==parent.run_generation&&external.status()==parent.status&&
            external.snapshot().ticket_sequence==parent.ticket_sequence,
            "external Check honors explicit exact extent or legacy metadata while preserving parent execution");
        odh::Vm* active=nullptr;std::optional<odh::VmSubroutineResult> result;
        odh::Vm during(programs(),{}, {},[&](const auto& frame,const auto&,const auto&,const auto&){
            result=active->evaluate_check(key(4097),frame.context,args,8,inspect,exact);
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        });active=&during;during.start(key(),{1,2,2});
        require(during.run(1)==odh::VmStatus::returned_true&&result&&result->started&&
            result->status==odh::VmStatus::returned_true&&observed==expected&&observed_exact==exact&&during.steps()==1,
            "executing Check honors source exact arguments without charging its separate instructions to Main");
    }
}

}
void scoped_register_bank_aliases(){
    const auto outer_key=key(4300),inner_key=key(4301);
    for(const int failure:{0,1,2}){
        odh::Vm owner({{key(),routine({{0,254,255,{}}})}});owner.start(key());
        owner.temps()[0]=7;owner.temp_xl()[0]=8;
        odh::Vm outer({{outer_key,routine({expr(5,0,11,8,7,1),expr(5,0,12345,42,7,2),{99,254,255,{}}})}});
        odh::Vm inner({{inner_key,routine({expr(5,0,44,8,7,1),expr(5,1,22,8,7,2),expr(5,1,5432,42,7,3),{100,254,255,{}}})}});
        outer.start(outer_key);unsigned callbacks=0;
        const auto result=outer.run_synchronous_check_with_registers(owner,true,16,
            [&](const auto&,const auto&,const odh::Ts1Temps& outer_values,const auto&)->odh::PrimitiveOutcome{
                require(&outer.temps()==&owner.temps()&& &outer.temp_xl()==&owner.temp_xl()&&
                    owner.temps()[0]==11&&owner.temp_xl()[0]==12345,
                    "outer EvaluateCheck immediately aliases the real Caller.Thread TEMP and XL arrays");
                inner.start(inner_key);
                const auto nested=inner.run_synchronous_check_with_registers(owner,true,16,
                    [&](const auto&,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
                        ++callbacks;require(&inner.temps()==&owner.temps()&& &inner.temp_xl()==&owner.temp_xl()&&
                            outer_values[0]==44&&outer_values[1]==22&&owner.temp_xl()[1]==5432,
                            "nested EvaluateCheck uses the same physical arrays even through an outer callback reference");
                        owner.temps()[2]=33;
                        require(inner.temps()[2]==33&&outer_values[2]==33,"real Caller.Thread writes are visible immediately in both nested frames");
                        if(failure==1)throw std::runtime_error("nested bank failure");
                        return {failure==2?odh::PrimitiveDisposition::wait:odh::PrimitiveDisposition::goto_false,{}};
                    });
                require(nested.started&&nested.status==(failure?odh::VmStatus::fault:odh::VmStatus::returned_false)&&
                    &inner.temps()!=&owner.temps()&&inner.temps()[0]==44&&inner.temps()[2]==33&&
                    outer_values[0]==44&&outer_values[2]==33,
                    "inner loan expires with final visible values without discarding false or fault register effects");
                return {odh::PrimitiveDisposition::goto_true,{}};
            });
        require(result.started&&result.status==odh::VmStatus::returned_true&&callbacks==1&&
            &outer.temps()!=&owner.temps()&&outer.temps()[0]==44&&owner.temps()[0]==44&&
            outer.temp_xl()[1]==5432&&owner.temp_xl()[1]==5432,
            "outer scope does not publish its stale pre-nested register copy over real Thread effects");
        owner.temps()[0]=99;require(outer.temps()[0]==44&&inner.temps()[0]==44,"expired register loans do not retain owner aliases");
        odh::Vm clone({{outer_key,routine({expr(5,0,55,8,7)})}});clone.start(outer_key);
        const auto copied=clone.run_synchronous_check_with_registers(owner,false,4);
        require(copied.status==odh::VmStatus::returned_true&&clone.temps()[0]==55&&owner.temps()[0]==99&&
            &clone.temps()!=&owner.temps(),"outside scheduler cloned register banks never modify the actual owner");
    }
}
void source_entity_thread_stack_view(){
    const auto check_key=key(4400),named=odh::named_routine_key(42,false);
    const odh::RoutineSet code{{key(),routine({{200,1,255,{}},{28,254,255,{}}})},
        {check_key,routine({{201,1,255,{}},{28,254,255,{}}})}};
    odh::Vm* actual=nullptr;unsigned check_views=0,named_views=0;bool from_check=false;
    odh::VmPrimitive checks;
    checks=[&](const odh::VmFrame& frame,const auto& op,const auto&,const auto&)->odh::PrimitiveOutcome{
        if(op.opcode==201){
            ++check_views;require(actual->stack().back().key.id==4400&&actual->entity_thread_frames().back().key.id==4096&&
                actual->entity_thread_frames().back().context.callee==2,
                "entity Thread stack retains its real Main while an ordinary EvaluateCheck uses temporary frames");
            require(actual->references_entity(odh::WorldEntityReference{3,33}),"parked real Main references remain strong during a temporary check");
            return {odh::PrimitiveDisposition::goto_true,{}};
        }
        if(op.opcode==202){
            ++named_views;
            require(from_check?actual->entity_thread_frames().back().key.id==4096:
                actual->entity_thread_frames().back().key.scope==odh::RoutineScope::named_private,
                "Named inside fresh Check preserves real Main view; Named on real Thread exposes its actual replacement stack");
            return {odh::PrimitiveDisposition::goto_true,{}};
        }
        auto context=frame.context;context.code_owner=77;
        const auto result=actual->run_named_in_stack(named,context,{},8,checks);
        require(result.status==odh::VmStatus::returned_true,"nested named stack view completes synchronously");
        return {odh::PrimitiveDisposition::goto_true,{}};
    };
    odh::Vm vm(code,{}, {},[&](const odh::VmFrame& frame,const auto& op,const auto& temps,const auto& dispatch)->odh::PrimitiveOutcome{
        if(op.opcode==28)return checks(frame,op,temps,dispatch);
        auto context=frame.context;context.callee=7;context.callee_generation=77;context.stack_object=8;context.stack_object_generation=88;
        from_check=true;const auto result=actual->evaluate_check(check_key,context,{},16,checks);from_check=false;
        require(result.status==odh::VmStatus::returned_true&&actual->entity_thread_frames().back().context.callee==2,
            "real Main view restores after all temporary check and named scopes");
        return {odh::PrimitiveDisposition::goto_true,{}};
    },{{77,{{named,routine({{202,254,255,{}}})}}}});actual=&vm;
    odh::VmContext context{1,2,3,0};context.entity_references_bound=true;
    context.caller_generation=11;context.callee_generation=22;context.stack_object_generation=33;
    require(vm.start(key(),context)==odh::VmStatus::running&&vm.run(2)==odh::VmStatus::returned_true&&
        check_views==1&&named_views==2&&vm.entity_thread_frames().empty(),"entity Thread view loans never escape to later scheduler state");
}
void named_check_call_destination_proof(){
    const auto named=odh::named_routine_key(42,false);
    for(const std::uint8_t destination:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{2}}){
        unsigned child_calls=0;
        odh::Vm vm({{key(),routine({{28,254,255,{0,0,0,0,1,destination,0,0}}})}}, {}, {},{},
            {{77,{{named,routine({{99,254,255,{}}})}}}});
        require(vm.start(key(),{1,1,2})==odh::VmStatus::running,"start an actual named-tree check instruction");
        const auto result=vm.run_synchronous_check(8,
            [&](const odh::VmFrame& frame,const odh::Instruction& instruction,const auto&,const auto&)->odh::PrimitiveOutcome{
                if(instruction.opcode==28){
                    auto context=frame.context;context.code_owner=77;
                    return {odh::PrimitiveDisposition::call,{}, {},odh::VmCall{named,context,{}}};
                }
                ++child_calls;return {odh::PrimitiveDisposition::goto_true,{}};
            });
        require(result.started&&result.status==(destination==2?odh::VmStatus::returned_true:odh::VmStatus::fault)&&
            child_calls==(destination==2?1U:0U),
            "only authored destination2 may push a named child from a check; forged0/1 CALLs have no child effects");
    }
}
void named_queue_ancestry(){
    const auto named=odh::named_routine_key(10,false);
    for(const bool from_check:{false,true})for(const bool override_present:{false,true}){
        const odh::RoutineSet code{{key(),routine({{200,254,255,{}}})},
            {key(4100),routine({expr(5,7,33,12,7,1),{28,254,255,{}}})}};
        odh::Vm* actual=nullptr;unsigned fallback_calls=0,body_calls=0;odh::VmSubroutineResult result;
        odh::VmPrimitive checks;
        checks=[&](const auto& frame,const auto& op,const auto&,const auto&)->odh::PrimitiveOutcome{
            if(op.opcode==28){
                auto named_context=frame.context;named_context.code_owner=77;
                const auto nested=actual->run_named_in_stack(named,named_context,{},10,checks);
                return {nested.status==odh::VmStatus::returned_true?odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::unsupported,nested.diagnostic};
            }
            ++body_calls;require(op.opcode==99&&actual->synchronous_queue_empty()&&
                actual->named_stack_inherits_parent_queue_index()==!from_check,
                "named queue ancestry distinguishes actual Main from a fresh logical EvaluateCheck Thread");
            const auto& memory=actual->effective_memory();
            require(memory.write(frame,29,9,88),"named body publishes durable Thread advertisement effect before later lookup");
            const auto value=memory.read(frame,12,override_present?7:8);
            if(!value)return {odh::PrimitiveDisposition::unsupported,"source empty queue retains an invalid old active index"};
            require(*value==(override_present?(from_check?33:55):0),
                "local override wins; fresh Check ancestry has known-null action rather than its old Main queue");
            return {odh::PrimitiveDisposition::goto_true,{}};
        };
        odh::Vm vm(code,{}, {},[&](const auto& frame,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
            if(from_check)result=actual->evaluate_check(key(4100),frame.context,{},20,checks);
            else {auto named_context=frame.context;named_context.code_owner=77;result=actual->run_named_in_stack(named,named_context,{},10,checks);}
            return {odh::PrimitiveDisposition::goto_true,{}};
        },{{77,{{named,routine({{99,254,255,{}}})}}}});actual=&vm;
        require(vm.set_advertisement_fallback([&](const auto&,auto,auto)->std::optional<std::int16_t>{
            ++fallback_calls;return vm.synchronous_queue_empty()&&vm.named_stack_inherits_parent_queue_index()?std::nullopt:std::optional<std::int16_t>{999};
        }),"bind actual protected active-queue failure without parent TTAB fallback");
        vm.start(key(),{1,2,3,0});odh::VmFrame address;
        require(vm.effective_memory().write(address,12,7,55),"seed parent real Thread override");
        require(vm.run(1)==odh::VmStatus::returned_true&&body_calls==1&&
            result.status==(!from_check&&!override_present?odh::VmStatus::fault:odh::VmStatus::returned_true)&&
            fallback_calls==(!from_check&&!override_present?1U:0U)&&!vm.synchronous_queue_empty(),
            "Main→Named missing ads faults, Check→Named missing ads reads zero, and local overrides work in both");
        require(vm.advertisement_changes()->at((1U<<16)|7)==55&&
            (from_check?vm.advertisement_changes()->count(9)==0:vm.advertisement_changes()->at(9)==88),
            "normal named Thread writes survive a fault while temporary Check advertisements remain isolated");
    }
}
void thread_advertisement_memory(){
    const odh::RoutineSet program{{key(),routine({expr(5,7,21,12,7,1),expr(5,-1,9,28,7,2),
        expr(5,7,-3,29,7,3),{99,4,255,{}},expr(5,0,7,8,12,5),expr(5,1,-1,8,28,6),expr(5,2,7,8,29)})},
        {key(4100),routine({expr(5,3,7,8,12,1),expr(5,7,44,12,7,2),{101,254,255,{}}})}};
    unsigned actual_motive_writes=0;odh::VmMemory base;
    base.write=[&](const auto&,auto,auto,auto){++actual_motive_writes;return true;};
    const auto wait=[](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
    odh::Vm live(program,base,{},wait);
    require(live.set_advertisement_fallback([](const auto&,auto,auto)->std::optional<std::int16_t>{return 77;}),"bind explicit live ActiveAction advertisement fallback");
    require(live.start(key())==odh::VmStatus::running&&live.run(4)==odh::VmStatus::waiting&&actual_motive_writes==0&&
        live.advertisement_changes()&&live.advertisement_changes()->size()==3,
        "ordinary Thread writes all advertisement types and waits without mutating actual motives");
    const auto saved=live.snapshot();odh::Vm fresh(program,base,{},wait);
    require(fresh.restore_snapshot(saved)&&fresh.advertisement_changes()==saved.advertisement_changes,
        "fresh VM restores ordinary Thread advertisement dictionary alongside its wait");
    const auto factory=[&](const odh::VmMemory& effective){return odh::VmPrimitive{[&,effective](const auto& frame,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
        require(effective.read(frame,12,7)==44&&effective.read(frame,28,7)==0&&effective.write(frame,29,-1,0),
            "operand-scope primitive uses the same fresh check advertisement memory as expressions");
        return {odh::PrimitiveDisposition::goto_false,{}};
    }};};
    const auto checked=fresh.evaluate_check_at_safe_point(key(4100),{}, {},10,{},false,factory);
    require(checked.started&&checked.status==odh::VmStatus::returned_false&&fresh.temps()[3]==0&&
        fresh.advertisement_changes()==saved.advertisement_changes&&fresh.pending()->ticket==saved.pending->ticket,
        "disposable queue check shares Temps but neither copies nor changes the real Thread advertisement map");
    require(fresh.complete(fresh.pending()->ticket,true)&&fresh.run(3)==odh::VmStatus::returned_true&&
        fresh.temps()[0]==21&&fresh.temps()[1]==9&&fresh.temps()[2]==-3,
        "saved ordinary continuation reads each exact advertisement type after resuming");
    require(fresh.reset_execution_state()&&fresh.advertisement_changes()==saved.advertisement_changes&&
        fresh.start_after_reset(key(),{})==odh::VmStatus::running&&fresh.advertisement_changes()==saved.advertisement_changes,
        "source Reset and new Main preserve the dictionary owned by the same real Thread");
    for(const int mode:{0,1,2}){
        const odh::RoutineSet code{{key(),routine({expr(5,7,21,12,7,1),{100,254,255,{}}})},
            {key(4100),routine({expr(5,3,7,8,12,1),expr(5,7,44,12,7,2),{101,254,255,{}}})}};
        odh::Vm* actual=nullptr;
        odh::Vm vm(code,{}, {},[&](const auto& frame,const auto& instruction,const auto&,const auto&)->odh::PrimitiveOutcome{
            require(instruction.opcode==100,"actual Main enters the source synchronous-check boundary");
            const auto parent=actual->advertisement_changes();
            const auto checked=actual->evaluate_check(key(4100),frame.context,{},10,
                [&](const auto&,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
                    if(mode==1)throw std::runtime_error("check advertisement failure after write");
                    return {mode==2?odh::PrimitiveDisposition::wait:odh::PrimitiveDisposition::goto_false,{}};
                });
            require(checked.started&&checked.status==(mode?odh::VmStatus::fault:odh::VmStatus::returned_false)&&
                actual->temps()[3]==0&&actual->advertisement_changes()==parent,
                "same-VM check always restores parent ads after false, callback error or unsupported wait");
            return {odh::PrimitiveDisposition::goto_true,{}};
        });actual=&vm;
        vm.set_advertisement_fallback([](const auto&,auto,auto)->std::optional<std::int16_t>{return 77;});
        require(vm.start(key())==odh::VmStatus::running&&vm.run(2)==odh::VmStatus::returned_true&&
            vm.advertisement_changes()->at((1U<<16)|7)==21,
            "check writes never escape to the normal Thread or inherit its active-action ads");
    }
}
void asynchronous_next_tick_completion(){
    odh::Vm vm({{key(),routine({{0,1,255,{}},expr(5,0,77)})}}, {}, {},
        [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
    require(vm.start(key())==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::waiting,
        "start an actual asynchronous wait for next-tick completion");
    const auto ticket=vm.pending()->ticket,steps=vm.steps();
    require(!vm.complete_next_tick(ticket+1,true)&&vm.pending()->ticket==ticket,
        "wrong next-tick completion ticket preserves the wait");
    require(vm.complete_next_tick(ticket,true)&&vm.status()==odh::VmStatus::next_tick&&
        !vm.pending()&&vm.stack().back().pc==1&&vm.steps()==steps&&vm.temps()[0]==0,
        "next-tick completion branches once without executing the continuation");
    require(!vm.complete_next_tick(ticket,true)&&vm.status()==odh::VmStatus::next_tick&&vm.steps()==steps,
        "duplicate completion cannot consume or resume the next-tick boundary");
    require(vm.run(1)==odh::VmStatus::next_tick&&vm.steps()==steps&&
        vm.resume_next_tick()&&vm.run(1)==odh::VmStatus::returned_true&&vm.temps()[0]==77,
        "the following scheduler release executes the continuation exactly once");
    odh::Vm terminal({{key(),routine({{0,254,255,{}}})}}, {}, {},
        [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
    terminal.start(key());terminal.run(1);
    require(terminal.complete_next_tick(terminal.pending()->ticket,true)&&terminal.status()==odh::VmStatus::returned_true,
        "next-tick completion preserves a terminal return rather than inventing a continuation");
}
void disposable_owner_resolver_inheritance(){
    const auto first_named=odh::named_routine_key(42,false),second_named=odh::named_routine_key(43,false);
    const auto first_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
        {first_named,routine({{28,254,255,{}}},0,0)},
        {key(4101),routine({{28,254,255,{}}},0,0)}});
    const auto second_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
        {second_named,routine({{99,254,255,{}}},0,0)}});
    const odh::VmContext check_context{11,44,55,0};
    unsigned terminals=0;std::map<odh::CodeOwner,unsigned> resolutions;
    odh::VmPrimitive checks;
    checks=[&](const odh::VmFrame& frame,const auto& op,const auto&,const odh::VmDispatch& dispatch)->odh::PrimitiveOutcome{
        require(dispatch.origin&&dispatch.origin->matches_active_query(frame,op,dispatch),
            "named owner lookup remains attached to its authentic caller frame/dispatch");
        auto* actual=const_cast<odh::Vm*>(dispatch.origin);
        if(op.opcode==99){
            ++terminals;
            require(frame.context.code_owner==88&&frame.context.caller==11&&frame.context.callee==44&&frame.context.stack_object==55,
                "nested lazy named owners preserve Check caller/callee/StackObject provenance");
            const auto nested=actual->evaluate_check(key(4100),check_context,{},4,checks);
            require(!nested.started,"inheriting the resolver does not enable forbidden nested ordinary checks");
            odh::PrimitiveOutcome result{odh::PrimitiveDisposition::goto_true,{}};result.temp_write=odh::VmTempWrite{6,321};return result;
        }
        require(op.opcode==28,"fixture enters actual named-tree primitive boundary");
        const auto before_stack=actual->stack();const auto before_status=actual->status();
        const auto before_steps=actual->steps(),before_run=actual->run_generation();auto context=frame.context;
        context.code_owner=frame.context.code_owner==77?88:77;
        const auto result=actual->run_named_in_stack(context.code_owner==77?first_named:second_named,context,{},16,checks);
        if(!result.started||result.status!=odh::VmStatus::returned_true)
            throw std::runtime_error("named and nested named trees resolve exact immutable missing-owner banks: status="+
                std::string(odh::vm_status_name(result.status))+" diagnostic="+result.diagnostic);
        require(actual->stack().size()==before_stack.size()&&odh::vm_frame_equal(actual->stack().back(),before_stack.back())&&
            actual->status()==before_status&&actual->steps()==before_steps&&actual->run_generation()==before_run,
            "each same-VM named scope restores its predecessor frame and execution counters");
        return {odh::PrimitiveDisposition::goto_true,{}};
    };
    const auto provider=[&](odh::CodeOwner owner)->std::shared_ptr<const odh::VmProgram>{
        ++resolutions[owner];return owner==77?first_bank:owner==88?second_bank:nullptr;};
    odh::Vm parent({{key(),routine({{0,254,255,{}}})},{key(4100),routine({{28,254,255,{}}})}}, {}, {},
        [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
    require(parent.set_owner_resolver(provider),"bind exact immutable resource provider on the real Thread");
    parent.start(key(),{11,22,33},{17},true);require(parent.run(1)==odh::VmStatus::waiting,"park real Main on its actual async ticket");
    const auto before=parent.snapshot();
    const auto intact=[&]{
        require(parent.status()==before.status&&parent.steps()==before.steps&&parent.run_generation()==before.run_generation&&
            parent.pending()&&parent.pending()->ticket==before.pending->ticket&&parent.stack().size()==before.stack.size()&&
            odh::vm_frame_equal(parent.stack().front(),before.stack.front()),
            "disposable owner resolution preserves the real Main continuation, pending ticket and counters");
        require(!parent.shared_program_for_owner(77)&&!parent.shared_program_for_owner(88),
            "disposable owner cache admissions never mutate the real Thread's immutable owner registry");
    };
    const auto external=parent.evaluate_check_at_safe_point(key(4100),check_context,{},32,checks,true);
    if(external.status!=odh::VmStatus::returned_true)
        throw std::runtime_error("external Check owner lookup: status="+std::string(odh::vm_status_name(external.status))+" diagnostic="+external.diagnostic);
    require(external.started&&external.status==odh::VmStatus::returned_true&&parent.temps()[6]==321,
        "external Check clone inherits provider for named/nested lookup and shares intended register effects");intact();
    auto lazy_root=check_context;lazy_root.code_owner=77;
    parent.temps()[6]=7;
    const auto entry=parent.evaluate_entry_point_check(key(4101),lazy_root,{},32,false,checks);
    require(entry.started&&entry.status==odh::VmStatus::returned_true&&parent.temps()[6]==7,
        "entry-point clone resolves its root and nested owner while retaining outside-tick register cloning");intact();
    const auto root=parent.evaluate_check_at_safe_point(key(4101),lazy_root,{},32,checks,true);
    require(root.started&&root.status==odh::VmStatus::returned_true&&parent.temps()[6]==321,
        "external clone inherits the owner provider before pushing even a lazy-owned root frame");intact();
    auto missing=check_context;missing.code_owner=99;
    const auto denied=parent.evaluate_check_at_safe_point(key(4101),missing,{},32,checks,true);
    require(denied.status==odh::VmStatus::fault&&resolutions[99]==1,
        "missing exact owner remains a visible failure without default-bank fallback");intact();
    for(const bool temporary:{false,true}){
        odh::Vm* actual=nullptr;
        odh::Vm source({{key(),routine({{static_cast<std::uint16_t>(temporary?200:28),254,255,{}}})},
                       {key(4100),routine({{28,254,255,{}}})}}, {}, {},
            [&](const auto& frame,const auto& op,const auto& temps,const auto& dispatch)->odh::PrimitiveOutcome{
                if(!temporary)return checks(frame,op,temps,dispatch);
                const auto parked=actual->stack().front();const auto parked_steps=actual->steps(),parked_run=actual->run_generation();
                const auto result=actual->evaluate_check(key(4100),check_context,{},32,checks,true);
                require(result.started&&result.status==odh::VmStatus::returned_true&&
                    odh::vm_frame_equal(actual->stack().front(),parked)&&actual->steps()==parked_steps&&
                    actual->run_generation()==parked_run,"ordinary temporary Check preserves real Main around lazy named lookup");
                return {odh::PrimitiveDisposition::goto_true,{}};
            });actual=&source;require(source.set_owner_resolver(provider),"bind provider for same-VM named/temporary paths");
        source.start(key(),check_context,{},true);
        require(source.run(1)==odh::VmStatus::returned_true&&source.shared_program_for_owner(77)==first_bank&&
            source.shared_program_for_owner(88)==second_bank,
            "same-VM named and temporary-stack paths keep the existing resolver and exact admitted immutable banks");
    }
    require(terminals==5&&resolutions[77]==5&&resolutions[88]==5,
        "each disposable clone owns its lazy cache; same-VM named scopes reuse their admitted owner banks");
}
void trusted_lifecycle_deletion() {
    const odh::PrimitiveOutcome deleted{odh::PrimitiveDisposition::lifecycle_deleted,{}};
    const auto root=routine({{18,1,255,{}},expr(5,0,91,8,7)});
    const auto handler=[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
        require(dispatch.check_mode&&dispatch.origin->matches_active_query(frame,instruction,dispatch),
            "lifecycle removal retains authenticated synchronous dispatch");
        return deleted;
    };
    odh::Vm vm({{key(),root}});vm.start(key());
    const auto terminal=vm.run_synchronous_check(8,handler,{},true);
    require(terminal.started&&terminal.lifecycle_deleted&&terminal.status==odh::VmStatus::ready&&
        terminal.steps==1&&vm.stack().empty()&&!vm.pending()&&vm.temps()[0]==0,
        "typed lifecycle deletion stops immediately without branch or fictitious true return");
    odh::Vm register_owner({{key(),root}});register_owner.temps()[2]=31;
    odh::Vm nested({{key(),routine({call(4097,1),expr(5,2,99,8,7)})},
        {key(4097),routine({expr(5,2,47,8,7,1),{18,254,255,{}}})}});
    nested.start(key());
    const auto child_stop=nested.run_synchronous_check_with_registers(register_owner,true,8,handler,{},true);
    require(child_stop.lifecycle_deleted&&child_stop.steps==3&&nested.stack().empty()&&
        register_owner.temps()[2]==47&&nested.temps()[2]==47,
        "nested Init deletion terminates all disposable frames and retains shared register effects");
    for(unsigned variant=0;variant<8;++variant){
        auto outcome=deleted;
        if(variant==2)outcome.write=odh::VmFrameWrite{odh::VmFrameBank::arguments,0,77};
        if(variant==3)outcome.temp_write=odh::VmTempWrite{0,77};
        if(variant==4)outcome.second_temp_write=odh::VmTempWrite{1,77};
        if(variant==5)outcome.stack_object_write=odh::VmStackObjectWrite{77};
        if(variant==6)outcome.call=odh::VmCall{key(),{}, {}};
        bool committed=false;
        if(variant==7)outcome.call_commit=[&]{committed=true;return true;};
        auto code=root;if(variant==1)code.instructions[0].opcode=42;
        odh::Vm checked({{key(),code}});checked.start(key(),{1,1,1},{12});
        const auto result=checked.run_synchronous_check(8,
            [&](const auto&,const auto&,const auto&,const auto&){return outcome;},{},variant!=0);
        require(result.status==odh::VmStatus::fault&&!result.lifecycle_deleted&&
            checked.stack().size()==1&&checked.stack()[0].pc==0&&checked.stack()[0].arguments[0]==12&&
            checked.temps()[0]==0&&checked.temps()[1]==0&&!committed,
            "ordinary checks, wrong opcode and deletion payloads cannot bypass execution or publish outputs");
    }
    odh::Vm ordinary({{key(),root}}, {}, {},
        [&](const auto&,const auto&,const auto&,const auto&){return deleted;});
    ordinary.start(key());
    require(ordinary.run(8)==odh::VmStatus::fault&&!ordinary.stack().empty(),
        "regular Main cannot use lifecycle-only terminal disposition");
    odh::Vm parent({{key(),root}});parent.start(key(),{1,1,1},{15});parent.temps()[0]=23;
    const auto result=parent.evaluate_entry_point_check(key(),{1,1,1},{0,0,0,0},8,false,handler,{},true);
    require(result.lifecycle_deleted&&result.status==odh::VmStatus::ready&&parent.temps()[0]==23&&
        parent.stack()[0].pc==0&&parent.stack()[0].arguments[0]==15&&parent.steps()==0&&
        parent.status()==odh::VmStatus::running,
        "disposable lifecycle stop leaves parent continuation and counters intact");
    const auto denied=parent.evaluate_entry_point_check(key(),{1,1,1},{0,0,0,0},8,false,handler);
    require(denied.status==odh::VmStatus::fault&&!denied.lifecycle_deleted,
        "entry point deletion permission is explicit per invocation and never leaks");
}
void variable_output_tests() {
    for(const auto disposition:{odh::PrimitiveDisposition::goto_true,odh::PrimitiveDisposition::goto_false,
                               odh::PrimitiveDisposition::goto_true_next_tick}){
        odh::PrimitiveOutcome output{disposition,{}};
        output.variable_write=odh::VmVariableWrite{8,0,13};
        auto vm=output_machine(output);vm.start(key(),{11,22,33},{17});vm.temps()[0]=42;
        require(vm.run(1)==(disposition==odh::PrimitiveDisposition::goto_true_next_tick?
                odh::VmStatus::next_tick:odh::VmStatus::budget_exhausted)&&vm.temps()[0]==13&&
                vm.stack()[0].pc==(disposition==odh::PrimitiveDisposition::goto_false?2:1),
            "single TS1 variable output commits before selecting its synchronous branch");
    }
    for(const auto scope:{std::uint16_t{9},std::uint16_t{25},std::uint16_t{11}}){
        odh::PrimitiveOutcome output{odh::PrimitiveDisposition::goto_true,{}};
        output.variable_write=odh::VmVariableWrite{scope,0,-32768};
        auto vm=output_machine(output);vm.start(key(),{11,22,33},{17});vm.temps()[0]=3;
        require(vm.run(1)==odh::VmStatus::budget_exhausted&&
            (scope==9?vm.stack()[0].arguments[0]:scope==25?vm.stack()[0].locals[0]:vm.temps()[3])==-32768,
            "variable output uses actual arguments, locals and indirect temporary selectors");
    }
    unsigned writes=0;std::int16_t stored=0;
    odh::VmMemory memory;memory.write=[&](const odh::VmFrame& frame,std::uint8_t scope,
            std::int16_t index,std::int16_t value){
        require(frame.context.caller==11&&frame.context.stack_object==33&&scope==3&&index==5,
            "external variable output borrows the current authenticated frame address");
        ++writes;stored=value;return true;
    };
    odh::PrimitiveOutcome external{odh::PrimitiveDisposition::goto_true,{}};
    external.variable_write=odh::VmVariableWrite{3,5,7};
    odh::Vm vm({{key(),routine({{99,254,255,{}}})}},memory,{},
        [external](const auto&,const auto&,const auto&,const auto&){return external;});
    vm.start(key(),{11,22,33});
    require(vm.run(1)==odh::VmStatus::returned_true&&writes==1&&stored==7,
        "external variable output goes through the interpreter's effective memory exactly once");
    for(unsigned variant=0;variant<7;++variant){
        odh::PrimitiveOutcome mixed{odh::PrimitiveDisposition::goto_true,{}};
        mixed.variable_write=odh::VmVariableWrite{8,0,7};
        if(variant==0)mixed.write=odh::VmFrameWrite{odh::VmFrameBank::locals,0,99};
        if(variant==1)mixed.temp_write=odh::VmTempWrite{1,99};
        if(variant==2)mixed.second_temp_write=odh::VmTempWrite{1,99};
        if(variant==3)mixed.stack_object_write=odh::VmStackObjectWrite{99};
        if(variant==4)mixed.call=odh::VmCall{key(),{},{}};
        if(variant==5)mixed.call_commit=[&]{++writes;return true;};
        if(variant==6)mixed.variable_write->scope=264; // Must not alias scope8.
        rejects_outputs(mixed);
    }
    for(const auto disposition:{odh::PrimitiveDisposition::wait,odh::PrimitiveDisposition::call,
        odh::PrimitiveDisposition::continue_execution,odh::PrimitiveDisposition::continue_next_tick,
        odh::PrimitiveDisposition::lifecycle_deleted,odh::PrimitiveDisposition::unsupported}){
        auto output=external;output.disposition=disposition;
        odh::Vm denied({{key(),routine({{99,254,255,{}}})}},memory,{},
            [output](const auto&,const auto&,const auto&,const auto&){return output;});
        denied.start(key(),{11,22,33});
        require(denied.run(1)==odh::VmStatus::fault&&writes==1&&stored==7&&!denied.pending(),
            "nonbranch variable output is rejected before invoking any setter");
    }
    auto readonly=external;readonly.variable_write=odh::VmVariableWrite{7,0,99};
    auto denied=output_machine(readonly);denied.start(key());
    require(denied.run(1)==odh::VmStatus::fault,"generic output never silently accepts a rejected setter");
    odh::PrimitiveOutcome check_output{odh::PrimitiveDisposition::goto_true,{}};
    check_output.variable_write=odh::VmVariableWrite{8,0,19};
    const odh::VmPrimitive handler=[check_output](const auto&,const auto&,const auto&,const auto&){return check_output;};
    odh::Vm parent({{key(),routine({{99,1,255,{}},expr(2,0,19,8,7)})}});
    parent.start(key(),{11,22,33},{17});parent.temps()[0]=42;
    const auto checked=parent.evaluate_entry_point_check(key(),{11,22,33},{},4,false,handler);
    require(checked.status==odh::VmStatus::returned_true&&parent.temps()[0]==42&&
        parent.steps()==0&&parent.stack()[0].pc==0&&parent.stack()[0].arguments[0]==17,
        "check variable output stays on the disposable interpreter frame and registers");
}
int main() {
    try {
        variable_output_tests();
        {
            odh::Instruction missing{1,254,255,{12,0,255,17,42,1,2,3}};
            odh::Vm vm({{key(),routine({missing})}});
            vm.start(key());
            require(vm.run(1)==odh::VmStatus::fault&&
                    vm.diagnostic()=="unsupported primitive 1 operands=[12,0,255,17,42,1,2,3]",
                    "unsupported logs identify the exact operation variant without another replay");
            odh::Vm specific({{key(),routine({missing})}}, {}, {},
                [](const auto&,const auto&,const auto&,const auto&){
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"specific resource failure"};
                });
            specific.start(key());
            require(specific.run(1)==odh::VmStatus::fault&&specific.diagnostic()=="specific resource failure",
                    "specific primitive diagnostic remains intact");
        }
        {
            odh::VmMemory memory;unsigned generic_reads=0,job_reads=0;
            memory.read=[&](const auto&,auto,auto)->std::optional<std::int16_t>{++generic_reads;return 999;};
            memory.job_data=[&](const odh::VmFrame& frame,std::uint16_t job,std::int16_t level,std::int16_t field)->std::optional<std::int16_t>{
                ++job_reads;require(frame.context.caller==11&&frame.context.callee==22,
                    "JobData callback receives exact reading frame context");
                if(job==65535&&level==1&&field==12)return 19;
                if(job==65535&&level==0&&field==12)return 7;
                return {};
            };
            const auto program=routine({expr(5,2,12,8,33)});
            odh::Vm actual({{key(),program}},memory);actual.start(key(),{11,22,33});
            actual.temps()[0]=-1;actual.temps()[1]=1;
            require(actual.run(2)==odh::VmStatus::returned_true&&actual.temps()[2]==19&&generic_reads==0&&job_reads==1,
                "Expression JobData uses actual thread Temp0 unsigned job and Temp1 level through dedicated provider");
            odh::Vm invalid({{key(),program}},memory);invalid.start(key(),{11,22,33});
            invalid.temps()[0]=-1;invalid.temps()[1]=2;
            require(invalid.run(2)==odh::VmStatus::fault&&generic_reads==0,
                "invalid career field/level remains a source fault instead of falling through generic memory");
            odh::Vm donor({{key(),routine({{0,254,255,{}}})}});donor.start(key());
            donor.temps()[0]=-1;donor.temps()[1]=0;
            odh::Vm check({{key(),program}},memory);check.start(key(),{11,22,33});
            check.temps()[0]=123;check.temps()[1]=2;
            const auto checked=check.run_synchronous_check_with_registers(donor,true,2);
            require(checked.status==odh::VmStatus::returned_true&&donor.temps()[2]==7&&check.temps()[2]==7,
                "disposable Check JobData observes the authenticated shared thread register loan, not stale cloned selectors");
            donor.temps()[1]=1;
            odh::Vm copied({{key(),program}},memory);copied.start(key(),{11,22,33});
            const auto checked_copy=copied.run_synchronous_check_with_registers(donor,false,2);
            require(checked_copy.status==odh::VmStatus::returned_true&&copied.temps()[2]==19&&donor.temps()[2]==7,
                "out-of-tick query clone selects its seeded job/level without modifying donor registers");
        }
        {
            odh::VmMemory memory;unsigned generic_reads=0,generic_writes=0;
            memory.read=[&](const auto&,auto,auto)->std::optional<std::int16_t>{++generic_reads;return 777;};
            memory.write=[&](const auto&,auto,auto,auto){++generic_writes;return true;};
            for(const auto index:{std::int16_t{-32768},std::int16_t{-1},std::int16_t{0},
                                 std::int16_t{2},std::int16_t{32767}}) {
                odh::Vm vm({{key(),routine({expr(5,3,index,8,34),expr(2,3,0,8,7)})}},memory);
                vm.start(key(),{5,5,32});vm.temps()[0]=1234;vm.temps()[1]=-1234;
                require(vm.run(2)==odh::VmStatus::returned_true&&vm.temps()[3]==0&&
                    vm.temps()[0]==1234&&vm.temps()[1]==-1234&&generic_reads==0,
                    "NeighborhoodData reads every index as zero before generic memory, independent of actor and selectors");
                odh::Vm write({{key(),routine({expr(5,index,99,34,7)})}},memory);write.start(key(),{5,5,32});
                require(write.run(1)==odh::VmStatus::fault&&generic_writes==0&&
                    write.diagnostic().find("NeighborhoodData writes are not implemented")!=std::string::npos,
                    "NeighborhoodData assignment remains unsupported even with permissive generic callback");
                odh::Vm increment({{key(),routine({expr(3,index,1,34,7)})}},memory);increment.start(key(),{5,5,32});
                require(increment.run(1)==odh::VmStatus::fault&&generic_reads==0&&generic_writes==0&&
                    increment.diagnostic().find("NeighborhoodData writes are not implemented")!=std::string::npos,
                    "NeighborhoodData read-modify-write cannot create mutable reference tutorial data");
            }
        }
        disposable_owner_resolver_inheritance();
        trusted_lifecycle_deletion();
        scoped_register_bank_aliases();
        source_entity_thread_stack_view();
        named_check_call_destination_proof();
        named_queue_ancestry();
        thread_advertisement_memory();
        asynchronous_next_tick_completion();
        {
            std::map<std::int16_t,std::uint64_t> live{{1,11},{2,22},{3,33}};
            odh::VmMemory memory;memory.resolve_live_entity=[&](std::int16_t id)->std::optional<odh::WorldEntityReference>{
                const auto at=live.find(id);return at==live.end()?std::nullopt:std::optional<odh::WorldEntityReference>{{id,at->second}};};
            const auto write_so=[](const auto&,const auto& instruction,const auto&,const auto&)->odh::PrimitiveOutcome{
                odh::PrimitiveOutcome result{odh::PrimitiveDisposition::goto_true,{}};
                if(instruction.opcode==99)result.stack_object_write=odh::VmStackObjectWrite{42};return result;};
            odh::Vm vm({{key(),routine({{99,1,255,{}},expr(5,0,0,8,10,2,255),{99,3,255,{}},{100,254,255,{}}})}},memory,{},write_so);
            require(vm.start(key(),{1,2,3})==odh::VmStatus::running&&vm.stack().front().context.caller_generation==11&&
                vm.stack().front().context.callee_generation==22&&vm.stack().front().context.stack_object_generation==33,
                "new frame binds three distinct participants once at real entry");
            require(vm.run(1)==odh::VmStatus::budget_exhausted&&vm.stack().back().context.stack_object==42&&
                vm.stack().back().context.entity_references_bound&&!vm.stack().back().context.stack_object_generation,
                "numeric StackObject output binds missing ID as explicit nil");
            live[42]=44;
            require(vm.run(1)==odh::VmStatus::budget_exhausted&&vm.temps()[0]==42&&!vm.stack().back().context.stack_object_generation,
                "StackObjectID preserves the written number while nil cannot bind a later replacement");
            require(vm.run(1)==odh::VmStatus::budget_exhausted&&vm.stack().back().context.stack_object_generation==44,
                "a genuinely new numeric assignment resolves the newly live generation");
            vm.temps()[3]=123;vm.temp_xl()[0]=456;
            require(vm.reset_execution_state()&&vm.start_after_reset(key(),{1,2,3})==odh::VmStatus::running&&
                vm.temps()[3]==123&&vm.temp_xl()[0]==456&&!vm.stack().front().action_tree&&vm.snapshot().action_tree_known,
                "source Reset Main start preserves shared registers and has known nonaction frames");
        }

        {
            std::map<std::int16_t,std::uint64_t> live{{1,11},{2,22},{3,33}};
            odh::VmMemory memory;memory.resolve_live_entity=[&](std::int16_t id)->std::optional<odh::WorldEntityReference>{
                const auto at=live.find(id);return at==live.end()?std::nullopt:std::optional<odh::WorldEntityReference>{{id,at->second}};};
            const odh::RoutineSet code{{key(),routine({call(4097)})},{key(4097),routine({{100,254,255,{}}})}};
            odh::Vm inherited(code,memory);
            require(inherited.start(key(),{1,2,3})==odh::VmStatus::running,"bind ordinary-call source refs");live[3]=333;
            require(inherited.run(1)==odh::VmStatus::budget_exhausted&&inherited.stack().back().context.stack_object_generation==33,
                "ordinary Gosub copies the existing reference rather than resolving the same number again");
            auto legacy=inherited.snapshot();legacy.action_tree_known=false;
            for(auto& frame:legacy.stack){frame.context.entity_references_bound=false;frame.context.caller_generation=0;
                frame.context.callee_generation=0;frame.context.stack_object_generation=0;frame.action_tree=false;}
            odh::Vm upgraded(code,memory);require(upgraded.restore_snapshot(legacy)&&upgraded.bind_legacy_entity_references()&&
                upgraded.stack().back().context.stack_object_generation==333,"legacy upgrade captures all proven live roles at its explicit admission boundary");
            legacy.stack.back().context.stack_object=181;
            odh::Vm missing(code,memory);require(missing.restore_snapshot(legacy)&&!missing.bind_legacy_entity_references()&&
                !missing.stack().back().context.entity_references_bound,"missing legacy body remains unknown instead of becoming fabricated nil");
        }
        {
            odh::RoutineSet code{{key(),routine({{99,254,255,{}}})},{key(4097),routine({call(4098)})},
                {key(4098),routine({{100,254,255,{}}})},{key(4100),routine({{100,254,255,{}}})}};
            odh::Vm vm(code,{}, {},[&](const auto& frame,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
                odh::PrimitiveOutcome result{odh::PrimitiveDisposition::call,{}};
                result.call=odh::VmCall{key(4097),frame.context,{},odh::VmCallReturnMode::ordinary,false,true};return result;
            });
            require(vm.start(key(),{1,1,1})==odh::VmStatus::running,"start nonaction Main before explicit action call");
            require(!vm.stack().front().action_tree&&vm.run(1)==odh::VmStatus::budget_exhausted&&
                vm.stack().back().action_tree&&vm.run(1)==odh::VmStatus::budget_exhausted&&vm.stack().back().action_tree,
                "explicit action-root metadata reaches its root and ordinary descendants while Main stays nonaction");
            bool checked=false;
            const auto answer=vm.evaluate_check_at_safe_point(key(4100),{1,1,1},{},8,
                [&](const odh::VmFrame& frame,const auto&,const auto&,const auto& dispatch)->odh::PrimitiveOutcome{
                    checked=dispatch.check_mode&&!frame.action_tree;return {odh::PrimitiveDisposition::goto_true,{}};
                });
            require(answer.started&&answer.status==odh::VmStatus::returned_true&&checked&&vm.stack().back().action_tree,
                "disposable checks are nonaction and preserve the suspended action-tree flags");
        }
        exact_check_argument_extents();
        service_callback_completion();
        storage_liveness_proof();
        immediate_frames();
        immediate_external_ancestry();
        primitive_immediate_frames();
        {
            odh::Vm owners{{{key(),routine({expr(2,0,0)})},
                            {key(256),routine({expr(2,0,0)})}}, {}, {64,4096,3}};
            require(owners.has_routine(key(),0)&&!owners.has_routine(key(4097),0)&&
                    !owners.has_routine(key(),11)&&!owners.has_routine(key(256),11),
                    "routine availability rejects missing roots and missing owners, even for globals");
            require(owners.install_owner(11,{{key(4097),routine({expr(2,0,0)})}})&&
                    owners.has_routine(key(4097),11)&&owners.has_routine(key(256),11)&&
                    !owners.has_routine(key(),11)&&!owners.has_routine(key(4097),0),
                    "routine availability resolves owner-local roots and shared globals without default fallback");
            owners.start(key());const auto before=owners.snapshot();
            require(owners.has_routine(key(4097),11)&&!owners.has_routine(key(),99)&&
                    owners.status()==before.status&&owners.steps()==before.steps&&
                    owners.run_generation()==before.run_generation&&owners.stack().size()==before.stack.size()&&
                    owners.stack().front().pc==before.stack.front().pc&&
                    !owners.install_owner(11,{{key(),routine({expr(2,0,0)})}}),
                    "availability queries preserve active VM state and installed owners remain immutable");
        }

        {
            auto global = std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(256),routine({expr(2,0,0,7,7)})}});
            auto bank = std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(),routine({expr(5,0,0,8,9,1),expr(2,0,0,8,9)})}});
            std::weak_ptr<const odh::VmProgram> lifetime = bank;
            odh::Vm first(global,odh::ShareVmProgramTag{}, {}, {64,4096,3}, {},
                          {{11,bank}},odh::ShareVmOwnerProgramsTag{});
            odh::Vm second(global,odh::ShareVmProgramTag{}, {}, {64,4096,3}, {},
                           {{11,bank}},odh::ShareVmOwnerProgramsTag{});
            bank.reset();
            require(lifetime.use_count()==2,
                    "two VMs retain the same private program after the caller releases it");
            first.start(key(),{1,1,1,11},{17});
            second.start(key(),{2,2,2,11},{29});
            require(first.run(1)==odh::VmStatus::budget_exhausted&&first.temps()[0]==17&&
                    second.steps()==0&&second.stack().front().pc==0&&second.temps()[0]==0,
                    "shared private code leaves execution frames and temps independent");
            require(second.run(2)==odh::VmStatus::returned_true&&second.temps()[0]==29&&
                    first.run(1)==odh::VmStatus::returned_true&&first.temps()[0]==17,
                    "shared owner programs execute independently after caller release");
            auto valid = lifetime.lock();
            auto invalid = std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {{odh::RoutineScope::semiglobal,4096},routine({expr(2,0,0)})}});
            auto globals = std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(256),routine({expr(2,0,0)})}});
            auto empty = std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
            const auto before = first.snapshot();
            require(!first.install_shared_owner(0,valid)&&!first.install_shared_owner(11,valid)&&
                    !first.install_shared_owner(12,nullptr)&&!first.install_shared_owner(12,empty)&&
                    !first.install_shared_owner(12,invalid)&&!first.install_shared_owner(12,globals)&&
                    !first.has_routine(key(),12)&&first.steps()==before.steps&&
                    first.status()==before.status,
                    "invalid shared owner installs preserve owner visibility and execution state");
            require(first.install_shared_owner(12,valid)&&first.has_routine(key(),12)&&
                    first.has_routine(key(256),12)&&!second.has_routine(key(),12)&&
                    !second.has_routine(key(256),12)&&!first.install_shared_owner(13,valid)&&
                    second.install_shared_owner(13,valid)&&!first.has_routine(key(),13),
                    "shared banks retain per-VM visibility and owner capacity including owner zero");
            const auto rejects = [&](odh::SharedRoutineOwners owners, odh::VmLimits limits) {
                try {
                    odh::Vm rejected(global,odh::ShareVmProgramTag{}, {}, limits, {},
                                     std::move(owners),odh::ShareVmOwnerProgramsTag{});
                } catch(const std::invalid_argument&) { return true; }
                return false;
            };
            require(rejects({{0,valid}},{64,4096,3})&&rejects({{11,nullptr}},{64,4096,3})&&
                    rejects({{11,invalid}},{64,4096,3})&&rejects({{11,globals}},{64,4096,3})&&
                    rejects({{11,valid}},{64,4096,1})&&rejects({}, {64,4096,0}),
                    "shared owner constructor preserves reserved owner, scope and capacity validation");
            odh::Vm* dispatch_vm = nullptr;
            bool refused = false;
            odh::Vm guarded(global,odh::ShareVmProgramTag{}, {}, {},
                [&](const auto&,const auto&,const auto&,const auto&) {
                    refused = !dispatch_vm->install_shared_owner(12,valid);
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                },{{11,std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                    {key(),routine({{49,254,255,{}}})}})}},odh::ShareVmOwnerProgramsTag{});
            dispatch_vm = &guarded;
            guarded.start(key(),{1,1,1,11});
            require(guarded.run(1)==odh::VmStatus::returned_true&&refused&&
                    !guarded.has_routine(key(),12),
                    "shared owner installation is refused during primitive dispatch");
        }

        // Independently specified byte header exercises format -> VM metadata.
        const std::string bytes = std::string("\x02\x80\x01\x00\x16\x02\x03\x00\x04\x00\xAA\xBB",12) +
            std::string("\x02\x00\xFE\xFF\x00\x00\x01\x00\x00\x05\x08\x07",12);
        std::istringstream stream(bytes);
        const auto parsed = odh::read_bhav(odh::View(stream, bytes.size()));
        require(parsed.metadata && parsed.metadata->arguments == 2 && parsed.metadata->locals == 3 &&
                parsed.metadata->type == 22 && parsed.metadata->tree_version == 4 && parsed.header[7] == 187,
                "BHAV header decode and unknown bytes preserved");
        auto vm = machine(parsed); vm.start(key());
        require(vm.run(1) == odh::VmStatus::returned_true && vm.temps()[0] == 1, "parsed expression executes");

        const odh::Instruction deferred_call{49,254,255,{}};
        odh::Vm call_commit({{key(),routine({deferred_call,expr(2,0,0)})},
                             {key(4097),routine({expr(2,0,0)})}}, {}, {},
            [&](const auto& frame,const auto& instruction,const auto&,const auto&){
                if(instruction.opcode!=49)return odh::PrimitiveOutcome{};
                odh::PrimitiveOutcome outcome{odh::PrimitiveDisposition::call,{}, {},
                    odh::VmCall{key(4097),frame.context,{}}};
                outcome.call_commit=[] {return false;};
                return outcome;
            });
        call_commit.start(key());
        require(call_commit.run(1)==odh::VmStatus::fault&&call_commit.stack().size()==1&&
                call_commit.stack()[0].key.id==4096&&call_commit.stack()[0].pc==0,
                "rejected child-state commit removes the created frame before it can execute");
        odh::Vm call_commit_throw({{key(),routine({deferred_call,expr(2,0,0)})},
                                   {key(4097),routine({expr(2,0,0)})}}, {}, {},
            [&](const auto& frame,const auto& instruction,const auto&,const auto&){
                if(instruction.opcode!=49)return odh::PrimitiveOutcome{};
                odh::PrimitiveOutcome outcome{odh::PrimitiveDisposition::call,{}, {},
                    odh::VmCall{key(4097),frame.context,{}}};
                outcome.call_commit=[]()->bool {throw std::runtime_error("fixture commit failure");};
                return outcome;
            });
        call_commit_throw.start(key());bool commit_exception=false;
        try{(void)call_commit_throw.run(1);}catch(const std::runtime_error&){commit_exception=true;}
        require(commit_exception&&call_commit_throw.status()==odh::VmStatus::fault&&
                call_commit_throw.stack().size()==1&&
                call_commit_throw.stack()[0].key.id==4096&&call_commit_throw.stack()[0].pc==0,
                "throwing child-state commit removes the created frame before propagating");

        const odh::Instruction interaction_call{49,254,255,{}};
        const odh::RoutineSet nested_program{{key(),routine({interaction_call})},
            {key(4097),routine({interaction_call})},{key(4098),routine({expr(2,0,0)})}};
        const auto nested_primitive=[](const auto& frame,const auto& instruction,const auto&,const auto&){
            if(instruction.opcode!=49)return odh::PrimitiveOutcome{};
            const auto target=frame.key.id==4096?4097U:4098U;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::call,{}, {},
                odh::VmCall{key(target),frame.context,{},odh::VmCallReturnMode::interaction_next_tick}};
        };
        odh::Vm nested_action_vm(nested_program,{}, {},nested_primitive);
        nested_action_vm.start(key(),{1,2,3,0});
        require(nested_action_vm.run(2)==odh::VmStatus::budget_exhausted&&nested_action_vm.stack().size()==3&&
            nested_action_vm.stack()[1].return_mode==odh::VmCallReturnMode::interaction_next_tick&&
            nested_action_vm.stack()[2].return_mode==odh::VmCallReturnMode::interaction_next_tick,
            "VM accepts an interaction child above an existing interaction frame");
        const auto nested_action_vm_state=nested_action_vm.snapshot();
        odh::Vm nested_restored(nested_program,{}, {},nested_primitive);
        require(nested_restored.restore_snapshot(nested_action_vm_state)&&nested_restored.stack().size()==3,
            "VM snapshot restores a nested interaction prefix");
        require(nested_action_vm.run(1)==odh::VmStatus::yielded&&nested_action_vm.interaction_return()&&
            nested_action_vm.stack().size()==2&&nested_action_vm.stack()[1].return_mode==odh::VmCallReturnMode::interaction_next_tick,
            "nested action return keeps its parent frame and publishes only the child's ticket");
        odh::Vm returned_child_restored(nested_program,{}, {},nested_primitive);
        require(returned_child_restored.restore_snapshot(nested_action_vm.snapshot())&&
            returned_child_restored.stack().size()==2&&returned_child_restored.interaction_return()&&
            returned_child_restored.interaction_return()->ticket==nested_action_vm.interaction_return()->ticket,
            "VM snapshot accepts a child return while its parent interaction remains active");

        std::optional<std::uint16_t> read_flag;
        odh::VmMemory sprite_memory;
        sprite_memory.dynamic_sprite_flag=[&](const odh::VmFrame&,std::uint16_t flag)->std::optional<bool>{
            read_flag=flag;return flag==5;
        };
        odh::Vm sprite_read({{key(),routine({expr(2,0,1,27,7)})}},sprite_memory);
        sprite_read.start(key());sprite_read.temps()[0]=5;
        require(sprite_read.run(1)==odh::VmStatus::returned_true&&read_flag==5,
                "scope 27 reads the sprite flag selected by the Temp register");
        std::optional<std::pair<std::uint16_t,bool>> written_flag;
        sprite_memory.set_dynamic_sprite_flag=[&](const odh::VmFrame&,std::uint16_t flag,bool enabled){
            written_flag=std::make_pair(flag,enabled);return true;
        };
        odh::Vm sprite_write({{key(),routine({expr(5,0,1,27,7)})}},sprite_memory);
        sprite_write.start(key());sprite_write.temps()[0]=69;
        require(sprite_write.run(1)==odh::VmStatus::returned_true&&written_flag&&
                written_flag->first==69&&written_flag->second,
                "scope 27 writes the selected sprite flag using the Temp register value");

        auto restored = machine(routine({expr(2,0,37,9,7)},2));
        odh::VmFrame saved_frame{key(),0,{12,13,14,0},{37},{44},
                                 odh::VmCallReturnMode::ordinary,0};
        std::array<std::int16_t,20> saved_temps{};
        saved_temps[0]=77;
        restored.restore_dormant_state({saved_frame},saved_temps);
        require(restored.status()==odh::VmStatus::dormant && restored.steps()==0 &&
                restored.stack().size()==1 && restored.stack()[0].pc==0 &&
                restored.stack()[0].arguments.size()==4 && restored.stack()[0].arguments[0]==37 &&
                restored.stack()[0].locals.size()==4 && restored.stack()[0].locals[0]==44 &&
                restored.temps()==saved_temps && restored.stack()[0].context.caller==12 &&
                restored.stack()[0].context.callee==13 && restored.stack()[0].context.stack_object==14,
                "saved VM state loads exactly and stays dormant with padded variables");
        require(restored.run(1)==odh::VmStatus::dormant && restored.steps()==0,
                "dormant VM does not execute before explicit activation");
        std::vector<odh::VmTrace> restored_trace;
        require(restored.activate_dormant_state() &&
                restored.run(1,[&](const auto& trace){restored_trace.push_back(trace);})==odh::VmStatus::returned_true &&
                restored_trace.size()==1 && restored_trace[0].key.id==key().id && restored_trace[0].pc==0,
                "explicit activation begins at the saved routine and address");

        odh::Vm restored_stack({{key(),routine({call(4097,1),expr(2,0,37,9,7)})},
                                {key(4097),routine({expr(2,0,37,9,7)})}});
        auto saved_parent=saved_frame; saved_parent.arguments={5}; saved_parent.locals={55};
        auto saved_child=saved_frame; saved_child.key=key(4097); saved_child.arguments={37}; saved_child.locals={66};
        restored_stack.restore_dormant_state({saved_parent,saved_child},saved_temps);
        std::vector<odh::VmTrace> stack_trace;
        require(restored_stack.activate_dormant_state() &&
                restored_stack.run(1,[&](const auto& trace){stack_trace.push_back(trace);})==odh::VmStatus::budget_exhausted &&
                stack_trace.size()==1 && stack_trace[0].key.id==4097 && stack_trace[0].pc==0 &&
                restored_stack.stack().size()==1 && restored_stack.stack()[0].key.id==4096 &&
                restored_stack.stack()[0].pc==1,
                "restored two-frame stack runs its saved top frame then returns to its saved parent");

        std::optional<odh::VmContext> temporary_effect_context;
        odh::Vm temporary({{key(),routine({expr(2,0,37,9,7)})},
                           {key(4097),routine({expr(5,1,0,8,9)})},
                           {key(4098),routine({expr(16,0,0,9,7)})},
                           {key(4099),routine({expr(5,2,7,8,7,1),expr(2,0,0,7,7)})},
                           {key(4100),routine({{49,254,255,{}}})}}, {}, {},
            [&](const auto& frame,const auto& instruction,const auto&,const auto&) {
                if(instruction.opcode!=49)return odh::PrimitiveOutcome{};
                temporary_effect_context=frame.context;
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            });
        temporary.restore_dormant_state({saved_frame},saved_temps);
        temporary.temp_xl()[0]=123456;
        const auto suspended_main=temporary.snapshot();
        const auto child_true=temporary.run_subroutine_in_temporary_stack(
            key(4097),{14,14,14,0},{55,66,77,88},8);
        require(child_true.started&&child_true.status==odh::VmStatus::returned_true&&
                child_true.steps==1&&child_true.diagnostic.empty(),
                "temporary named tree reports synchronous true and its bounded step count");
        require(temporary.status()==suspended_main.status&&temporary.steps()==suspended_main.steps&&
                temporary.stack().size()==suspended_main.stack.size()&&
                temporary.stack()[0].pc==suspended_main.stack[0].pc&&
                temporary.stack()[0].arguments==suspended_main.stack[0].arguments&&
                temporary.stack()[0].locals==suspended_main.stack[0].locals&&
                temporary.stack()[0].context.caller==suspended_main.stack[0].context.caller&&
                temporary.stack()[0].context.callee==suspended_main.stack[0].context.callee&&
                temporary.stack()[0].context.stack_object==suspended_main.stack[0].context.stack_object&&
                temporary.run_generation()==suspended_main.run_generation,
                "temporary named tree restores the suspended Main frame and status exactly");
        require(temporary.temps()[0]==saved_temps[0]&&temporary.temps()[1]==55&&
                temporary.temp_xl()[0]==123456,
                "temporary named tree receives caller arguments while retaining target thread registers");

        const odh::Instruction sleep_wait{0,254,255,{0,0,0,0,0,0,0,0}};
        odh::Vm sleeping_main({{key(),routine({sleep_wait})},
                               {key(4097),routine({expr(2,0,0,7,7)})}}, {}, {},
            [](const auto&,const auto& instruction,const auto&,const auto&){
                return instruction.opcode==0?odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}}:
                                             odh::PrimitiveOutcome{};
            });
        sleeping_main.restore_dormant_state({saved_frame},saved_temps);
        require(sleeping_main.activate_dormant_state()&&
                sleeping_main.run(1)==odh::VmStatus::waiting&&sleeping_main.pending()&&
                sleeping_main.pending()->instruction.instruction.opcode==0,
                "restored Main reaches its scheduled Sleep wait");
        const auto sleeping_main_before=sleeping_main.snapshot();
        const auto child_during_sleep=sleeping_main.run_subroutine_in_temporary_stack(
            key(4097),{14,14,14,0},{},8);
        require(child_during_sleep.started&&child_during_sleep.status==odh::VmStatus::returned_true&&
                sleeping_main.status()==odh::VmStatus::waiting&&sleeping_main.pending()&&
                sleeping_main.pending()->ticket==sleeping_main_before.pending->ticket&&
                sleeping_main.stack().size()==sleeping_main_before.stack.size()&&
                sleeping_main.stack()[0].pc==sleeping_main_before.stack[0].pc&&
                sleeping_main.steps()==sleeping_main_before.steps,
                "temporary named tree runs over an idle Sleep and restores its pending Main wait");

        const odh::Instruction other_wait{36,254,255,{}};
        odh::Vm non_sleeping_main({{key(),routine({other_wait})},
                                   {key(4097),routine({expr(2,0,0,7,7)})}}, {}, {},
            [](const auto&,const auto& instruction,const auto&,const auto&){
                return instruction.opcode==36?odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}}:
                                              odh::PrimitiveOutcome{};
            });
        non_sleeping_main.restore_dormant_state({saved_frame},saved_temps);
        require(non_sleeping_main.activate_dormant_state()&&
                non_sleeping_main.run(1)==odh::VmStatus::waiting,
                "non-sleeping Main fixture enters its private dialog wait");
        const auto rejected_during_dialog=non_sleeping_main.run_subroutine_in_temporary_stack(
            key(4097),{14,14,14,0},{},8);
        require(!rejected_during_dialog.started&&non_sleeping_main.status()==odh::VmStatus::waiting&&
                non_sleeping_main.pending()&&non_sleeping_main.pending()->instruction.instruction.opcode==36,
                "temporary named tree does not hide a player-facing dialog wait");

        const auto child_false=temporary.run_subroutine_in_temporary_stack(
            key(4098),{14,14,14,0},{0,0,0,0},8);
        require(child_false.started&&child_false.status==odh::VmStatus::returned_false&&
                temporary.status()==suspended_main.status&&temporary.stack()[0].pc==suspended_main.stack[0].pc,
                "temporary named tree returns false without advancing Main");
        const auto child_notify=temporary.run_subroutine_in_temporary_stack(
            key(4100),{31,32,33,0},{},8);
        require(child_notify.started&&child_notify.status==odh::VmStatus::returned_true&&
                temporary_effect_context&&temporary_effect_context->caller==31&&
                temporary_effect_context->callee==32&&temporary_effect_context->stack_object==33,
                "temporary named tree sends opcode effects through the target thread context");
        const auto child_budget=temporary.run_subroutine_in_temporary_stack(
            key(4099),{14,14,14,0},{},1);
        require(child_budget.started&&child_budget.status==odh::VmStatus::budget_exhausted&&
                child_budget.steps==1&&temporary.status()==suspended_main.status&&
                temporary.stack().size()==suspended_main.stack.size()&&temporary.stack()[0].pc==0&&
                temporary.temps()[2]==7,
                "budgeted child restores Main even when its own tree does not finish");
        const auto missing_temporary=temporary.run_subroutine_in_temporary_stack(
            key(4101),{14,14,14,0},{},8);
        require(!missing_temporary.started&&missing_temporary.status==odh::VmStatus::fault&&
                temporary.status()==suspended_main.status&&temporary.stack()[0].pc==0,
                "missing temporary routine leaves the suspended Main untouched");

        // Queue-skipped entry points run on disposable check threads outside a tick.
        // They must use explicit synchronous handlers and preserve exact object context.
        // Scope3 is the external ObjectData backend; scope12 now correctly belongs to Thread advertisements.
        for(const bool returned:{true,false}){
            unsigned effects=0;bool exact_context=false,check_dispatch=false;
            odh::VmMemory entry_memory;
            entry_memory.write=[&](const odh::VmFrame& frame,std::uint8_t scope,
                                   std::int16_t index,std::int16_t value){
                exact_context=frame.context.caller==22&&frame.context.callee==22&&
                    frame.context.stack_object==11&&frame.context.code_owner==7;
                if(scope!=3||index!=0||value!=123)return false;
                ++effects;return true;
            };
            unsigned implicit_calls=0;
            odh::Vm entry({},entry_memory,{},[&](const auto&,const auto&,const auto&,const auto&){
                ++implicit_calls;return odh::PrimitiveOutcome{};
            },{{7,{{key(),routine({expr(5,0,123,3,7,1),
                {99,static_cast<std::uint8_t>(returned?254:255),255,{}}})}}}});
            entry.start(key(),{22,22,11,7},{0,0,0,0});
            entry.temps()[2]=19;entry.temp_xl()[1]=123456;
            const auto result=entry.run_synchronous_check(8,[&](const auto& frame,const auto&,
                    const auto& temps,const odh::VmDispatch& dispatch){
                check_dispatch=dispatch.check_mode&&dispatch.origin==&entry&&
                    frame.arguments==std::vector<std::int16_t>({0,0,0,0})&&temps[2]==19;
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            });
            require(result.started&&result.steps==2&&result.status==(returned?
                    odh::VmStatus::returned_true:odh::VmStatus::returned_false)&&
                effects==1&&exact_context&&check_dispatch&&!implicit_calls&&entry.temp_xl()[1]==123456,
                "disposable entry check retains false/true World effects with exact callee context and explicit handler");
            require(!entry.run_synchronous_check(8).started,"entry check cannot execute twice after returning");
        }
        for(const auto disposition:{odh::PrimitiveDisposition::wait,odh::PrimitiveDisposition::call}){
            auto entry=machine(routine({{99,254,255,{}}}));entry.start(key());
            const auto result=entry.run_synchronous_check(8,[&](const auto&,const auto&,const auto&,const auto&){
                return odh::PrimitiveOutcome{disposition,{}};
            });
            require(result.started&&result.status==odh::VmStatus::fault&&!entry.pending()&&
                    entry.stack().size()==1,"disposable entry check rejects asynchronous service without publishing pending state");
        }
        auto bounded_entry=machine(routine({expr(5,2,9,8,7,0)}));bounded_entry.start(key());
        require(!bounded_entry.run_synchronous_check(0).started,"entry check refuses zero budget before execution");
        const auto bounded_result=bounded_entry.run_synchronous_check(3);
        require(bounded_result.started&&bounded_result.status==odh::VmStatus::budget_exhausted&&
                bounded_result.steps==3&&bounded_entry.temps()[2]==9&&!bounded_entry.pending(),
                "disposable entry check bounds infinite BHAV loop and retains effects without fabricating false");

        // EP4 runs inside automatic enqueue ticks and at external UI boundaries.
        // Both modes seed from the callee, but only the tick publishes register effects.
        for(const bool share:{false,true})for(const unsigned outcome:{0U,1U,2U,3U}){
            odh::Vm register_owner({{key(),routine({{99,254,255,{}}})}}, {}, {},
                [](const auto&,const auto&,const auto&,const auto&){
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
                });
            register_owner.start(key(),{22,22,0,0});register_owner.run(1);
            register_owner.temps()[2]=19;register_owner.temp_xl()[1]=10;
            const auto ticket=register_owner.pending()->ticket;
            auto last=odh::Instruction{99,static_cast<std::uint8_t>(outcome==1?255:254),255,{}};
            if(outcome==3)last=expr(5,4,1,8,7,3); // bounded infinite loop after effects
            auto entry=machine(routine({expr(5,3,2,8,8,1),expr(5,2,78,8,7,2),
                expr(5,1,77,42,7,3),last}));entry.start(key(),{22,22,11,0},{0,0,0,0});
            const auto result=entry.run_synchronous_check_with_registers(register_owner,share,6,
                [&](const auto&,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
                    if(outcome==2)throw std::runtime_error("EP4 fixture exception after register writes");
                    return {odh::PrimitiveDisposition::goto_true,{}};
                });
            const auto expected=outcome==0?odh::VmStatus::returned_true:outcome==1?
                odh::VmStatus::returned_false:outcome==2?odh::VmStatus::fault:odh::VmStatus::budget_exhausted;
            require(result.started&&result.status==expected&&entry.temps()[3]==19&&
                register_owner.temps()[2]==(share?78:19)&&register_owner.temp_xl()[1]==(share?77:10)&&
                register_owner.status()==odh::VmStatus::waiting&&register_owner.pending()->ticket==ticket&&
                register_owner.steps()==1&&register_owner.stack()[0].pc==0,
                "EP4 clones outside tick and publishes callee TEMP/XL within tick even on false, exception or limit without advancing callee Main");
        }

        {
            auto owner=machine(routine({expr(5,0,1)}));owner.start(key());owner.temps()[5]=333;
            auto entry=machine(routine({{99,254,255,{}}}));entry.start(key());entry.temps()[5]=91;
            bool nested_rejected=false;
            const auto result=entry.run_synchronous_check(4,[&](const auto&,const auto&,const auto&,const auto&){
                const auto nested=entry.run_synchronous_check_with_registers(owner,true,4);
                nested_rejected=!nested.started&&entry.temps()[5]==91&&owner.temps()[5]==333;
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            });
            require(result.status==odh::VmStatus::returned_true&&nested_rejected&&owner.temps()[5]==333,
                "rejected register-seeded check reentry cannot clobber the running check or seed owner");
        }

        // A Check BHAV can run synchronously from an executing Main primitive.
        // Its stack is separate, but the actor's registers and World backend are shared.
        std::optional<odh::VmContext> check_world_write_context;
        std::optional<std::int16_t> check_world_write_value;
        odh::VmMemory check_memory;
        check_memory.write=[&](const odh::VmFrame& frame,std::uint8_t scope,
                               std::int16_t index,std::int16_t value){
            if(scope!=3||index!=0)return false;
            check_world_write_context=frame.context;check_world_write_value=value;return true;
        };
        odh::Vm* checking_vm=nullptr;
        std::optional<odh::VmSubroutineResult> check_result;
        const odh::RoutineSet check_owner{{key(4097),routine({
            expr(5,2,1,8,9,1),expr(5,1,77,42,7,2),expr(5,0,123,3,7,3),
            expr(2,0,0,7,7)})}};
        odh::Vm check_host({{key(),routine({{49,254,255,{}}})}},check_memory,{},
            [&](const auto&,const auto& instruction,const auto&,const auto&){
                if(instruction.opcode!=49)return odh::PrimitiveOutcome{};
                check_result=checking_vm->evaluate_check(key(4097),{11,22,33,7},
                    {111,222,333,444},16);
                return odh::PrimitiveOutcome{check_result->status==odh::VmStatus::returned_true
                    ?odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::goto_false,{}};
            },odh::RoutineOwners{{7,check_owner}});
        checking_vm=&check_host;
        check_host.start(key(),{1,2,3,0});
        check_host.temps()[2]=9;check_host.temp_xl()[1]=10;
        require(check_host.run(1)==odh::VmStatus::returned_true&&check_result&&
                check_result->started&&check_result->status==odh::VmStatus::returned_true&&
                check_result->steps==4,
                "Check BHAV completes under its own bound during a one-instruction Main slice");
        require(check_host.steps()==1&&check_host.temps()[2]==222&&
                check_host.temp_xl()[1]==77&&check_world_write_context&&
                check_world_write_context->caller==11&&check_world_write_context->callee==22&&
                check_world_write_context->stack_object==33&&check_world_write_context->code_owner==7&&
                check_world_write_value==123,
                "Check BHAV preserves shared TEMP/XL and exact owner/context World effects without charging Main slice");

        // Only an explicitly supplied synchronous handler is used by a check;
        // asynchronous outcomes fault the check without publishing a wait/call.
        for(const auto disposition:{odh::PrimitiveDisposition::wait,odh::PrimitiveDisposition::call}){
            odh::Vm* rejected_vm=nullptr;
            std::optional<odh::VmSubroutineResult> rejected_result;
            odh::RoutineOwners rejected_owners{{7,{{key(4098),routine({{49,254,255,{}}})}}}};
            odh::Vm rejected_check({{key(),routine({{49,254,255,{}}})}}, {}, {},
                [&](const auto&,const auto& instruction,const auto&,const auto&){
                    if(instruction.opcode!=49)return odh::PrimitiveOutcome{};
                    rejected_result=rejected_vm->evaluate_check(key(4098),{1,2,3,7},{},8,
                        [disposition](const auto&,const auto&,const auto&,const auto& dispatch){
                            require(dispatch.check_mode,"Check BHAV primitive dispatch is explicitly marked");
                            return odh::PrimitiveOutcome{disposition,"check fixture async result"};
                        });
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                        rejected_result->diagnostic};
                },std::move(rejected_owners));
            rejected_vm=&rejected_check;rejected_check.start(key(),{1,2,3,0});
            require(rejected_check.run(1)==odh::VmStatus::fault&&rejected_result&&
                    rejected_result->started&&rejected_result->status==odh::VmStatus::fault&&
                    !rejected_check.pending()&&rejected_check.stack().size()==1&&
                    rejected_check.stack()[0].pc==0,
                    "Check BHAV refuses asynchronous wait/call and leaves Main frame resumable for host policy");
        }

        // A Check BHAV cannot recursively open another temporary check stack.
        odh::Vm* nested_vm=nullptr;
        bool nested_refused=false;
        odh::Vm nested_check({{key(),routine({{49,254,255,{}}})}}, {}, {},
            [&](const auto&,const auto& instruction,const auto&,const auto&){
                if(instruction.opcode!=49)return odh::PrimitiveOutcome{};
                const auto outer=nested_vm->evaluate_check(key(4097),{1,2,3,7},{},8,
                    [&](const auto&,const auto&,const auto&,const auto&){
                        const auto nested=nested_vm->evaluate_check(key(4097),{1,2,3,7},{},8);
                        nested_refused=!nested.started&&nested.diagnostic.find("nested Check")!=std::string::npos;
                        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                    });
                return odh::PrimitiveOutcome{outer.status==odh::VmStatus::returned_true
                    ?odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::goto_false,{}};
            },odh::RoutineOwners{{7,{{key(4097),routine({{49,254,255,{}}})}}}});
        nested_vm=&nested_check;nested_check.start(key(),{1,2,3,0});
        require(nested_check.run(1)==odh::VmStatus::returned_true&&nested_refused,
                "nested Check BHAV evaluation is explicitly refused");

        odh::RoutineOwners restored_owners{{42,{{key(),routine({expr(2,0,37,9,7)})}}}};
        odh::Vm restored_owner({{key(0),routine({expr(2,0,0)})}}, {}, {}, {},
                               std::move(restored_owners));
        auto owner_frame=saved_frame; owner_frame.context.code_owner=42;
        restored_owner.restore_dormant_state({owner_frame},saved_temps);
        require(restored_owner.status()==odh::VmStatus::dormant &&
                restored_owner.stack()[0].context.code_owner==42,
                "restore resolves a private routine through its installed code owner");

        auto atomic_restore=machine(routine({expr(2,0,1)}));
        atomic_restore.start(key());
        const auto old_status=atomic_restore.status();
        const auto old_stack=atomic_restore.stack();
        const auto old_temps=atomic_restore.temps();
        const auto old_generation=atomic_restore.run_generation();
        auto bad_frame=saved_frame; bad_frame.pc=99;
        bool restore_rejected=false;
        try { atomic_restore.restore_dormant_state({bad_frame},saved_temps); }
        catch (const std::invalid_argument&) { restore_rejected=true; }
        require(restore_rejected && atomic_restore.status()==old_status &&
                atomic_restore.stack().size()==old_stack.size() &&
                atomic_restore.stack()[0].pc==old_stack[0].pc &&
                atomic_restore.temps()==old_temps && atomic_restore.run_generation()==old_generation,
                "invalid restore rejects atomically and preserves active VM state");
        odh::Vm restore_over_limit({{key(),routine({expr(2,0,1)},4097)}}, {}, {64,4096,64});
        bool metadata_limit_rejected=false;
        try { restore_over_limit.restore_dormant_state({saved_frame},saved_temps); }
        catch (const std::invalid_argument&) { metadata_limit_rejected=true; }
        require(metadata_limit_rejected && restore_over_limit.status()==odh::VmStatus::ready &&
                restore_over_limit.stack().empty(),
                "restore rejects metadata that would exceed the frame variable limit");

        for (const auto pair : {std::pair<unsigned,int>{0,0}, {1,1}, {2,0}, {14,0}, {15,1}, {16,1}}) {
            auto m = machine(routine({expr(pair.first, -2, -1, 7, 7)})); m.start(key());
            require(m.run(1) == (pair.second ? odh::VmStatus::returned_true : odh::VmStatus::returned_false),
                    "signed comparisons");
        }
        for (const auto spec : {std::array<int,4>{3,32767,1,-32768}, {4,-32768,1,32767},
             {6,-32768,-1,-32768}, {7,-7,2,-3}, {12,-7,3,2}, {13,7,3,3},
             {18,4,2,6}, {19,7,3,4}, {9,0,16,-32768}, {10,-1,16,32767}}) {
            auto m = machine(routine({expr(static_cast<unsigned>(spec[0]),0,spec[2])}));
            m.start(key()); m.temps()[0] = static_cast<std::int16_t>(spec[1]);
            require(m.run(1) == odh::VmStatus::returned_true && m.temps()[0] == spec[3], "defined 16-bit arithmetic");
        }
        auto flag = machine(routine({expr(8,0,16)})); flag.start(key()); flag.temps()[0] = -32768;
        require(flag.run(1) == odh::VmStatus::returned_true, "high-bit flag check");
        for (const auto op : {7,12}) {
            auto m = machine(routine({expr(static_cast<unsigned>(op),0,0)})); m.start(key());
            require(m.run(1) == odh::VmStatus::returned_true && m.stack().empty() &&
                    m.temps()[0] == (op == 7 ? -1 : 0),
                    "pinned TS1 zero divisor result");
        }
        // Run slices retain state; this is an instruction budget, not a simulated tick.
        auto loop = machine(routine({expr(5,0,0,8,7,1),expr(11,0,3,8,7,1,254)})); loop.start(key());
        require(loop.run(0) == odh::VmStatus::budget_exhausted && loop.steps() == 0, "zero budget");
        require(loop.run(2) == odh::VmStatus::budget_exhausted && loop.temps()[0] == 1, "resumable loop budget");
        require(loop.run(2) == odh::VmStatus::returned_true && loop.temps()[0] == 3 && loop.steps() == 4,
                "resume neither restarts nor skips instructions");
        require(loop.run(2) == odh::VmStatus::returned_true && loop.steps() == 4, "terminal run stable");
        auto alias = machine(routine({expr(11,0,0,8,8)})); alias.start(key());
        require(alias.run(1) == odh::VmStatus::returned_false && alias.temps()[0] == 1, "rhs read after increment");

        // Nested private/global/semiglobal calls, argument substitution and local isolation.
        odh::Vm nested({{key(), routine({call(256,1), expr(2,0,0,25,7,2), expr(2,0,42)},1)},
                       {key(256), routine({expr(5,0,99,25,7,1),call(8192,2),expr(2,0,99,25,7)},1)},
                       {key(8192), routine({expr(5,0,0,8,9)},1)}});
        nested.start(key()); nested.temps()[0] = 42;
        std::vector<odh::VmTrace> trace;
        require(nested.run(20,[&](const auto& e){trace.push_back(e);}) == odh::VmStatus::returned_true,
                "nested returns and temp substitution");
        require(trace.size() == 7 && trace[2].depth == 2 && trace[3].depth == 3 && nested.temps()[0] == 42,
                "trace preserves call site/depth");
        auto literal_call = call(4097); literal_call.operands = {255,255,0,0,0,0,0,0};
        odh::Vm literal({{key(),routine({literal_call})},{key(4097),routine({expr(2,0,-1,9,7)})}});
        literal.start(key()); literal.temps()[0] = 42;
        require(literal.run(3) == odh::VmStatus::returned_true, "-1 literal when args1..3 are zero");
        odh::Vm false_call({{key(),routine({call(4097)})},{key(4097),routine({expr(2,1,2,7,7)})}});
        false_call.start(key()); require(false_call.run(3)==odh::VmStatus::returned_false,"false unwinds parent");
        auto fd = machine(routine({expr(2,0,1,7,7,254,253)})); fd.start(key());
        require(fd.run(1) == odh::VmStatus::returned_true, "FD uses sole defined edge (reference profile)");
        auto fd_error = machine(routine({expr(2,0,1,7,7,253,253)})); fd_error.start(key());
        require(fd_error.run(1) == odh::VmStatus::fault, "two FD targets fault");

        for (const auto bad : {expr(2,0,0,16,7),expr(2,-1,0),expr(2,20,0),
                               expr(2,4,0,25,7),expr(21,0,0)}) {
            auto m = machine(routine({bad})); m.start(key());
            require(m.run(1) == odh::VmStatus::fault && !m.diagnostic().empty(), "unsupported scope/index/operator");
        }
        auto read_only_literal = machine(routine({expr(5,0,0,7,7)}));
        read_only_literal.start(key());
        require(read_only_literal.run(1) == odh::VmStatus::returned_false,
                "literal setter is a known rejection");
        auto square_root = machine(routine({expr(20,0,81)})); square_root.start(key());
        require(square_root.run(1) == odh::VmStatus::returned_true && square_root.temps()[0] == 9,
                "pinned TS1 sqrt operator");
        auto wide_flag = machine(routine({expr(9,0,17)})); wide_flag.start(key());
        require(wide_flag.run(1) == odh::VmStatus::returned_true && wide_flag.temps()[0] == 0,
                "flag mask is 32-bit before narrow store");
        auto missing = machine(routine({call(4097)})); missing.start(key());
        require(missing.run(1) == odh::VmStatus::fault && missing.stack().size()==1,"missing call not success");
        auto primitive = machine(routine({{49,254,255,{}}})); primitive.start(key());
        require(primitive.run(1) == odh::VmStatus::fault, "unknown primitive not success");
        auto invalid = machine(routine({expr(2,0,0,7,7,1)}));
        require(invalid.start(key()) == odh::VmStatus::fault, "invalid branch rejected at start");
        auto legacy_bhav = routine({expr(2,0,0,7,7)}); legacy_bhav.version=0x8001; legacy_bhav.metadata.reset();
        auto legacy = machine(legacy_bhav);
        require(legacy.start(key())==odh::VmStatus::running && legacy.stack()[0].arguments.size()==4 &&
                legacy.stack()[0].locals.size()==4,"legacy frame uses bounded four-argument/no-local profile");
        require(legacy.run(1)==odh::VmStatus::returned_true,"8001 BHAV executes under legacy frame profile");
        auto called_legacy_bhav=routine({expr(2,0,42,9,7)});called_legacy_bhav.version=0x8001;called_legacy_bhav.metadata.reset();
        odh::Vm legacy_call({{key(),routine({call(4097)})},{key(4097),called_legacy_bhav}});
        legacy_call.start(key());legacy_call.temps()[0]=42;
        require(legacy_call.run(3)==odh::VmStatus::returned_true,"8002 caller can invoke a four-argument 8001 BHAV");
        auto empty = machine(routine({})); require(empty.start(key())==odh::VmStatus::fault,"empty routine not success");
        odh::Vm recursive({{key(),routine({call(4096)})}}, {}, {3,4096}); recursive.start(key());
        require(recursive.run(10)==odh::VmStatus::fault && recursive.stack().size()==3,"recursion bounded");
        odh::Vm huge({{key(),routine({expr(2,0,0,7,7)},4097)}});
        require(huge.start(key())==odh::VmStatus::fault,"frame allocation bounded");

        // World reads must be supplied; an explicit zero is distinct from missing.
        int reads = 0;
        odh::Vm world({{key(),routine({expr(2,0,0,16,7)})}},
            {[&](const auto& f,unsigned scope,int index)->std::optional<std::int16_t> {
                ++reads; require(f.context.caller==12 && f.context.stack_object==34,"world context identity");
                return scope==16 && index==0 ? std::optional<std::int16_t>(0) : std::nullopt;
            }, {}});
        world.start(key(),{12,34,34});
        require(world.run(1)==odh::VmStatus::returned_true && reads==1,"explicit zero world value");
        std::int16_t written = 0;
        odh::Vm world_write({{key(),routine({expr(5,0,17,18,7)})}},
            {{},[&](const auto&,unsigned scope,int index,std::int16_t value) {
                if (scope!=18 || index!=0) return false;
                written=value; return true;
            }});
        world_write.start(key());
        require(world_write.run(1)==odh::VmStatus::returned_true && written==17,"explicit world write");
        auto reentrant = machine(routine({expr(2,0,0,7,7)})); reentrant.start(key());
        bool rejected = false;
        try { reentrant.run(1,[&](const auto&){reentrant.start(key());}); }
        catch (const std::logic_error&) { rejected = true; }
        require(rejected && reentrant.stack().size()==1 && reentrant.steps()==0,"reentry cannot invalidate live frame");
        require(reentrant.run(1)==odh::VmStatus::returned_true,"callback exception releases execution guard");
        auto dec = machine(routine({expr(17,0,1)})); dec.start(key()); dec.temps()[0]=2;
        require(dec.run(1)==odh::VmStatus::returned_false && dec.temps()[0]==1,"decrement comparison");
        auto set_stack = machine(routine({expr(5,0,42,10,7,1),expr(2,0,42,10,7)}));
        set_stack.start(key(),{1,2,2});
        require(set_stack.run(2)==odh::VmStatus::returned_true,"stack object context read/write");

        // Primitive outputs publish as one validated synchronous operation. The second
        // temp is applied last when both outputs address the same register.
        for (const auto disposition : {odh::PrimitiveDisposition::goto_true,
                                       odh::PrimitiveDisposition::goto_false}) {
            for (const bool aliased : {false,true}) {
                odh::PrimitiveOutcome output{disposition,{}};
                output.write=odh::VmFrameWrite{odh::VmFrameBank::arguments,0,99};
                output.temp_write=odh::VmTempWrite{0,0};
                output.second_temp_write=odh::VmTempWrite{aliased ? 0u : 19u,-32768};
                output.stack_object_write=odh::VmStackObjectWrite{-42};
                auto vm=output_machine(output); vm.start(key(),{11,22,33},{17});
                vm.temps().fill(73);
                auto expected=vm.temps(); expected[0]=0;
                expected[aliased ? 0 : 19]=-32768;
                require(vm.run(1)==odh::VmStatus::budget_exhausted && vm.temps()==expected &&
                        vm.stack()[0].arguments[0]==99 && vm.stack()[0].context.stack_object==-42 &&
                        vm.stack()[0].context.caller==11 && vm.stack()[0].context.callee==22,
                        "paired output publishes frame, signed temps and stack object together");
                require(vm.stack()[0].pc==(disposition==odh::PrimitiveDisposition::goto_true ? 1 : 2),
                        "paired output takes requested branch");
                require(vm.run(1)==(disposition==odh::PrimitiveDisposition::goto_true ?
                        odh::VmStatus::returned_true : odh::VmStatus::returned_false),
                        "paired output preserves true and false returns");
            }
        }
        odh::PrimitiveOutcome paired{odh::PrimitiveDisposition::goto_true,{}};
        paired.write=odh::VmFrameWrite{odh::VmFrameBank::locals,0,99};
        paired.temp_write=odh::VmTempWrite{20,9};
        paired.second_temp_write=odh::VmTempWrite{19,-7};
        paired.stack_object_write=odh::VmStackObjectWrite{-42};
        rejects_outputs(paired); // Invalid first temp must not publish a valid second temp.
        paired.temp_write=odh::VmTempWrite{0,9};
        paired.second_temp_write=odh::VmTempWrite{20,-7};
        rejects_outputs(paired); // Invalid second temp must not publish the first or frame/context.
        paired.second_temp_write=odh::VmTempWrite{19,-7};
        paired.write=odh::VmFrameWrite{odh::VmFrameBank::locals,4,99};
        rejects_outputs(paired);
        paired.write=odh::VmFrameWrite{static_cast<odh::VmFrameBank>(99),0,99};
        rejects_outputs(paired);
        // Exercise second_temp_write independently, so rejection cannot be caused
        // by the established first-temp or stack-object disposition checks.
        paired.temp_write.reset(); paired.stack_object_write.reset();
        paired.write=odh::VmFrameWrite{odh::VmFrameBank::arguments,0,99};
        for (const auto disposition : {odh::PrimitiveDisposition::wait,
                                       odh::PrimitiveDisposition::call,
                                       odh::PrimitiveDisposition::unsupported}) {
            paired.disposition=disposition;
            if (disposition==odh::PrimitiveDisposition::call) paired.call=odh::VmCall{key(),{},{}};
            else paired.call.reset();
            rejects_outputs(paired);
        }
        paired.disposition=odh::PrimitiveDisposition::goto_true; paired.call.reset();
        auto second_only=output_machine(paired); second_only.start(key(),{11,22,33},{17});
        second_only.temps()[0]=42;
        require(second_only.run(1)==odh::VmStatus::budget_exhausted &&
                second_only.temps()[0]==42 && second_only.temps()[19]==-7 &&
                second_only.stack()[0].arguments[0]==99,
                "second temp can publish without first temp");

        const auto service_wait=[](std::uint16_t opcode) {
            return odh::Instruction{opcode,1,2,{0,0,0,0,0,0,0,0}};
        };
        odh::RoutineSet external_routines{
            {key(),routine({service_wait(45),expr(0,1,0,7,7),expr(2,0,0,7,7,254,253)})},
            {key(4097),routine({service_wait(46),expr(0,1,0,7,7),expr(2,0,0,7,7,254,253)})},
            {key(4098),routine({expr(0,1,0,7,7)})}
        };
        odh::Vm external(std::move(external_routines),{}, {},
            [](const auto&,const auto& instruction,const auto&,const auto&) {
                if(instruction.opcode==45||instruction.opcode==46)
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
                return odh::PrimitiveOutcome{};
            });
        external.start(key());
        require(external.run(1)==odh::VmStatus::waiting&&external.pending(),
                "service parent waits before executing an external child BHAV");
        const auto parent_ticket=external.pending()->ticket;
        require(!external.call_pending(parent_ticket+1,{key(4097),{},{}})&&
                !external.call_pending(parent_ticket,{key(4099),{},{}})&&
                external.status()==odh::VmStatus::waiting&&external.pending()->ticket==parent_ticket&&
                external.stack().size()==1,
                "stale ticket and missing child routine preserve the suspended parent atomically");
        require(external.call_pending(parent_ticket,{key(4097),{},{}})&&
                external.status()==odh::VmStatus::running&&external.stack().size()==2&&!external.pending(),
                "external child call suspends the parent ticket without re-dispatching its BHAV");
        require(external.run(1)==odh::VmStatus::waiting&&external.pending(),
                "external child can own a separate service wait");
        const auto child_ticket=external.pending()->ticket;
        require(child_ticket!=parent_ticket&&external.call_pending(child_ticket,{key(4098),{},{}}),
                "nested external frame suspends the child wait independently");
        require(external.run(1)==odh::VmStatus::yielded&&external.external_frame_return()&&
                external.external_frame_return()->ticket==child_ticket&&
                external.external_frame_return()->result&&
                external.external_frame_return()->code_owner==0&&
                external.external_frame_return()->run_generation==external.run_generation()&&
                external.pending()&&external.pending()->ticket==child_ticket,
                "nested BHAV return publishes identity/result and restores the exact child ticket");
        require(external.resume_external_frame(child_ticket)&&external.status()==odh::VmStatus::waiting&&
                external.complete(child_ticket,true),
                "service resumes after nested return and completes the waiting child BHAV");
        require(external.run(2)==odh::VmStatus::yielded&&external.external_frame_return()&&
                external.external_frame_return()->ticket==parent_ticket&&
                external.external_frame_return()->result&&
                external.pending()&&external.pending()->ticket==parent_ticket,
                "outer portal BHAV return restores the original route service wait");
        require(external.resume_external_frame(parent_ticket)&&external.status()==odh::VmStatus::waiting,
                "outer service can resume after its portal BHAV returns");
        require(external.complete(parent_ticket,true),
                "outer route service completes the exact parent ticket");
        require(external.run(1)==odh::VmStatus::returned_true&&external.stack().empty(),
                "root BHAV follows the completed route result without re-dispatch");
        {
            const auto reset_wait=routine({{45,254,255,{}}});
            odh::Vm resetting({{key(),reset_wait}}, {}, {},
                [](const auto&,const auto&,const auto&,const auto&){
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
                });
            resetting.start(key(),{37,37,0,0});
            resetting.temps()[3]=123;resetting.temp_xl()[1]=123456;
            if(resetting.run(1)!=odh::VmStatus::waiting)
                throw std::runtime_error("reset fixture service wait: "+resetting.diagnostic());
            const auto old_ticket=resetting.pending()->ticket;
            const auto generation=resetting.run_generation();
            require(resetting.reset_execution_state()&&resetting.stack().empty()&&
                    !resetting.pending()&&!resetting.interaction_return()&&!resetting.external_frame_return()&&
                    resetting.status()==odh::VmStatus::returned_false&&resetting.steps()==0&&
                    resetting.run_generation()==generation+1&&resetting.diagnostic().empty()&&
                    resetting.temps()[3]==123&&resetting.temp_xl()[1]==123456,
                    "execution reset discards stale execution while preserving Temp and TempXL");
            require(!resetting.complete(old_ticket,true),"reset invalidates the former service completion");
            odh::Vm restored({{key(),reset_wait}});
            require(restored.restore_snapshot(resetting.snapshot()),
                    "idle reset state is a valid serializable VM state");
            resetting.start(key(),{37,37,0,0});
            require(resetting.run(1)==odh::VmStatus::waiting&&resetting.pending()->ticket>old_ticket&&
                    !resetting.complete(old_ticket,true),
                    "new execution cannot reuse a pre-reset service ticket");
            auto exhausted=resetting.snapshot();
            exhausted.run_generation=std::numeric_limits<std::uint64_t>::max();
            odh::Vm no_generation({{key(),reset_wait}});
            require(no_generation.restore_snapshot(exhausted)&&!no_generation.reset_execution_state()&&
                    no_generation.run_generation()==exhausted.run_generation&&no_generation.pending()&&
                    no_generation.pending()->ticket==exhausted.pending->ticket,
                    "generation overflow refuses reset without discarding the old execution");
            odh::Vm* active=nullptr;bool reentry_refused=false;
            odh::Vm reentrant({{key(),routine({{45,254,255,{}}})}}, {}, {},
                [&](const auto&,const auto&,const auto&,const auto&){
                    reentry_refused=!active->reset_execution_state();
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                });
            active=&reentrant;reentrant.start(key());
            require(reentrant.run(1)==odh::VmStatus::returned_true&&reentry_refused&&
                    reentrant.run_generation()==1,
                    "primitive dispatch cannot clear its own executing stack");
        }
        {
            odh::Vm* active=nullptr;odh::VmFrame captured;odh::Instruction operation{};
            odh::VmDispatch token{};bool real_callback=false,trace_refused=false;
            odh::Vm proof({{key(),routine({{43,254,255,{}}})}}, {}, {},
                [&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                    captured=frame;operation=instruction;token=dispatch;
                    real_callback=active->matches_active_primitive(frame,instruction,dispatch)&&
                        active->matches_active_primitive(captured,operation,token);
                    auto stale=dispatch;++stale.ticket;
                    auto changed=frame;++changed.context.callee;
                    auto wrong=instruction;wrong.operands[0]^=1;
                    require(!active->matches_active_primitive(frame,instruction,stale)&&
                        !active->matches_active_primitive(changed,instruction,dispatch)&&
                        !active->matches_active_primitive(frame,wrong,dispatch),
                        "synchronous effect proof rejects stale tickets and altered call data");
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                });
            active=&proof;proof.start(key());
            require(proof.run(1,[&](const auto& trace){
                const odh::VmDispatch forged{active,1,false};
                trace_refused=!active->matches_active_primitive(active->stack().back(),trace.instruction,forged);
            })==odh::VmStatus::returned_true&&real_callback&&trace_refused&&
                !proof.matches_active_primitive(captured,operation,token),
                "only the active primitive callback can authorize effects, never trace or later code");
        }
        {
            const odh::RoutineSet bank{{key(),routine({{20,254,255,{}}})},
                {key(4097),routine({{99,254,255,{}}},2,0)}};
            const odh::VmPrimitive handler=[](const auto&,const auto& instruction,const auto&,const auto&){
                if(instruction.opcode==20){
                    odh::PrimitiveOutcome result{odh::PrimitiveDisposition::call,{}};
                    result.call=odh::VmCall{key(4097),{1,2,2,0},{},odh::VmCallReturnMode::ordinary,true};
                    return result;
                }
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_false,{}};
            };
            odh::Vm source(bank,{}, {},handler);source.start(key(),{1,1,2});
            require(source.run(1)==odh::VmStatus::budget_exhausted&&source.stack().size()==2&&
                source.stack().back().arguments.empty()&&source.stack().back().exact_arguments&&
                source.stack().back().locals.size()==2&&source.stack().front().pc==0,
                "source functional child keeps exact zero argument extent and parks parent PC");
            odh::Vm restored(bank,{}, {},handler);
            require(restored.restore_snapshot(source.snapshot())&&restored.stack().back().arguments.empty()&&
                restored.run(2)==odh::VmStatus::returned_false,
                "restored exact child keeps its extent and returns through parent false branch");
        }
        {
            const auto tree=routine({expr(2,0,4,7,9)},0,5);
            odh::Vm source({{key(),tree}});source.start(key(),{}, {0,0,0,0},true);
            require(source.stack().back().arguments.size()==4&&source.stack().back().locals.size()==5,
                "exact condition arguments are four even if metadata declares five");
            odh::Vm restored({{key(),tree}});
            require(restored.restore_snapshot(source.snapshot())&&restored.stack().back().arguments.size()==4&&
                restored.run(1)==odh::VmStatus::fault,
                "exact argument extent survives restore and out-of-range reads fault rather than invent zero");
        }
        {
            unsigned effects=0;
            odh::Vm repeating({{key(),routine({{20,254,255,{}}})}}, {}, {},
                [&](const auto&,const auto&,const auto&,const auto&){++effects;
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::continue_execution,{}};});
            repeating.start(key());
            require(repeating.run(3)==odh::VmStatus::budget_exhausted&&effects==3&&
                repeating.stack().back().pc==0&&!repeating.pending(),
                "source CONTINUE repeats empty Push at same PC within instruction budget");
        }
        {
            odh::Vm* active=nullptr;const odh::VmFrame* current_frame=nullptr;
            const odh::Instruction* current_instruction=nullptr;odh::VmDispatch token{};
            odh::VmCall child{key(4097),{1,2,2,77},{},odh::VmCallReturnMode::ordinary,true};
            unsigned resolves=0;bool recursive_accepted=true;
            auto bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4097),routine({{99,254,255,{}}},0,0)}});
            odh::Vm source({{key(),routine({{20,254,255,{}}})}}, {}, {},
                [&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                    if(instruction.opcode!=20)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                    current_frame=&frame;current_instruction=&instruction;token=dispatch;
                    require(active->prepare_primitive_call(frame,instruction,dispatch,child),
                        "active primitive loads exact bounded owner before child preflight");
                    odh::PrimitiveOutcome result{odh::PrimitiveDisposition::call,{}};result.call=child;return result;
                });active=&source;
            require(source.set_owner_resolver([&](auto owner){++resolves;
                require(owner==77,"prepared call resolves requested exact owner");
                recursive_accepted=active->prepare_primitive_call(*current_frame,*current_instruction,token,child);
                return bank;}),"bind resource owner resolver before primitive");
            source.start(key(),{1,1,2});
            require(source.run(3)==odh::VmStatus::returned_true&&resolves==1&&!recursive_accepted,
                "owner resolver cannot recursively prepare another primitive call");
        }
        {
            odh::Vm* active=nullptr;odh::VmFrame parent;odh::Instruction operation{};odh::VmDispatch token{};
            bool committed=false,trace_rejected=false;
            odh::Vm source({{key(),routine({{20,254,255,{}}})},{key(4097),routine({{99,254,255,{}}},0,0)}}, {}, {},
                [&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                    if(instruction.opcode!=20)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                    parent=frame;operation=instruction;token=dispatch;
                    odh::PrimitiveOutcome result{odh::PrimitiveDisposition::call,{}};
                    result.call=odh::VmCall{key(4097),{1,2,2,0},{},odh::VmCallReturnMode::ordinary,true};
                    result.call_commit=[&]{committed=active->matches_active_call_commit(parent,operation,token,1);return committed;};
                    return result;
                });active=&source;source.start(key(),{1,1,2});
            require(source.run(3,[&](const auto& event){if(event.key.id==4097)
                trace_rejected=!source.matches_active_call_commit(parent,operation,token,1);
            })==odh::VmStatus::returned_true&&committed&&trace_rejected&&
                !source.matches_active_call_commit(parent,operation,token,1),
                "only kernel child-commit phase grants parent proof, never the next trace or later code");
        }
        {
            odh::Vm* active=nullptr;bool trace_rejected=false,primitive_accepted=false;
            odh::Vm checked({{key(),routine({{99,254,255,{}}})},{key(4097),routine({expr(5,3,17)})}}, {}, {},
                [&](const auto& frame,const auto&,const auto&,const auto&){
                    const auto result=active->evaluate_check(key(4097),frame.context,{},2);
                    primitive_accepted=result.started&&result.status==odh::VmStatus::returned_true;
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                });active=&checked;checked.start(key(),{1,2,2});
            require(checked.run(1,[&](const auto&){
                const auto result=checked.evaluate_check(key(4097),{1,2,2},{},2);
                trace_rejected=!result.started&&checked.temps()[3]==0;
            })==odh::VmStatus::returned_true&&trace_rejected&&primitive_accepted&&checked.temps()[3]==17,
                "in-dispatch checks require real active primitive authority, never an earlier trace callback");
        }
        {
            odh::Vm* active=nullptr;odh::VmFrame saved;odh::Instruction operation{};odh::VmDispatch token{};
            odh::Vm source({{key(),routine({{30,254,255,{}}})}}, {}, {},
                [&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                    saved=frame;operation=instruction;token=dispatch;
                    for(unsigned invalid=0;invalid<5;++invalid){auto altered=frame;auto op=instruction;auto proof=dispatch;
                        if(invalid==0)++proof.ticket;if(invalid==1)proof.check_mode=true;
                        if(invalid==2)++altered.context.stack_object;if(invalid==3)op.opcode=17;if(invalid==4)op.on_false=254;
                        require(!active->set_active_primitive_stack_object(altered,op,proof,44)&&frame.context.stack_object==3,
                            "Gosub StackObject mutation rejects forged ticket/check/frame/opcode/branches atomically");}
                    require(active->set_active_primitive_stack_object(frame,instruction,dispatch,44)&&frame.context.caller==1&&
                        frame.context.callee==2&&frame.context.stack_object==44&&frame.context.code_owner==0&&frame.pc==0&&
                        !active->set_active_primitive_stack_object(saved,instruction,dispatch,55),
                        "only exact live30 changes current StackObject and old frame cannot replay after mutation");
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_false,{}};
                });active=&source;source.start(key(),{1,2,3,0});
            require(source.run(1)==odh::VmStatus::returned_false&&
                !source.set_active_primitive_stack_object(saved,operation,token,66),"completed30 proof expires outside primitive");
            odh::Vm* check_vm=nullptr;bool check_rejected=false;
            const odh::VmPrimitive inspect_check=[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                check_rejected=dispatch.check_mode&&!check_vm->set_active_primitive_stack_object(frame,instruction,dispatch,44);
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            };
            odh::Vm check({{key(),routine({{30,254,255,{}}})}}, {}, {},inspect_check);
            check_vm=&check;check.start(key(),{1,2,3,0});
            require(check.run_synchronous_check(1,inspect_check).status==odh::VmStatus::returned_true&&check_rejected,
                "real synchronous Check dispatch never receives Gosub frame mutation authority");
        }
        for(const bool checking:{false,true}) {
            odh::Vm* active=nullptr;odh::VmFrame captured;odh::Instruction operation{};odh::VmDispatch token{};
            bool accepted=false;
            odh::Vm unrelated({{key(),routine({{37,254,255,{}}})}});
            const odh::VmPrimitive inspect=[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                captured=frame;operation=instruction;token=dispatch;
                accepted=dispatch.check_mode==checking&&active->matches_active_query(frame,instruction,dispatch);
                require(active->matches_active_primitive(frame,instruction,dispatch)==!checking,
                    "read-only check proof never grants mutating primitive authority");
                auto forged=dispatch;forged.check_mode=!dispatch.check_mode;
                auto stale=dispatch;++stale.ticket;
                auto wrong_frame=frame;++wrong_frame.pc;
                auto wrong_op=instruction;wrong_op.operands[0]^=1;
                require(!active->matches_active_query(frame,instruction,forged)&&
                    !active->matches_active_query(frame,instruction,stale)&&
                    !active->matches_active_query(wrong_frame,instruction,dispatch)&&
                    !active->matches_active_query(frame,wrong_op,dispatch)&&
                    !unrelated.matches_active_query(frame,instruction,dispatch),
                    "query proof rejects forged mode, ticket, frame, instruction and cross-VM origin");
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            };
            odh::Vm source({{key(),routine({{37,254,255,{}}})}}, {}, {},inspect);active=&source;
            source.start(key(),{1,2,3,0});
            const auto status=checking?source.run_synchronous_check(1,inspect).status:source.run(1);
            require(status==odh::VmStatus::returned_true&&accepted&&
                !source.matches_active_query(captured,operation,token),
                "actual normal/check query dispatch authenticates only while its callback is active");
        }
        std::cout << checks << " VM checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "VM test failed: " << e.what() << '\n'; return 1; }
}
