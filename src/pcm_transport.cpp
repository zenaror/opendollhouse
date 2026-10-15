#include "odh/pcm_transport.hpp"
#include <algorithm>

namespace odh {
namespace {
[[noreturn]] void fail(const char* why){throw FormatError(std::string("PCM transport: ")+why);}
std::uint32_t checked_rate(PcmSink& sink){
    const auto format=sink.format();
    if(format.channels!=2)fail("sink must be stereo PCM16");
    return format.sample_rate;
}
void validate_controls(PcmControls controls){
    if(controls.gain_q15>32768 || controls.pan_q15< -32768 || controls.pan_q15>32768)
        fail("invalid Q15 controls");
}
}

PcmTransport::PcmTransport(PcmSink& sink,PcmMixerLimits limits)
    :sink_(sink),mixer_(checked_rate(sink),limits),
     block_(static_cast<std::size_t>(limits.block_frames)*2),block_frames_(limits.block_frames),
     pending_limit_(limits.pending_frames){
    // An already populated queue belongs to someone else. Never clear it.
    if(sink_.queued_frames()!=0)fail("sink queue must be empty");
    sink_.pause();
}
PcmTransport::~PcmTransport() noexcept {
    try{sink_.pause();}catch(...){}
    try{sink_.clear();}catch(...){}
}
void PcmTransport::ensure_ready() const {if(failed_)fail("sink failed closed");}
void PcmTransport::fail_closed() noexcept {
    failed_=true;running_=false;
    try{sink_.pause();}catch(...){}
    try{sink_.clear();}catch(...){}
}
PcmNoteHandle PcmTransport::add(XaAudio&& audio,PcmControls controls){
    ensure_ready();return mixer_.add(std::move(audio),controls);
}
PcmNoteState PcmTransport::state(PcmNoteHandle handle) const{return mixer_.state(handle);}
PcmMixerUsage PcmTransport::usage() const{return mixer_.usage();}
std::uint64_t PcmTransport::available_pcm_bytes() const{return mixer_.available_pcm_bytes();}

void PcmTransport::observe_consumption(){
    const auto usage=mixer_.usage();
    try{
        const auto queued=sink_.queued_frames();
        if(queued>usage.submitted_frame-usage.consumed_frame)fail("sink queue exceeds submitted frames");
        mixer_.acknowledge(usage.submitted_frame-queued);
    }catch(...){fail_closed();throw;}
}
void PcmTransport::flush_pending(){
    // Pausing first makes the following queue count a stable consumed prefix.
    try{sink_.pause();running_=false;}
    catch(...){fail_closed();throw;}
    observe_consumption();
    try{sink_.clear();}
    catch(...){fail_closed();throw;}
    retained_frames_=retained_cursor_=0;
}
void PcmTransport::controls(PcmNoteHandle handle,PcmControls controls){
    ensure_ready();validate_controls(controls);
    const auto state=mixer_.state(handle); // Reject unknown handles before sink I/O.
    const auto usage=mixer_.usage();
    if((state==PcmNoteState::active || state==PcmNoteState::rendered) &&
       usage.rendered_frame>usage.consumed_frame){
        flush_pending();mixer_.controls(handle,controls);mixer_.rewind_pending();
    }else mixer_.controls(handle,controls);
}
void PcmTransport::stop(PcmNoteHandle handle){
    const auto state=mixer_.state(handle); // Allow cleanup even after failure.
    if(state==PcmNoteState::stopped || state==PcmNoteState::complete)return;
    if(failed_){mixer_.stop(handle);return;}
    if(mixer_.usage().rendered_frame>mixer_.usage().consumed_frame){
        try{flush_pending();}
        catch(...){mixer_.stop(handle);throw;}
        mixer_.stop(handle);mixer_.rewind_pending();
    }else mixer_.stop(handle);
}
void PcmTransport::release(PcmNoteHandle handle){
    const auto state=mixer_.state(handle);
    if((state==PcmNoteState::active || state==PcmNoteState::rendered) && !failed_ &&
       mixer_.usage().rendered_frame>mixer_.usage().consumed_frame){
        try{flush_pending();}
        catch(...){mixer_.release(handle);throw;}
        mixer_.release(handle);mixer_.rewind_pending();
    }else mixer_.release(handle);
}
void PcmTransport::submit_retained(){
    if(retained_cursor_==retained_frames_)return;
    const auto remaining=retained_frames_-retained_cursor_;
    try{
        const auto accepted=sink_.queue(block_.data()+retained_cursor_*2,remaining);
        if(accepted>remaining)fail("sink accepted beyond offered prefix");
        mixer_.commit(accepted);
        retained_cursor_+=accepted;
        if(accepted && !running_){sink_.start();running_=true;}
    }catch(...){fail_closed();throw;}
    if(retained_cursor_==retained_frames_)retained_frames_=retained_cursor_=0;
}
void PcmTransport::tick(){
    ensure_ready();observe_consumption();
    if(retained_cursor_<retained_frames_){submit_retained();return;}
    const auto usage=mixer_.usage();
    if(!usage.active)return;
    const auto pending=usage.rendered_frame-usage.consumed_frame;
    if(pending>=pending_limit_)return;
    const auto frames=std::min(block_frames_,static_cast<std::size_t>(pending_limit_-pending));
    mixer_.render(block_.data(),frames);
    retained_frames_=frames;retained_cursor_=0;
    submit_retained();
}
}
