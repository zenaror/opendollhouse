#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace odh_host {
constexpr bool valid_simulation_speed(std::uint8_t multiplier) noexcept {
    return multiplier==1||multiplier==3||multiplier==10;
}

// Presentation-side batching; it never advances or omits a World tick. The
// reference TS1GameScreen/UIDesktopUCP selects 1,3,10 and VM.Update targets
// 30 simulation ticks per second at normal speed. A host can run each tick
// normally, polling input between them, and present once per completed batch.
class SimulationSpeedPacer {
public:
    using Clock=std::chrono::steady_clock;
    using TimePoint=Clock::time_point;
    static constexpr auto frame_period=std::chrono::nanoseconds(1000000000/30);

    std::uint8_t speed()const noexcept{return speed_;}
    std::uint8_t remaining_ticks()const noexcept{return remaining_;}
    void set_speed(std::uint8_t multiplier){
        if(!valid_simulation_speed(multiplier))throw std::invalid_argument("unsupported simulation speed");
        if(multiplier!=speed_){speed_=multiplier;reset();}
    }
    // Discard only unstarted presentation work, never simulation state. Call
    // on pause, modal/editor entry or any suspension/resumption so time spent
    // there cannot become catch-up work after returning to Live.
    void reset()noexcept{remaining_=0;next_frame_.reset();}

    // Call immediately before ONE real simulation tick, then sleep_until the
    // returned point. Active batches return now; only their first tick waits.
    // At most one frame's minor lateness is retained. A longer stall rebases
    // instead of manufacturing an unbounded backlog of simulation ticks.
    TimePoint begin_tick(TimePoint now){
        if(remaining_)return now;
        const auto period=std::chrono::duration_cast<Clock::duration>(frame_period);
        if(!next_frame_||(now>*next_frame_&&*next_frame_<=TimePoint::max()-period&&now>*next_frame_+period))next_frame_=now;
        const auto ready=*next_frame_;
        if(ready>TimePoint::max()-period)throw std::overflow_error("simulation presentation deadline overflow");
        next_frame_=ready+period;
        remaining_=speed_;
        return ready;
    }
    // Call exactly once after that real tick, including ticks whose VM work
    // schedules a travel continuation. True means this batch can be presented.
    bool finish_tick(){
        if(!remaining_)throw std::logic_error("simulation tick finished without a presentation batch");
        return --remaining_==0;
    }
private:
    std::uint8_t speed_=1,remaining_=0;
    std::optional<TimePoint> next_frame_;
};
}
