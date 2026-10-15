#include "odh/pcm_hit_owner.hpp"
#include <algorithm>

namespace odh {
namespace {
struct CallbackGuard {
    bool& busy;
    explicit CallbackGuard(bool& value):busy(value){
        if(busy)throw FormatError("PCM HIT owner: callback reentrancy");
        busy=true;
    }
    ~CallbackGuard(){busy=false;}
};
}
PcmHitOwner::PcmHitOwner(PcmTransport& transport,HitPcmLoader loader,
                       PcmControls controls,std::size_t note_limit)
    :transport_(transport),loader_(std::move(loader)),controls_(controls),note_limit_(note_limit){
    if(!loader_ || !note_limit_ || note_limit_>256 || controls.gain_q15>32768 ||
       controls.pan_q15< -32768 || controls.pan_q15>32768)
        throw FormatError("PCM HIT owner: invalid loader, controls or note limit");
    handles_.reserve(note_limit_);
}
PcmHitOwner::~PcmHitOwner(){stop();}
std::optional<std::uint64_t> PcmHitOwner::start(std::uint32_t patch){
    CallbackGuard guard(callback_busy_);
    if(stopped_ || handles_.size()>=note_limit_)return std::nullopt;
    if(transport_.failed())throw FormatError("PCM HIT owner: transport failed");
    auto pcm=loader_(patch,transport_.available_pcm_bytes());
    if(stopped_)return std::nullopt; // A callback may request owner cleanup.
    const auto handle=transport_.add(std::move(pcm),controls_);
    handles_.push_back(handle); // Preallocated before any note was admitted.
    return handle;
}
HitNoteStatus PcmHitOwner::status(std::uint64_t handle) const {
    CallbackGuard guard(callback_busy_);
    if(std::find(handles_.begin(),handles_.end(),handle)==handles_.end())
        throw FormatError("PCM HIT owner: unknown note handle");
    if(stopped_ || transport_.failed())return HitNoteStatus::failed;
    switch(transport_.state(handle)){
        case PcmNoteState::active:
        case PcmNoteState::rendered:return HitNoteStatus::active;
        case PcmNoteState::complete:
        case PcmNoteState::stopped:return HitNoteStatus::complete;
    }
    throw FormatError("PCM HIT owner: invalid note state");
}
HitNoteBackend PcmHitOwner::backend(std::function<void(std::uint8_t)> kill_group){
    HitNoteBackend result;
    result.start=[this](std::uint32_t patch){return start(patch);};
    result.status=[this](std::uint64_t handle){return status(handle);};
    if(kill_group)result.kill_group=[this,handler=std::move(kill_group)](std::uint8_t group){
        CallbackGuard guard(callback_busy_);
        if(stopped_)throw FormatError("PCM HIT owner: group callback after stop");
        if(transport_.failed())throw FormatError("PCM HIT owner: transport failed");
        handler(group);
    };
    return result;
}
void PcmHitOwner::stop() noexcept {
    if(stopped_)return;
    stopped_=true;
    for(const auto handle:handles_){
        try{transport_.release(handle);}
        catch(...){cleanup_failed_=true;}
    }
}
}
