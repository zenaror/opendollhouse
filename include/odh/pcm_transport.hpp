#pragma once
#include "odh/mixer.hpp"

namespace odh {
struct PcmSinkFormat {
    std::uint32_t sample_rate;
    std::uint16_t channels;
};

// One owner controls the entire sink queue. Its format stays fixed for the
// transport lifetime; pause is a quiescent barrier for consumption and queue.
// queue accepts an interleaved stereo PCM16 prefix and reports frames accepted.
class PcmSink {
public:
    virtual ~PcmSink()=default;
    virtual PcmSinkFormat format() const=0;
    virtual std::size_t queued_frames() const=0;
    virtual std::size_t queue(const std::int16_t* samples,std::size_t frames)=0;
    virtual void pause()=0;
    virtual void start()=0;
    virtual void clear()=0;
};

// Single-threaded, caller-ticked transport. The sink reference must outlive
// this object. No other owner may queue/clear/start the sink during its life.
// Notes retain PcmMixer acknowledgement semantics, including rendered notes
// staying active to HIT until their final frame has actually been consumed.
class PcmTransport {
public:
    explicit PcmTransport(PcmSink& sink,PcmMixerLimits limits={});
    ~PcmTransport() noexcept;
    PcmTransport(const PcmTransport&)=delete;
    PcmTransport& operator=(const PcmTransport&)=delete;

    PcmNoteHandle add(XaAudio&& audio,PcmControls controls={});
    PcmNoteState state(PcmNoteHandle handle) const;
    void controls(PcmNoteHandle handle,PcmControls controls);
    void stop(PcmNoteHandle handle);
    void release(PcmNoteHandle handle);
    // One bounded poll/render/submit step. The caller supplies scheduling.
    void tick();
    PcmMixerUsage usage() const;
    std::uint64_t available_pcm_bytes() const;
    bool failed() const noexcept {return failed_;}
private:
    PcmSink& sink_;
    PcmMixer mixer_;
    std::vector<std::int16_t> block_;
    std::size_t block_frames_=0,retained_frames_=0,retained_cursor_=0;
    std::uint32_t pending_limit_=0;
    bool running_=false,failed_=false;
    void ensure_ready() const;
    void fail_closed() noexcept;
    void observe_consumption();
    void flush_pending();
    void submit_retained();
};
}
