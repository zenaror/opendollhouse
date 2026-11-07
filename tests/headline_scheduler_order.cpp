#include "odh/entity_scheduler.hpp"
#include "odh/headline.hpp"
#include "odh/sleep.hpp"
#include <iostream>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
const auto low_key=odh::routine_key(4096),high_key=odh::routine_key(4097);
odh::RoutineSet low_program() {
    return {{low_key,odh::Bhav{0x8002,{
        {0,1,255,{}},
        {41,254,255,{1,0,1,7,3,0,255,0}} // target StackObject3, new duration3.
    },{},odh::Bhav::Metadata{0,4,0,0}}}};
}
odh::RoutineSet high_program() {
    return {{high_key,odh::Bhav{0x8002,{
        {41,1,255,{0,0,1,7,2,0,255,0}}, // initial headline duration2.
        {0,254,255,{}}
    },{},odh::Bhav::Metadata{0,4,0,0}}}};
}
struct Fixture {
    odh::World world;
    odh::BalloonHeadlines headlines{world};
    unsigned low_rng=0,high_rng=0;
    odh::Sleep low_sleep{world,[this]{++low_rng;}},high_sleep{world,[this]{++high_rng;}};
    odh::Vm low{low_program(),world.vm_memory(),{},[this](const auto& f,const auto& i,const auto& t,const auto& d){
        return i.opcode==41?headlines.primitive()(f,i,t,d):low_sleep.primitive()(f,i,t,d);
    }};
    odh::Vm high{high_program(),world.vm_memory(),{},[this](const auto& f,const auto& i,const auto& t,const auto& d){
        return i.opcode==41?headlines.primitive()(f,i,t,d):high_sleep.primitive()(f,i,t,d);
    }};
    odh::EntityVmScheduler scheduler{2};
    explicit Fixture() {
        require(world.create(1,odh::EntityKind::object,{})&&world.create(3,odh::EntityKind::object,{}),
            "ordered source and target World objects");
        require(low_sleep.bind(low)&&high_sleep.bind(high),"bind actual Sleep controllers");
        low_sleep.set_run_every_frame_source([this]{return headlines.active(1);});
        high_sleep.set_run_every_frame_source([this]{return headlines.active(3);});
        require(scheduler.add(1,low,[this](auto now){return low_sleep.tick(now);})&&
            scheduler.add(3,high,[this](auto now){return high_sleep.tick(now);}),
            "register ascending entity IDs with real pre-tick controllers");
        require(low.start(low_key,{1,1,3},{2})==odh::VmStatus::running&&
            high.start(high_key,{3,3,3},{9})==odh::VmStatus::running,"start low delay2 and high delay9 programs");
    }
    void tick(std::uint64_t now,odh::EntityVmScheduler::ControllerOrder order) {
        require(scheduler.tick(now,20,10,{}, {},[this](auto id,auto,bool ran){
            const auto& sleep=id==1?low_sleep:high_sleep;
            return !(ran||sleep.evaluated_this_tick())||headlines.tick(id);
        },order),"scheduler processes actual controller/VM/lifecycle phases");
    }
};
}
int main(){try{
    using Order=odh::EntityVmScheduler::ControllerOrder;
    Fixture source,legacy;
    for(std::uint64_t now=1;now<=2;++now) {
        source.tick(now,Order::per_entity);legacy.tick(now,Order::all_before_dispatch);
        require(source.low.status()==odh::VmStatus::waiting&&legacy.low.status()==odh::VmStatus::waiting,
            "low entity stays waiting until source Tick3");
        require(source.high_sleep.snapshot().job->next_evaluation==now+1&&
            legacy.high_sleep.snapshot().job->next_evaluation==now+1,
            "high entity retains next-tick schedule selected while headline was active");
        if(now==1)require(source.headlines.snapshot().size()==1&&source.headlines.snapshot()[0].duration==1&&
            source.headlines.snapshot()[0].anim==1,"first high VM Tick creates and advances duration2 headline");
        if(now==2)require(source.headlines.snapshot().empty()&&legacy.headlines.snapshot().empty(),
            "initial headline expires after Sleep already captured Tick3");
    }
    const auto source_ticket=source.high.pending()->ticket,legacy_ticket=legacy.high.pending()->ticket;
    source.tick(3,Order::per_entity);legacy.tick(3,Order::all_before_dispatch);
    require(source.low.status()==odh::VmStatus::returned_true&&source.low_rng==1&&
        legacy.low.status()==odh::VmStatus::returned_true&&legacy.low_rng==1,
        "Tick3 low Sleep completes and dispatches real cross-object opcode41");
    require(source.high.status()==odh::VmStatus::waiting&&source.high.pending()->ticket==source_ticket&&
        source.high_sleep.snapshot().job->deadline==10&&source.high_sleep.snapshot().job->next_evaluation==4,
        "source per-entity ordering sees new headline before high Sleep reschedules to Tick4");
    require(legacy.high.status()==odh::VmStatus::waiting&&legacy.high.pending()->ticket==legacy_ticket&&
        legacy.high_sleep.snapshot().job->deadline==10&&!legacy.high_sleep.snapshot().job->next_evaluation,
        "legacy all-before dispatch demonstrates the wrong natural-deadline10 scheduling regression");
    const auto state=source.headlines.snapshot(),old=legacy.headlines.snapshot();
    require(state.size()==1&&state[0].target==3&&state[0].icon_target==3&&state[0].duration==2&&state[0].anim==1&&
        old.size()==1&&old[0].duration==2&&old[0].anim==1,
        "both execute headline instruction; only scheduling order differs, not a fabricated no-op");
    source.tick(4,Order::per_entity);legacy.tick(4,Order::all_before_dispatch);
    require(source.high_sleep.evaluated_this_tick()&&source.high_sleep.snapshot().job->next_evaluation==5&&
        source.headlines.snapshot()[0].duration==1&&source.headlines.snapshot()[0].anim==2,
        "source captured Tick4 advances actual Sleep and headline together");
    require(!legacy.high_sleep.evaluated_this_tick()&&legacy.headlines.snapshot()[0].duration==2,
        "legacy regression leaves newly created headline frozen until its incorrectly chosen deadline");
    require(source.high_rng==0&&legacy.high_rng==0,"comparison does not prematurely complete high Sleep or cycle RNG");
    std::cout<<checks<<" headline scheduler order checks passed (native component integration)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
