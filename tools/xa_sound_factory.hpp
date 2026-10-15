#pragma once
#include "xa_patch_catalog.hpp"
#include "odh/pcm_hit_owner.hpp"
#include "odh/sound_factory.hpp"

namespace odh_host {
// Host assembly of the existing explicit HOT/HIT profile. All resources and
// transport are borrowed and must outlive the returned instance. One owner
// thread; callbacks may not reenter or mutate the borrowed resources.
// Decoding is deferred until NoteOn, with current aggregate PCM allowance.
// Controls, HIT arguments/jump profile and group effects remain explicit;
// PlaySound volume/sample-rate and spatial flags are not converted here.
std::unique_ptr<odh::SoundThreadInstance> make_xa_hit_sound(
    const odh::HitFile&,const odh::Hot&,const odh::SoundRequest&,
    const odh::SoundSourceBinding&,odh::PcmTransport&,const XaPatchCatalog&,
    odh::HitChooser,odh::PcmControls,odh::HitThreadLimits,odh::HitJumpOperand,
    odh::HitArguments,std::function<void(std::uint8_t)> kill_group,
    odh::SoundEventFactoryLimits = {});
}
