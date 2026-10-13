#include "odh/playback.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace odh {
namespace {
PrimitiveOutcome unsupported(const std::string& text) {return {PrimitiveDisposition::unsupported,text};}
std::int16_t event_code(const std::string& text) {
    auto first=text.data(),last=first+text.size();
    const auto space=[](char c){return c==' '||c=='\t'||c=='\n'||c=='\r';};
    while(first!=last&&space(*first))++first;
    while(first!=last&&space(last[-1]))--last;
    if(first!=last&&*first=='+'){++first;if(first!=last&&*first=='-')return 0;}
    int value=0;const auto result=std::from_chars(first,last,value);
    return result.ec==std::errc{}&&result.ptr==last&&value>=-32768&&value<=32767?static_cast<std::int16_t>(value):0;
}
}
bool Playback::bind(Vm& vm) {if(executing_||(vm_&&vm_!=&vm))return false;vm_=&vm;return true;}
VmPrimitive Playback::primitive() {return [this](const auto& f,const auto& i,const auto&,const auto& d){return request(f,i,d);};}
std::optional<PlaybackSample> Playback::sample() const {
    if(!state_ || !vm_ || state_->vm_generation!=vm_->run_generation())return {};
    const auto* actor=world_.find(state_->actor);
    if(!actor || actor->generation!=state_->actor_generation)return {};
    if(wait_ && (!vm_->pending() || vm_->pending()->ticket!=wait_->ticket))return {};
    return PlaybackSample{state_->clip,state_->actor,state_->actor_generation,state_->vm_generation,
                          state_->frame,state_->frames,state_->end,state_->loop};
}
std::optional<PlaybackProgress> Playback::progress() const {
    if(!state_)return {};
    return PlaybackProgress{state_->clip->name,state_->frame,state_->frames,state_->queue.size(),state_->end,state_->loop};
}
std::optional<Playback::State> Playback::prepare(std::shared_ptr<const AnimationHeader> clip,const Entity& actor,
        bool backwards,bool loop,bool hurryable,std::string& error) const {
    const auto reject=[&](const std::string& text)->std::optional<State>{error=text;return {};};
    if(!clip)return reject("animation resource not supplied by explicit resolver");
    State next{};next.clip=std::move(clip);next.actor=actor.id;next.actor_generation=actor.generation;
    next.vm_generation=vm_->run_generation();next.backwards=backwards;next.loop=loop;next.speed=30.0f/25.0f;
    if(hurryable){const auto walk=actor.object_words.find(17);
        if(walk==actor.object_words.end())return reject("hurryable animation requires explicit WalkStyle");
        if(walk->second==1)next.speed*=2;}
    std::size_t counted=0,pairs=0,counted_short_events=0;
    for(const auto& motion:next.clip->motions){next.frames=std::max(next.frames,motion.frames);
        for(const auto& group:motion.time_properties)for(const auto& event:group){
            if(next.events.size()>=limits_.events)return reject("animation event budget exceeded");
            for(const auto& pair:event.pairs){
                if(pairs>=limits_.events)return reject("animation property budget exceeded");
                ++pairs;
                if(pair.first!="xevt"&&pair.first!="footstep")return reject("animation property service not implemented: "+pair.first);
                if(pair.first=="xevt"){
                    if(counted>=limits_.events)return reject("animation queue budget exceeded");
                    ++counted;
                    if(event_code(pair.second)<100&&++counted_short_events>255)return reject("animation event counter exceeds byte profile");}
            }
            next.events.push_back(event);
        }}
    if(!next.frames||next.frames>limits_.frames||next.frames>static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())||!limits_.ticks)return reject("animation frame/tick budget exceeded");
    next.frame=next.backwards?static_cast<float>(next.frames):0;next.high=next.events.size();return next;
}
PrimitiveOutcome Playback::request(const VmFrame& f,const Instruction& i,const VmDispatch& d) {
    if(i.opcode!=44)return {};
    if(executing_||!vm_||d.origin!=vm_)return unsupported("animation controller identity/reentry rejected");
    diagnostic_.clear();
    if(state_&&state_->vm_generation!=vm_->run_generation()){state_.reset();wait_.reset();}
    const auto flags=i.operands[5];const auto mode=(flags&1U)|((flags>>3)&2U);
    if(flags&0x88U)return unsupported("animation flags outside primary playback profile");
    auto id=static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8));
    std::uint32_t source=i.operands[4];
    if(flags&4U){if(id>=f.arguments.size())return unsupported("animation parameter index outside frame");
        id=static_cast<std::uint16_t>(f.arguments[id]);if(source==0)source=65536;}
    const auto* actor=world_.find(f.context.caller);
    if(!actor||actor->kind!=EntityKind::avatar)return unsupported("animation requires avatar caller");
    if(wait_)return unsupported("animation already has a pending wait");
    if(id==0){
        if(mode==3)return unsupported("animation reset carry cancellation not implemented");
        // FreeSO 4c6b3e8: Posture=0, HeadSeekState=42. An absent hand slot
        // is unknown, not evidence that the carry backend can be skipped.
        if(actor->slots.empty()||actor->slots[0]!=0)return unsupported("animation reset requires explicitly empty avatar hand slot zero");
        if(actor->person_data.size()<=42)return unsupported("animation reset requires Posture and HeadSeekState person data");
        if(!reset_resolver_)return unsupported("animation reset resource resolver not supplied");
        const auto posture=actor->person_data[0];
        const auto normalized=static_cast<std::int16_t>((posture==1||posture==2)?posture:3);
        std::string error;auto next=prepare(reset_resolver_(f,normalized),*actor,(flags&2U)!=0,true,false,error);
        if(!next)return unsupported(error);
        // Commit only after resource and budget validation. Gesture rendering
        // and carrying remain outside this silent, known-empty-hand profile.
        if(actor->person_data[42]==1)world_.set_person_data(actor->id,42,4);
        state_=std::move(*next);return {PrimitiveDisposition::goto_true,{}};
    }
    if(mode>=2)return unsupported("animation carry mode outside primary playback profile");
    const auto bank=(flags&32U)?VmFrameBank::locals:VmFrameBank::arguments;
    const auto index=static_cast<std::size_t>((flags&32U)?i.operands[2]:0);
    if(mode==0&&index>=(bank==VmFrameBank::locals?f.locals.size():f.arguments.size()))return unsupported("animation event destination outside frame");
    const auto clip=resolver_?resolver_(f,source,id):nullptr;
    if(!clip)return unsupported("animation resource not supplied by explicit resolver");
    if(mode==1||!state_||state_->clip!=clip||state_->actor!=actor->id||state_->actor_generation!=actor->generation) {
        std::string error;auto next=prepare(clip,*actor,(flags&2U)!=0,mode==1,(flags&64U)!=0,error);
        if(!next)return unsupported(error);
        state_=std::move(*next);
    }
    if(mode==1)return {PrimitiveDisposition::goto_true,{}};
    // On reentry, queued events take precedence over the end marker.
    if(state_->end)while(state_->events_run<i.operands[6]){
        if(state_->queue.size()>=limits_.events)return unsupported("animation queue budget exceeded");
        state_->queue.push_back(static_cast<std::int16_t>(state_->events_run++));}
    if(!state_->queue.empty()){const auto value=state_->queue.front();state_->queue.pop_front();
        return {PrimitiveDisposition::goto_false,{},VmFrameWrite{bank,index,value}};}
    if(state_->end){state_.reset();return {PrimitiveDisposition::goto_true,{}};}
    wait_=Wait{d.ticket,bank,index,i.operands[6]};return {PrimitiveDisposition::wait,{}};
}
bool Playback::deliver() {
    if(!wait_||!state_)return false;
    if(state_->end)while(state_->events_run<wait_->expected){
        if(state_->queue.size()>=limits_.events){const auto ticket=wait_->ticket;wait_.reset();state_.reset();
            vm_->complete(ticket,false);diagnostic_="animation queue budget exceeded";return true;}
        state_->queue.push_back(static_cast<std::int16_t>(state_->events_run++));}
    if(!state_->queue.empty()){
        const auto value=state_->queue.front();
        if(!vm_->complete(wait_->ticket,false,VmFrameWrite{wait_->bank,wait_->index,value}))return false;
        state_->queue.pop_front();wait_.reset();return true;
    }
    if(state_->end){const auto accepted=vm_->complete(wait_->ticket,true);state_.reset();wait_.reset();return accepted;}
    return false;
}
void Playback::cancel() {
    if(executing_)throw std::logic_error("animation callbacks must not reenter cancellation");
    const auto ticket=wait_?std::optional<std::uint64_t>(wait_->ticket):std::nullopt;
    wait_.reset();state_.reset();diagnostic_="animation canceled by host";
    if(ticket&&vm_)vm_->complete(*ticket,false);
}
bool Playback::tick(std::uint64_t now,const std::function<void(const PlaybackEvent&)>& observed) {
    if(executing_)throw std::logic_error("animation ticks must not reenter");
    struct Guard {bool& flag;explicit Guard(bool& f):flag(f){flag=true;}~Guard(){flag=false;}} guard(executing_);
    if(last_tick_&&(now<=*last_tick_||now-*last_tick_!=1))return false;
    last_tick_=now;if(!state_||!vm_)return false;
    if(state_->vm_generation!=vm_->run_generation() || (wait_&&(!vm_->pending()||vm_->pending()->ticket!=wait_->ticket))) {
        state_.reset();wait_.reset();diagnostic_="animation canceled by VM restart/completion";return false;}
    const auto* actor=world_.find(state_->actor);
    if(!actor||actor->generation!=state_->actor_generation || state_->ticks>=limits_.ticks) {
        const auto ticket=wait_?std::optional<std::uint64_t>(wait_->ticket):std::nullopt;state_.reset();wait_.reset();
        diagnostic_="animation canceled by entity change or tick budget";if(ticket)vm_->complete(*ticket,false);return true;}
    bool changed=deliver();if(!state_)return changed;
    ++state_->ticks;
    std::vector<PlaybackEvent> notifications;
    if(!state_->end){state_->frame+=state_->backwards?-state_->speed:state_->speed;
        // Preserve the reference's one-wrap rule while preventing excessive
        // custom limits or tiny hurryable loops from exceeding its int frame range.
        const auto frame_value=static_cast<double>(state_->frame);
        if(!std::isfinite(frame_value)||frame_value<std::numeric_limits<std::int32_t>::min()||frame_value>std::numeric_limits<std::int32_t>::max()){
            const auto ticket=wait_?std::optional<std::uint64_t>(wait_->ticket):std::nullopt;
            state_.reset();wait_.reset();diagnostic_="animation frame range exceeded";
            if(ticket)vm_->complete(*ticket,false);
            return true;}
        const auto time=(state_->frame*1000.0f)/30.0f;
        while(state_->low<state_->high){const auto pos=state_->backwards?state_->high-1:state_->low;
            const auto& event=state_->events[pos];
            const auto event_time=static_cast<float>(event.id);
            if(state_->backwards?event_time<time:event_time>time)break;
            if(state_->backwards)--state_->high;else ++state_->low;
            for(const auto& pair:event.pairs)if(pair.first=="xevt"){
                if(state_->queue.size()>=limits_.events){
                    const auto ticket=wait_?std::optional<std::uint64_t>(wait_->ticket):std::nullopt;
                    state_.reset();wait_.reset();diagnostic_="animation queue budget exceeded";
                    if(ticket)vm_->complete(*ticket,false);
                    return true;}
                const auto code=event_code(pair.second);state_->queue.push_back(code);
                if(code<100)++state_->events_run;}
            notifications.push_back({now,state_->actor,event.id,event.pairs});
        }
        // Truncation matches SilentFrameProgress, without an overflowing
        // float-to-integer conversion under unusually large host limits.
        const auto frame=std::trunc(static_cast<double>(state_->frame));
        if(frame<0||frame>=static_cast<double>(state_->frames)){
            if(state_->loop)state_->frame+=state_->backwards?static_cast<float>(state_->frames):-static_cast<float>(state_->frames);
            else state_->end=true;
        }
        changed=true;
    }
    for(const auto& event:notifications)if(observed)observed(event);
    return changed;
}
}
