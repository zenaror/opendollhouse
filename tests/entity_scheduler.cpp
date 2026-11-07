#include "odh/entity_scheduler.hpp"
#include "odh/entity_thread_controllers.hpp"
#include "odh/interaction.hpp"
#include "odh/notify.hpp"
#include "odh/playback.hpp"
#include <algorithm>
#include <iostream>

namespace {
int checks=0;
void require(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
odh::RoutineKey key(unsigned id) {return odh::routine_key(static_cast<std::uint16_t>(id));}
odh::Instruction idle() {return {17,254,255,{0,0,1,0,0,0,0,0}};}
odh::Instruction set(unsigned index,unsigned value) {
    return {2,254,255,{static_cast<std::uint8_t>(index),0,static_cast<std::uint8_t>(value),0,0,5,8,7}};
}
odh::Bhav routine(std::initializer_list<odh::Instruction> instructions) {
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,4,4,0}};
}

void scheduler_budget_yield_diagnostics(){
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object),
            "budget diagnostic fixture creates independent entities");
    auto first=set(0,1);first.on_true=1;
    auto second=set(0,2);second.on_true=2;
    const odh::Instruction sleeping{0,254,255,{}};
    auto program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
        {key(4096),routine({first,second,sleeping})},{key(4097),routine({{99,254,255,{}}})}});
    odh::EntityThreadControllers worker_thread(world,1);
    odh::Vm worker(program,odh::ShareVmProgramTag{},world.vm_memory(),{},worker_thread.sleep_primitive());
    odh::Vm dormant(program,odh::ShareVmProgramTag{},world.vm_memory(),{},
        [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
    require(worker_thread.bind(worker)&&worker.start(key(4096),{1,1,1},{20})==odh::VmStatus::running&&
            dormant.start(key(4097),{2,2,2})==odh::VmStatus::running&&dormant.run(1)==odh::VmStatus::waiting,
            "worker starts before Sleep while dormant entity still owns a fair-share slot");
    odh::EntityVmScheduler scheduler(2);
    bool observed_before_after_callback=false;
    const auto before=[&](std::uint64_t tick){return worker_thread.before_run(tick);};
    const auto after=[&]{
        if(worker.status()==odh::VmStatus::budget_exhausted)
            observed_before_after_callback=scheduler.budget_yield_count()==1&&scheduler.first_budget_yield()&&
                scheduler.first_budget_yield()->entity==1&&scheduler.first_budget_yield()->tick==1;
        return true;
    };
    require(scheduler.budget_yield_count()==0&&!scheduler.first_budget_yield()&&
            scheduler.add(1,worker,before,after)&&scheduler.add(2,dormant),
            "fresh scheduler has no budget-yield evidence");
    require(scheduler.tick(1,4,4,{},{},{},odh::EntityVmScheduler::ControllerOrder::per_entity)&&
            worker.status()==odh::VmStatus::budget_exhausted&&worker.steps()==2&&scheduler.instructions_last_tick()==2&&
            observed_before_after_callback,"fair-share exhaustion is observed before callbacks even when aggregate budget remains");
    const auto evidence=scheduler.first_budget_yield();
    require(evidence&&evidence->entity==1&&evidence->tick==1&&evidence->granted==2&&evidence->used==2,
            "first diagnostic identifies actual run grant and consumption");
    require(scheduler.tick(2,4,4,{},{},{},odh::EntityVmScheduler::ControllerOrder::per_entity)&&
            worker.status()==odh::VmStatus::waiting&&worker.pending()&&worker.pending()->instruction.instruction.opcode==0&&
            scheduler.instructions_last_tick()==1&&scheduler.budget_yield_count()==1&&
            scheduler.first_budget_yield()->tick==1,
            "later Sleep waiting status does not erase the earlier budget-induced delay");
    const auto saved=scheduler.snapshot();
    require(!scheduler.restore_snapshot(saved)&&scheduler.budget_yield_count()==1&&scheduler.first_budget_yield()->used==2,
            "rejected nonfresh restore leaves diagnostics untouched");
    const auto copied=scheduler;
    require(copied.budget_yield_count()==1&&copied.first_budget_yield()->tick==1,
            "ordinary scheduler copies retain their bounded diagnostic observation");
    odh::EntityVmScheduler restored(2);
    require(restored.add(1,worker,before,after)&&restored.add(2,dormant)&&restored.restore_snapshot(saved)&&
            restored.budget_yield_count()==0&&!restored.first_budget_yield(),
            "restored continuation begins a fresh observation without serialized budget history");
    const auto roundtrip=restored.snapshot();
    require(roundtrip.entity_limit==saved.entity_limit&&roundtrip.budget_cursor==saved.budget_cursor&&
            roundtrip.last_tick==saved.last_tick&&roundtrip.instructions_last_tick==saved.instructions_last_tick&&
            roundtrip.faulted==saved.faulted&&roundtrip.diagnostic==saved.diagnostic&&
            roundtrip.entities.size()==saved.entities.size()&&
            std::equal(roundtrip.entities.begin(),roundtrip.entities.end(),saved.entities.begin(),[](const auto& a,const auto& b){
                return a.id==b.id&&a.run_generation==b.run_generation&&a.steps==b.steps&&a.status==b.status&&
                    a.before==b.before&&a.after==b.after&&a.interrupt==b.interrupt;
            }),"logical scheduler snapshot roundtrips unchanged while diagnostic count resets");
}
void per_entity_callback_slots(){
    for(const auto hook:{0,1,2})for(bool later:{false,true}){
        const std::int16_t sender_id=later?1:3,target_id=later?3:1;
        odh::World world;require(world.create(sender_id,odh::EntityKind::avatar)&&world.create(target_id,odh::EntityKind::avatar),"create per-entity callback actors");
        const odh::Instruction sleeping{0,1,255,{0,0,0,0,0,0,0,0}};
        auto program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
            {key(4096),routine({{99,254,255,{}}})},{key(4097),routine({sleeping,set(0,7)})},{key(4098),routine({set(0,19)})}});
        odh::EntityVmScheduler scheduler(3);odh::EntityThreadControllers target_thread(world,target_id);
        odh::Vm sender(program,odh::ShareVmProgramTag{},world.vm_memory(),{},
            [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
        odh::Vm target(program,odh::ShareVmProgramTag{},world.vm_memory(),{},target_thread.sleep_primitive());
        odh::Vm newborn(program,odh::ShareVmProgramTag{},world.vm_memory());
        require(target_thread.bind(target)&&sender.start(key(4096),{sender_id,sender_id,sender_id})==odh::VmStatus::running&&
            target.start(key(4097),{target_id,target_id,target_id},{20})==odh::VmStatus::running&&newborn.start(key(4098))==odh::VmStatus::running&&
            sender.run(1)==odh::VmStatus::waiting&&target.run(1)==odh::VmStatus::waiting,"sender has no runnable BHAV while target owns real future Sleep");
        bool sent=false;unsigned interrupts=0;
        const auto send=[&]{require(scheduler.running()&&scheduler.current_entity()==sender_id&&!sender.dispatch_active(),"source slot covers callback at a VM safe point");
            if(!sent){sent=true;require(!scheduler.add(5,newborn)&&!scheduler.remove(sender_id),"callback may not add newborn or remove a scheduler entity");
                require(scheduler.interrupt(target_id),"non-VM source service notifies target through scheduler");}return true;};
        require(scheduler.add(sender_id,sender,[&](auto){require(scheduler.current_entity()==sender_id,"before callback owns selected source entity");return hook!=0||send();},
            [&]{require(scheduler.current_entity()==sender_id,"after callback owns selected source entity even without VM work");return hook!=1||send();})&&
            scheduler.add(target_id,target,[&](auto tick){require(scheduler.current_entity()==target_id,"target before hook owns its own entity slot");return target_thread.before_run(tick);},{},
                [&]{++interrupts;require(!sender.dispatch_active()&&scheduler.current_entity()==sender_id,"interrupt publishes at sender callback safe point before next entity");return target_thread.notify();}),"register callback-only service and sleeping target");
        const odh::EntityVmScheduler::EntityTick lifecycle=[&](auto id,auto,bool ran){require(scheduler.current_entity()==id,"entity lifecycle remains inside selected entity scope");
            if(id==sender_id){require(!ran,"parked source service consumes no VM budget");if(hook==2)return send();}return true;};
        std::vector<std::int16_t> trace;
        require(scheduler.tick(1,8,4,[&](auto id,const auto&){require(scheduler.current_entity()==id,"VM dispatch remains in same selected scope");trace.push_back(id);},{},lifecycle,
            odh::EntityVmScheduler::ControllerOrder::per_entity)&&sent&&interrupts==1&&!scheduler.current_entity(),"complete callback notification tick and clear ephemeral entity identity");
        require(sender.steps()==1&&(later?(target.status()==odh::VmStatus::returned_true&&target.temps()[0]==7&&trace==std::vector<std::int16_t>{target_id,target_id}):
            (target.status()==odh::VmStatus::waiting&&target.steps()==1&&trace.empty())),"later entity runs this tick; earlier entity is not revisited after its slot");
        require(scheduler.add(5,newborn)&&newborn.steps()==0,"newborn Main admitted only outside dispatch, after creation tick");
        require(scheduler.tick(2,12,4,{},{},lifecycle,odh::EntityVmScheduler::ControllerOrder::per_entity)&&target.temps()[0]==7&&newborn.temps()[0]==19&&newborn.steps()==1,
            "earlier notification and newly admitted Main execute at next scheduler boundary");
    }
}
}

