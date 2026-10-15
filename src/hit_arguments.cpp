#include "odh/hit_arguments.hpp"

namespace odh {
HitArguments resolve_hit_arguments(const HotEvent& event,const HitArguments& caller){
    if(!event.unsupported_reason.empty())
        throw FormatError("HOT EventMapping argument resolution: unsupported record: "+event.unsupported_reason);
    HitArguments resolved=caller;
    for(std::size_t n=0;n<resolved.size();++n){
        const auto override=event.argument_overrides[n];
        if(!override)throw FormatError("HOT EventMapping argument resolution: unresolved override");
        if(*override)resolved[n]=*override;
    }
    return resolved;
}
HitArguments play_sound_arguments(const HotEvent& event,std::int16_t selected_source){
    const HitArguments caller{static_cast<std::int32_t>(selected_source),0,0};
    return resolve_hit_arguments(event,caller);
}
}
