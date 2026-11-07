#include "odh/sleep.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
int checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
const auto key=odh::routine_key(4096);
odh::RoutineSet program(std::array<std::uint8_t,8> operands={}) {
    return {{key,odh::Bhav{0x8002,{{0,1,255,operands},{2,254,255,{0,0,0,0,0,5,8,9}}},{},
                          odh::Bhav::Metadata{0,4,0,0}}}};
}
odh::RoutineSet program_with_async_child() {
    auto result=program();
    result.emplace(odh::routine_key(4097),odh::Bhav{0x8002,{{42,254,255,{}}},{},
        odh::Bhav::Metadata{0,4,0,0}});
    return result;
}
struct Fixture {
    odh::World world;
    int rng=0;
    odh::Sleep sleep{world,[this]{++rng;}};
    odh::Vm vm{program(),world.vm_memory(),{},sleep.primitive()};
    Fixture() {world.create(1,odh::EntityKind::object);sleep.bind(vm);}
    void start(std::int16_t count) {vm.start(key,{1,1,1},{count});vm.run(1);}
    std::int16_t count() const {return vm.stack().back().arguments[0];}
};
}
void retained_caller_sleep_tests(){
    for(bool interrupted:{false,true}){
        Fixture f;
        require(f.vm.start(key,{1,1,1},{0})==odh::VmStatus::running,"capture caller body before immediate Sleep");
        const auto old=f.world.find(1)->generation;
        require(f.world.remove_retaining_body({1,old})&&f.world.create(1,odh::EntityKind::object),"retained caller numeric ID reused before Sleep executes");
        if(interrupted)require(f.sleep.interrupt(),"interrupt uses original dead Caller reference");
        const auto status=f.vm.run(1);
        require(status==(interrupted?odh::VmStatus::budget_exhausted:odh::VmStatus::next_tick)&&f.rng==(interrupted?0:1)&&
            f.vm.stack().back().pc==1&&f.vm.stack().back().arguments[0]==-1,
            "dead Caller immediate natural Sleep yields next tick; interrupt returns immediately without RNG");
        require(f.world.frame_entity(f.vm.stack().back(),odh::VmEntityRole::caller)->generation==old&&f.world.find(1)->generation!=old,
            "Sleep never substitutes reused numeric Caller");
    }
    Fixture f;require(f.sleep.tick(1),"establish nonzero source scheduling tick");f.start(2);const auto old=f.world.find(1)->generation;
    require(f.world.remove_retaining_body({1,old})&&f.world.create(1,odh::EntityKind::object)&&f.sleep.tick(2)&&f.sleep.active(),
        "pending Sleep retains original Caller until authored deadline");
    const auto world=f.world.snapshot();const auto vm=f.vm.snapshot();const auto saved=f.sleep.snapshot();
    odh::World restored;int rng=0;odh::Sleep resumed(restored,[&]{++rng;});
    odh::Vm thread(program(),restored.vm_memory(),{},resumed.primitive());
    require(restored.restore_snapshot(world)&&thread.restore_snapshot(vm)&&resumed.bind(thread),"fresh graph restores old Caller and numeric replacement");
    auto wrong=saved;wrong.actor->generation=restored.find(1)->generation;
    require(!resumed.restore_snapshot(wrong)&&resumed.restore_snapshot(saved),"restore validates pending Caller generation against exact Sleep actor body");
    const auto steps=f.vm.steps();
    require(f.sleep.tick(3)&&resumed.tick(3)&&f.vm.status()==odh::VmStatus::next_tick&&thread.status()==odh::VmStatus::next_tick&&
        !f.sleep.active()&&!resumed.active()&&f.rng==1&&rng==1&&f.vm.steps()==steps&&f.vm.stack().back().arguments[0]<0,
        "async natural dead Caller completion branches once then waits next tick in uninterrupted and fresh graphs");
    require(f.vm.run(1)==odh::VmStatus::next_tick&&f.vm.resume_next_tick()&&f.vm.run(1)==odh::VmStatus::returned_true,
        "dead Caller continuation executes only after explicit next-tick resume");
    Fixture interrupted;interrupted.start(20);const auto dead=interrupted.world.find(1)->generation;
    require(interrupted.world.remove_retaining_body({1,dead})&&interrupted.world.create(1,odh::EntityKind::object)&&interrupted.sleep.interrupt()&&
        interrupted.vm.status()==odh::VmStatus::running&&!interrupted.sleep.active()&&interrupted.rng==0,
        "async interrupt of dead Caller completes True immediately without natural RNG cycle");
}
void source_recovery_sleep_tests(){
    {
        Fixture f;std::uint64_t shared_idle=0;unsigned writes=0,reads=0;
        f.sleep.set_schedule_idle_clock([&]{++reads;return std::optional<std::uint64_t>{shared_idle};},
            [&](auto value){++writes;shared_idle=value;});
        require(f.sleep.tick(1),"source recovery establishes an existing clock");f.start(20);
        require(f.sleep.tick(2),"source recovery preserves a nonzero advanced clock");
        shared_idle=42;const auto writes_before=writes,reads_before=reads;
        const auto expected=odh::WorldEntityReference{1,f.world.find(1)->generation};
        const auto before=f.vm.snapshot();const auto clock=f.sleep.clock();
        auto wrong=expected;++wrong.generation;
        require(!f.sleep.discard_for_source_recovery(wrong)&&f.sleep.active(),"wrong recovery actor cannot retire a Sleep job");
        require(f.sleep.discard_for_source_recovery(expected)&&!f.sleep.active()&&
            f.sleep.source_recovery_detached(expected,f.vm)&&
            f.vm.pending()->ticket==before.pending->ticket&&f.vm.stack().back().arguments==before.stack.back().arguments&&
            f.vm.steps()==before.steps&&f.vm.run_generation()==before.run_generation&&f.rng==0,
            "Sleep retirement never completes the old ticket or changes its Args/run/RNG");
        bool refused_snapshot=false;try{(void)f.sleep.snapshot();}catch(const std::logic_error&){refused_snapshot=true;}
        require(refused_snapshot&&!f.sleep.tick(3)&&!f.sleep.service_due()&&!f.sleep.interrupt()&&!f.sleep.notify()&&
            !f.sleep.rebase_after_source_recovery(expected)&&f.sleep.clock()==clock,
            "half-detached Sleep cannot run/save or rebase before actual Main restart");
        require(f.vm.reset_execution_state()&&f.vm.start_after_reset(key,{1,1,1},{10},true)==odh::VmStatus::running&&
            f.sleep.rebase_after_source_recovery(expected),"same live Sleep rebases after genuine same-VM restart");
        const auto after=f.sleep.snapshot();
        require(!f.sleep.source_recovery_detached(expected,f.vm),"rebased Sleep no longer grants retired-service proof");
        require(after.actor&&after.actor->run_generation==f.vm.run_generation()&&after.clock==clock&&after.last_tick==clock&&
            shared_idle==42&&writes==writes_before&&reads==reads_before&&f.rng==0&&!after.job,
            "rebase preserves old clock/last tick/shared idle state without reading or writing callbacks");
        require(f.sleep.tick(3)&&f.vm.run(1)==odh::VmStatus::waiting&&f.sleep.active(),
            "rebased controller resumes at consecutive tick and accepts the new run wait");
    }
    {
        Fixture f;f.start(20);const auto old=odh::WorldEntityReference{1,f.world.find(1)->generation};
        require(f.sleep.discard_for_source_recovery(old)&&f.world.remove(1)&&f.world.create(1,odh::EntityKind::object)&&
            f.vm.reset_execution_state()&&f.vm.start_after_reset(key,{1,1,1},{10},true)==odh::VmStatus::running&&
            !f.sleep.rebase_after_source_recovery(old)&&!f.sleep.rebase_after_source_recovery({1,f.world.find(1)->generation}),
            "retired actor cannot rebase into a reused numeric ID");
    }
    {
        Fixture f;const auto expected=odh::WorldEntityReference{1,f.world.find(1)->generation};
        require(f.vm.start(key,{1,1,1},{10})==odh::VmStatus::running&&
            f.sleep.discard_for_source_recovery(expected)&&f.vm.reset_execution_state()&&
            f.sleep.rebase_after_source_recovery(expected)&&!f.sleep.snapshot().job,
            "never-slept actor requires its real root caller and can rebase a genuine no-Main idle reset");
        require(!f.sleep.restore_snapshot(f.sleep.snapshot()),
            "source recovery consumes the never-used controller's fresh restore capability");
    }
    {
        Fixture f;bool discard_rejected=false,rebase_rejected=false;
        const auto expected=odh::WorldEntityReference{1,f.world.find(1)->generation};
        f.sleep.set_run_every_frame_source([&]{discard_rejected=!f.sleep.discard_for_source_recovery(expected);
            rebase_rejected=!f.sleep.rebase_after_source_recovery(expected);return false;});
        f.start(10);
        require(discard_rejected&&rebase_rejected&&f.sleep.active()&&f.vm.pending(),
            "recovery cannot mutate Sleep from a real dispatch callback");
    }
}
int main() {
    try {
        retained_caller_sleep_tests();
        source_recovery_sleep_tests();
        {Fixture f;bool every_frame=false;unsigned samples=0;
         f.sleep.set_run_every_frame_source([&]{++samples;return every_frame;});
         require(f.sleep.tick(1),"source scheduling initial tick");f.start(10);
         require(f.sleep.evaluated_this_tick()&&!f.sleep.snapshot().job->next_evaluation&&samples==1,
             "request samples scheduling policy and reports its evaluation");
         every_frame=true;
         require(f.sleep.tick(2)&&!f.sleep.evaluated_this_tick()&&f.count()==9&&samples==1,
             "headline added after scheduling cannot advance pending evaluation");
         for(std::uint64_t tick=3;tick<=10;++tick)require(f.sleep.tick(tick)&&!f.sleep.evaluated_this_tick(),
             "existing natural schedule survives later RunEveryFrame change");
         require(f.sleep.tick(11)&&f.sleep.evaluated_this_tick()&&f.rng==1,
             "pending wait reaches its original natural evaluation");}
        {Fixture f;bool every_frame=true;
         f.sleep.set_run_every_frame_source([&]{return every_frame;});
         f.sleep.tick(1);f.start(5);
         require(f.sleep.snapshot().job->deadline==6&&f.sleep.snapshot().job->next_evaluation==2,
             "RunEveryFrame captures next tick separately from natural deadline");
         require(f.sleep.tick(2)&&f.count()==3&&f.sleep.evaluated_this_tick()&&
             f.sleep.snapshot().job->next_evaluation==3,"each actual evaluation resamples scheduling source");
         const auto saved_sleep=f.sleep.snapshot();const auto saved_vm=f.vm.snapshot();
         Fixture copy;copy.sleep.set_run_every_frame_source([]{return false;});
         require(copy.vm.restore_snapshot(saved_vm)&&copy.sleep.restore_snapshot(saved_sleep)&&
             !copy.sleep.evaluated_this_tick(),"restored scheduled override does not fabricate an evaluation");
         every_frame=false;
         require(f.sleep.tick(3)&&copy.sleep.tick(3)&&f.count()==2&&copy.count()==2&&
             !f.sleep.snapshot().job->next_evaluation&&!copy.sleep.snapshot().job->next_evaluation,
             "expired headline still honors previously scheduled next tick then restores natural delay");
         require(f.sleep.tick(4)&&copy.sleep.tick(4)&&!f.sleep.evaluated_this_tick()&&
             !copy.sleep.evaluated_this_tick()&&f.sleep.tick(5)&&copy.sleep.tick(5),"no synthetic intermediate evaluations");
         require(f.sleep.tick(6)&&copy.sleep.tick(6)&&f.rng==1&&copy.rng==1&&f.count()==-1&&copy.count()==-1,
             "override save/restore preserves final counter and RNG boundary");}
        {Fixture f;f.sleep.set_run_every_frame_source([]{return true;});f.sleep.tick(1);f.start(1);
         require(!f.sleep.snapshot().job->next_evaluation&&f.sleep.snapshot().job->deadline==2,
             "natural one-tick delay needs no serialized override");}
        {Fixture f;f.start(3);
         require(f.vm.status()==odh::VmStatus::waiting && f.sleep.active() && f.count()==2 && f.vm.steps()==1,
                 "initial evaluation subtracts one and publishes Args before WAIT");
         const auto ticket=f.vm.pending()->ticket;
         require(f.sleep.tick(0) && f.sleep.tick(1) && f.sleep.tick(2) && f.count()==2 && f.rng==0,
                 "silent consecutive ticks leave counter and RNG untouched");
         require(!f.sleep.tick(2) && !f.sleep.tick(1) && !f.sleep.tick(4) && f.sleep.clock()==2 && f.count()==2,
                 "duplicate/backward/gap ticks preserve clock and frame");
         require(f.sleep.tick(3) && f.count()==1 && f.vm.pending()->ticket==ticket && f.vm.steps()==1,
                 "idleStart zero sentinel subtracts one at first deadline and reschedules same ticket");
         require(f.sleep.tick(4) && f.count()==1 && f.sleep.tick(5) && f.count()==-1 && f.rng==1 &&
                 f.vm.status()==odh::VmStatus::running && !f.sleep.active() && f.vm.steps()==1,
                 "nonzero idleStart subtracts elapsed at deadline and completes one wait");
         require(f.vm.run(1)==odh::VmStatus::returned_true && f.vm.temps()[0]==-1,
                 "branch after natural completion observes written final counter");}
        {Fixture f;f.sleep.tick(1);f.start(2);f.sleep.tick(2);
         require(f.sleep.dispatch_due(3)&&f.sleep.tick(3,true)&&f.sleep.dispatch_due(3)&&
             !f.sleep.evaluated_this_tick()&&f.count()==1&&f.vm.status()==odh::VmStatus::waiting,
             "deferred tick advances the shared clock but leaves due Sleep untouched");
         require(f.sleep.service_due()&&!f.sleep.dispatch_due(3)&&f.sleep.evaluated_this_tick()&&
             f.count()==-1&&f.vm.status()==odh::VmStatus::running&&f.rng==1,
             "service_due evaluates once at the already-advanced clock boundary");}
        {Fixture f;require(f.sleep.tick(1),"first tick one allowed");f.start(3);
         require(f.sleep.tick(2) && f.sleep.tick(3) && f.sleep.tick(4) && f.count()==-1 && f.rng==1,
                 "nonzero start evaluates once at exact computed deadline");}
        for(const auto value:{0,-1,-32767}) {
            Fixture f;f.start(static_cast<std::int16_t>(value));
            require(f.vm.status()==odh::VmStatus::budget_exhausted && f.count()==value-1 && f.rng==1 && !f.sleep.active(),
                    "negative final subtraction branches immediately and cycles RNG");
        }
        {Fixture f;f.start(-32768);
         require(f.count()==32767 && f.sleep.active(),"signed counter subtraction wraps negative minimum to positive maximum");
         for(std::uint64_t n=1;n<=32768;++n) require(f.sleep.tick(n),"maximum positive delay remains bounded");
         require(f.count()==32766 && f.sleep.active() && f.rng==0,"zero sentinel still applies after 32768 ticks");
         for(std::uint64_t n=32769;n<=65535;++n) require(f.sleep.tick(n),"maximum reschedule stays consecutive");
         require(f.count()==-1 && !f.sleep.active() && f.rng==1,"wrapped counter eventually completes naturally");}
        {Fixture f;f.sleep.tick(1);f.start(-32768);
         for(std::uint64_t n=2;n<=32769;++n) require(f.sleep.tick(n),"elapsed short boundary tick accepted");
         require(f.count()==-1 && f.rng==1 && !f.sleep.active(),
                 "elapsed 32768 wraps to negative short before subtraction, preserving compound assignment wrap");}
        {Fixture f;f.sleep.tick(1);f.start(5);const auto ticket=f.vm.pending()->ticket;
         require(f.vm.update_pending(ticket,{odh::VmFrameBank::arguments,0,10}),"external host may update sleeping argument");
         for(std::uint64_t n=2;n<=6;++n) require(f.sleep.tick(n),"changed counter first deadline ticks");
         require(f.count()==5 && f.vm.pending()->ticket==ticket && f.vm.steps()==1 && f.rng==0,
                 "reevaluation reads actual pending frame argument and updates without redispatch");
         for(std::uint64_t n=7;n<=12;++n) require(f.sleep.tick(n),"changed counter reschedule ticks");
         require(f.count()==-1 && f.rng==1 && f.vm.steps()==1,"new counter controls rescheduled deadline");}
        {Fixture f;f.start(20);f.sleep.tick(1);f.sleep.tick(2);
         require(f.sleep.interrupt() && f.count()==18 && !f.sleep.active() && f.rng==0 &&
                 f.vm.status()==odh::VmStatus::running && f.vm.steps()==1,
                 "interrupt evaluates immediately at same frame, honors zero sentinel, and skips RNG");
         require(f.vm.run(1)==odh::VmStatus::returned_true && f.vm.temps()[0]==18 && !f.sleep.interrupt(),
                 "interrupted counter is observable and terminal VM cannot fake another wake");}
        {Fixture f;f.sleep.tick(1);f.start(20);f.sleep.tick(2);f.sleep.tick(3);
         require(f.sleep.interrupt() && f.count()==17 && f.rng==0,"nonzero idle start interrupt subtracts elapsed immediately");}
        {Fixture f;require(!f.sleep.interrupt(),"ready empty VM cannot register an interrupt");
         f.vm.start(key,{1,1,1},{0});require(f.sleep.interrupt(),"interrupt persists before first Sleep dispatch");
         require(f.vm.run(1)==odh::VmStatus::budget_exhausted && f.count()==-1 && f.rng==0,
                 "preexisting interrupt subtracts counter, takes true, and suppresses natural RNG");
         f.vm.start(key,{1,1,1},{0});f.vm.run(1);require(f.rng==1,"new VM run clears consumed interrupt");}
        {Fixture f;f.vm.start(key,{1,1,1},{20});f.sleep.interrupt();f.vm.start(key,{1,1,1},{20});f.vm.run(1);
         require(f.sleep.active() && f.count()==19,"restart clears unconsumed interrupt");}
        {Fixture f;f.start(20);const auto old=f.vm.pending()->ticket;f.vm.start(key,{1,1,1},{7});
         require(!f.sleep.tick(1) && !f.sleep.active() && f.vm.steps()==0 && f.count()==7 &&
                 !f.vm.complete(old,true) && f.rng==0,"restart cancels job without effects on new invocation");
         f.vm.run(1);require(f.sleep.active() && f.count()==6,"same stable controller accepts fresh invocation");}
        {Fixture f;f.start(20);const auto old=f.world.find(1)->generation;f.world.remove(1);f.world.create(1,odh::EntityKind::object);
         require(f.world.find(1)->generation!=old && !f.sleep.interrupt() && f.sleep.active() &&
                 f.vm.status()==odh::VmStatus::waiting && f.rng==0 && !f.sleep.interrupt(),
                 "missing old body rejects without fake false completion or adopting replacement");
         f.start(20);require(f.sleep.active() && f.count()==19,"restart can explicitly register replacement generation");}
        {Fixture f;f.start(20);f.world.remove(1);
         require(!f.sleep.tick(1) && f.vm.status()==odh::VmStatus::waiting && f.sleep.active() && f.rng==0,
                 "missing caller body is unsupported without completing or writing the pending frame");}
        {Fixture f;odh::Vm other(program(),{}, {},f.sleep.primitive());other.start(key,{1,1,1},{20});
         require(!f.sleep.bind(other) && other.run(1)==odh::VmStatus::fault && !f.sleep.active(),
                 "foreign VM cannot rebind or issue Sleep against controller");}
        for(const auto index:{-1,4,32767,-32768}) {
            odh::World world;world.create(1,odh::EntityKind::object);odh::Sleep sleep(world);
            const auto bits=static_cast<std::uint16_t>(index);
            odh::Vm vm(program({static_cast<std::uint8_t>(bits),static_cast<std::uint8_t>(bits>>8),0,0,0,0,0,0}),
                       {},{},sleep.primitive());sleep.bind(vm);vm.start(key,{1,1,1},{7});
            require(vm.run(1)==odh::VmStatus::fault && vm.stack().back().arguments[0]==7 && !vm.pending() && !sleep.active(),
                    "signed little endian index rejects negative or out of range without mutation");
        }
        {odh::World world;world.create(1,odh::EntityKind::object);int rng=0;odh::Sleep sleep(world,[&]{++rng;});
         odh::Vm vm(program({3,0,255,254,253,252,251,250}),{}, {},sleep.primitive());sleep.bind(vm);
         vm.start(key,{1,1,1},{8,9,10,0});vm.run(1);
         require(vm.stack().back().arguments==std::vector<std::int16_t>({8,9,10,-1}) && rng==1,
                 "valid nonzero index is decoded and all six trailing operand bytes ignored");}
        {odh::World world;world.create(1,odh::EntityKind::object);odh::Sleep sleep(world);
         odh::Vm vm(program(),{}, {},sleep.primitive());sleep.bind(vm);vm.start(key,{1,1,1},{0});vm.run(1);
         require(sleep.diagnostic().find("RNG")!=std::string::npos && vm.status()==odh::VmStatus::budget_exhausted,
                 "missing RNG backend is an observable limitation on natural completion");}
        {odh::World world;odh::Sleep sleep(world);odh::Vm vm(program(),{}, {},sleep.primitive());sleep.bind(vm);
         vm.start(key,{1,1,1},{0});require(vm.run(1)==odh::VmStatus::fault && !sleep.interrupt(),
                 "caller registration is required before Sleep or interrupt");}
        {odh::World world;require(world.create(7,odh::EntityKind::object),"birth actor fixture");
         int rng=0;odh::Sleep sleep(world,[&]{++rng;});
         odh::Vm vm(program(),world.vm_memory(),{},sleep.primitive());sleep.bind(vm);
         vm.start(key,{7,7,0},{5});const auto steps=vm.steps(),run=vm.run_generation();
         require(!sleep.join_at(40,9)&&sleep.clock()==0&&!sleep.snapshot().actor,
                 "missing birth actor rejects without changing service clock or identity");
         require(sleep.join_at(40,7),"prepared object Main binds its birth clock and identity");
         const auto state=sleep.snapshot();
         require(state.actor&&state.actor->id==7&&state.actor->generation==world.find(7)->generation&&
                 state.actor->run_generation==run&&!state.job&&state.last_tick==40&&state.clock==40&&
                 vm.steps()==steps&&vm.run_generation()==run&&vm.status()==odh::VmStatus::running&&rng==0,
                 "birth identity is serializable before dispatch without VM or RNG effects");
         require(!sleep.join_at(41,7)&&sleep.tick(41)&&rng==0,
                 "birth clock still advances once and never rejoins");}
        {Fixture f;
         require(!f.sleep.tick(2) && !f.sleep.tick(std::numeric_limits<std::uint32_t>::max()) &&
                 !f.sleep.tick(static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())+1) &&
                 !f.sleep.tick(std::numeric_limits<std::uint64_t>::max()) && f.sleep.clock()==0,
                 "first tick must zero/one; uint32 and uint64 overflow proposals preserve clock");
         require(f.sleep.tick(0) && !f.sleep.tick(std::numeric_limits<std::uint64_t>::max()) && f.sleep.tick(1),
                 "rejected overflow tick never prevents valid consecutive progression");}
        {odh::World world;world.create(1,odh::EntityKind::object);odh::Sleep sleep(world);
         odh::Vm vm(program(),{}, {},sleep.primitive());sleep.bind(vm);
         require(sleep.join_at(40) && sleep.clock()==40 && sleep.tick(41) && sleep.clock()==41,
                 "new controller can join an already-running world clock and advance contiguously");
         require(!sleep.join_at(41) && !sleep.tick(43) && sleep.clock()==41,
                 "clock join is one-time and still rejects a later tick gap");}
        {odh::World world;odh::Sleep sleep(world);
         require(!sleep.join_at(40) && sleep.clock()==0,
                 "clock join requires a bound controller");}
        {odh::World world;world.create(1,odh::EntityKind::object);int rng=0;bool tick_guard=false,interrupt_guard=false,
                 bind_guard=false,request_guard=false,vm_guard=false;
         odh::Sleep* controller=nullptr;odh::Vm* machine=nullptr;
         odh::Sleep sleep(world,[&] {
             ++rng;
             try{controller->tick(controller->clock()+1);}catch(const std::logic_error&){tick_guard=true;}
             try{controller->interrupt();}catch(const std::logic_error&){interrupt_guard=true;}
             bind_guard=!controller->bind(*machine);
             if(!machine->stack().empty()) {
                 request_guard=controller->primitive()(machine->stack().back(),{0,254,255,{}},machine->temps(),
                                                       {machine,123}).disposition==odh::PrimitiveDisposition::unsupported;
                 try{machine->start(key,{1,1,1},{0});}catch(const std::logic_error&){vm_guard=true;}
             }
         });controller=&sleep;odh::Vm vm(program(),{}, {},sleep.primitive());machine=&vm;sleep.bind(vm);
         vm.start(key,{1,1,1},{0});vm.run(1);
         require(rng==1 && tick_guard && interrupt_guard && bind_guard && request_guard && vm_guard &&
                 vm.stack().back().arguments[0]==-1 && sleep.clock()==0,
                 "RNG dispatch callbacks cannot reenter request/tick/interrupt/bind or executing VM");}
        {odh::World world;world.create(1,odh::EntityKind::object);int rng=0;bool tick_guard=false,interrupt_guard=false;
         odh::Sleep* controller=nullptr;odh::Vm* machine=nullptr;
         odh::Sleep sleep(world,[&] {
             ++rng;
             require(machine->stack().back().arguments[0]==-1 && !machine->pending(),
                     "scheduled natural completion publishes Args before RNG notification");
             try{controller->tick(controller->clock()+1);}catch(const std::logic_error&){tick_guard=true;}
             try{controller->interrupt();}catch(const std::logic_error&){interrupt_guard=true;}
         });controller=&sleep;odh::Vm vm(program(),{}, {},sleep.primitive());machine=&vm;sleep.bind(vm);
         sleep.tick(1);vm.start(key,{1,1,1},{2});vm.run(1);sleep.tick(2);sleep.tick(3);
         require(rng==1 && tick_guard && interrupt_guard && sleep.clock()==3 && vm.steps()==1,
                 "scheduled RNG callback cannot consume ticks or inject interrupts during evaluation");}
        {Fixture f;std::size_t writes=0;f.sleep.set_schedule_idle_clock(
             []{return std::optional<std::uint64_t>{};},[&](std::uint64_t){++writes;});
         f.sleep.tick(1);f.start(3);
         require(f.count()==2&&f.sleep.snapshot().job->idle_start==1&&writes==0,
             "null shared clock preserves standalone decrement and schedule behavior");}
        {odh::World world;world.create(1,odh::EntityKind::object);int rng=0;std::uint64_t shared_idle_start=0;
         std::vector<std::uint64_t> writes;odh::Sleep sleep(world,[&]{++rng;});
         sleep.set_schedule_idle_clock([&]{return std::optional<std::uint64_t>{shared_idle_start};},
             [&](std::uint64_t value){shared_idle_start=value;writes.push_back(value);});
         auto sleep_primitive=sleep.primitive();
         odh::Vm vm(program_with_async_child(),world.vm_memory(),{},
             [sleep_primitive](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
                 if(instruction.opcode==42)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
                 return sleep_primitive(frame,instruction,temps,dispatch);
             });sleep.bind(vm);vm.start(key,{1,1,1},{100});
         require(sleep.join_at(10,1)&&vm.run(1)==odh::VmStatus::waiting&&sleep.dispatch_due(110)&&
             !sleep.dispatch_due(109)&&shared_idle_start==10&&sleep.snapshot().job->deadline==110,
             "shared ScheduleIdleStart and actual natural deadline are published together");
         const auto parent_ticket=vm.pending()->ticket;
         require(!sleep.park_for_immediate(parent_ticket)&&sleep.active()&&shared_idle_start==10,
             "stale park without a matching immediate continuation changes nothing");
         const odh::VmCall child{odh::routine_key(4097),{1,1,1,0},{0,0,0,0}};
         const auto immediate_ticket=vm.inject_immediate(child,[](const auto& pending,const auto&){
             return pending.instruction.instruction.opcode==0;
         });
         require(immediate_ticket&& !sleep.park_for_immediate(parent_ticket+1)&&sleep.active()&&
             sleep.park_for_immediate(parent_ticket)&&!sleep.active()&&shared_idle_start==10&&writes.size()==1,
             "successful immediate injection parks only the matching Sleep job and preserves shared seed");
         require(vm.run(1)==odh::VmStatus::waiting&&vm.pending()->instruction.instruction.opcode==42&&
             !sleep.retry_immediate_parent(parent_ticket)&&shared_idle_start==10&&rng==0,
             "async CreateObjectInstance wait does not resume or alter the parked parent");
         for(std::uint64_t tick=11;tick<=111;++tick)require(sleep.tick(tick),
             "parked parent clock advances while async child remains pending");
         const auto child_ticket=vm.pending()->ticket;
         require(vm.complete(child_ticket,true)&&vm.run(2)==odh::VmStatus::yielded&&vm.immediate_return()&&
             vm.resume_immediate(*immediate_ticket)&&!sleep.retry_immediate_parent(parent_ticket+1)&&
             sleep.retry_immediate_parent(parent_ticket),
             "matching immediate return restores the parent Sleep ticket for retry");
         require(vm.run(1)==odh::VmStatus::budget_exhausted&&vm.stack().back().arguments[0]==-2&&
             shared_idle_start==0&&writes==std::vector<std::uint64_t>({10,0})&&rng==1&&!sleep.active(),
             "parent Sleep subtracts 101 ticks from the preserved seed after async child completion");}
        {odh::World world;world.create(1,odh::EntityKind::object);int rng=0;std::uint64_t shared_idle_start=0;
         std::vector<std::uint64_t> writes;odh::Sleep sleep(world,[&]{++rng;});
         sleep.set_schedule_idle_clock([&]{return std::optional<std::uint64_t>{shared_idle_start};},
             [&](std::uint64_t value){shared_idle_start=value;writes.push_back(value);});
         odh::Vm vm(program(),world.vm_memory(),{},sleep.primitive());sleep.bind(vm);
         vm.start(key,{1,1,1},{100});
         require(sleep.join_at(10,1)&&vm.run(1)==odh::VmStatus::waiting&&shared_idle_start==10,
             "parent Sleep establishes shared source idle start");
         const auto parent_ticket=vm.pending()->ticket;
         const odh::VmCall child{key,{1,1,1,0},{100,0,0,0}};
         const auto immediate_ticket=vm.inject_immediate(child,[](const auto& pending,const auto&){
             return pending.instruction.instruction.opcode==0;
         });
         require(immediate_ticket&&sleep.park_for_immediate(parent_ticket)&&
             vm.run(1)==odh::VmStatus::waiting&&sleep.active()&&shared_idle_start==10,
             "immediate child Sleep replaces the parked service with its own wait");
         require(sleep.dispatch_due(110),"child Sleep becomes due at its captured deadline");
         for(std::uint64_t tick=11;tick<=110;++tick)require(sleep.tick(tick),
             "child Sleep advances to its scheduled deadline");
         require(!sleep.active()&&shared_idle_start==0&&rng==1&&vm.run(4)==odh::VmStatus::yielded&&
             vm.immediate_return()&&sleep.tick(111)&&vm.resume_immediate(*immediate_ticket)&&
             sleep.retry_immediate_parent(parent_ticket),
             "child Sleep completion resets shared ScheduleIdleStart before parent retry");
         require(vm.run(1)==odh::VmStatus::waiting&&vm.stack().front().arguments[0]==98&&
             shared_idle_start==111&&writes==std::vector<std::uint64_t>({10,10,0,111})&&rng==1,
             "parent Sleep subtracts only one after child Sleep reset the shared seed");}
        std::cout<<checks<<" Sleep checks passed (synthetic clock/World fixtures only)\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Sleep test failed: "<<e.what()<<'\n';return 1;}
}
