#include "odh/mixer.hpp"
#include <limits>

namespace odh {
namespace {
[[noreturn]] void fail(const char* why){throw FormatError(std::string("PCM mixer: ")+why);}
void validate(PcmControls c){if(c.gain_q15>32768 || c.pan_q15< -32768 || c.pan_q15>32768)fail("invalid Q15 controls");}
void check_frequency_range(std::uint32_t frequency){
    if(frequency<100 || frequency>192000)fail("invalid effective frequency");
}
std::int16_t clamp(std::int64_t value){return static_cast<std::int16_t>(value>32767?32767:value< -32768?-32768:value);}
std::uint64_t add_mod(std::uint64_t a,std::uint64_t b,std::uint64_t modulus){
    return a>=modulus-b?a-(modulus-b):a+b;
}
std::uint64_t mul_mod(std::uint64_t a,std::uint32_t b,std::uint64_t modulus){
    a%=modulus;
    std::uint64_t result=0;
    while(b){
        if(b&1)result=add_mod(result,a,modulus);
        b>>=1;
        if(b)a=add_mod(a,a,modulus);
    }
    return result;
}
}
PcmMixer::PcmMixer(std::uint32_t rate,PcmMixerLimits limits):rate_(rate),limits_(limits){
    if(rate<8000 || rate>192000 || !limits.pcm_bytes || !limits.notes || limits.notes>256 ||
       !limits.block_frames || limits.block_frames>8192 || limits.pending_frames<limits.block_frames ||
       limits.pending_frames>65536)fail("invalid format or limits");
    notes_.resize(limits.notes);
}
const PcmMixer::Note& PcmMixer::find(PcmNoteHandle id) const {
    if(id)for(const auto& n:notes_)if(n.id==id)return n;
    fail("unknown note handle");
}
PcmMixer::Note& PcmMixer::find(PcmNoteHandle id){return const_cast<Note&>(static_cast<const PcmMixer&>(*this).find(id));}
void PcmMixer::free_pcm(Note& n){pcm_bytes_-=n.bytes;n.bytes=0;std::vector<std::int16_t>().swap(n.audio.samples);}
PcmNoteHandle PcmMixer::admit(XaAudio&& audio,PcmControls controls,bool reusable){
    validate(controls);
    if(audio.sample_rate<8000 || audio.sample_rate>192000 || (audio.channels!=1 && audio.channels!=2) ||
       audio.samples.size()%audio.channels)fail("invalid PCM source");
    if(audio.samples.capacity()>available_pcm_bytes()/sizeof(std::int16_t))fail("aggregate PCM budget exhausted");
    Note* slot=nullptr;for(auto& n:notes_)if(!n.id){slot=&n;break;}
    if(!slot)fail("note record budget exhausted");
    if(next_id_==std::numeric_limits<PcmNoteHandle>::max())fail("note handle sequence exhausted");
    const auto frames=audio.samples.size()/audio.channels;
    const auto max=std::numeric_limits<std::uint64_t>::max();
    if(frames>(max-(reusable?192000:audio.sample_rate))/rate_)fail("source duration overflow");
    const auto phase=static_cast<std::uint64_t>(frames)*rate_;
    const auto duration=phase/audio.sample_rate+(phase%audio.sample_rate!=0);
    if(!reusable && duration>max-rendered_)fail("note end overflow");
    slot->id=next_id_++;slot->start=rendered_;slot->end=reusable?rendered_:rendered_+duration;
    slot->state=reusable?PcmNoteState::stopped:(duration?PcmNoteState::active:PcmNoteState::complete);
    slot->controls=controls;slot->bytes=audio.samples.capacity()*sizeof(std::int16_t);
    slot->reusable=reusable;slot->frequency_hz=audio.sample_rate;slot->anchor_time=rendered_;
    pcm_bytes_+=slot->bytes;slot->audio=std::move(audio);
    if(!reusable && !duration)free_pcm(*slot);
    return slot->id;
}
PcmNoteHandle PcmMixer::add(XaAudio&& audio,PcmControls controls){
    return admit(std::move(audio),controls,false);
}
PcmNoteHandle PcmMixer::load_reusable(XaAudio&& audio,PcmControls controls){
    return admit(std::move(audio),controls,true);
}
std::uint64_t PcmMixer::duration(const Note& n,std::uint32_t frequency_hz) const {
    const auto count=n.audio.samples.size()/n.audio.channels;
    if(!count || n.loop)return 0;
    const auto period=static_cast<std::uint64_t>(count)*rate_;
    const auto phase=n.frame*rate_+n.fraction;
    if(phase>=period)return 0;
    const auto remaining=period-phase;
    return remaining/frequency_hz+(remaining%frequency_hz!=0);
}
void PcmMixer::validate_replay(PcmNoteHandle id,PcmReplayOptions options) const {
    const auto& n=find(id);
    if(!n.reusable)fail("replay requires reusable handle");
    check_frequency_range(options.frequency_hz);
    if(!options.loop){
        const auto count=n.audio.samples.size()/n.audio.channels;
        const auto period=static_cast<std::uint64_t>(count)*rate_;
        const auto frames=period/options.frequency_hz+(period%options.frequency_hz!=0);
        if(frames>std::numeric_limits<std::uint64_t>::max()-rendered_)fail("note end overflow");
    }
}
void PcmMixer::replay(PcmNoteHandle id,PcmReplayOptions options){
    validate_replay(id,options);
    if(rendered_!=consumed_)fail("replay requires consumed output boundary");
    auto& n=find(id);
    n.loop=options.loop;n.frequency_hz=options.frequency_hz;
    n.start=n.anchor_time=rendered_;n.frame=n.anchor_frame=0;
    n.fraction=n.anchor_fraction=0;
    const auto count=n.audio.samples.size()/n.audio.channels;
    n.end=rendered_+(options.loop?0:duration(n,n.frequency_hz));
    n.state=count?PcmNoteState::active:PcmNoteState::complete;
}
void PcmMixer::validate_frequency(PcmNoteHandle id,std::uint32_t frequency_hz) const {
    const auto& n=find(id);
    if(!n.reusable)fail("frequency change requires reusable handle");
    check_frequency_range(frequency_hz);
    if((n.state==PcmNoteState::active || n.state==PcmNoteState::rendered) && !n.loop){
        // A transport may discard pending output and rewind to an earlier
        // consumed cursor. Bound a full replay at today's later horizon so
        // the post-flush end calculation cannot fail after sink I/O.
        const auto count=n.audio.samples.size()/n.audio.channels;
        const auto period=static_cast<std::uint64_t>(count)*rate_;
        const auto maximum=period/frequency_hz+(period%frequency_hz!=0);
        if(maximum>std::numeric_limits<std::uint64_t>::max()-rendered_)
            fail("note end overflow");
    }
}
void PcmMixer::set_frequency(PcmNoteHandle id,std::uint32_t frequency_hz){
    validate_frequency(id,frequency_hz);
    auto& n=find(id);
    if((n.state==PcmNoteState::active || n.state==PcmNoteState::rendered) &&
       rendered_!=consumed_)
        fail("frequency change requires consumed output boundary");
    if(n.state==PcmNoteState::active){
        n.anchor_time=rendered_;n.anchor_frame=n.frame;n.anchor_fraction=n.fraction;
        if(!n.loop)n.end=rendered_+duration(n,frequency_hz);
    }
    n.frequency_hz=frequency_hz;
}
PcmNoteState PcmMixer::state(PcmNoteHandle id) const{return find(id).state;}
void PcmMixer::controls(PcmNoteHandle id,PcmControls controls){validate(controls);find(id).controls=controls;}
void PcmMixer::stop(PcmNoteHandle id){
    auto& n=find(id);if(n.state==PcmNoteState::complete || n.state==PcmNoteState::stopped)return;
    if(!n.reusable)free_pcm(n);
    n.state=PcmNoteState::stopped;
}
void PcmMixer::release(PcmNoteHandle id){stop(id);auto& n=find(id);free_pcm(n);n=Note{};}
void PcmMixer::render(std::int16_t* output,std::size_t frames){
    if(frames>limits_.block_frames || (frames && !output))fail("invalid output block");
    if(frames>limits_.pending_frames-(rendered_-consumed_))fail("pending output budget exhausted");
    if(frames>std::numeric_limits<std::uint64_t>::max()-rendered_)fail("output timeline overflow");
    for(std::size_t f=0;f<frames;++f){
        std::int64_t left=0,right=0;
        for(auto& n:notes_){
            if(!n.id || n.state!=PcmNoteState::active || rendered_+f<n.start)continue;
            const auto count=n.audio.samples.size()/n.audio.channels;
            const auto next=n.frame+1<count?n.frame+1:(n.loop?0:n.frame);
            auto sample=[&](unsigned channel){
                const auto ch=n.audio.channels==1?0:channel;
                const auto a=n.audio.samples[static_cast<std::size_t>(n.frame)*n.audio.channels+ch];
                const auto b=n.audio.samples[static_cast<std::size_t>(next)*n.audio.channels+ch];
                return (static_cast<std::int64_t>(a)*(rate_-n.fraction)+static_cast<std::int64_t>(b)*n.fraction)/rate_;
            };
            const auto pan=n.controls.pan_q15;
            left+=sample(0)*n.controls.gain_q15*(pan>0?32768-pan:32768)/1073741824LL;
            right+=sample(1)*n.controls.gain_q15*(pan<0?32768+pan:32768)/1073741824LL;
            n.frame+=n.frequency_hz/rate_;
            n.fraction+=n.frequency_hz%rate_;
            n.frame+=n.fraction/rate_;n.fraction%=rate_;
            if(n.loop)n.frame%=count;
            else if(n.frame>=count)n.state=PcmNoteState::rendered;
        }
        output[f*2]=clamp(left);output[f*2+1]=clamp(right);
    }
    rendered_+=frames;
}
void PcmMixer::commit(std::uint64_t frames){
    if(frames>rendered_-submitted_)fail("commit exceeds rendered prefix");
    submitted_+=frames;
}
void PcmMixer::acknowledge(std::uint64_t consumed){
    if(consumed<consumed_ || consumed>submitted_)fail("invalid backend consumption");
    consumed_=consumed;
    for(auto& n:notes_)if(n.id && n.state==PcmNoteState::rendered && n.end<=consumed_){
        if(!n.reusable)free_pcm(n);
        n.state=PcmNoteState::complete;
    }
}
void PcmMixer::rewind_pending(){
    rendered_=submitted_=consumed_;
    for(auto& n:notes_){
        if(!n.id || n.state==PcmNoteState::complete || n.state==PcmNoteState::stopped)continue;
        const auto when=consumed_>n.start?consumed_:n.start;
        const auto elapsed=when-n.anchor_time;
        const auto count=n.audio.samples.size()/n.audio.channels;
        const auto period=static_cast<std::uint64_t>(count)*rate_; // Bounded on admission.
        const auto anchor=n.anchor_frame*rate_+n.anchor_fraction;
        std::uint64_t phase=0;
        if(n.loop){
            phase=add_mod(anchor,mul_mod(elapsed,n.frequency_hz,period),period);
        }else if(elapsed>(period-anchor)/n.frequency_hz){
            phase=period;
        }else{
            phase=anchor+elapsed*n.frequency_hz;
            if(phase>period)phase=period;
        }
        n.frame=phase/rate_;n.fraction=static_cast<std::uint32_t>(phase%rate_);
        n.state=n.frame>=count?PcmNoteState::rendered:PcmNoteState::active;
    }
}
PcmMixerUsage PcmMixer::usage() const {
    PcmMixerUsage u{pcm_bytes_,0,0,rendered_,submitted_,consumed_};
    for(const auto& n:notes_)if(n.id){++u.handles;if(n.state==PcmNoteState::active)++u.active;}
    return u;
}
}
