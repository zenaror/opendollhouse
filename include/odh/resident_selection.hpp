#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace odh {

// Pure selection rules shared by the resident window and headless tests.
std::size_t advance_resident_selection(std::size_t current, int delta,
                                       std::size_t resident_count);
std::optional<std::size_t> resident_selection_row(std::size_t page_start,
        std::size_t row, std::size_t page_size, std::size_t resident_count);

// Resolve a rendered pick token only when it identifies the active household.
// Zero and out-of-range IDs are reserved for non-resident pixels.
std::optional<std::int16_t> resolve_resident_pick(std::uint16_t pick_id,
        std::int16_t primary_resident, const std::vector<std::int16_t>& other_residents);

} // namespace odh
