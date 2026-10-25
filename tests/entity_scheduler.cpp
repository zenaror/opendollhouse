#include "odh/entity_scheduler.hpp"
#include "odh/entity_thread_controllers.hpp"
#include "odh/interaction.hpp"
#include "odh/notify.hpp"
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
}

int main() {
    try {
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
            require(notify_scheduler.tick(1,8,4,[&](auto actor,const auto&){notified_trace.push_back(actor);})&&
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
            require(notify_scheduler.tick(1,8,4,[&](auto actor,const auto&){first_trace.push_back(actor);})&&
                    first_trace==std::vector<std::int16_t>{3}&&target.status()!=odh::VmStatus::waiting&&target.steps()==1,
                    "Notify cannot run an earlier-ID thread after its turn has passed");
            require(notify_scheduler.tick(2,8,4,[&](auto actor,const auto&){next_trace.push_back(actor);})&&
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
            require(notify_scheduler.tick(1,8,4,[&](auto actor,const auto&){notified_trace.push_back(actor);})&&
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
            require(notify_scheduler.tick(1,8,4)&&notify_scheduler.tick(2,8,4)&&
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
        require(!exception_scheduler.tick(1,1,1)&&callback_effects==1,
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
