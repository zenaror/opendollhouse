#pragma once
#include "odh/formats.hpp"
#include <map>
#include <string>

namespace odh {
struct FamiLimits {
    std::uint64_t payload_bytes = 64 * 1024;
    std::uint32_t members = 256;
};
// Proven original FAMI9 suffix, after member GUIDs. Six signed words are
// family spell state; the preceding DWORD retains its unknown meaning.
struct FamilyV9Extension {
    std::uint32_t extension_dword=0;
    std::array<std::int16_t,6> spell_words{};
};
// Raw FAMI metadata and GUIDs. FamilyNumber is not the runtime family ID.
struct Family {
    std::uint32_t reserved = 0, version = 0;
    std::int32_t house_number = 0, family_number = 0, budget = 0;
    std::int32_t value_in_arch = 0, family_friends = 0, unknown = 0;
    std::vector<std::uint32_t> member_guids;
    std::uint64_t remaining_bytes = 0; // TOTAL tail extent, including known fields.
    std::vector<std::uint8_t> tail_bytes; // Complete immutable-input suffix, including unknown trailing bytes.
    std::optional<FamilyV9Extension> original_v9_extension; // Only version9 with a complete >=16-byte suffix.
};
// Fixed IMAF prefix and signed member count. Versions remain structural metadata.
// Tails remain optional structurally; incomplete v9 data never invents spell words.
Family read_fami(const View&, const FamiLimits& = {});
// Encodes only a complete, tail-free payload. Use encode_preserved_fami for
// a decoded suffix; this default path rejects rather than discarding it.
std::vector<std::uint8_t> encode_fami(const Family&, const FamiLimits& = {});
// Explicit preservation path. Encodes the structural prefix and original raw
// suffix, patching the typed v9 fields if present. Requires exact tail extent;
// unknown bytes are retained. The default tail-free encoder stays unchanged.
std::vector<std::uint8_t> encode_preserved_fami(const Family&, const FamiLimits& = {});
// Explicit Simitone CreateFamily reference profile: appends four zero dwords
// after the member GUIDs. This documents that reference's output only; it does
// not establish that the same profile is valid for the original TS1 executable.
// The supplied FamilyNumber and budget are serialized as given, without defaults.
std::vector<std::uint8_t> encode_simitone_create_family_fami(
    const Family&, const FamiLimits& = {});
struct FamilyEntry {
    std::uint16_t chunk_id;
    Family family;
    // Player-created entries can carry a name from their sidecar; source
    // Neighborhood.iff entries remain unnamed until their FAMs names are read.
    std::string display_name{};
};
// Owns a bounded runtime copy of family records. The last serialized family
// for a signed low16 house key wins, following TS1NeighbourProvider; never sort
// by chunk ID. Runtime edits never mutate the caller's original records.
class FamilyContext {
public:
    explicit FamilyContext(const std::vector<FamilyEntry>&);
    const std::vector<FamilyEntry>& source() const { return source_; }
    FamilyEntry* by_house(std::int16_t);
    const FamilyEntry* by_house(std::int16_t) const;
    FamilyEntry* by_id(std::uint16_t);
    const FamilyEntry* by_id(std::uint16_t) const;
    static bool contains(const FamilyEntry&, std::uint32_t guid);
private:
    std::vector<FamilyEntry> source_;
    std::map<std::int16_t,std::size_t> houses_;
    std::map<std::uint16_t,std::size_t> ids_;
};
} // namespace odh
