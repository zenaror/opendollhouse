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

struct SoundRequest {
    std::uint16_t event_id, sample_rate;
    std::uint8_t flags, volume;
    std::int16_t source;
    std::string name;
    bool pan, zoom, loop;
};
using SoundResolver = std::function<std::optional<SoundEvent>(const VmFrame&, std::uint16_t)>;
using SoundSink = std::function<bool(const SoundRequest&)>;
// World must outlive the primitive. The resolver supplies explicit code-owner
// resource lookup. Submission is nonblocking; acceptance does not prove playback.
// HIT/sample resolution, active sound deduplication and spatial audio belong to
// a future backend. Sample rate, volume and flags are preserved as metadata.
VmPrimitive sound_primitive(World&, SoundResolver, SoundSink = {},
    std::function<void(const SoundRequest&, const std::string& status)> observed = {});
}
