#pragma once

#include "odh/hit_resources.hpp"
#include <limits>

namespace odh {
// The native controller bank covers IDs 17..47. An absent value or INT32_MIN
// means unset; neither suppresses the corresponding proven default.
using HitNativeControllerValues = std::array<std::optional<std::int32_t>, 31>;

// Owned, raw pre-script ArgStyle 2 state. locals[index] corresponds to HIT ID
// index+16; nullopt means no supported native startup value is known. The
// spatial values are caller-supplied raw control values, not computed geometry
// or backend gain/pan settings. Selection validates the builder-produced
// known mask and final setter invariants before execution. No bytecode write
// side effects are implied.
struct HitNativeControls {
    std::array<std::optional<std::int32_t>, 54> locals{};
    std::array<std::int32_t, 3> raw_arguments{};
    std::int32_t raw_track_volume = 0;
    std::int32_t raw_track_ducking_priority = 0;
    std::int32_t raw_track_patch = 0;
    std::int32_t spatial_external_volume = 0;
    std::int32_t spatial_pan = 0;
    // The final pre-scheduling pan setter also establishes interpreter flags.
    bool initial_zero_flag = false;
    bool initial_sign_flag = false;
};

// Requires resolved ArgStyle 2, volume, ducking priority and raw patch ID.
// Defaults are applied first, then non-unset Track.volume and
// Track.ducking_priority, non-unset controller values, ArgStyle 2 raw argument
// stores (IDs 19,20,47),
// the pre-activation isplaying ID 46 store, and finally the explicit
// post-spatial values for IDs 19/20. ID 16 is known zero;
// ID 50 retains raw Track.patch_id even if initial list choice selects another
// patch. Other IDs 48..69 stay unknown. No chooser/backend effects occur.
HitNativeControls build_native_hit_controls(
    const HotTrack& track, const std::array<std::int32_t, 3>& raw_arguments,
    std::int32_t spatial_external_volume, std::int32_t spatial_pan,
    const HitNativeControllerValues& controllers = {});
}
