#pragma once

#include "odh/world.hpp"

namespace odh {
struct SoundEvent {
    std::string name;
    std::uint64_t remaining_bytes;
};
// NUL-terminated raw event bytes. The limit excludes the terminator; unknown
// trailing bytes remain in the input view and are counted, not interpreted.
SoundEvent read_fwav(const View&, std::size_t text_limit = 65536);

enum class SoundVariableProfile { simitone_avatar, simitone_object };
struct SoundSourceVariables {
    SoundVariableProfile profile;
    std::uint64_t generation;
    std::vector<std::int32_t> fields;
};
// Owned submission snapshot of Simitone's SubmitHITVars profile. Avatars need
// PersonData indices through 65 and produce 29 fields. Objects produce zero
// followed by their attribute bank. Missing banks/budget return nullopt rather
// than inventing avatar data or truncating object attributes. Negative signed
// skill/personality values retain division toward zero; skills only cap at100.
// Generation identifies this entity incarnation, not a bank revision. A future
// live sound thread must refresh variables separately as TickSounds does.
std::optional<SoundSourceVariables> sound_source_variables(const Entity&,
                                                          std::size_t field_limit = 256);

struct SoundRequest {
    std::uint16_t event_id, sample_rate;
    std::uint8_t flags, volume;
    std::int16_t source;
    std::string name;
    bool pan, zoom, loop;
    std::optional<SoundSourceVariables> source_variables;
};
using SoundResolver = std::function<std::optional<SoundEvent>(const VmFrame&, std::uint16_t)>;
using SoundSink = std::function<bool(const SoundRequest&)>;
struct PlaybackEvent;
// Optional presentation adapter for the first exact "sound" time-property key.
// Validates the emitting actor incarnation before capturing 29 avatar variables;
// accepts names of 1..256 bytes without embedded NUL. No audio executes here.
// The observer caller owns VM-generation validation and dispatch policy. Sink
// acceptance means submission only; resource resolution, event deduplication,
// shared ownership, spatial updates and audio persistence belong to the backend.
// Raw BHAV-only metadata stays zero; pan/zoom are true and loop is false.
void animation_sound_event(World&,const PlaybackEvent&,SoundSink = {},
    std::function<void(const SoundRequest&,const std::string& status)> observed = {});
// Live source lookup guarded by the submission's entity generation. World
// must outlive all copies. No Entity pointer or bank snapshot is retained.
// This is an ODH lifetime guard; it does not schedule or cancel audio.
class SoundSourceBinding {
public:
    static std::optional<SoundSourceBinding> bind(const World&, const SoundRequest&,
                                                 std::size_t field_limit = 256);
    bool valid() const; // Entity identity only; required banks may be missing.
    std::optional<SoundSourceVariables> refresh() const;
    std::optional<std::int32_t> read(std::int32_t source, std::int32_t field) const;
private:
    SoundSourceBinding(const World& world, std::int16_t source,
                       std::uint64_t generation, SoundVariableProfile profile, std::size_t limit)
        : world_(&world), source_(source), generation_(generation), profile_(profile), limit_(limit) {}
    const World* world_;
    std::int16_t source_;
    std::uint64_t generation_;
    SoundVariableProfile profile_;
    std::size_t limit_;
};
// World must outlive the primitive. The resolver supplies explicit code-owner
// resource lookup. Submission is nonblocking; acceptance does not prove playback.
// Source variables are captured before resolver/backend callbacks. An absent
// snapshot leaves existing nonblocking submission behavior unchanged; it does
// not establish a usable HIT source. HIT/sample resolution, active sound
// deduplication and spatial audio belong to
// a future backend. Sample rate, volume and flags are preserved as metadata.
VmPrimitive sound_primitive(World&, SoundResolver, SoundSink = {},
    std::function<void(const SoundRequest&, const std::string& status)> observed = {});
}
