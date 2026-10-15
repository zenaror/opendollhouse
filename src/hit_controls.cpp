#include "odh/hit_controls.hpp"

namespace odh {
namespace {
std::int32_t signed_bits(std::uint32_t bits) {
    if (bits <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return static_cast<std::int32_t>(bits);
    return -1 - static_cast<std::int32_t>(~bits);
}
}
HitNativeControls build_native_hit_controls(
    const HotTrack& track, const std::array<std::int32_t, 3>& raw_arguments,
    std::int32_t spatial_external_volume, std::int32_t spatial_pan,
    const HitNativeControllerValues& controllers) {
    if (!track.unsupported_reason.empty())
        throw FormatError("native HIT controls require a supported HOT track: " + track.unsupported_reason);
    if (!track.arg_type || *track.arg_type != 2)
        throw FormatError("native HIT controls require resolved ArgStyle 2");
    if (!track.volume || !track.ducking_priority || !track.patch_id ||
        !track.sound_pressure_level)
        throw FormatError("native HIT controls require resolved Track.volume/ducking_priority/patch_id/Spl");

    HitNativeControls result;
    result.raw_arguments = raw_arguments;
    result.raw_track_volume = signed_bits(*track.volume);
    result.raw_track_ducking_priority = signed_bits(*track.ducking_priority);
    result.raw_track_patch = signed_bits(*track.patch_id);
    result.raw_track_sound_pressure_level = signed_bits(*track.sound_pressure_level);
    result.spatial_external_volume = spatial_external_volume;
    result.spatial_pan = spatial_pan;
    result.initial_zero_flag = spatial_pan == 0;
    result.initial_sign_flag = spatial_pan < 0;
    result.locals[0] = 0; // HSM ID 16, h4: fresh register initialization.
    for (std::size_t id = 17; id <= 47; ++id) result.locals[id - 16] = 0;
    result.locals[17 - 16] = 32;
    result.locals[18 - 16] = 1024;
    result.locals[19 - 16] = 1024;
    result.locals[20 - 16] = 512;
    result.locals[21 - 16] = 3600;
    result.locals[28 - 16] = 100;
    result.locals[29 - 16] = 10000;
    result.locals[39 - 16] = 1;
    // Track fields are controller stores. The initializer skips its unset
    // sentinel, leaving the already installed global defaults in place.
    if (result.raw_track_volume != std::numeric_limits<std::int32_t>::min())
        result.locals[18 - 16] = result.raw_track_volume;
    if (result.raw_track_ducking_priority != std::numeric_limits<std::int32_t>::min())
        result.locals[25 - 16] = result.raw_track_ducking_priority;
    for (std::size_t id = 50; id <= 62; ++id) result.locals[id - 16] = 0;
    result.locals[52 - 16] = 120;
    result.locals[57 - 16] = 20;
    if (result.raw_track_patch != std::numeric_limits<std::int32_t>::min())
        result.locals[50 - 16] = result.raw_track_patch;
    if (result.raw_track_sound_pressure_level != std::numeric_limits<std::int32_t>::min())
        result.locals[57 - 16] = result.raw_track_sound_pressure_level;
    for (std::size_t i = 0; i < controllers.size(); ++i)
        if (controllers[i] && *controllers[i] != std::numeric_limits<std::int32_t>::min())
            result.locals[17 + i - 16] = *controllers[i];
    result.locals[19 - 16] = raw_arguments[1];
    result.locals[20 - 16] = raw_arguments[2];
    result.locals[47 - 16] = raw_arguments[0];
    result.locals[46 - 16] = 1;
    result.locals[19 - 16] = spatial_external_volume;
    result.locals[20 - 16] = spatial_pan;
    return result;
}
}
