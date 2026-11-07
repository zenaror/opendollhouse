#include "simulation_speed.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){try{
    using Pacer=odh_host::SimulationSpeedPacer;
    const auto origin=Pacer::TimePoint(std::chrono::seconds(10));
    const auto period=std::chrono::duration_cast<Pacer::Clock::duration>(Pacer::frame_period);
    for(const auto speed:{1,3,10}){
        Pacer p;p.set_speed(static_cast<std::uint8_t>(speed));
        unsigned actual_ticks=0,frames=0;
        auto now=origin;
        for(unsigned frame=0;frame<4;++frame){
            const auto ready=p.begin_tick(now);
            require(ready==origin+period*frame,"new batch deadline differs");
            now=ready;
            for(int tick=0;tick<speed;++tick){
                if(tick)require(p.begin_tick(now)==now,"tick inside batch unexpectedly waits");
                ++actual_ticks;
                const bool present=p.finish_tick();
                require(present==(tick==speed-1),"presentation split or omitted a batch");
                if(present)++frames;
            }
        }
        require(actual_ticks==4U*speed&&frames==4,"batching skipped simulation work");
        // A long pause/render stall never turns into many catch-up batches.
        now+=std::chrono::hours(1);
        require(p.begin_tick(now)==now,"long stall retained a presentation backlog");
        for(int n=0;n<speed;++n)(void)p.finish_tick();
        require(p.begin_tick(now)==now+period,"rebased batch cadence differs");
    }
    Pacer negative;
    const auto negative_origin=Pacer::TimePoint(-std::chrono::seconds(10));
    require(negative.begin_tick(negative_origin)==negative_origin,"valid negative clock epoch rejected");
    require(negative.finish_tick(),"negative-epoch tick did not finish");
    Pacer near_limit;bool overflow_rejected=false;
    try{(void)near_limit.begin_tick(Pacer::TimePoint::max());}catch(const std::overflow_error&){overflow_rejected=true;}
    require(overflow_rejected,"overflowing presentation deadline was accepted");
    Pacer p;p.set_speed(10);(void)p.begin_tick(origin);
    require(!p.finish_tick()&&p.remaining_ticks()==9,"fast batch setup differs");
    p.reset();require(p.speed()==10&&!p.remaining_ticks(),"pause reset lost selected speed or retained work");
    require(p.begin_tick(origin+std::chrono::hours(2))==origin+std::chrono::hours(2),"resume waited on an old deadline");
    (void)p.finish_tick();p.set_speed(3);
    require(p.speed()==3&&!p.remaining_ticks(),"speed change retained old batch");
    (void)p.begin_tick(origin);require(p.remaining_ticks()==3,"new speed budget differs");
    p.set_speed(3);require(p.remaining_ticks()==3,"selecting same speed discarded an active batch");
    p.reset();bool rejected=false;try{p.finish_tick();}catch(const std::logic_error&){rejected=true;}
    require(rejected,"unpaired finish silently altered pacing");
    for(const auto invalid:{0,2,4,255}){rejected=false;try{p.set_speed(static_cast<std::uint8_t>(invalid));}catch(const std::invalid_argument&){rejected=true;}
        require(rejected&&p.speed()==3,"invalid speed changed policy");}
    std::cout<<"simulation speed batches, suspension and bounded pacing passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
