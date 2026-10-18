#include "odh/hit_native_audio_controls.hpp"
#include "odh/hit_native_controller.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>

namespace {

void check(bool result, const char* case_name) {
    if (!result) {
        std::cerr << "FAIL: " << case_name << '\n';
        std::abort();
    }
}

void set(odh::NativeHitState& state, unsigned id, std::int32_t value) {
    state.controls_17_47[id - 17] = value;
}

odh::NativeHitSpatialGainInputs spatial_fixture(
    const odh::NativeHitState& state, std::optional<std::int32_t> group_gain,
    std::int32_t duck_threshold_123) {
    // This fixture explicitly chooses raw bank entries as getter results.
    // Production callers must resolve the effective getter values themselves.
    const auto value = [&](unsigned id) { return state.controls_17_47[id - 17]; };
    return {{value(18), value(19), value(25), value(26),
             {value(30), value(31), value(32)}},
            value(20), group_gain, duck_threshold_123};
}

void basic_pan_and_gain() {
    odh::NativeHitState state;
    set(state, 18, 512);
    set(state, 19, 800);
    set(state, 20, -123);
    set(state, 25, -20);
    const auto result = odh::compose_native_hit_spatial_gain(
        spatial_fixture(state, std::nullopt, 0));
    check(result.placement == odh::NativeHitPlacement::pan, "pan selected by zero ID26");
    check(result.pan_raw == -123, "pan uses local ID20");
    check(result.gain_raw == 400, "gain without group or duck");
}

void group_and_duck() {
    odh::NativeHitState state;
    set(state, 18, 512);
    set(state, 19, 800);
    set(state, 25, 5);
    const auto result = odh::compose_native_hit_spatial_gain(spatial_fixture(state, 768, 10));
    check(result.gain_raw == 150, "group then volume then signed duck");
    check(odh::compose_native_hit_spatial_gain(spatial_fixture(state, 768, 5)).gain_raw == 300,
          "duck threshold equality does not duck");
    check(odh::compose_native_hit_spatial_gain(spatial_fixture(state, 0, 0)).gain_raw == 0,
          "present zero group gain is distinct from absent group");
    set(state, 18, 900);
    set(state, 19, 2047);
    check(odh::compose_native_hit_spatial_gain(spatial_fixture(state, 600, 0)).gain_raw == 1053,
          "group and volume each divide after their own multiplication");
}

void truncation_and_wrap() {
    odh::NativeHitState state;
    set(state, 18, 1024);
    set(state, 19, 513);
    set(state, 25, 0);
    check(odh::compose_native_hit_spatial_gain(spatial_fixture(state, -3, 0)).gain_raw == -1,
          "negative group division truncates toward zero");
    check(odh::compose_native_hit_spatial_gain(spatial_fixture(state, -3, 1)).gain_raw == 0,
          "negative duck division truncates toward zero");

    set(state, 19, std::numeric_limits<std::int32_t>::max());
    check(odh::compose_native_hit_spatial_gain(
              spatial_fixture(state, std::nullopt, 0)).gain_raw == -1,
          "volume multiplication keeps signed low 32 bits");
    check(odh::compose_native_hit_spatial_gain(spatial_fixture(state, 1024, 0)).gain_raw == -1,
          "unit group gain bypasses multiplication");
    set(state, 18, 1);
    check(odh::compose_native_hit_spatial_gain(spatial_fixture(state, 1024, 0)).gain_raw == 2097151,
          "unit group bypass is observable across later volume multiply");

    set(state, 18, 1024);
    set(state, 19, 1024);
    check(odh::compose_native_hit_spatial_gain(
              spatial_fixture(state, std::numeric_limits<std::int32_t>::max(), 0))
              .gain_raw == -1,
          "group multiplication keeps signed low 32 bits");
    set(state, 19, std::numeric_limits<std::int32_t>::min());
    check(odh::compose_native_hit_spatial_gain(
              spatial_fixture(state, std::nullopt, 0)).gain_raw == 0,
          "INT32_MIN volume multiply wraps before division");
}

void position_and_phase_switch() {
    odh::NativeHitState first;
    set(first, 18, 1024);
    set(first, 19, 333);
    set(first, 20, 44);
    set(first, 21, 3700);
    set(first, 30, 7);
    set(first, 31, -8);
    set(first, 32, 9);
    const auto before = odh::compose_native_hit_spatial_gain(
        spatial_fixture(first, std::nullopt, 0));
    check(before.placement == odh::NativeHitPlacement::pan && before.pan_raw == 44,
          "first phase uses local pan");

    odh::NativeHitState second = first;
    set(second, 26, -1);
    const auto after = odh::compose_native_hit_spatial_gain(
        spatial_fixture(second, std::nullopt, 0));
    check(after.placement == odh::NativeHitPlacement::position,
          "nonzero signed ID26 selects position");
    check(after.position_raw == std::array<std::int32_t, 3>{7, -8, 9},
          "position keeps X/Y/Z argument order");
    check(after.gain_raw == before.gain_raw && after.gain_raw == 333,
          "ID26 phase switch preserves gain");
    check(odh::compose_native_hit_pitch({first, 3600}).argument_raw ==
          odh::compose_native_hit_pitch({second, 3600}).argument_raw,
          "pitch does not depend on spatial phase");
}

void pitch_wrap_and_signed_priority() {
    odh::NativeHitState state;
    set(state, 21, 100);
    check(odh::compose_native_hit_pitch({state, 3500}).argument_raw == 0,
          "pitch local plus Patch21 minus 3600");
    set(state, 21, std::numeric_limits<std::int32_t>::max());
    check(odh::compose_native_hit_pitch({state, 1}).argument_raw == 2147480048,
          "pitch add/sub wraps at signed boundary");
    set(state, 21, std::numeric_limits<std::int32_t>::min());
    check(odh::compose_native_hit_pitch({state, -1}).argument_raw == 2147480047,
          "negative pitch underflow wraps at signed boundary");

    set(state, 18, 1024);
    set(state, 19, 4);
    set(state, 25, std::numeric_limits<std::int32_t>::min());
    check(odh::compose_native_hit_spatial_gain(
              spatial_fixture(state, std::nullopt,
                              std::numeric_limits<std::int32_t>::max()))
              .gain_raw == 2,
          "duck comparison uses signed priority and threshold");
}

void resolved_getters_override_raw_sentinels() {
    odh::NativeHitState raw;
    constexpr auto sentinel = std::numeric_limits<std::int32_t>::min();
    for (unsigned id : {18u, 19u, 25u, 26u, 30u, 31u, 32u}) {
        set(raw, id, sentinel);
    }
    set(raw, 20, sentinel);

    odh::NativeHitSpatialGainInputs inputs{
        {512, 800, 9, 0, {4, 5, 6}}, raw.controls_17_47[20 - 17],
        std::nullopt, 10};
    const auto pan = odh::compose_native_hit_spatial_gain(inputs);
    check(pan.placement == odh::NativeHitPlacement::pan && pan.pan_raw == sentinel,
          "effective zero ID26 selects pan, preserving local INT32_MIN ID20");
    check(pan.gain_raw == 200,
          "effective getter 18/19/25 values override divergent raw sentinels");

    inputs.effective.spatial_26 = 1;
    const auto position = odh::compose_native_hit_spatial_gain(inputs);
    check(position.placement == odh::NativeHitPlacement::position &&
              position.position_raw == std::array<std::int32_t, 3>{4, 5, 6},
          "effective spatial selector and XYZ override divergent raw sentinels");
}

} // namespace

int main() {
    basic_pan_and_gain();
    group_and_duck();
    truncation_and_wrap();
    position_and_phase_switch();
    pitch_wrap_and_signed_priority();
    resolved_getters_override_raw_sentinels();
    std::cout << "native audio control requests: 6 cases passed\n";
}
