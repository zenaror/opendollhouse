#include "odh/resident_selection.hpp"

#include "odh/formats.hpp"

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

} // namespace odh
