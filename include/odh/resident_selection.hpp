#pragma once

#include <cstddef>
#include <optional>

namespace odh {

// Pure selection rules shared by the resident window and headless tests.
std::size_t advance_resident_selection(std::size_t current, int delta,
                                       std::size_t resident_count);
std::optional<std::size_t> resident_selection_row(std::size_t page_start,
        std::size_t row, std::size_t page_size, std::size_t resident_count);

} // namespace odh
