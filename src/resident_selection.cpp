#include "odh/resident_selection.hpp"

#include "odh/formats.hpp"

#include <algorithm>
#include <limits>

namespace odh {

std::size_t advance_resident_selection(std::size_t current, int delta,
                                       std::size_t resident_count) {
    if (!resident_count) throw FormatError("resident selection requires at least one member");
    current %= resident_count;
    if (delta > 0) {
        const auto amount = static_cast<std::size_t>(delta) % resident_count;
        return current >= resident_count - amount ? current - (resident_count - amount) : current + amount;
    }
    if (delta < 0) {
        const auto amount = static_cast<std::size_t>(-(static_cast<long long>(delta))) % resident_count;
        return current >= amount ? current - amount : resident_count - (amount - current);
    }
    return current;
}

std::optional<std::size_t> resident_selection_row(std::size_t page_start,
        std::size_t row, std::size_t page_size, std::size_t resident_count) {
    if (!page_size || row >= page_size || page_start >= resident_count ||
        row >= resident_count - page_start) return std::nullopt;
    return page_start + row;
}

std::optional<std::int16_t> resolve_resident_pick(std::uint16_t pick_id,
        std::int16_t primary_resident, const std::vector<std::int16_t>& other_residents) {
    if (!pick_id || pick_id > static_cast<std::uint16_t>(std::numeric_limits<std::int16_t>::max()))
        return std::nullopt;
    const auto resident = static_cast<std::int16_t>(pick_id);
    if (primary_resident > 0 && resident == primary_resident) return resident;
    if (std::find(other_residents.begin(), other_residents.end(), resident) != other_residents.end())
        return resident;
    return std::nullopt;
}

} // namespace odh
