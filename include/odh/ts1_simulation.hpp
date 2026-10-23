#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
namespace odh {
struct Ts1ClockSnapshot {
    std::uint64_t ticks=0;
    std::uint16_t minute_fraction=0;
    std::uint8_t minute=0,hour=0,day=1,month=6;
    std::int32_t year=1997;
};
// TS1 profile: 30 simulation ticks/minute, 30-day months. No wall clock.
class Ts1Clock {
public:
    static constexpr unsigned ticks_per_minute=30;
    bool initialize(unsigned hour,unsigned day,unsigned minute,unsigned month,std::int32_t year);
    // Pinned Simitone activator uses seconds*30, deliberately without /60.
    bool initialize_simi(unsigned hour,unsigned day,unsigned minute,unsigned seconds,unsigned month,std::int32_t year);
    std::optional<std::int16_t> global(std::int16_t index) const;
    // Paused or exhausted representable clock: false, preserving state.
    bool tick(bool simulation_running);
    const Ts1ClockSnapshot& snapshot() const {return state_;}
    bool restore(const Ts1ClockSnapshot&);
    unsigned seconds() const {return state_.minute_fraction*60U/ticks_per_minute;}
private:
    Ts1ClockSnapshot state_;
};
enum class Ts1Need : std::uint8_t {hunger,comfort,hygiene,bladder,energy,fun,social};
constexpr std::size_t ts1_need_count=7;
// Explicit VM motive-bank mapping, independent of decay array order.
constexpr unsigned ts1_motive_index(Ts1Need need) {
    constexpr unsigned mapping[ts1_need_count]={7,6,8,9,5,15,14};
    return static_cast<unsigned>(need)<ts1_need_count ? mapping[static_cast<unsigned>(need)] : 16;
}
struct Ts1MotiveInputs {
    std::array<std::int16_t,ts1_need_count> needs{};
    std::array<bool,ts1_need_count> active_changes{};
    std::optional<std::int16_t> active_personality,outgoing_personality;
    std::int16_t sleep_state=0,room_score=0;
    bool hidden=false;
};
struct Ts1MotiveSnapshot {
    std::uint8_t last_two_minute_bucket=0;
    std::array<std::int16_t,ts1_need_count> fractions{};
};
enum class Ts1MotiveStatus {updated,unchanged,unsupported};
// Consume needs/mood/sleep_state only for updated; unsupported never mutates decay state.
struct Ts1MotiveResult {
    Ts1MotiveStatus status=Ts1MotiveStatus::unsupported;
    std::array<std::int16_t,ts1_need_count> needs{};
    std::int16_t sleep_state=0,room_score=0,mood=0;
};
// Default reference constants and need limits [-100,100] only. Custom tuning,
// overfilled needs and room-score calculation remain outside this model.
class Ts1MotiveDecay {
public:
    Ts1MotiveResult update(unsigned hour,unsigned minute,const Ts1MotiveInputs&);
    const Ts1MotiveSnapshot& snapshot() const {return state_;}
    bool restore(const Ts1MotiveSnapshot&);
private:
    Ts1MotiveSnapshot state_;
};
}
