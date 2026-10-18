#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace odh {

struct NativeHitState;

// Values returned by the native getters in the A/spatial phase. The caller
// resolves any provider fallback before constructing these values; a raw bank
// entry (including INT32_MIN) does not necessarily equal its getter result.
struct NativeHitSpatialGetterValues {
    std::int32_t volume_18;
    std::int32_t external_19;
    std::int32_t priority_25;
    std::int32_t spatial_26;
    std::array<std::int32_t, 3> position_30_32; // X, Y, Z
};

// Raw arguments to the original wrapper's spatial and gain calls. The caller
// supplies effective getter results plus the controller's local ID20, which
// the pan call uses even when its public getter differs. This calculation
// acquires no resources.
// A missing group bypasses group gain. An existing group with gain 1024 also
// bypasses that multiplication, matching the native call sequence.
struct NativeHitSpatialGainInputs {
    NativeHitSpatialGetterValues effective;
    std::int32_t local_pan_20;
    std::optional<std::int32_t> group_gain;
    std::int32_t duck_threshold_123;
};

enum class NativeHitPlacement { pan, position };

struct NativeHitSpatialGainRequest {
    NativeHitPlacement placement;
    std::int32_t gain_raw;
    // Only the member selected by placement is a wrapper argument.
    std::int32_t pan_raw;
    std::array<std::int32_t, 3> position_raw; // X, Y, Z
};

// The wrapper receives pan or position before gain. Neither distance nor
// playback scaling/clamping is represented by this request.
NativeHitSpatialGainRequest compose_native_hit_spatial_gain(
    const NativeHitSpatialGainInputs& inputs) noexcept;

struct NativeHitPitchInputs {
    const NativeHitState& controller;
    std::int32_t patch_21;
};

struct NativeHitPitchRequest {
    std::int32_t argument_raw;
};

// Argument to the wrapper's pitch call, before its own pitch conversion.
NativeHitPitchRequest compose_native_hit_pitch(
    const NativeHitPitchInputs& inputs) noexcept;

} // namespace odh
