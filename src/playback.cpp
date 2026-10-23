#include "odh/playback.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace odh {
namespace {
std::optional<HandGesture> gesture(const std::string& text){
    const auto space=[](char c){return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v';};
    const auto* first=text.data();const auto* last=first+text.size();
    while(first!=last&&space(*first))++first;
    while(first!=last&&space(last[-1]))--last;
    if(first!=last&&*first=='+'){++first;if(first!=last&&*first=='-')return {};}
    int value=0;const auto r=std::from_chars(first,last,value);
    if(r.ec!=std::errc{}||r.ptr!=last||value<0||value>3)return {};
    return static_cast<HandGesture>(value);
}
const std::string* property(const AnimationProperties& pairs,const char* key){
    for(const auto& pair:pairs)if(pair.first==key)return &pair.second;
    return nullptr;
}
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
PlaybackResourceKey resource_key(const VmFrame& frame,const Entity& actor,PlaybackResourceKey::Kind kind,
                                 std::uint32_t source,std::uint16_t id,std::int16_t posture=0) {
    PlaybackResourceKey key{kind,source,id,posture};key.caller=frame.context.caller;key.callee=frame.context.callee;
    key.stack_object=frame.context.stack_object;key.code_owner=frame.context.code_owner;
    key.actor_age=actor.person_data.size()>58?actor.person_data[58]:0;return key;
}
bool wait_matches_vm(const PlaybackWaitSnapshot& saved,const PlaybackResourceKey& resource,
                     std::int16_t actor,const VmSnapshot& vm) {
    if(vm.status!=VmStatus::waiting||!vm.pending||vm.pending->ticket!=saved.ticket||
       vm.pending->context.caller!=actor||!vm.pending->instruction.depth||
       vm.pending->instruction.depth>vm.stack.size())return false;
    const auto& trace=vm.pending->instruction;const auto& instruction=trace.instruction;
    if(instruction.opcode!=44||resource.kind!=PlaybackResourceKey::Kind::animation||
       resource.caller!=vm.pending->context.caller||resource.callee!=vm.pending->context.callee||
       resource.stack_object!=vm.pending->context.stack_object||resource.code_owner!=vm.pending->context.code_owner)return false;
    const auto flags=instruction.operands[5];
    const auto mode=(flags&1U)|((flags>>3)&2U);
    if(mode!=0&&mode!=3)return false;
    const auto bank=(flags&32U)?VmFrameBank::locals:VmFrameBank::arguments;
    const auto index=static_cast<std::size_t>((flags&32U)?instruction.operands[2]:0);
    const auto& frame=vm.stack[trace.depth-1];
    if(saved.bank!=bank||saved.index!=index||saved.expected!=instruction.operands[6]||
       saved.index>=(saved.bank==VmFrameBank::arguments?frame.arguments.size():frame.locals.size()))return false;
    auto id=static_cast<std::uint16_t>(instruction.operands[0]|(instruction.operands[1]<<8));
    std::uint32_t source=instruction.operands[4];
    if(flags&4U){if(id>=frame.arguments.size())return false;id=static_cast<std::uint16_t>(frame.arguments[id]);if(source==0)source=65536;}
    return id!=0&&resource.source==source&&resource.id==id&&resource.posture==0;
}
}
bool Playback::bind(Vm& vm) {if(executing_||(vm_&&vm_!=&vm))return false;vm_=&vm;return true;}
bool Playback::capture_snapshot(PlaybackSnapshot& out,std::string* error) const {
    const auto reject=[&](const char* text){if(error)*error=text;return false;};
    if(executing_)return reject("animation snapshot requested during callback/tick");
    if(wait_&&!state_)return reject("animation wait exists without playback state");
    if(bool(carry_clip_)!=bool(carry_resource_))return reject("animation carry resource identity is missing");
    if(hands_){const auto* actor=world_.find(hands_->actor);
        if(!actor||actor->generation!=hands_->actor_generation||!vm_||hands_->vm_generation!=vm_->run_generation())
            return reject("animation hand state identity is stale");}
    PlaybackSnapshot next{};next.last_tick=last_tick_;
    if(state_){
        if(!vm_)return reject("animation snapshot has no bound VM");
        if(!hands_)return reject("animation state is missing its required hand state");
        if(!state_->resource.source&&!state_->resource.id&&state_->resource.kind==PlaybackResourceKey::Kind::animation)
            return reject("animation snapshot has no stable resource key");
        const auto* actor=world_.find(state_->actor);
        if(!actor||actor->generation!=state_->actor_generation||state_->vm_generation!=vm_->run_generation()||
           state_->resource.caller!=state_->actor||state_->resource.actor_age!=(actor->person_data.size()>58?actor->person_data[58]:0)||
           !std::isfinite(state_->frame)||!std::isfinite(state_->speed)||state_->speed<=0||!state_->frames||
           state_->frames>limits_.frames||state_->events.size()>limits_.events||state_->low>state_->high||
           state_->high>state_->events.size()||state_->queue.size()>limits_.events||state_->events_run>255||
           state_->ticks>limits_.ticks||(hands_&&(hands_->actor!=state_->actor||
               hands_->actor_generation!=state_->actor_generation||hands_->vm_generation!=state_->vm_generation))||
           (wait_&&!wait_matches_vm(
               PlaybackWaitSnapshot{wait_->ticket,wait_->bank,wait_->index,wait_->expected},
               state_->resource,state_->actor,vm_->snapshot())))
            return reject("animation snapshot identity or continuation state is stale");
        if(!wait_){const auto vm_state=vm_->snapshot();
            if(vm_state.pending&&vm_state.pending->context.caller==state_->actor&&
               vm_state.pending->instruction.instruction.opcode==44)
                return reject("animation VM wait exists without playback wait state");}
        PlaybackClipSnapshot saved{};saved.resource=state_->resource;saved.actor=state_->actor;
        saved.actor_generation=state_->actor_generation;saved.vm_generation=state_->vm_generation;
        saved.frame=state_->frame;saved.speed=state_->speed;saved.frames=state_->frames;
        saved.backwards=state_->backwards;saved.end=state_->end;saved.loop=state_->loop;
        saved.low=state_->low;saved.high=state_->high;saved.queue=state_->queue;
        saved.events_run=state_->events_run;saved.ticks=state_->ticks;
        if(wait_)saved.wait=PlaybackWaitSnapshot{wait_->ticket,wait_->bank,wait_->index,wait_->expected};
        next.state=std::move(saved);
    }else if(vm_){
        const auto vm_state=vm_->snapshot();
        if(vm_state.pending&&vm_state.pending->instruction.instruction.opcode==44)
            return reject("animation VM wait exists without playback state");
    }
    if(carry_clip_)next.carry=carry_resource_;
    if(hands_)next.hands=PlaybackHandsSnapshot{hands_->value,hands_->actor,hands_->actor_generation,hands_->vm_generation};
    out=std::move(next);if(error)error->clear();return true;
}
bool Playback::restore_snapshot(const PlaybackSnapshot& saved,Vm& vm,const PlaybackRestoreResolver& resolver,std::string* error) {
    const auto reject=[&](const char* text){if(error)*error=text;return false;};
    if(executing_)return reject("animation restore requested during callback/tick");
    if(vm_&&vm_!=&vm)return reject("animation restore VM identity mismatch");
    const auto fail_key=[](const PlaybackResourceKey& key){
        return (key.kind!=PlaybackResourceKey::Kind::animation&&key.kind!=PlaybackResourceKey::Kind::posture_reset)||
               (key.kind==PlaybackResourceKey::Kind::animation&&((!key.source&&!key.id)||key.posture!=0))||
               (key.kind==PlaybackResourceKey::Kind::posture_reset&&
                (key.source||key.id||(key.posture!=1&&key.posture!=2&&key.posture!=3)));
    };
    std::optional<State> restored;std::optional<Wait> restored_wait;
    std::shared_ptr<const AnimationHeader> carry;
    const auto original_vm=vm_;
    struct BindingRollback {Vm*& binding;Vm* old;bool committed=false;~BindingRollback(){if(!committed)binding=old;}} binding{vm_,original_vm};
    vm_=&vm;
    const auto rollback=[&](){vm_=original_vm;};
    if(saved.state){
        const auto& s=*saved.state;
        const auto* actor=world_.find(s.actor);
        if(!saved.hands||fail_key(s.resource)||!actor||actor->kind!=EntityKind::avatar||actor->generation!=s.actor_generation||
           s.resource.caller!=s.actor||s.resource.actor_age!=(actor->person_data.size()>58?actor->person_data[58]:0)||
           s.vm_generation!=vm.run_generation()||!std::isfinite(s.frame)||!std::isfinite(s.speed)||s.speed<=0||
           !s.frames||s.frames>limits_.frames||s.ticks>limits_.ticks||s.queue.size()>limits_.events||
           s.events_run>255||s.low>s.high||!resolver){rollback();return reject("animation snapshot state identity or limits invalid");}
        const auto clip=resolver(s.resource);
        if(!clip){rollback();return reject("animation snapshot resource key did not resolve");}
        std::string why;auto base=prepare(clip,*actor,s.backwards,s.loop,false,why);
        if(!base){rollback();if(error)*error=why;return false;}
        const auto terminal_frame=std::trunc(static_cast<double>(s.frame));
        const bool invalid_frame_state=(s.end&&s.loop)||
            (!s.loop&&(s.end?(s.backwards?terminal_frame>=0:terminal_frame<s.frames):
                              (s.backwards?(terminal_frame<0||s.frame>static_cast<float>(s.frames)):
                                            (s.frame<0||terminal_frame>=s.frames))));
        if(base->frames!=s.frames||s.low>base->events.size()||s.high>base->events.size()||
           (s.backwards?(s.low!=0):(s.high!=base->events.size()))||
           (s.frame<static_cast<float>(std::numeric_limits<std::int32_t>::min())||
            s.frame>static_cast<float>(std::numeric_limits<std::int32_t>::max()))||
           invalid_frame_state) {
            rollback();return reject("animation snapshot cursor or frame invalid for resolved resource");}
        base->resource=s.resource;base->frame=s.frame;base->speed=s.speed;base->end=s.end;
        base->low=s.low;base->high=s.high;base->queue=s.queue;base->events_run=s.events_run;base->ticks=s.ticks;
        restored=std::move(*base);
        if(s.wait){
            const auto vm_state=vm.snapshot();
            if(s.wait->bank!=VmFrameBank::arguments&&s.wait->bank!=VmFrameBank::locals){rollback();return reject("animation wait bank invalid");}
            if(!wait_matches_vm(*s.wait,s.resource,s.actor,vm_state)){
                rollback();return reject("animation wait does not match restored VM pending call");}
            restored_wait=Wait{s.wait->ticket,s.wait->bank,s.wait->index,s.wait->expected};
        }else {
            const auto vm_state=vm.snapshot();
            if(vm_state.pending&&vm_state.pending->context.caller==s.actor&&
               vm_state.pending->instruction.instruction.opcode==44){rollback();return reject("restored animation VM wait has no playback controller wait");}
        }
    }else {
        const auto vm_state=vm.snapshot();
        if(vm_state.pending&&vm_state.pending->instruction.instruction.opcode==44){
            rollback();return reject("restored animation VM wait has no playback state");}
    }
    if(saved.carry){
        if(fail_key(*saved.carry)||saved.carry->kind!=PlaybackResourceKey::Kind::animation||!resolver){rollback();return reject("animation carry key invalid or resolver unavailable");}
        const auto* actor=world_.find(saved.carry->caller);
        if(!actor||actor->kind!=EntityKind::avatar||saved.carry->actor_age!=(actor->person_data.size()>58?actor->person_data[58]:0)){
            rollback();return reject("animation carry actor context is stale");}
        carry=resolver(*saved.carry);if(!carry){rollback();return reject("animation carry key did not resolve");}
    }
    if(saved.hands){
        const auto& h=*saved.hands;const auto* actor=world_.find(h.actor);
        if(!valid_hand_gesture(h.value.left)||!valid_hand_gesture(h.value.right)||!actor||
           actor->kind!=EntityKind::avatar||actor->generation!=h.actor_generation||h.vm_generation!=vm.run_generation()){
            rollback();return reject("animation hand snapshot identity or value invalid");}
    }
    if(restored&&saved.hands&&(saved.hands->actor!=restored->actor||saved.hands->actor_generation!=restored->actor_generation||
       saved.hands->vm_generation!=restored->vm_generation)){rollback();return reject("animation state and hands identities disagree");}
    if(restored&&restored->vm_generation!=vm.run_generation()){rollback();return reject("animation VM generation mismatch");}
    state_=std::move(restored);wait_=restored_wait;carry_clip_=std::move(carry);
    carry_resource_=saved.carry;
    hands_.reset();if(saved.hands)hands_=HandsState{saved.hands->value,saved.hands->actor,saved.hands->actor_generation,saved.hands->vm_generation};
    last_tick_=saved.last_tick;diagnostic_.clear();vm_=&vm;binding.committed=true;if(error)error->clear();return true;
}
std::optional<AvatarHands> Playback::hands() const {
    if(!hands_||!vm_||hands_->vm_generation!=vm_->run_generation())return {};
    const auto* actor=world_.find(hands_->actor);
    if(!actor||actor->generation!=hands_->actor_generation)return {};
    return hands_->value;
}
VmPrimitive Playback::primitive() {return [this](const auto& f,const auto& i,const auto&,const auto& d){return request(f,i,d);};}
std::optional<PlaybackSample> Playback::sample() const {
    if(!state_ || !vm_ || state_->vm_generation!=vm_->run_generation())return {};
    const auto* actor=world_.find(state_->actor);
    if(!actor || actor->generation!=state_->actor_generation)return {};
    if(wait_ && (!vm_->pending() || vm_->pending()->ticket!=wait_->ticket))return {};
    return PlaybackSample{state_->clip,state_->actor,state_->actor_generation,state_->vm_generation,
                          state_->frame,state_->frames,state_->end,state_->loop,hands().value_or(AvatarHands{})};
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
                if(pair.first!="xevt"&&pair.first!="footstep"&&pair.first!="lefthand"&&pair.first!="righthand")return reject("animation property service not implemented: "+pair.first);
                if(pair.first=="xevt"){
                    if(counted>=limits_.events)return reject("animation queue budget exceeded");
                    ++counted;
                    if(event_code(pair.second)<100&&++counted_short_events>255)return reject("animation event counter exceeds byte profile");}
            }
            for(const auto* key:{"righthand","lefthand"})if(const auto* value=property(event.pairs,key))
                if(!gesture(*value))return reject("animation hand gesture outside supported numeric range 0..3");
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
    const auto flags=i.operands[5];auto mode=(flags&1U)|((flags>>3)&2U);
    if(flags&0x80U)return unsupported("animation flags outside playback profile");
    auto id=static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8));
    std::uint32_t source=i.operands[4];
    if(flags&4U){if(id>=f.arguments.size())return unsupported("animation parameter index outside frame");
        id=static_cast<std::uint16_t>(f.arguments[id]);if(source==0)source=65536;}
    const auto* actor=world_.find(f.context.caller);
    if(!actor||actor->kind!=EntityKind::avatar)return unsupported("animation requires avatar caller");
    if(wait_)return unsupported("animation already has a pending wait");
    if(id==0){
        if(mode==3){carry_clip_.reset();carry_resource_.reset();return {PrimitiveDisposition::goto_true,{}};}
        // FreeSO 4c6b3e8: Posture=0, HeadSeekState=42. An absent hand slot
        // is unknown, not evidence that the carry backend can be skipped.
        if(actor->slots.empty()||actor->slots[0]!=0)return unsupported("animation reset requires explicitly empty avatar hand slot zero");
        if(actor->person_data.size()<=42)return unsupported("animation reset requires Posture and HeadSeekState person data");
        if(!reset_resolver_)return unsupported("animation reset resource resolver not supplied");
        const auto posture=actor->person_data[0];
        const auto normalized=static_cast<std::int16_t>((posture==1||posture==2)?posture:3);
        std::string error;auto next=prepare(reset_resolver_(f,normalized),*actor,(flags&2U)!=0,true,false,error);
        if(!next)return unsupported(error);
        // Commit only after resource and budget validation.
        if(actor->person_data[42]==1)world_.set_person_data(actor->id,42,4);
        next->resource=resource_key(f,*actor,PlaybackResourceKey::Kind::posture_reset,0,0,normalized);
        state_=std::move(*next);hands_=HandsState{{},actor->id,actor->generation,vm_->run_generation()};
        return {PrimitiveDisposition::goto_true,{}};
    }
    if(mode==3){carry_clip_.reset();carry_resource_.reset();}
    if(mode==2){
        const auto clip=resolver_?resolver_(f,source,id):nullptr;
        if(!clip)return unsupported("carry animation resource not supplied by explicit resolver");
        carry_clip_=clip;carry_resource_=resource_key(f,*actor,PlaybackResourceKey::Kind::animation,source,id);
        return {PrimitiveDisposition::goto_true,{}};
    }
    const auto bank=(flags&32U)?VmFrameBank::locals:VmFrameBank::arguments;
    const auto index=static_cast<std::size_t>((flags&32U)?i.operands[2]:0);
    if(mode==0&&index>=(bank==VmFrameBank::locals?f.locals.size():f.arguments.size()))return unsupported("animation event destination outside frame");
    const auto clip=resolver_?resolver_(f,source,id):nullptr;
    if(!clip)return unsupported("animation resource not supplied by explicit resolver");
    if(mode==1||!state_||state_->clip!=clip||state_->actor!=actor->id||state_->actor_generation!=actor->generation) {
        std::string error;auto next=prepare(clip,*actor,(flags&2U)!=0,mode==1,(flags&64U)!=0,error);
        if(!next)return unsupported(error);
        next->resource=resource_key(f,*actor,PlaybackResourceKey::Kind::animation,source,id);
        state_=std::move(*next);
        hands_=HandsState{{},actor->id,actor->generation,vm_->run_generation()};
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
            // PropertyList's keyed lookup returns its first matching pair.
            // Apply both sides before observers see the event. Raw VM banks stay untouched.
            if(const auto* value=property(event.pairs,"righthand"))hands_->value.right=*gesture(*value);
            if(const auto* value=property(event.pairs,"lefthand"))hands_->value.left=*gesture(*value);
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
