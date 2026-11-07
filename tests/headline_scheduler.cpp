#include "odh/entity_scheduler.hpp"
#include "odh/headline.hpp"
#include "odh/headline_snapshot_codec.hpp"
#include "odh/sleep.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include <iostream>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
const auto key=odh::routine_key(4096);
odh::RoutineSet program() {
    return {{key,odh::Bhav{0x8002,{
        {41,1,255,{0,0,1,7,3,0,255,0}}, // Caller headline, StackObject icon, duration3.
        {0,2,255,{}},                  // Sleep argument0.
        {2,254,255,{0,0,77,0,0,5,8,7}} // TEMP0=77 then return true.
    },{},odh::Bhav::Metadata{0,4,0,0}}}};
}
struct Fixture {
    odh::World world;
    odh::BalloonHeadlines headlines{world};
    unsigned rng=0;
    odh::Sleep sleep{world,[this]{++rng;}};
    odh::Vm vm{program(),world.vm_memory(),{},[this](const auto& f,const auto& i,const auto& t,const auto& d){
        if(i.opcode==41)return headlines.primitive()(f,i,t,d);
        return sleep.primitive()(f,i,t,d);
    }};
    odh::EntityVmScheduler scheduler{2};
    bool lifecycle=false,ran_vm=false;
    explicit Fixture(const odh::WorldSnapshot* restored=nullptr) {
        require(restored?world.restore_snapshot(*restored):
            world.create(2,odh::EntityKind::object,{})&&world.create(3,odh::EntityKind::object,{}),
            "equivalent object and icon World");
        require(sleep.bind(vm),"bind real Sleep controller");
        sleep.set_run_every_frame_source([this]{return headlines.active(2);});
        require(scheduler.add(2,vm,[this](auto now){return sleep.tick(now);}),
            "register object VM and Sleep scheduling hook");
    }
    void start(){require(vm.start(key,{2,2,3},{9})==odh::VmStatus::running,"start headline then Sleep program");}
    void tick(std::uint64_t now) {
        lifecycle=false;ran_vm=false;
        require(scheduler.tick(now,20,20,{}, {},[this](auto id,auto,bool ran){
            ran_vm=ran;
            if(ran||sleep.evaluated_this_tick()) {lifecycle=true;return headlines.tick(id);}
            return true;
        }),"real scheduler tick completes");
    }
};
void same(const Fixture& control,const Fixture& restored) {
    require(odh::encode_vm_snapshot(control.vm.snapshot())==odh::encode_vm_snapshot(restored.vm.snapshot()),
        "restored VM frame/PC/temps/tickets are byte-identical");
    require(odh::encode_balloon_headline_snapshot(control.headlines.snapshot())==
        odh::encode_balloon_headline_snapshot(restored.headlines.snapshot()),
        "restored headline lifecycle is byte-identical");
    const auto a=control.sleep.snapshot(),b=restored.sleep.snapshot();
    require(a.clock==b.clock&&a.last_tick==b.last_tick&&a.interrupted==b.interrupted&&
        a.notify_resume==b.notify_resume&&a.diagnostic==b.diagnostic&&a.actor.has_value()==b.actor.has_value()&&
        a.job.has_value()==b.job.has_value(),"restored Sleep clock and control state match");
    if(a.actor)require(a.actor->id==b.actor->id&&a.actor->generation==b.actor->generation&&
        a.actor->run_generation==b.actor->run_generation,"restored Sleep actor identity matches");
    if(a.job)require(a.job->ticket==b.job->ticket&&a.job->idle_start==b.job->idle_start&&
        a.job->deadline==b.job->deadline&&a.job->argument==b.job->argument&&
        a.job->next_evaluation==b.job->next_evaluation,"restored Sleep ticket and next schedule match");
    const auto x=control.scheduler.snapshot(),y=restored.scheduler.snapshot();
    require(x.last_tick==y.last_tick&&x.instructions_last_tick==y.instructions_last_tick&&
        x.budget_cursor==y.budget_cursor&&x.entity_limit==y.entity_limit&&x.faulted==y.faulted&&
        x.diagnostic==y.diagnostic&&x.entities.size()==y.entities.size(),"restored scheduler clock and work match");
    require(control.rng==restored.rng&&control.lifecycle==restored.lifecycle&&control.ran_vm==restored.ran_vm&&
        control.sleep.evaluated_this_tick()==restored.sleep.evaluated_this_tick(),
        "restored RNG and actual entity lifecycle phases match");
}
}
int main(){try{
    Fixture control;control.start();control.tick(1);
    const auto first=control.headlines.snapshot();
    require(first.size()==1&&first[0].target==2&&first[0].icon_target==3&&first[0].duration==2&&first[0].anim==1,
        "opcode41 creates real state and entity lifecycle decrements after VM in same Tick");
    require(control.vm.status()==odh::VmStatus::waiting&&control.vm.steps()==2&&
        control.sleep.snapshot().job->next_evaluation==2&&control.sleep.snapshot().job->deadline==10,
        "Sleep captures RunEveryFrame next2 while retaining argument9 natural deadline10");
    const auto ticket=control.vm.pending()->ticket;
    control.tick(2);
    const auto second=control.headlines.snapshot();
    require(second.size()==1&&second[0].duration==1&&second[0].anim==2&&
        control.sleep.snapshot().job->next_evaluation==3&&!control.ran_vm&&control.lifecycle,
        "waiting VM still receives scheduled entity Tick and second headline update");
    const auto world=control.world.snapshot();
    const auto vm_bytes=odh::encode_vm_snapshot(control.vm.snapshot());
    const auto headline_bytes=odh::encode_balloon_headline_snapshot(second);
    const auto sleep=control.sleep.snapshot();const auto scheduler=control.scheduler.snapshot();
    Fixture restored(&world);
    require(restored.vm.restore_snapshot(odh::decode_vm_snapshot(vm_bytes))&&
        restored.sleep.restore_snapshot(sleep)&&restored.headlines.restore_snapshot(
            odh::decode_balloon_headline_snapshot(headline_bytes))&&restored.scheduler.restore_snapshot(scheduler),
        "checkpoint2 restores real World VM Sleep headlines and scheduler");
    restored.rng=control.rng;
    for(std::uint64_t now=3;now<=10;++now) {
        control.tick(now);restored.tick(now);same(control,restored);
        if(now==3)require(control.headlines.snapshot().empty()&&control.sleep.snapshot().job->next_evaluation==4&&
            control.vm.pending()->ticket==ticket,"headline expires after Sleep has already scheduled next Tick4");
        if(now==4)require(control.sleep.evaluated_this_tick()&&control.lifecycle&&!control.headlines.active(2)&&
            !control.sleep.snapshot().job->next_evaluation&&control.sleep.snapshot().job->deadline==10&&
            control.vm.pending()->ticket==ticket,"captured Tick4 survives expiry then selects natural deadline10");
        if(now>=5&&now<10)require(!control.sleep.evaluated_this_tick()&&!control.lifecycle&&!control.ran_vm&&
            control.vm.pending()->ticket==ticket,"silent natural wait does not fabricate entity lifecycle ticks");
    }
    require(control.vm.status()==odh::VmStatus::returned_true&&control.vm.temps()[0]==77&&control.vm.steps()==3&&
        control.rng==1&&!control.sleep.active()&&control.sleep.clock()==10&&control.lifecycle&&control.ran_vm,
        "Tick10 completes Sleep once and executes real continuation with one RNG cycle");
    std::cout<<checks<<" headline scheduler integration checks passed (synthetic native components)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
