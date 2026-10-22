#pragma once
#include "odh/formats.hpp"
#include <utility>
namespace odh {
struct AnimationLimits {
    std::uint32_t strings=65536,animations=4096,motions=4096,properties=65536,samples=1048576;
    std::size_t text_bytes=4194304;
};
enum class StringTableState { absent,header_only,decoded };
struct StringEntry {std::uint8_t language;std::string value,comment;};
struct StringTable {
    StringTableState state;
    std::optional<std::int16_t> format;
    std::vector<StringEntry> entries;
    std::uint64_t remaining_bytes;
};
StringTable read_strings(const View&,AnimationLimits limits={});
// Explicit language group. Codes0/1 are US; no automatic language fallback.
const StringEntry* string_in_language(const StringTable&,std::uint8_t,std::size_t);
// User-facing text lookup: use the requested group, then English US (code 1).
// This does not change the exact lookup used by engine identifiers.
const StringEntry* string_in_language_or_english(const StringTable&,std::uint8_t,std::size_t);
using AnimationProperties=std::vector<std::pair<std::string,std::string>>;
struct AnimationTimeProperty {std::int32_t id;AnimationProperties pairs;};
struct BoneMotion {
    std::string bone;
    std::uint32_t frames,duration_bits,has_translation,has_rotation;
    std::int32_t first_translation,first_rotation;
    std::vector<AnimationProperties> properties;
    std::vector<std::vector<AnimationTimeProperty>> time_properties;
};
struct AnimationHeader {
    std::string name,xskill;
    std::uint32_t duration_bits,distance_bits,moving,translations,rotations;
    std::vector<BoneMotion> motions;
};
struct AnimationCollection {std::vector<AnimationHeader> animations;std::uint64_t remaining_bytes;};
// Animation-only binary BCF profile (zero skeleton and appearance counts).
// Transform data remains external CFP; properties are preserved, not executed.
AnimationCollection read_bcf_animations(const View&,AnimationLimits limits={});
struct CfpPlanes {std::array<std::vector<std::uint32_t>,7> bits;std::uint64_t remaining_bytes;};
// Raw coordinate planes: translation XYZ then rotation XYZW. No handedness
// conversion or skeleton evaluation. Delta/repeat outputs retain float32 bits.
CfpPlanes read_cfp(const View&,std::uint32_t translations,std::uint32_t rotations,AnimationLimits limits={});
}
