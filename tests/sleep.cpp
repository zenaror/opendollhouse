#include "odh/sleep.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
const auto key=odh::routine_key(4096);
odh::RoutineSet program(std::array<std::uint8_t,8> operands={}) {
    return {{key,odh::Bhav{0x8002,{{0,1,255,operands},{2,254,255,{0,0,0,0,0,5,8,9}}},{},
                          odh::Bhav::Metadata{0,4,0,0}}}};
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
int main() {
    try {
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
         require(f.world.find(1)->generation!=old && !f.sleep.interrupt() && !f.sleep.active() &&
                 f.vm.status()==odh::VmStatus::returned_false && f.rng==0 && !f.sleep.interrupt(),
                 "reused entity cancels old ticket and repeated interrupt cannot adopt replacement");
         f.start(20);require(f.sleep.active() && f.count()==19,"restart can explicitly register replacement generation");}
        {Fixture f;f.start(20);f.world.remove(1);
         require(!f.sleep.tick(1) && f.vm.status()==odh::VmStatus::returned_false && f.rng==0,
                 "removed caller cancels wait without RNG or frame write");}
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
        {Fixture f;
         require(!f.sleep.tick(2) && !f.sleep.tick(std::numeric_limits<std::uint32_t>::max()) &&
                 !f.sleep.tick(static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())+1) &&
                 !f.sleep.tick(std::numeric_limits<std::uint64_t>::max()) && f.sleep.clock()==0,
                 "first tick must zero/one; uint32 and uint64 overflow proposals preserve clock");
         require(f.sleep.tick(0) && !f.sleep.tick(std::numeric_limits<std::uint64_t>::max()) && f.sleep.tick(1),
                 "rejected overflow tick never prevents valid consecutive progression");}
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
        std::cout<<checks<<" Sleep checks passed (synthetic clock/World fixtures only)\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Sleep test failed: "<<e.what()<<'\n';return 1;}
}
