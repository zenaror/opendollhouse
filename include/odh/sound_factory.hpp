#pragma once
#include "odh/sound_threads.hpp"
namespace odh {
struct ResolvedHitEvent {
    std::uint32_t track_id, start_pc;
    const HotTrack* initial_track;
};
struct SoundEventFactoryLimits {
    std::size_t event_name_bytes = 256; // Configurable 1..65536, before copying.
};
// Native controller metadata treats patch INT32_MIN as unset (effective 0).
// Raw mode preserves every patch ID, including that bit pattern.
enum class HitPatchMode { raw, native_controller };
// HIT/HOT are borrowed immutable resources and must outlive the instance.
// Entry resolution does not choose the final patch or execute HIT.
// Profile: event type1, resolved nonlooping bytecode track, one HOT group.
// Missing resources/unsupported profiles throw FormatError without fallback.
ResolvedHitEvent resolve_hit_event(
    const HitFile&, const Hot&, const SoundRequest&,
    SoundEventFactoryLimits = {}, HitPatchMode = HitPatchMode::raw);
// Arguments, chooser and backend are explicit; no unit/volume conversion.
// Backend lifetime must own resources with idempotent noexcept stop/RAII.
// Initial SetTrack may call chooser; no note starts before a later tick.
std::unique_ptr<SoundThreadInstance> make_hit_sound(
    const HitFile&, const Hot&, const SoundRequest&, const SoundSourceBinding&,
    HitChooser, HitNoteBackend, std::unique_ptr<SoundThreadLifetime>,
    HitThreadLimits, HitJumpOperand, HitArguments, SoundEventFactoryLimits = {},
    std::optional<HitNativeControls> = {});
}
