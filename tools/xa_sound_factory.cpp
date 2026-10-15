#include "xa_sound_factory.hpp"

namespace odh_host {
std::unique_ptr<odh::SoundThreadInstance> make_xa_hit_sound(
    const odh::HitFile& hit,const odh::Hot& hot,const odh::SoundRequest& request,
    const odh::SoundSourceBinding& binding,odh::PcmTransport& transport,const XaPatchCatalog& catalog,
    odh::HitChooser chooser,odh::PcmControls controls,odh::HitThreadLimits limits,odh::HitJumpOperand operand,
    odh::HitArguments arguments,std::function<void(std::uint8_t)> kill_group,
    odh::SoundEventFactoryLimits factory_limits){
    auto owner=std::make_unique<odh::PcmHitOwner>(transport,
        [&hot,&catalog](std::uint32_t patch,std::uint64_t allowance){
            return catalog.load(hot,patch,allowance).audio;
        },controls,limits.notes);
    auto backend=owner->backend(std::move(kill_group));
    return odh::make_hit_sound(hit,hot,request,binding,std::move(chooser),std::move(backend),
        std::move(owner),limits,operand,arguments,factory_limits);
}
}
