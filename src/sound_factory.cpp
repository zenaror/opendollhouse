#include "odh/sound_factory.hpp"
#include <limits>

namespace odh {
namespace {
[[noreturn]] void fail(const char* reason) { throw FormatError(std::string("sound factory: ")+reason); }
struct StopOnFailure {
    std::unique_ptr<SoundThreadLifetime>& owner;
    ~StopOnFailure() { if(owner) owner->stop(); }
};
}
ResolvedHitEvent resolve_hit_event(const HitFile& hit,const Hot& hot,const SoundRequest& request,
                                   SoundEventFactoryLimits limits) {
    if(!limits.event_name_bytes || limits.event_name_bytes>65536)fail("invalid event name budget");
    if(request.loop)fail("loop request outside supported profile");
    if(request.name.empty() || request.name.size()>limits.event_name_bytes ||
       request.name.find('\0')!=std::string::npos)fail("invalid or over-budget event name");
    auto name=request.name;
    for(auto& c:name)if(c>='A' && c<='Z')c=static_cast<char>(c+('a'-'A'));
    const auto event=hot.events.find(name);
    if(event==hot.events.end())fail("event missing in HOT");
    const auto& mapping=event->second;
    if(!mapping.unsupported_reason.empty() || !mapping.event_type || *mapping.event_type!=1 || !mapping.track_id)
        fail("unsupported or unresolved event mapping");
    const auto found=hot.tracks.find(*mapping.track_id);
    if(found==hot.tracks.end())fail("track missing in HOT");
    const auto& track=found->second;
    if(!track.unsupported_reason.empty() || !track.subroutine_offset || !track.hitlist_id || !track.patch_id)
        fail("unsupported or unresolved track");
    if(hit.major_version!=1 || (hit.minor_version!=0 && hit.minor_version!=8))fail("unsupported HIT version");
    const auto end=hit.entry_table_offset.value_or(hit.bytes.size());
    if(hit.bytes.size()>std::numeric_limits<std::uint32_t>::max() || end>hit.bytes.size() || end<=16 ||
       *track.subroutine_offset<16 || *track.subroutine_offset>=end)fail("track entry outside HIT code region");
    if(*track.hitlist_id) {
        const auto list=hot.hitlists.find(*track.hitlist_id);
        if(list==hot.hitlists.end() || list->second.ids.empty() || !list->second.unsupported_reason.empty())
            fail("initial hitlist missing, empty or unsupported");
    }
    if(*track.patch_id) {
        const auto patch=hot.patches.find(*track.patch_id);
        if(patch==hot.patches.end())fail("initial patch missing in HOT");
        if(patch->second.looped)fail("looped initial patch outside supported profile");
    } else if(!*track.hitlist_id)fail("track has neither initial patch nor hitlist");
    return {*mapping.track_id,*track.subroutine_offset,&track};
}
std::unique_ptr<SoundThreadInstance> make_hit_sound(const HitFile& hit,const Hot& hot,
    const SoundRequest& request,const SoundSourceBinding& binding,HitChooser chooser,
    HitNoteBackend backend,std::unique_ptr<SoundThreadLifetime> owner,HitThreadLimits limits,
    HitJumpOperand operand,HitArguments args,SoundEventFactoryLimits factory_limits) {
    StopOnFailure cleanup{owner};
    if(!owner)fail("backend lifetime owner required");
    const auto current=binding.refresh();
    if(!request.source_variables || !current || !binding.read(request.source,0) ||
       current->generation!=request.source_variables->generation ||
       current->profile!=request.source_variables->profile)fail("source binding does not match submission");
    const auto resolved=resolve_hit_event(hit,hot,request,factory_limits);
    HitSourceFieldReader reader=[binding](std::int32_t source,std::int32_t field){return binding.read(source,field);};
    std::array<std::int32_t,29> unused{}; // Reader replaces these values, never a fallback.
    auto thread=std::make_unique<HitThread>(hit,hot,resolved.start_pc,unused,std::move(chooser),
        std::move(backend),limits,resolved.initial_track,operand,std::move(reader),args);
    // Initial SetTrack's chooser can mutate the World through other aliases.
    if(!binding.valid())fail("source removed during initialization");
    return std::make_unique<SoundThreadInstance>(std::move(thread),std::move(owner));
}
}
