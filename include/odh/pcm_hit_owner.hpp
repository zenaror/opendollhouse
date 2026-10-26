#pragma once
#include "odh/pcm_transport.hpp"
#include "odh/sound_threads.hpp"

namespace odh {
// Explicit execution profile for SeqGroupKill. The default requires a supplied
// callback. Simitone Instance (operand zero) stops this owner's current notes;
// nonzero operands are the pinned source's unimplemented no-op path. This is
// neither a native global group registry nor a request to stop this owner.
enum class PcmSequenceGroupProfile { explicit_callback, simitone_instance };
using HitPcmLoader=std::function<XaAudio(std::uint32_t patch,std::uint64_t pcm_allowance)>;

// Owns all PCM note handles started by one HIT thread. The transport and any
// references captured by the loader/group callback must outlive this owner.
// Returned backend callbacks borrow this stable owner and must not outlive it
// or reenter its callbacks. All calls use the transport's single owner thread.
// HIT complete means inactive: either fully consumed or deliberately stopped
// while this owner remains live. Owner stop and sink failure report HIT failed.
class PcmHitOwner final:public SoundThreadLifetime {
public:
    explicit PcmHitOwner(PcmTransport& transport,HitPcmLoader loader,
                         PcmControls controls={},std::size_t note_limit=64);
    ~PcmHitOwner() override;
    PcmHitOwner(const PcmHitOwner&)=delete;
    PcmHitOwner& operator=(const PcmHitOwner&)=delete;
    PcmHitOwner(PcmHitOwner&&)=delete;
    PcmHitOwner& operator=(PcmHitOwner&&)=delete;

    HitNoteBackend backend(std::function<void(std::uint8_t)> kill_group={},
        PcmSequenceGroupProfile = PcmSequenceGroupProfile::explicit_callback);
    void stop() noexcept override;
    bool cleanup_failed() const noexcept{return cleanup_failed_;}
private:
    PcmTransport& transport_;
    HitPcmLoader loader_;
    PcmControls controls_;
    std::size_t note_limit_;
    std::vector<PcmNoteHandle> handles_;
    bool stopped_=false,cleanup_failed_=false;
    mutable bool callback_busy_=false;
    std::optional<std::uint64_t> start(std::uint32_t patch);
    HitNoteStatus status(std::uint64_t handle) const;
};
}