int main() {
    try {
        scheduler_budget_yield_diagnostics();
        per_entity_callback_slots();
        {
            odh::Vm broken({{key(4096),routine({{99,254,255,{}}})},
                {key(4097),routine({set(0,77)})}});
            broken.start(key(4096));
            odh::EntityVmScheduler recovery;
            require(recovery.add(37,broken),"register object before explicit fault recovery");
            unsigned recoveries=0;
            const odh::EntityVmScheduler::FaultRecovery reset=[&](auto id,odh::Vm& vm,auto tick){
                ++recoveries;
                require(id==37&&tick==1&&!vm.dispatch_active()&&vm.status()==odh::VmStatus::fault&&
                        vm.steps()==1&&recovery.instructions_last_tick()==1,
                        "host sees exact failing object at a safe point after its slice is charged");
                return vm.reset_execution_state()&&vm.start(key(4097))==odh::VmStatus::running;
            };
            require(recovery.tick(1,1,1,{},reset)&&recoveries==1&&broken.steps()==0&&
                    broken.status()==odh::VmStatus::running&&broken.temps()[0]==0,
                    "recovery prepares Main without granting extra instructions in the fault tick");
            require(recovery.tick(2,1,1,{},reset)&&broken.status()==odh::VmStatus::returned_true&&
                    broken.temps()[0]==77&&recoveries==1,
                    "prepared Main executes only on the next scheduler tick");
            odh::Vm unhandled({{key(4096),routine({{99,254,255,{}}})}});
            unhandled.start(key(4096));odh::EntityVmScheduler strict;
            require(strict.add(38,unhandled)&&!strict.tick(1,1,1)&&
                    unhandled.status()==odh::VmStatus::fault,
                    "hosts without an explicit recovery policy still fail on unhandled faults");
        }
        {
            odh::World lot;
            odh::EntityVmScheduler objects(4096);
            constexpr odh::CodeOwner owner=0x80000000U;
            auto globals=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
            auto code=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4096),routine({{2,254,255,{25,0,0,0,0,5,4,8}}})}});
            std::vector<std::unique_ptr<odh::Vm>> threads;
            for(std::int16_t id=1;id<=180;++id) {
                require(lot.create(id,odh::EntityKind::object)&&lot.declare_object_word(id,25,0),
                    "create synthetic lot object with independent state");
                auto thread=std::make_unique<odh::Vm>(globals,odh::ShareVmProgramTag{},lot.vm_memory(),
                    odh::VmLimits{},odh::VmPrimitive{},odh::SharedRoutineOwners{{owner,code}},
                    odh::ShareVmOwnerProgramsTag{});
                thread->start(key(4096),{id,id,id,owner});thread->temps()[0]=id;
                require(objects.add(id,*thread),"schedule saved-lot-sized shared object bank");
                threads.push_back(std::move(thread));
            }
            require(objects.tick(1,90,1)&&objects.instructions_last_tick()==90&&
                    objects.tick(2,90,1)&&objects.instructions_last_tick()==90,
                    "bounded scheduler fairly executes all 180 objects across two ticks");
            for(std::int16_t id=1;id<=180;++id)
                require(lot.find(id)->object_words.at(25)==id&&
                        threads[static_cast<std::size_t>(id-1)]->status()==odh::VmStatus::returned_true&&
                        threads[static_cast<std::size_t>(id-1)]->steps()==1,
                        "shared code keeps each object's registers and World effects independent");
        }
        {
            odh::EntityVmScheduler phase;
            bool before_phase=false,primitive_phase=false,after_phase=false;
            odh::Vm action({{key(4096),routine({{99,254,255,{}}})}}, {}, {},
                [&](const auto&,const auto&,const auto&,const auto&){
                    primitive_phase=phase.running()&&phase.current_entity()==1;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                });
            action.start(key(4096));
            require(!phase.running()&&phase.add(1,action,[&](auto){before_phase=phase.running()&&phase.current_entity()==1;return true;},
                [&]{after_phase=phase.running()&&phase.current_entity()==1;return true;}),"scheduler starts at an external register-clone boundary");
            require(phase.tick(1,8,8)&&before_phase&&primitive_phase&&after_phase&&!phase.running(),
                "scheduler exposes active simulation phase inside hooks and BHAV then restores external mode");
            require(!phase.tick(2,0,8)&&!phase.running(),"rejected tick never enters shared-register mode");
        }
        { // Automatic queue skip during a real scheduler dispatch, with nonzero EP4.
            odh::World state;
            require(state.create(1,odh::EntityKind::avatar,{0,80,0,16})&&
                state.create(2,odh::EntityKind::object)&&state.declare_object_word(1,8,16)&&
                state.declare_object_word(2,25,0),"automatic skip entities initialized");
            odh::EntityVmScheduler dispatch;
            odh::InteractionQueue queue(state,1,8);
            odh::Vm callee({{key(4096),routine({{99,254,255,{}}})}},state.vm_memory(),{},
                [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
            callee.start(key(4096),{2,2,0,0});callee.run(1);
            callee.temps()[2]=19;callee.temp_xl()[1]=10;const auto callee_ticket=callee.pending()->ticket;
            unsigned callbacks=0;bool accepted=false,exact_context=false;
            queue.set_queue_skipped_handler([&](const odh::InteractionActionSnapshot& action){
                ++callbacks;
                const odh::RoutineSet ep4{{key(4100),routine({
                    {2,1,255,{3,0,2,0,0,5,8,8}},
                    {2,2,255,{2,0,78,0,0,5,8,7}},
                    {2,3,255,{1,0,77,0,0,5,42,7}},
                    {2,255,253,{25,0,9,0,0,5,3,7}}})}};
                odh::Vm check({},state.vm_memory(),{}, {},{{22,ep4}});
                check.start(key(4100),{2,2,1,22},{0,0,0,0});
                const auto result=check.run_synchronous_check_with_registers(callee,dispatch.running(),8);
                exact_context=action.call.context.callee==2&&check.temps()[3]==19;
                return result.started&&result.status==odh::VmStatus::returned_false;
            });
            odh::Vm actor({{key(4096),routine({{99,254,255,{}}})}},state.vm_memory(),{},
                [&](const auto&,const auto&,const auto&,const auto&){
                    accepted=queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,
                        odh::InteractionQueueMode::normal,0,false,false);
                    return odh::PrimitiveOutcome{accepted?odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::unsupported,{}};
                },{{22,{{key(4097),routine({set(1,33)})},{key(4098),routine({set(1,44)})}}}});
            actor.start(key(4096),{1,1,1,0});require(queue.bind(actor),"bind automatic skip thread");
            require(queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false),"automatic skip pivot queued");
            const auto pivot=queue.last_enqueued_uid();
            require(queue.enqueue_ts1(key(4098),{1,2,2,22},{},10,odh::InteractionQueueMode::normal,0,false,true),"automatic skip suffix queued");
            const auto suffix=queue.last_enqueued_uid();
            require(dispatch.add(1,actor)&&dispatch.tick(1,16,16)&&accepted&&callbacks==1&&exact_context&&
                queue.size()==2&&queue.contains_uid(pivot)&&!queue.contains_uid(suffix)&&
                callee.temps()[2]==78&&callee.temp_xl()[1]==77&&state.find(2)->object_words.at(25)==9&&
                callee.status()==odh::VmStatus::waiting&&callee.pending()->ticket==callee_ticket&&callee.steps()==1&&
                actor.steps()==1&&!dispatch.running(),
                "automatic skip invokes false-returning EP4 once inside tick, preserving its callee register/World effects without advancing callee Main");
        }
        odh::World world;
        require(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object)&&
                world.create(3,odh::EntityKind::avatar)&&world.declare_object_word(2,25,0),
                "shared World entities initialized");
        const auto main=routine({idle(),set(2,99)});
        const odh::Instruction action_write{2,254,255,{25,0,7,0,0,5,4,7}};
        auto program=std::make_shared<const odh::VmProgram>(
            odh::RoutineSet{{key(4096),main}});
        odh::EntityThreadControllers mover_thread(world,1),blocker_thread(world,3);
        auto& mover_queue=mover_thread.interactions();
        auto& blocker_queue=blocker_thread.interactions();
        odh::Vm mover(program,odh::ShareVmProgramTag{},world.vm_memory(),{},mover_thread.interaction_primitive());
        odh::Vm blocker(program,odh::ShareVmProgramTag{},world.vm_memory(),{},blocker_thread.interaction_primitive(),
            {{22,{{key(4097),routine({action_write})}}}});
        require(mover_thread.bind(mover)&&blocker_thread.bind(blocker),
                "per-entity controller bundles bind to distinct execution states");
        mover.start(key(4096),{1,1,1,0},{3});
        blocker.start(key(4096),{3,3,3,0},{3});
        require(blocker_queue.enqueue(key(4097),{3,2,2,22}),"blocker receives action in its own queue");

        odh::EntityVmScheduler scheduler(2);
        std::vector<int> phases;
        require(scheduler.add(1,mover,[&](auto tick){phases.push_back(100+static_cast<int>(tick));return mover_thread.advance(tick);},
            [&]{phases.push_back(201);return mover_thread.observe();})&&
            scheduler.add(3,blocker,[&](auto tick){phases.push_back(300+static_cast<int>(tick));return blocker_thread.advance(tick);},
            [&]{phases.push_back(203);return blocker_thread.observe();}),
            "bounded scheduler registers both avatar states");
        require(!scheduler.add(3,mover)&&scheduler.size()==2,"duplicate entity rejected without changing scheduler");
        const auto trace=[&](std::int16_t actor,const odh::VmTrace&){phases.push_back(static_cast<int>(actor));};
        require(scheduler.tick(1,4,2,trace),"tick one runs with aggregate and per-avatar budgets");
        require(scheduler.instructions_last_tick()<=4&&world.find(2)->object_words.at(25)==7,
                "blocker executes the shared-world action within the fixed tick budget");
        require(blocker_queue.completion()&&blocker_queue.completion()->result&&
                blocker_queue.completion()->tick==1&&mover_queue.size()==0,
                "blocker action completes independently on tick one");
        require(phases.size()>=6&&phases[0]==101&&phases[1]==301&&
                phases[2]==1&&phases[3]==201&&phases.back()==203,
                "all entity clocks advance before VMs run in ascending order with local observe hooks");
        require(scheduler.tick(2,4,2,trace)&&scheduler.instructions_last_tick()<=4,
                "same scheduler advances both states on the next consecutive tick");
        require(mover.status()==odh::VmStatus::waiting&&blocker.status()==odh::VmStatus::waiting,
                "empty per-avatar queues leave both Main routines waiting independently");
        require(!scheduler.tick(4,4,2,trace),"scheduler rejects skipped logical tick");
        require(scheduler.tick(3,4,2,trace),"rejected clock value does not consume the next valid tick");
        require(scheduler.remove(1)&&scheduler.size()==1,"entity state can be removed outside a tick");

        {
            odh::World immediate_world;
            require(immediate_world.create(1,odh::EntityKind::avatar)&&
                    immediate_world.create(2,odh::EntityKind::avatar),
                    "immediate scheduler fixture creates actor and action target");
            const odh::Instruction parent_sleep{0,1,255,{0,0,0,0,0,0,0,0}};
            const odh::Instruction parent_after{2,254,255,{1,0,77,0,0,5,8,7}};
            const odh::Instruction child_wait{99,1,255,{0,0,0,0,0,0,0,0}};
            const odh::Instruction child_return{98,254,255,{0,0,0,0,0,0,0,0}};
            auto immediate_program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4096),routine({parent_sleep,parent_after})},
                {key(4097),routine({child_wait,child_return})}});
            odh::EntityThreadControllers immediate_thread(immediate_world,1);
            bool child_first_wait=true;
            const auto make_primitive=[&](odh::EntityThreadControllers& thread,bool& first_wait){
                const auto sleep=thread.sleep_primitive();
                return odh::VmPrimitive{[&thread,&first_wait,sleep](const auto& frame,const auto& instruction,
                        const auto& temps,const auto& dispatch){
                    if(instruction.opcode==0)return sleep(frame,instruction,temps,dispatch);
                    if(instruction.opcode==99&&first_wait){
                        first_wait=false;
                        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
                    }
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                }};
            };
            odh::Vm immediate_vm(immediate_program,odh::ShareVmProgramTag{},immediate_world.vm_memory(),{},
                make_primitive(immediate_thread,child_first_wait));
            require(immediate_thread.bind(immediate_vm)&&
                    immediate_vm.start(key(4096),{1,1,1,0},{2})==odh::VmStatus::running,
                    "parent Main starts with a real controller Sleep instruction");
            odh::EntityVmScheduler immediate_scheduler(1);
            require(immediate_scheduler.add(1,immediate_vm,
                    [&](auto tick){return immediate_thread.advance(tick);},
                    [&]{return immediate_thread.observe();}),
                    "register immediate parent thread with staged controller hooks");
            require(immediate_scheduler.tick(1,3,3)&&immediate_vm.status()==odh::VmStatus::waiting&&
                    immediate_vm.pending()->instruction.instruction.opcode==0&&
                    immediate_thread.sleep().active()&&immediate_vm.immediate_continuations().empty()&&
                    immediate_scheduler.instructions_last_tick()==1,
                    "Main starts a timed Sleep at the first scheduled boundary");
            require(immediate_thread.interactions().enqueue_ts1(key(4097),{1,2,2,0},{},1,
                    odh::InteractionQueueMode::idle,4,false,false),
                    "RunImmediately action is enqueued through the TS1 flag-4 path");
            require(immediate_scheduler.tick(2,3,3)&&immediate_vm.status()==odh::VmStatus::waiting&&
                    immediate_vm.pending()->instruction.instruction.opcode==0&&
                    immediate_thread.interactions().size()==1&&immediate_vm.immediate_continuations().empty()&&
                    immediate_thread.sleep().active()&&immediate_scheduler.instructions_last_tick()==0,
                    "queued immediate action does not wake Sleep before its deadline");
            require(immediate_scheduler.tick(3,3,3)&&immediate_vm.status()==odh::VmStatus::waiting&&
                    immediate_vm.pending()->instruction.instruction.opcode==99&&
                    immediate_vm.immediate_continuations().size()==1&&
                    immediate_vm.immediate_continuations().back().parent_pending&&
                    immediate_vm.immediate_continuations().back().parent_pending->instruction.instruction.opcode==0&&
                    !immediate_thread.sleep().active()&&immediate_thread.snapshot().schedule_idle_start==1&&
                    immediate_scheduler.instructions_last_tick()==1,
                    "due Sleep injects child, parks parent state, and child can wait independently");

            const auto saved_vm=immediate_vm.snapshot();
            const auto saved_controllers=immediate_thread.snapshot();
            const auto saved_scheduler=immediate_scheduler.snapshot();
            odh::World restored_world;
            require(restored_world.restore_snapshot(immediate_world.snapshot()),
                    "restore independent World for parked parent continuation");
            odh::EntityThreadControllers restored_thread(restored_world,1);
            bool restored_child_wait=false;
            odh::Vm restored_vm(immediate_program,odh::ShareVmProgramTag{},restored_world.vm_memory(),{},
                make_primitive(restored_thread,restored_child_wait));
            require(restored_vm.restore_snapshot(saved_vm)&&restored_thread.bind(restored_vm)&&
                    restored_thread.restore_snapshot(saved_controllers),
                    "restore VM and shared controllers while immediate child is waiting");
            auto missing_sleep_actor=saved_controllers;
            missing_sleep_actor.sleep.actor.reset();
            odh::EntityThreadControllers rejected_thread(restored_world,1);
            odh::Vm rejected_vm(immediate_program,odh::ShareVmProgramTag{},restored_world.vm_memory(),{},
                make_primitive(rejected_thread,restored_child_wait));
            require(rejected_vm.restore_snapshot(saved_vm)&&rejected_thread.bind(rejected_vm)&&
                    !rejected_thread.restore_snapshot(missing_sleep_actor),
                    "parked opcode-0 parent cannot restore without its Sleep actor identity");
            odh::EntityVmScheduler restored_scheduler(1);
            require(restored_scheduler.add(1,restored_vm,
                    [&](auto tick){return restored_thread.advance(tick);},
                    [&]{return restored_thread.observe();})&&
                    restored_scheduler.restore_snapshot(saved_scheduler)&&
                    restored_vm.complete(restored_vm.pending()->ticket,true),
                    "restored child completion is admitted with original scheduler clock and budget state");
            const auto before_resume=restored_vm.steps();
            require(restored_scheduler.tick(4,3,3)&&restored_vm.steps()-before_resume==3&&
                    restored_scheduler.instructions_last_tick()==3&&
                    restored_vm.status()==odh::VmStatus::returned_true&&restored_vm.temps()[1]==77&&
                    restored_vm.immediate_continuations().empty()&&!restored_thread.sleep().active(),
                    "child returns and parent retries Sleep then continues in the same bounded slice after restore");
        }

        {
            odh::World source_world;
            require(source_world.create(1,odh::EntityKind::avatar),
                    "composed playback fixture creates its actor");
            auto clip=std::make_shared<odh::AnimationHeader>();
            clip->name="immediate-parent";
            odh::BoneMotion motion{};motion.frames=8;
            motion.time_properties={{{20,{{"xevt","7"}}}}};
            clip->motions.push_back(motion);
            const odh::Instruction parent_anim{44,254,1,{1,0,0,0,0,0,1,0}};
            const odh::Instruction parent_done{2,254,255,{1,0,88,0,0,5,8,7}};
            const odh::Instruction child_async{99,1,255,{0,0,0,0,0,0,0,0}};
            const odh::Instruction child_return{98,254,255,{0,0,0,0,0,0,0,0}};
            auto program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4096),routine({parent_anim,parent_done})},
                {key(4097),routine({child_async,child_return})}});

            const auto run_graph=[&](odh::World& world,
                    odh::EntityThreadControllers& controllers,odh::Playback& playback,
                    bool& first_child_wait,std::vector<int>& order) {
                const auto interactions=controllers.interaction_primitive();
                const auto anim=playback.primitive();
                const auto sleep=controllers.sleep_primitive();
                odh::VmPrimitive primitive=[&,interactions,anim,sleep](const auto& frame,const auto& instruction,
                        const auto& temps,const auto& dispatch) mutable {
                    if(instruction.opcode==44){order.push_back(44);return anim(frame,instruction,temps,dispatch);}
                    if(instruction.opcode==0)return sleep(frame,instruction,temps,dispatch);
                    if(instruction.opcode==99&&first_child_wait) {
                        first_child_wait=false;order.push_back(99);
                        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
                    }
                    if(instruction.opcode==98){order.push_back(98);return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};}
                    return interactions(frame,instruction,temps,dispatch);
                };
                (void)world;
                return primitive;
            };

            odh::EntityThreadControllers source_thread(source_world,1);
            bool source_child_wait=true;std::vector<int> source_order;
            odh::Playback source_playback(source_world,[&](const auto&,auto,auto){return clip;});
            odh::Vm source_vm(program,odh::ShareVmProgramTag{},source_world.vm_memory(),{},
                run_graph(source_world,source_thread,source_playback,
                          source_child_wait,source_order));
            require(source_thread.bind(source_vm)&&source_playback.bind(source_vm)&&
                source_thread.set_immediate_service({
                    [&](const auto& pending,const auto& frame){return source_playback.can_park_for_immediate(pending,frame);},
                    [&](auto ticket){return source_playback.park_for_immediate(ticket);},
                    [&](auto ticket){return source_playback.retry_immediate_parent(ticket);},
                    [&](const auto& pending,const auto& frame){return source_playback.validates_immediate_parent(pending,frame);}}),
                "bind actual Playback as the opcode-44 immediate-parent service");
            require(source_vm.start(key(4096),{1,1,1,0})==odh::VmStatus::running&&
                    source_vm.run(1)==odh::VmStatus::waiting&&source_vm.pending()&&
                    source_vm.pending()->instruction.instruction.opcode==44,
                "parent enters a real Playback event wait before an immediate action arrives");
            require(source_thread.interactions().enqueue_ts1(key(4097),{1,1,1,0},{},1,
                    odh::InteractionQueueMode::idle,4,false,false),
                "enqueue immediate child through the actual TS1 flags-4 queue path");

            odh::EntityVmScheduler source_scheduler(1);std::vector<int> trace;
            require(source_scheduler.add(1,source_vm,[&](auto tick){return source_thread.before_run(tick);},
                    [&]{return source_thread.observe();}),"register parent for per-entity service ordering");
            const auto tick_entity=[&](odh::Playback& player,std::vector<int>& sequence){
                return [&player,&sequence](std::int16_t,std::uint64_t tick,bool){
                    sequence.push_back(400+static_cast<int>(tick));
                    return player.advance(tick,[&](const auto&){sequence.push_back(500+static_cast<int>(tick));});
                };
            };
            odh::PlaybackSnapshot initial_playback;
            require(source_scheduler.tick(1,4,4,[&](auto,const auto& item){trace.push_back(item.instruction.opcode);},
                    {},tick_entity(source_playback,source_order),odh::EntityVmScheduler::ControllerOrder::per_entity)&&
                    source_vm.pending()&&source_vm.pending()->instruction.instruction.opcode==99&&
                    source_vm.immediate_continuations().size()==1&&
                    source_vm.immediate_continuations().back().parent_pending&&
                    source_vm.immediate_continuations().back().parent_pending->instruction.instruction.opcode==44&&
                    source_playback.capture_snapshot(initial_playback)&&initial_playback.state&&
                    !initial_playback.state->wait&&
                    source_playback.progress()&&source_playback.progress()->queued_events==1&&
                    source_scheduler.instructions_last_tick()==1&&
                    source_order==std::vector<int>({44,99,401,501}),
                "immediate selection parks opcode 44 before service, then advances its shared clip after child waits");

            const auto saved_vm=source_vm.snapshot();
            const auto saved_controllers=source_thread.snapshot();
            const auto saved_playback=[&]{odh::PlaybackSnapshot value;std::string why;
                require(source_playback.capture_snapshot(value,&why),"capture shared Playback while child is waiting");
                return value;}();
            const auto saved_scheduler=source_scheduler.snapshot();
            odh::World resumed_world;
            require(resumed_world.restore_snapshot(source_world.snapshot()),"restore World for composed animation continuation");
            odh::EntityThreadControllers resumed_thread(resumed_world,1);
            bool resumed_child_wait=false;std::vector<int> resumed_order;
            odh::Playback resumed_playback(resumed_world,[&](const auto&,auto,auto){return clip;});
            odh::Vm resumed_vm(program,odh::ShareVmProgramTag{},resumed_world.vm_memory(),{},
                run_graph(resumed_world,resumed_thread,resumed_playback,
                          resumed_child_wait,resumed_order));
            require(resumed_vm.restore_snapshot(saved_vm)&&resumed_thread.bind(resumed_vm)&&
                    resumed_playback.restore_snapshot(saved_playback,resumed_vm,
                        [&](const auto& resource){return resource.kind==odh::PlaybackResourceKey::Kind::animation&&
                            resource.id==1?clip:std::shared_ptr<const odh::AnimationHeader>{};})&&
                    resumed_thread.set_immediate_service({
                        [&](const auto& pending,const auto& frame){return resumed_playback.can_park_for_immediate(pending,frame);},
                        [&](auto ticket){return resumed_playback.park_for_immediate(ticket);},
                        [&](auto ticket){return resumed_playback.retry_immediate_parent(ticket);},
                        [&](const auto& pending,const auto& frame){return resumed_playback.validates_immediate_parent(pending,frame);}})&&
                    resumed_thread.restore_snapshot(saved_controllers),
                "restore the shared animation, child wait, and validated parked parent into a fresh graph");
            odh::EntityVmScheduler resumed_scheduler(1);
            require(resumed_scheduler.add(1,resumed_vm,[&](auto tick){return resumed_thread.before_run(tick);},
                    [&]{return resumed_thread.observe();})&&resumed_scheduler.restore_snapshot(saved_scheduler)&&
                    resumed_vm.complete(resumed_vm.pending()->ticket,true),
                "restore scheduler and complete the asynchronous child without replacing playback state");
            const auto prior_steps=resumed_vm.steps();
            std::vector<std::int16_t> resumed_parent_args_at_done;
            const bool resumed_tick_ok=resumed_scheduler.tick(2,3,3,
                [&](auto,const auto& item){
                    trace.push_back(item.instruction.opcode);
                    if(item.key.scope==key(4096).scope&&item.key.id==4096&&item.instruction.opcode==2)
                        resumed_parent_args_at_done.push_back(resumed_vm.stack().back().arguments.at(0));
                },
                {},tick_entity(resumed_playback,resumed_order),odh::EntityVmScheduler::ControllerOrder::per_entity);
            const bool resumed_state_ok=resumed_vm.status()==odh::VmStatus::returned_true&&
                resumed_vm.steps()-prior_steps==3&&resumed_scheduler.instructions_last_tick()==3&&
                resumed_vm.stack().empty()&&resumed_parent_args_at_done==std::vector<std::int16_t>{7}&&
                resumed_vm.temps()[1]==88&&resumed_vm.immediate_continuations().empty()&&
                resumed_order==std::vector<int>({98,44,402});
            if(!resumed_tick_ok||!resumed_state_ok) {
                std::cerr<<"composed playback diagnostic: tick="<<resumed_tick_ok
                    <<" status="<<odh::vm_status_name(resumed_vm.status())
                    <<" steps="<<(resumed_vm.steps()-prior_steps)
                    <<" charged="<<resumed_scheduler.instructions_last_tick()
                    <<" stack="<<resumed_vm.stack().size();
                std::cerr<<" temp1="<<resumed_vm.temps()[1]
                    <<" continuations="<<resumed_vm.immediate_continuations().size()
                    <<" order=";
                for(const auto value:resumed_order)std::cerr<<value<<',';
                std::cerr<<" pending=";
                if(resumed_vm.pending())std::cerr<<static_cast<unsigned>(resumed_vm.pending()->instruction.instruction.opcode);
                std::cerr<<" diag="<<resumed_scheduler.diagnostic()<<'\n';
            }
            require(resumed_tick_ok&&resumed_state_ok,
                "restored child returns, parent consumes the current shared event, and Main continues within the same budget");
        }

        odh::World controller_world;
        require(controller_world.create(1,odh::EntityKind::avatar)&&
                controller_world.create(3,odh::EntityKind::avatar),
                "independent controller clocks have separate World actors");
        const odh::Instruction sleep_instruction{0,1,255,{0,0,0,0,0,0,0,0}};
        auto sleep_program=std::make_shared<const odh::VmProgram>(
            odh::RoutineSet{{key(4096),routine({sleep_instruction,set(0,1)})}});
        odh::EntityThreadControllers sleeping_one(controller_world,1),sleeping_two(controller_world,3);
        odh::Vm sleeping_vm_one(sleep_program,odh::ShareVmProgramTag{},controller_world.vm_memory(),{},
                                sleeping_one.sleep_primitive());
        odh::Vm sleeping_vm_two(sleep_program,odh::ShareVmProgramTag{},controller_world.vm_memory(),{},
                                sleeping_two.sleep_primitive());
        require(sleeping_one.bind(sleeping_vm_one)&&sleeping_two.bind(sleeping_vm_two),
                "each thread bundle binds its own sleep and interaction controllers");
        sleeping_vm_one.start(key(4096),{1,1,1,0},{4});
        sleeping_vm_two.start(key(4096),{3,3,3,0},{4});
        require(sleeping_vm_one.run(1)==odh::VmStatus::waiting&&
                sleeping_vm_two.run(1)==odh::VmStatus::waiting,
                "both independent VM states enter their own Sleep wait");
        require(sleeping_one.advance(1)&&sleeping_one.sleep().clock()==1&&
                sleeping_two.sleep().clock()==0,
                "advancing one thread leaves the other thread clock untouched");

        {
            odh::World notify_world;
            require(notify_world.create(1,odh::EntityKind::avatar)&&
                    notify_world.create(3,odh::EntityKind::avatar),
                    "higher-ID notification fixture creates two actors");
            const odh::Instruction notify_instruction{49,254,255,{0,0,0,0,0,0,0,0}};
            auto notify_program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4096),routine({notify_instruction})},
                {key(4097),routine({sleep_instruction,set(0,7)})}});
            odh::EntityThreadControllers target_thread(notify_world,3);
            odh::EntityVmScheduler notify_scheduler(2);
            const auto notify=odh::notify_idle_primitive(notify_world,
                [&](std::int16_t id){return notify_scheduler.interrupt(id);});
            odh::Vm sender(notify_program,odh::ShareVmProgramTag{},notify_world.vm_memory(),{},notify);
            odh::Vm target(notify_program,odh::ShareVmProgramTag{},notify_world.vm_memory(),{},
                           target_thread.sleep_primitive());
            require(target_thread.bind(target),"higher-ID target binds its own wait controllers");
            sender.start(key(4096),{1,1,3,0},{0,0,0,0});
            target.start(key(4097),{3,3,3,0},{10});
            require(target.run(1)==odh::VmStatus::waiting,"higher-ID target enters Sleep before the tick");
            std::uint64_t steps_when_notified=0;
            require(notify_scheduler.add(1,sender)&&notify_scheduler.add(3,target,
                        [&](auto tick){return target_thread.advance(tick);},{},
                        [&]{const auto accepted=target_thread.notify();steps_when_notified=target.steps();return accepted;}),
                    "higher-ID target registers an interrupt handler");
            std::vector<std::int16_t> notified_trace;
            require(notify_scheduler.tick(1,8,4,[&](auto actor,const auto&){notified_trace.push_back(actor);},{},{},odh::EntityVmScheduler::ControllerOrder::per_entity)&&
                    std::find(notified_trace.begin(),notified_trace.end(),3)!=notified_trace.end()&&
                    steps_when_notified==1&&target.steps()>1&&target.steps()<=5&&target.status()!=odh::VmStatus::waiting,
                    "Notify releases a later Sleep wait, then its instruction runs within the same-tick budget");
        }
        {
            odh::World notify_world;
            require(notify_world.create(1,odh::EntityKind::avatar)&&
                    notify_world.create(3,odh::EntityKind::avatar),
                    "lower-ID notification fixture creates two actors");
            const odh::Instruction notify_instruction{49,254,255,{0,0,0,0,0,0,0,0}};
            auto notify_program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4096),routine({notify_instruction})},
                {key(4097),routine({sleep_instruction,set(0,7)})}});
            odh::EntityThreadControllers target_thread(notify_world,1);
            odh::EntityVmScheduler notify_scheduler(2);
            const auto notify=odh::notify_idle_primitive(notify_world,
                [&](std::int16_t id){return notify_scheduler.interrupt(id);});
            odh::Vm target(notify_program,odh::ShareVmProgramTag{},notify_world.vm_memory(),{},
                           target_thread.sleep_primitive());
            odh::Vm sender(notify_program,odh::ShareVmProgramTag{},notify_world.vm_memory(),{},notify);
            require(target_thread.bind(target),"lower-ID target binds its own wait controllers");
            target.start(key(4097),{1,1,1,0},{10});
            sender.start(key(4096),{3,3,1,0},{0,0,0,0});
            require(target.run(1)==odh::VmStatus::waiting,"lower-ID target enters Sleep before the tick");
            require(notify_scheduler.add(1,target,[&](auto tick){return target_thread.advance(tick);},{},
                        [&]{return target_thread.notify();})&&notify_scheduler.add(3,sender),
                    "lower-ID target registers before its sender");
            std::vector<std::int16_t> first_trace,next_trace;
            require(notify_scheduler.tick(1,8,4,[&](auto actor,const auto&){first_trace.push_back(actor);},{},{},odh::EntityVmScheduler::ControllerOrder::per_entity)&&
                    first_trace==std::vector<std::int16_t>{3}&&target.status()==odh::VmStatus::waiting&&target.steps()==1,
                    "Notify cannot run an earlier-ID thread after its turn has passed");
            require(notify_scheduler.tick(2,8,4,[&](auto actor,const auto&){next_trace.push_back(actor);},{},{},odh::EntityVmScheduler::ControllerOrder::per_entity)&&
                    std::find(next_trace.begin(),next_trace.end(),1)!=next_trace.end()&&target.steps()>1,
                    "an interrupted earlier-ID thread runs on the next tick");
        }
        {
            odh::World notify_world;
            require(notify_world.create(2,odh::EntityKind::object),
                    "self-notification fixture creates its object");
            const odh::Instruction notify_instruction{49,1,255,{0,0,0,0,0,0,0,0}};
            const odh::Instruction sleep_to_set{0,2,255,{0,0,0,0,0,0,0,0}};
            auto notify_program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4096),routine({notify_instruction,sleep_to_set,set(0,7)})}});
            odh::Sleep object_sleep(notify_world);
            odh::EntityVmScheduler notify_scheduler(1);
            bool in_notify_primitive=false,callback_reentered=false;
            odh::VmPrimitive notify;
            notify=odh::notify_idle_primitive(notify_world,
                [&](std::int16_t id){return notify_scheduler.interrupt(id);});
            odh::Vm object(notify_program,odh::ShareVmProgramTag{},notify_world.vm_memory(),{},
                [&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
                    if(instruction.opcode==49){
                        in_notify_primitive=true;
                        const auto result=notify(frame,instruction,temps,dispatch);
                        in_notify_primitive=false;
                        return result;
                    }
                    if(instruction.opcode==0)
                        return object_sleep.primitive()(frame,instruction,temps,dispatch);
                    return odh::PrimitiveOutcome{};
                });
            require(object_sleep.bind(object),"self-notification object binds its sleep controller");
            object.start(key(4096),{2,2,2,0},{10});
            require(notify_scheduler.add(2,object,[&](auto tick){return object_sleep.tick(tick);},{},
                        [&]{callback_reentered=callback_reentered||in_notify_primitive;return object_sleep.interrupt();}),
                    "self-notification object registers its scheduler callback");
            require(notify_scheduler.tick(1,8,8)&&!callback_reentered&&
                    object.status()==odh::VmStatus::running&&object.steps()==2,
                    "self Notify waits until the Main returns to a scheduler safe point");
            require(notify_scheduler.tick(2,8,8)&&object.status()==odh::VmStatus::returned_true&&
                    object.steps()==3,
                    "deferred self Notify wakes Sleep once and the Main continues on its next turn");
        }
        {
            odh::World notify_world;
            require(notify_world.create(1,odh::EntityKind::avatar)&&
                    notify_world.create(3,odh::EntityKind::avatar),
                    "queue notification fixture creates two actors");
            const odh::Instruction notify_instruction{49,254,255,{0,0,0,0,0,0,0,0}};
            const odh::Instruction queue_idle{17,254,255,{0,0,0,0,0,0,0,0}};
            auto notify_program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4096),routine({notify_instruction})},
                {key(4097),routine({queue_idle,set(0,7)})}});
            odh::EntityThreadControllers target_thread(notify_world,3);
            odh::EntityVmScheduler notify_scheduler(2);
            const auto notify=odh::notify_idle_primitive(notify_world,
                [&](std::int16_t id){return notify_scheduler.interrupt(id);});
            odh::Vm sender(notify_program,odh::ShareVmProgramTag{},notify_world.vm_memory(),{},notify);
            odh::Vm target(notify_program,odh::ShareVmProgramTag{},notify_world.vm_memory(),{},
                           target_thread.interaction_primitive());
            require(target_thread.bind(target),"queue target binds its own interaction controller");
            sender.start(key(4096),{1,1,3,0},{0,0,0,0});
            target.start(key(4097),{3,3,3,0},{10});
            require(target.run(1)==odh::VmStatus::waiting,"queue target enters IdleForInput before the tick");
            require(notify_scheduler.add(1,sender)&&notify_scheduler.add(3,target,
                        [&](auto tick){return target_thread.advance(tick);},{},
                        [&]{return target_thread.notify();}),
                    "queue target registers an interrupt handler");
            std::vector<std::int16_t> notified_trace;
            require(notify_scheduler.tick(1,8,4,[&](auto actor,const auto&){notified_trace.push_back(actor);},{},{},odh::EntityVmScheduler::ControllerOrder::per_entity)&&
                    std::find(notified_trace.begin(),notified_trace.end(),3)!=notified_trace.end()&&
                    target.status()!=odh::VmStatus::waiting,
                    "Notify releases a later-ID IdleForInput wait in the same tick");
        }
        {
            odh::World notify_world;
            require(notify_world.create(1,odh::EntityKind::avatar)&&
                    notify_world.create(2,odh::EntityKind::object)&&
                    notify_world.create(3,odh::EntityKind::avatar)&&
                    notify_world.declare_object_word(3,8,0),
                    "queued-idle Notify fixture creates the actor and target");
            const odh::Instruction queued_idle{17,1,255,{0,0,0,0,0,0,0,0}};
            const odh::Instruction sleep_next{0,2,255,{0,0,0,0,0,0,0,0}};
            auto notify_program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4096),routine({queued_idle,sleep_next,set(0,7)})}});
            odh::EntityThreadControllers target_thread(notify_world,3);
            odh::Vm target(notify_program,odh::ShareVmProgramTag{},notify_world.vm_memory(),{},
                [&](const auto& frame,const auto& instruction,const auto& trace,const auto& dispatch) {
                    if(instruction.opcode==17)return target_thread.interaction_primitive()(frame,instruction,trace,dispatch);
                    if(instruction.opcode==0)return target_thread.sleep_primitive()(frame,instruction,trace,dispatch);
                    return odh::PrimitiveOutcome{};
                },{{2,{{key(4097),routine({set(0,4)})}}}});
            require(target_thread.bind(target),"queued-idle target binds its shared thread interrupt");
            target.start(key(4096),{3,3,3,0},{10});
            require(target_thread.interactions().enqueue(key(4097),{3,2,2,2}),
                    "queued-idle actor gets pending work before it starts");
            require(target_thread.notify()&&target_thread.thread_interrupted(),
                    "Notify before the first wait records one thread-level interrupt");
            require(target.run(8)==odh::VmStatus::returned_true&&target.steps()==3&&
                    !target_thread.thread_interrupted(),
                    "queued IdleForInput leaves the signal for the following Sleep to consume");
        }
        {
            odh::World notify_world;
            require(notify_world.create(1,odh::EntityKind::avatar)&&
                    notify_world.create(3,odh::EntityKind::avatar),
                    "repeated-notify fixture creates two actors");
            const odh::Instruction notify_twice{49,254,255,{0,0,0,0,0,0,0,0}};
            auto notify_program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4096),routine({notify_twice,notify_twice})},
                {key(4097),routine({sleep_instruction,sleep_instruction,set(0,7)})}});
            odh::EntityThreadControllers target_thread(notify_world,1);
            odh::EntityVmScheduler notify_scheduler(2);
            const auto notify=odh::notify_idle_primitive(notify_world,
                [&](std::int16_t id){return notify_scheduler.interrupt(id);});
            odh::Vm target(notify_program,odh::ShareVmProgramTag{},notify_world.vm_memory(),{},
                           target_thread.sleep_primitive());
            odh::Vm sender(notify_program,odh::ShareVmProgramTag{},notify_world.vm_memory(),{},notify);
            require(target_thread.bind(target),"repeated-notify target binds controllers");
            target.start(key(4097),{1,1,1,0},{10});
            sender.start(key(4096),{3,3,1,0},{0,0,0,0});
            require(target.run(1)==odh::VmStatus::waiting&&
                    notify_scheduler.add(1,target,[&](auto tick){return target_thread.advance(tick);},{},
                        [&]{return target_thread.notify();})&&notify_scheduler.add(3,sender),
                    "repeated-notify target waits before sender joins scheduler");
            require(notify_scheduler.tick(1,8,4,{},{},{},odh::EntityVmScheduler::ControllerOrder::per_entity)&&
                    notify_scheduler.tick(2,8,4,{},{},{},odh::EntityVmScheduler::ControllerOrder::per_entity)&&
                    target.steps()==3&&target.status()==odh::VmStatus::waiting&&
                    !target_thread.thread_interrupted(),
                    "two Notify calls coalesce to one Sleep wake; the next Sleep still waits");
        }

        odh::World fairness_world;
        auto fairness_program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4096),routine({set(2,99),set(2,100)})}});
        odh::Vm first(fairness_program,odh::ShareVmProgramTag{},fairness_world.vm_memory());
        odh::Vm second(fairness_program,odh::ShareVmProgramTag{},fairness_world.vm_memory());
        odh::Vm third(fairness_program,odh::ShareVmProgramTag{},fairness_world.vm_memory());
        first.start(key(4096),{1,1,1,0},{3});
        second.start(key(4096),{2,2,2,0},{3});
        third.start(key(4096),{3,3,3,0},{3});
        odh::EntityVmScheduler fair_scheduler(3);
        require(fair_scheduler.add(1,first)&&fair_scheduler.add(2,second)&&fair_scheduler.add(3,third),
                "round-robin fixture registers three entities");
        std::vector<std::int16_t> first_tick_order,second_tick_order,third_tick_order;
        require(fair_scheduler.tick(1,1,1,[&](auto actor,const auto&){first_tick_order.push_back(actor);})&&
                first_tick_order==std::vector<std::int16_t>{1},"first entity receives the initial single-instruction slice");
        require(fair_scheduler.remove(1),"entity ahead of the round-robin cursor can be removed");
        require(fair_scheduler.tick(2,1,1,[&](auto actor,const auto&){second_tick_order.push_back(actor);})&&
                second_tick_order==std::vector<std::int16_t>{2},
                "removing the previous recipient advances the fair grant to the next entity");
        require(fair_scheduler.tick(3,1,1,[&](auto actor,const auto&){third_tick_order.push_back(actor);})&&
                third_tick_order==std::vector<std::int16_t>{3},
                "a one-instruction budget eventually grants every entity a turn");

        odh::World ordered_world;
        require(ordered_world.create(1,odh::EntityKind::avatar)&&
                ordered_world.create(2,odh::EntityKind::object)&&
                ordered_world.create(3,odh::EntityKind::avatar)&&
                ordered_world.declare_object_word(2,25,0),
                "ordered-turn fixture creates a shared object");
        odh::Vm writer(program,odh::ShareVmProgramTag{},ordered_world.vm_memory(),{}, {},
            {{22,{{key(4097),routine({action_write})}}}});
        odh::Vm observer(fairness_program,odh::ShareVmProgramTag{},ordered_world.vm_memory());
        writer.start(key(4097),{1,2,2,22},{3});
        observer.start(key(4096),{3,3,3,0},{3});
        odh::EntityVmScheduler ordered_scheduler(2);
        bool saw_earlier_write=false;
        require(ordered_scheduler.add(1,writer)&&ordered_scheduler.add(3,observer),
                "ordered-turn fixture registers writer before observer");
        require(ordered_scheduler.tick(1,2,1,[&](auto actor,const auto&){
                    if(actor==3)saw_earlier_write=ordered_world.find(2)->object_words.at(25)==7;
                })&&saw_earlier_write,
                "later VM sees the earlier entity's shared-world change in the same tick");

        odh::Vm dormant_one(fairness_program,odh::ShareVmProgramTag{},fairness_world.vm_memory());
        odh::Vm dormant_two(fairness_program,odh::ShareVmProgramTag{},fairness_world.vm_memory());
        odh::Vm active_last(fairness_program,odh::ShareVmProgramTag{},fairness_world.vm_memory());
        dormant_one.start(key(4096),{1,1,1,0},{3});
        dormant_two.start(key(4096),{2,2,2,0},{3});
        active_last.start(key(4096),{3,3,3,0},{3});
        require(dormant_one.run(4)==odh::VmStatus::returned_true&&
                dormant_two.run(4)==odh::VmStatus::returned_true,
                "inactive entities finish their synthetic Main routines");
        odh::EntityVmScheduler work_conserving_scheduler(3);
        std::vector<std::int16_t> active_trace;
        require(work_conserving_scheduler.add(1,dormant_one)&&
                work_conserving_scheduler.add(2,dormant_two)&&
                work_conserving_scheduler.add(3,active_last)&&
                work_conserving_scheduler.tick(1,1,1,[&](auto actor,const auto&){active_trace.push_back(actor);})&&
                active_trace==std::vector<std::int16_t>{3},
                "inactive entities do not consume the only instruction grant");

        odh::Vm exception_vm(program,odh::ShareVmProgramTag{},fairness_world.vm_memory());
        exception_vm.start(key(4096),{4,4,4,0},{3});
        odh::EntityVmScheduler exception_scheduler(1);
        int callback_effects=0;
        require(exception_scheduler.add(4,exception_vm,[&](auto){++callback_effects;throw std::runtime_error("fixture failure");return true;}),
                "exception fixture registers");
        require(!exception_scheduler.tick(1,1,1)&&callback_effects==1&&!exception_scheduler.current_entity(),
                "throwing callback faults the scheduler after its partial effect");
        require(!exception_scheduler.tick(1,1,1)&&callback_effects==1&&
                exception_scheduler.diagnostic().find("fixture failure")!=std::string::npos,
                "faulted scheduler refuses to repeat a partially applied tick");
        std::cout<<checks<<" entity scheduler checks passed (synthetic fixtures only)\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
