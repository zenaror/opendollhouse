#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <istream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace odh {

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A bounded view of a seekable input. Views share a stream and are not thread-safe.
// No payload is mapped or copied wholesale; offsets are relative to this view.
class View {
public:
    View(std::istream& input, std::uint64_t size);
    std::uint64_t size() const { return size_; }
    View subview(std::uint64_t offset, std::uint64_t length) const;
    void read(std::uint64_t offset, void* destination, std::size_t length) const;
    std::uint8_t u8(std::uint64_t offset) const;
    std::uint16_t le16(std::uint64_t offset) const;
    std::uint16_t be16(std::uint64_t offset) const;
    std::uint32_t le32(std::uint64_t offset) const;
    std::uint32_t be32(std::uint64_t offset) const;
private:
    View(std::istream& input, std::uint64_t base, std::uint64_t size);
    void check(std::uint64_t offset, std::uint64_t length) const;
    std::istream* input_;
    std::uint64_t base_;
    std::uint64_t size_;
};

struct Limits {
    std::uint32_t far_entries = 200000;
    std::uint32_t iff_chunks = 100000;
    std::uint32_t sprite_frames = 1000000;
    std::uint32_t ttab_interactions = 4096;
    std::uint32_t ttab_motives = 256;
};

struct FarEntry {
    std::string name;
    std::uint32_t offset;
    std::uint32_t length;
};

// Current corpus profile: FAR v1, 32-bit name lengths, equal data lengths.
// Other variants are rejected explicitly, rather than guessed.
std::vector<FarEntry> far1_index(const View&, const Limits& = {});

struct Chunk {
    std::string type; // Preserve the original FourCC, including case.
    std::uint16_t id;
    std::uint16_t flags;
    std::uint64_t offset;
    View payload;
};

// Return the IFF version; deliver only headers and bounded payload views.
std::string iff_chunks(const View&, const std::function<void(const Chunk&)>&,
                       const Limits& = {});

struct Instruction {
    std::uint16_t opcode;
    std::uint8_t on_true;
    std::uint8_t on_false;
    std::array<std::uint8_t, 8> operands;
};

struct Bhav {
    std::uint16_t version;
    std::vector<Instruction> instructions;
    std::array<std::uint8_t, 8> header{}; // Preserve legacy/unknown metadata.
    struct Metadata {
        std::uint8_t type;
        std::uint8_t arguments;
        std::uint16_t locals;
        std::uint16_t tree_version;
    };
    std::optional<Metadata> metadata; // Defined only for the current 8002 profile.
};

// Decode the structure only. No instruction execution or operand semantics.
Bhav read_bhav(const View&);

struct SpriteFrame {
    std::uint16_t width;
    std::uint16_t height;
    std::uint32_t flags;
};

// SPR2 1000 header inspection; RLE pixels, palettes and Z are not decoded.
void spr2_frames(const View&, const std::function<void(const SpriteFrame&)>&,
                 const Limits& = {});

enum class TtabStatus { decoded, empty, legacy_unsupported, absent_payload };
struct MotiveEntry {
    std::int16_t minimum;
    std::int16_t delta;
    std::uint16_t personality_modifier;
};
struct Interaction {
    std::uint16_t action;
    std::uint16_t test;
    std::uint32_t flags;
    std::uint32_t label_index;
    std::uint32_t attenuation_code;
    std::uint32_t attenuation_bits; // Preserve IEEE-754 bits; no arithmetic here.
    std::uint32_t autonomy_threshold;
    std::int32_t joining_index;
    std::vector<MotiveEntry> motives;
};
struct Ttab {
    TtabStatus status;
    std::uint16_t declared_count;
    std::optional<std::uint16_t> version;
    bool compressed;
    std::uint64_t remaining_bits; // Expose padding/unknown tail, not silently discard.
    std::vector<Interaction> interactions;
};
// TS1 profile: raw 4..8, version 9 compression 0/1. Versions 2/3 and absent
// payload are explicitly classified, not converted into executable empty tables.
Ttab read_ttab(const View&, const Limits& = {});

} // namespace odh
