#include "odh/entity_scheduler.hpp"
#include "odh/interaction.hpp"
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
        odh::InteractionQueue mover_queue(world,1),blocker_queue(world,3);
        odh::Vm mover(program,odh::ShareVmProgramTag{},world.vm_memory(),{},mover_queue.primitive());
        odh::Vm blocker(program,odh::ShareVmProgramTag{},world.vm_memory(),{},blocker_queue.primitive(),
            {{22,{{key(4097),routine({action_write})}}}});
        require(mover_queue.bind(mover)&&blocker_queue.bind(blocker),"per-avatar queues bind to distinct execution states");
        mover.start(key(4096),{1,1,1,0},{3});
        blocker.start(key(4096),{3,3,3,0},{3});
        require(blocker_queue.enqueue(key(4097),{3,2,2,22}),"blocker receives action in its own queue");

        odh::EntityVmScheduler scheduler(2);
        std::vector<int> phases;
        require(scheduler.add(1,mover,[&](auto tick){phases.push_back(100+static_cast<int>(tick));return mover_queue.tick(tick);},
            [&]{phases.push_back(201);return mover_queue.observe();})&&
            scheduler.add(3,blocker,[&](auto tick){phases.push_back(300+static_cast<int>(tick));return blocker_queue.tick(tick);},
            [&]{phases.push_back(203);return blocker_queue.observe();}),
            "bounded scheduler registers both avatar states");
        require(!scheduler.add(3,mover)&&scheduler.size()==2,"duplicate entity rejected without changing scheduler");
        const auto trace=[&](std::int16_t actor,const odh::VmTrace&){phases.push_back(static_cast<int>(actor));};
        require(scheduler.tick(1,4,2,trace),"tick one runs with aggregate and per-avatar budgets");
        require(scheduler.instructions_last_tick()<=4&&world.find(2)->object_words.at(25)==7,
                "blocker executes the shared-world action within the fixed tick budget");
        require(blocker_queue.completion()&&blocker_queue.completion()->result&&
                blocker_queue.completion()->tick==1&&mover_queue.size()==0,
                "blocker action completes independently on tick one");
        require(phases.size()>=6&&phases[0]==101&&phases[1]==301&&phases[phases.size()-2]==201&&phases.back()==203,
                "all entity controllers advance before VM work and observe after it");
        require(scheduler.tick(2,4,2,trace)&&scheduler.instructions_last_tick()<=4,
                "same scheduler advances both states on the next consecutive tick");
        require(mover.status()==odh::VmStatus::waiting&&blocker.status()==odh::VmStatus::waiting,
                "empty per-avatar queues leave both Main routines waiting independently");
        require(!scheduler.tick(4,4,2,trace),"scheduler rejects skipped logical tick");
        require(scheduler.tick(3,4,2,trace),"rejected clock value does not consume the next valid tick");
        require(scheduler.remove(1)&&scheduler.size()==1,"entity state can be removed outside a tick");

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
        std::vector<std::int16_t> first_tick_order,second_tick_order;
        require(fair_scheduler.tick(1,1,1,[&](auto actor,const auto&){first_tick_order.push_back(actor);})&&
                first_tick_order==std::vector<std::int16_t>{1},"first entity receives the initial single-instruction slice");
        require(fair_scheduler.remove(1),"entity ahead of the round-robin cursor can be removed");
        require(fair_scheduler.tick(2,1,1,[&](auto actor,const auto&){second_tick_order.push_back(actor);})&&
                second_tick_order==std::vector<std::int16_t>{2},
                "removing an earlier ID preserves the next entity in the round-robin order");

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
