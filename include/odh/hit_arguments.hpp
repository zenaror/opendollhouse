#pragma once
#include "odh/hit_selection.hpp"

namespace odh {
// Applies resolved EventMapping words independently to the raw caller tuple:
// a nonzero override replaces that word; zero preserves the caller word.
// Unknown/unsupported records reject instead of supplying an invented zero.
// No ArgStyle, source-cache sentinel, gain, pan or sample-rate conversion.
HitArguments resolve_hit_arguments(const HotEvent&,const HitArguments& caller);

// The already selected int16 entity source is sign-extended for argument 1;
// arguments 2/3 are raw zero in the observed PlaySound wrapper path. This
// helper does not select an owner or resolve native source sentinels.
HitArguments play_sound_arguments(const HotEvent&,std::int16_t selected_source);
}
