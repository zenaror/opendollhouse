#include "odh/mixer.hpp"
#include <limits>

namespace odh {
namespace {
[[noreturn]] void fail(const char* why){throw FormatError(std::string("PCM mixer: ")+why);}
void validate(PcmControls c){if(c.gain_q15>32768 || c.pan_q15< -32768 || c.pan_q15>32768)fail("invalid Q15 controls");}
std::int16_t clamp(std::int64_t value){return static_cast<std::int16_t>(value>32767?32767:value< -32768?-32768:value);}
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
PcmNoteHandle PcmMixer::add(XaAudio&& audio,PcmControls controls){
    validate(controls);
    if(audio.sample_rate<8000 || audio.sample_rate>192000 || (audio.channels!=1 && audio.channels!=2) ||
       audio.samples.size()%audio.channels)fail("invalid PCM source");
    if(audio.samples.capacity()>available_pcm_bytes()/sizeof(std::int16_t))fail("aggregate PCM budget exhausted");
    Note* slot=nullptr;for(auto& n:notes_)if(!n.id){slot=&n;break;}
    if(!slot)fail("note record budget exhausted");
    if(next_id_==std::numeric_limits<PcmNoteHandle>::max())fail("note handle sequence exhausted");
    const auto frames=audio.samples.size()/audio.channels;
    const auto max=std::numeric_limits<std::uint64_t>::max();
    if(frames>(max-audio.sample_rate)/rate_)fail("source duration overflow");
    const auto duration=(static_cast<std::uint64_t>(frames)*rate_+audio.sample_rate-1)/audio.sample_rate;
    if(duration>max-rendered_)fail("note end overflow");
    slot->id=next_id_++;slot->start=rendered_;slot->end=rendered_+duration;
    slot->state=duration?PcmNoteState::active:PcmNoteState::complete;
    slot->controls=controls;slot->bytes=audio.samples.capacity()*sizeof(std::int16_t);
    pcm_bytes_+=slot->bytes;slot->audio=std::move(audio);
    if(!duration)free_pcm(*slot);
    return slot->id;
}
PcmNoteState PcmMixer::state(PcmNoteHandle id) const{return find(id).state;}
void PcmMixer::controls(PcmNoteHandle id,PcmControls controls){validate(controls);find(id).controls=controls;}
void PcmMixer::stop(PcmNoteHandle id){
    auto& n=find(id);if(n.state==PcmNoteState::complete || n.state==PcmNoteState::stopped)return;
    free_pcm(n);n.state=PcmNoteState::stopped;
}
void PcmMixer::release(PcmNoteHandle id){stop(id);find(id)=Note{};}
void PcmMixer::render(std::int16_t* output,std::size_t frames){
    if(frames>limits_.block_frames || (frames && !output))fail("invalid output block");
    if(frames>limits_.pending_frames-(rendered_-consumed_))fail("pending output budget exhausted");
    if(frames>std::numeric_limits<std::uint64_t>::max()-rendered_)fail("output timeline overflow");
    for(std::size_t f=0;f<frames;++f){
        std::int64_t left=0,right=0;
        for(auto& n:notes_){
            if(!n.id || n.state!=PcmNoteState::active || rendered_+f<n.start)continue;
            const auto count=n.audio.samples.size()/n.audio.channels;
            const auto next=n.frame+1<count?n.frame+1:n.frame;
            auto sample=[&](unsigned channel){
                const auto ch=n.audio.channels==1?0:channel;
                const auto a=n.audio.samples[static_cast<std::size_t>(n.frame)*n.audio.channels+ch];
                const auto b=n.audio.samples[static_cast<std::size_t>(next)*n.audio.channels+ch];
                return (static_cast<std::int64_t>(a)*(rate_-n.fraction)+static_cast<std::int64_t>(b)*n.fraction)/rate_;
            };
            const auto pan=n.controls.pan_q15;
            left+=sample(0)*n.controls.gain_q15*(pan>0?32768-pan:32768)/1073741824LL;
            right+=sample(1)*n.controls.gain_q15*(pan<0?32768+pan:32768)/1073741824LL;
            n.frame+=n.audio.sample_rate/rate_;
            n.fraction+=n.audio.sample_rate%rate_;
            n.frame+=n.fraction/rate_;n.fraction%=rate_;
            if(n.frame>=count)n.state=PcmNoteState::rendered;
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
    for(auto& n:notes_)if(n.id && n.state==PcmNoteState::rendered && n.end<=consumed_){free_pcm(n);n.state=PcmNoteState::complete;}
}
void PcmMixer::rewind_pending(){
    rendered_=submitted_=consumed_;
    for(auto& n:notes_){
        if(!n.id || n.state==PcmNoteState::complete || n.state==PcmNoteState::stopped)continue;
        const auto elapsed=consumed_>n.start?consumed_-n.start:0;
        const auto phase=elapsed*n.audio.sample_rate; // Bounded by duration validation in add.
        n.frame=phase/rate_;n.fraction=static_cast<std::uint32_t>(phase%rate_);
        n.state=PcmNoteState::active;
    }
}
PcmMixerUsage PcmMixer::usage() const {
    PcmMixerUsage u{pcm_bytes_,0,0,rendered_,submitted_,consumed_};
    for(const auto& n:notes_)if(n.id){++u.handles;if(n.state==PcmNoteState::active)++u.active;}
    return u;
}
}
