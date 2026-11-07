#include "odh/ts1_simulation.hpp"
#include <algorithm>
#include <limits>
namespace odh {
namespace {
bool valid(const Ts1ClockSnapshot& s) {
    return (s.minute_fraction<Ts1Clock::ticks_per_minute ||
        (s.ticks==0 && s.minute_fraction<=59*Ts1Clock::ticks_per_minute && s.minute_fraction%Ts1Clock::ticks_per_minute==0)) && s.minute<60 && s.hour<24 &&
        s.day>=1 && s.day<=30 && s.month>=1 && s.month<=12 && s.year>=1;
}
}
bool Ts1Clock::initialize(unsigned hour,unsigned day,unsigned minute,unsigned month,std::int32_t year) {
    if(hour>=24 || day<1 || day>30 || minute>=60 || month<1 || month>12 || year<1) return false;
    Ts1ClockSnapshot next;
    next.hour=static_cast<std::uint8_t>(hour);next.day=static_cast<std::uint8_t>(day);
    next.minute=static_cast<std::uint8_t>(minute);next.month=static_cast<std::uint8_t>(month);next.year=year;
    state_=next;return true;
}
bool Ts1Clock::initialize_simi(unsigned hour,unsigned day,unsigned minute,unsigned seconds,unsigned month,std::int32_t year) {
    if(seconds>=60) return false;
    if(!initialize(hour,day,minute,month,year)) return false;
    state_.minute_fraction=static_cast<std::uint16_t>(seconds*ticks_per_minute);return true;
}
std::optional<std::int16_t> Ts1Clock::global(std::int16_t index) const {
    switch(index) {
    case 0:return state_.hour;case 1:return state_.day;case 4:return 0;
    case 5:return state_.minute;case 6:return static_cast<std::int16_t>(seconds());
    case 7:return state_.month;
    case 8:{const auto low=static_cast<std::uint32_t>(state_.year)&65535U;
        return static_cast<std::int16_t>(low<32768?static_cast<std::int32_t>(low):static_cast<std::int32_t>(low)-65536);}
    default:return std::nullopt;
    }
}
bool Ts1Clock::restore(const Ts1ClockSnapshot& s) {if(!valid(s)) return false;state_=s;return true;}
bool Ts1Clock::tick(bool running) {
    if(!running || state_.ticks==std::numeric_limits<std::uint64_t>::max()) return false;
    auto next=state_;
    ++next.ticks;
    if(++next.minute_fraction>=ticks_per_minute) {
        next.minute_fraction=0;
        if(++next.minute>=60) {
            next.minute=0;
            if(++next.hour>=24) {
                next.hour=0;
                if(++next.day>30) {
                    next.day=1;
                    if(++next.month>12) {
                        next.month=1;
                        if(next.year==std::numeric_limits<std::int32_t>::max()) return false;
                        ++next.year;
                    }
                }
            }
        }
    }
    state_=next;return true;
}
namespace {
std::int16_t source_short(std::int32_t value) {
    const auto bits=static_cast<std::uint16_t>(static_cast<std::uint32_t>(value)&65535U);
    return static_cast<std::int16_t>(bits<=32767U?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536);
}
std::int16_t default_motive_setter(std::int16_t old,std::int16_t value){
    return std::max(std::int16_t{-100},std::min(value,std::max(old,std::int16_t{100})));
}
}
bool Ts1MotiveDecay::restore(const Ts1MotiveSnapshot& s) {
    // Source LastMinute begins at0 and is assigned only Clock.Minutes/2.
    // Fractions are serialized signed shorts, including unprocessed carry.
    if(s.last_two_minute_bucket>=30)return false;
    state_=s;return true;
}
Ts1MotiveResult Ts1MotiveDecay::update(unsigned hour,unsigned minute,const Ts1MotiveInputs& in) {
    Ts1MotiveResult out;out.needs=in.needs;out.sleep_state=in.sleep_state;out.room_score=in.room_score;out.mood=in.mood;
    if(hour>=24||minute>=60)return out;
    if(in.hidden||minute/2==state_.last_two_minute_bucket){out.status=Ts1MotiveStatus::unchanged;return out;}
    // Source reads personality lazily, only for the corresponding unskipped
    // motive. Preflight unavailable capabilities before any native-state change.
    if((!in.active_changes[static_cast<std::size_t>(Ts1Need::comfort)]&&!in.active_personality)||
       (!in.active_changes[static_cast<std::size_t>(Ts1Need::social)]&&!in.outgoing_personality))return out;
    state_.last_two_minute_bucket=static_cast<std::uint8_t>(minute/2);
    const bool sleeping=in.sleep_state!=0;
    std::int32_t mood=in.room_score;
    for(std::size_t i=0;i<ts1_need_count;++i) {
        if(in.active_changes[i]&&i!=static_cast<unsigned>(Ts1Need::energy))continue;
        auto motive=out.needs[i];
        // The actual stored Hunger is read afresh after its own source setter.
        const std::int32_t hunger_rate=2*(100+static_cast<std::int32_t>(out.needs[0]));
        std::int32_t fraction=0;
        switch(static_cast<Ts1Need>(i)) {
        case Ts1Need::hunger:fraction=hunger_rate;break;
        case Ts1Need::comfort:fraction=*in.active_personality>666?400:(*in.active_personality<666?600:500);break;
        case Ts1Need::hygiene:fraction=sleeping?80:170;break;
        case Ts1Need::bladder:fraction=(sleeping?150:300)+hunger_rate*300/1000;break;
        case Ts1Need::energy:
            if(sleeping&&hour==7&&motive>=80){out.sleep_state=0;out.sleep_state_written=true;}
            // sleeping is captured before the wake write; this rereads the
            // changed SleepState only for the exact-1 Energy branch.
            fraction=sleeping?(out.sleep_state==-1?-1286:(hour>=7?10:0)):375;
            break;
        case Ts1Need::fun:fraction=sleeping?0:250;break;
        case Ts1Need::social:fraction=55;break; // trunc(0.000125f*1000)==0; the source still reads outgoing.
        }
        auto carry=source_short(static_cast<std::int32_t>(state_.fractions[i])+source_short(fraction));
        if(carry>=1000){
            motive=source_short(static_cast<std::int32_t>(motive)-source_short(carry/1000));
            carry=static_cast<std::int16_t>(carry%1000);
            if(motive<-100)motive=-100;
            out.needs[i]=default_motive_setter(in.needs[i],motive);out.needs_written[i]=true;
        }
        if(carry<0){
            const auto increase=source_short(1-carry/1000);
            motive=source_short(static_cast<std::int32_t>(motive)+increase);
            carry=source_short(static_cast<std::int32_t>(carry)+source_short(static_cast<std::int32_t>(increase)*1000));
            out.needs[i]=default_motive_setter(in.needs[i],motive);out.needs_written[i]=true;
        }
        state_.fractions[i]=carry;
        mood+=motive; // Source uses the local short, not the clamped stored result.
    }
    out.mood=default_motive_setter(in.mood,source_short(mood/8));out.mood_written=true;
    out.status=Ts1MotiveStatus::updated;return out;
}
}
