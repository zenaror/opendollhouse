#pragma once
#include "odh/formats.hpp"
#include <map>

namespace odh {
struct NgbhLimits {
    std::uint64_t payload_bytes = 1024 * 1024;
    std::uint32_t entries = 4096; // Serialized entries, including overwritten IDs.
    std::uint64_t inventory_items = 65536; // Cumulative, including overwritten lists.
};
struct InventoryItem {
    std::int32_t type;
    std::uint32_t guid;
    std::uint16_t count;
};
struct Ngbh {
    std::uint32_t reserved;
    std::uint32_t version;
    std::array<std::uint8_t,4> magic;
    std::array<std::int16_t,16> neighborhood_data;
    bool inventory_present = false; // Exact EOF at byte44 is a known empty inventory.
    std::map<std::int16_t,std::vector<InventoryItem>> inventory_by_id;
    std::map<std::int16_t,std::int32_t> inventory_markers; // Final raw marker per ID.
    std::uint64_t remaining_bytes = 0;
};
// Little-endian FreeSO NGBH structural profile. Header version/magic and entry
// markers are retained, not interpreted/validated by the reference reader.
// Last serialized duplicate NID replaces its entire list and marker. Items
// retain serialized order and duplicate Type/GUID pairs without normalization.
// Map key iteration is numeric, not serialized order. Negative counts and
// truncation fail as FormatError; a malformed extension is never known empty.
Ngbh read_ngbh(const View&, const NgbhLimits& = {});
}
