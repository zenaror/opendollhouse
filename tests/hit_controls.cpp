#include "odh/hit_controls.hpp"
#include <iostream>

namespace {
int checks = 0;
void require(bool condition, const char* reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}
template<class F> void rejects(F&& action) {
    bool failed = false;
    try { action(); } catch (const odh::FormatError&) { failed = true; }
    require(failed, "invalid native control input rejects");
}
}
int main() { try {
    odh::HotTrack track;
    track.arg_type = 2;
    track.volume = 777;
    track.ducking_priority = 9;
    track.patch_id = 200;
    track.sound_pressure_level = 30;
    const std::array<std::int32_t, 3> raw{{-1, 0, 1024}};
    const auto plain = odh::build_native_hit_controls(track, raw, 888, 444);
    require(plain.raw_arguments == raw && plain.raw_track_volume == 777 &&
            plain.raw_track_ducking_priority == 9 && plain.locals[25 - 16] == 9 &&
            plain.raw_track_patch == 200 && plain.locals[50 - 16] == 200 &&
            plain.raw_track_sound_pressure_level == 30 && plain.locals[57 - 16] == 30 &&
            plain.spatial_external_volume == 888 && plain.spatial_pan == 444 &&
            !plain.initial_zero_flag && !plain.initial_sign_flag,
            "builder owns raw arguments and explicit track/spatial inputs");
    for (const auto pan : {std::int32_t{0}, std::int32_t{-512}, std::int32_t{512}}) {
        const auto flagged = odh::build_native_hit_controls(track, raw, 888, pan);
        require(flagged.locals[20 - 16] == pan && flagged.initial_zero_flag == (pan == 0) &&
                flagged.initial_sign_flag == (pan < 0),
                "final spatial pan determines native pre-script flags");
    }
    require(plain.locals[0] == 0 && plain.locals[1] == 32 && plain.locals[2] == 777 &&
            plain.locals[3] == 888 && plain.locals[4] == 444 && plain.locals[5] == 3600 &&
            plain.locals[46 - 16] == 1 && plain.locals[47 - 16] == -1,
            "known native defaults, Track.volume and post-spatial controls installed");
    require(plain.locals[28 - 16] == 100 && plain.locals[29 - 16] == 10000 &&
            plain.locals[39 - 16] == 1, "additional proven nonzero bank defaults installed");
    for (std::size_t id = 16; id <= 47; ++id)
        require(plain.locals[id - 16].has_value(), "every proven native startup control is known");
    for (std::size_t id = 50; id <= 62; ++id)
        require(plain.locals[id - 16].has_value(), "every proven fresh second-bank control is known");
    require(plain.locals[52 - 16] == 120 && plain.locals[55 - 16] == 0 &&
            plain.locals[53 - 16] == 0, "second-bank defaults include speed and active-update gates");
    for (std::size_t id = 48; id <= 69; ++id) if (id < 50 || id > 62)
        require(!plain.locals[id - 16], "unproven later locals stay unknown");
    odh::HitNativeControllerValues controllers{};
    controllers[0] = std::numeric_limits<std::int32_t>::min(); // ID 17 unset.
    controllers[1] = -300; // ID 18 overrides Track.volume.
    controllers[2] = -700; // ID 19 overwritten by raw R2, then spatial.
    controllers[3] = -600; // ID 20 overwritten by raw R3, then spatial.
    controllers[10] = 4444; // ID 27.
    controllers[25 - 17] = 55; // ID 25 overrides Track.ducking_priority.
    controllers[46 - 17] = 0;
    controllers[47 - 17] = 999;
    const auto applied = odh::build_native_hit_controls(track, raw, -123, 0, controllers);
    require(applied.locals[1] == 32 && applied.locals[2] == -300 &&
            applied.locals[3] == -123 && applied.locals[4] == 0 &&
            applied.locals[27 - 16] == 4444 && applied.locals[25 - 16] == 55 &&
            applied.locals[46 - 16] == 1 && applied.locals[47 - 16] == -1,
            "unset sentinel skipped, controller order and spatial override retained");
    require(track.volume == 777 && raw == std::array<std::int32_t, 3>{{-1, 0, 1024}} &&
            controllers[1] == -300, "pure builder leaves inputs unchanged");
    track.volume = 0x80000000U;
    track.ducking_priority = 0x80000000U;
    const auto unset_track = odh::build_native_hit_controls(track, raw, 0, 0);
    require(unset_track.raw_track_volume == std::numeric_limits<std::int32_t>::min() &&
            unset_track.raw_track_ducking_priority == std::numeric_limits<std::int32_t>::min() &&
            unset_track.locals[18 - 16] == 1024 && unset_track.locals[25 - 16] == 0,
            "Track unset bit patterns remain in receipt but preserve global defaults");
    odh::HitNativeControllerValues after_unset{};
    after_unset[18 - 17] = 321;
    after_unset[25 - 17] = -7;
    const auto overridden_unset = odh::build_native_hit_controls(track, raw, 0, 0, after_unset);
    require(overridden_unset.locals[18 - 16] == 321 &&
            overridden_unset.locals[25 - 16] == -7,
            "valid later controllers override defaults after unset Track fields");
    track.ducking_priority = 9;
    track.patch_id = 0x80000000U;
    track.sound_pressure_level = 0x80000000U;
    const auto raw_patch = odh::build_native_hit_controls(track, raw, 0, 0);
    require(raw_patch.raw_track_patch == std::numeric_limits<std::int32_t>::min() &&
            raw_patch.locals[50 - 16] == 0 &&
            raw_patch.raw_track_sound_pressure_level == std::numeric_limits<std::int32_t>::min() &&
            raw_patch.locals[57 - 16] == 20,
            "Track patch/Spl sentinels retain raw metadata and preserve fresh bank defaults");
    track.patch_id = 200;
    track.sound_pressure_level = 0;
    require(odh::build_native_hit_controls(track, raw, 0, 0).locals[57 - 16] == 0,
            "resolved Spl zero overrides fresh default twenty");
    track.volume.reset(); rejects([&] { odh::build_native_hit_controls(track, raw, 0, 0); });
    track.volume = 0;
    track.patch_id.reset(); rejects([&] { odh::build_native_hit_controls(track, raw, 0, 0); });
    track.patch_id = 200;
    track.ducking_priority.reset(); rejects([&] { odh::build_native_hit_controls(track, raw, 0, 0); });
    track.ducking_priority = 9;
    track.sound_pressure_level.reset(); rejects([&] { odh::build_native_hit_controls(track, raw, 0, 0); });
    track.sound_pressure_level = 30;
    track.arg_type.reset(); rejects([&] { odh::build_native_hit_controls(track, raw, 0, 0); });
    track.arg_type = 1; rejects([&] { odh::build_native_hit_controls(track, raw, 0, 0); });
    track.arg_type = 2; track.unsupported_reason = "unresolved field";
    rejects([&] { odh::build_native_hit_controls(track, raw, 0, 0); });
    std::cout << checks << " native HIT control checks passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
