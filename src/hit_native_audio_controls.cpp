#include "odh/hit_native_audio_controls.hpp"

#include "odh/hit_native_controller.hpp"

#include <cstring>

namespace odh {
namespace {

static_assert(sizeof(std::int32_t) == sizeof(std::uint32_t),
              "native HIT arithmetic requires 32-bit words");

std::uint32_t bits(std::int32_t value) noexcept {
    std::uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::int32_t signed_word(std::uint32_t value) noexcept {
    std::int32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::int32_t wrap_mul(std::int32_t a, std::int32_t b) noexcept {
    return signed_word(bits(a) * bits(b));
}

std::int32_t wrap_add(std::int32_t a, std::int32_t b) noexcept {
    return signed_word(bits(a) + bits(b));
}

std::int32_t wrap_sub(std::int32_t a, std::int32_t b) noexcept {
    return signed_word(bits(a) - bits(b));
}

std::int32_t local(const NativeHitState& state, std::uint8_t id) noexcept {
    return state.controls_17_47[id - 17];
}

} // namespace

NativeHitSpatialGainRequest compose_native_hit_spatial_gain(
    const NativeHitSpatialGainInputs& inputs) noexcept {
    const NativeHitSpatialGetterValues& effective = inputs.effective;
    std::int32_t gain = effective.external_19;
    if (inputs.group_gain && *inputs.group_gain != 1024) {
        gain = wrap_mul(*inputs.group_gain, gain) / 1024;
    }
    gain = wrap_mul(effective.volume_18, gain) / 1024;
    if (inputs.duck_threshold_123 != 0 &&
        effective.priority_25 < inputs.duck_threshold_123) {
        gain /= 2;
    }

    const bool position = effective.spatial_26 != 0;
    return {position ? NativeHitPlacement::position : NativeHitPlacement::pan,
            gain,
            inputs.local_pan_20,
            effective.position_30_32};
}

NativeHitPitchRequest compose_native_hit_pitch(
    const NativeHitPitchInputs& inputs) noexcept {
    return {wrap_sub(wrap_add(local(inputs.controller, 21), inputs.patch_21), 3600)};
}

} // namespace odh
