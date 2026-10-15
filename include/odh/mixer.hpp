#pragma once
#include "odh/xa_audio.hpp"

namespace odh {
using PcmNoteHandle=std::uint64_t;
enum class PcmNoteState { active, rendered, complete, stopped };
struct PcmControls {std::uint32_t gain_q15=32768;std::int32_t pan_q15=0;};
struct PcmMixerLimits {
    std::uint64_t pcm_bytes=4*1024*1024;
    std::uint32_t notes=64,block_frames=1024,pending_frames=8192;
};
struct PcmMixerUsage {
    std::uint64_t pcm_bytes,handles,active,rendered_frame,submitted_frame,consumed_frame;
};
// Portable deterministic stereo PCM16 profile: linear rational resampling,
// explicit Q15 gain and linear balance, wide sum then one saturation. Not a
// claim about the original game's resampler/pan law. One owner, no callbacks.
class PcmMixer {
public:
    explicit PcmMixer(std::uint32_t output_rate=44100,PcmMixerLimits = {});
    PcmMixer(const PcmMixer&)=delete;
    PcmMixer& operator=(const PcmMixer&)=delete;
    // Moves PCM only after validation/admission. Charges vector capacity, not
    // just size, until completion/stop/release; failed admission leaves input.
    PcmNoteHandle add(XaAudio&&,PcmControls = {});
    PcmNoteState state(PcmNoteHandle) const;
    void controls(PcmNoteHandle,PcmControls);
    void stop(PcmNoteHandle); // Future samples only; already mixed data persists.
    void release(PcmNoteHandle); // Stops and invalidates handle; IDs never reused.
    // Caller supplies frames*2 samples, disjoint from submitted PCM. No render
    // allocation; retains unaccepted output externally until commit or rewind.
    void render(std::int16_t* output,std::size_t frames);
    void commit(std::uint64_t frames); // Prefix actually accepted by backend.
    void acknowledge(std::uint64_t consumed_frame); // Monotonic, <=submitted.
    // First acknowledge latest consumption BEFORE backend clear, then discard
    // all unconsumed output, including caller's
    // retained buffers. Surviving notes resume at consumed_frame; stopped notes
    // stay stopped. Consumed backend data cannot be recalled. No clock reset.
    void rewind_pending();
    PcmMixerUsage usage() const;
    std::uint32_t output_rate() const {return rate_;}
    std::uint64_t available_pcm_bytes() const {return limits_.pcm_bytes-pcm_bytes_;}
private:
    struct Note {
        PcmNoteHandle id=0;
        PcmNoteState state=PcmNoteState::complete;
        XaAudio audio{};
        PcmControls controls{};
        std::uint64_t bytes=0,start=0,end=0,frame=0;
        std::uint32_t fraction=0;
    };
    std::uint32_t rate_;
    PcmMixerLimits limits_;
    std::vector<Note> notes_;
    std::uint64_t pcm_bytes_=0,next_id_=1,rendered_=0,submitted_=0,consumed_=0;
    Note& find(PcmNoteHandle);
    const Note& find(PcmNoteHandle) const;
    void free_pcm(Note&);
};
}
