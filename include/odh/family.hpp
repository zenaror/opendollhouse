#pragma once
#include "odh/formats.hpp"
#include <map>

namespace odh {
struct FamiLimits {
    std::uint64_t payload_bytes = 64 * 1024;
    std::uint32_t members = 256;
};
// Raw FAMI metadata and GUIDs. FamilyNumber is not the runtime family ID.
struct Family {
    std::uint32_t reserved = 0, version = 0;
    std::int32_t house_number = 0, family_number = 0, budget = 0;
    std::int32_t value_in_arch = 0, family_friends = 0, unknown = 0;
    std::vector<std::uint32_t> member_guids;
    std::uint64_t remaining_bytes = 0; // Unknown tail extent, without invented defaults.
};
// Fixed IMAF prefix and signed member count. Versions remain structural metadata.
// The reference's optional 3/4-word tail is deliberately not required or decoded.
Family read_fami(const View&, const FamiLimits& = {});
struct FamilyEntry {
    std::uint16_t chunk_id;
    Family family;
};
// Owns an immutable bounded snapshot. The last serialized family for a signed
// low16 house key wins, following TS1NeighbourProvider; never sort by chunk ID.
class FamilyContext {
public:
    explicit FamilyContext(const std::vector<FamilyEntry>&);
    const std::vector<FamilyEntry>& source() const { return source_; }
    const FamilyEntry* by_house(std::int16_t) const;
    const FamilyEntry* by_id(std::uint16_t) const;
    static bool contains(const FamilyEntry&, std::uint32_t guid);
private:
    std::vector<FamilyEntry> source_;
    std::map<std::int16_t,std::size_t> houses_;
    std::map<std::uint16_t,std::size_t> ids_;
};
} // namespace odh
