#include "odh/household_spawn.hpp"

#include "odh/formats.hpp"

namespace odh {
namespace {
bool inside(std::int32_t x, std::int32_t y,
            std::uint16_t width, std::uint16_t height) {
    return x >= 0 && y >= 0 && x <= 32767 && y <= 32767 &&
           x / 16 < width && y / 16 < height;
}
void append(std::vector<EntityPosition>& result, std::int32_t x, std::int32_t y,
            std::int16_t level, std::uint8_t direction,
            std::uint16_t width, std::uint16_t height) {
    if (inside(x, y, width, height)) result.push_back({x, y, level, direction});
}
}

std::vector<EntityPosition> household_spawn_candidates(
    EntityPosition mailbox, std::uint16_t lot_width, std::uint16_t lot_height) {
    if (mailbox.x < 0 || mailbox.y < 0 || mailbox.level < 1 || mailbox.level > 2 ||
        mailbox.direction > 7 || !lot_width || !lot_height ||
        !inside(mailbox.x, mailbox.y, lot_width, lot_height))
        throw FormatError("household spawn mailbox or lot bounds are invalid");

    // Mirrors VMTS1LotState.VerifyFamily -> VMFindLocationFor.FindLocationFor:
    // four facings on the mailbox, then nine expanding square rings. Keep the
    // repeated trials from the source loops; SetPosition decides their result.
    std::vector<EntityPosition> result;
    result.reserve(1444);
    for (std::uint8_t direction = 0; direction < 8; direction += 2)
        append(result, mailbox.x, mailbox.y, mailbox.level, direction,
               lot_width, lot_height);

    const auto tile_x = mailbox.x / 16;
    const auto tile_y = mailbox.y / 16;
    for (std::int32_t ring = 1; ring < 10; ++ring) {
        for (std::int32_t x = -ring; x <= ring; ++x) {
            for (std::uint8_t direction = 0; direction < 8; ++direction) {
                const auto y = tile_y + ((direction % 2) * 2 - 1) * ring;
                append(result, (tile_x + x) * 16 + 8, y * 16 + 8,
                       mailbox.level, static_cast<std::uint8_t>((direction / 2) * 2),
                       lot_width, lot_height);
            }
        }
        for (std::int32_t y = 1 - ring; y < ring; ++y) {
            for (std::uint8_t direction = 0; direction < 8; ++direction) {
                const auto x = tile_x + ((direction % 2) * 2 - 1) * ring;
                append(result, x * 16 + 8, (tile_y + y) * 16 + 8,
                       mailbox.level, static_cast<std::uint8_t>((direction / 2) * 2),
                       lot_width, lot_height);
            }
        }
    }
    return result;
}

std::optional<EntityPosition> first_household_spawn_candidate(
    EntityPosition mailbox, std::uint16_t lot_width, std::uint16_t lot_height,
    const std::function<bool(const EntityPosition&)>& accepts) {
    if (!accepts) throw FormatError("household spawn placement validator is required");
    for (const auto& candidate : household_spawn_candidates(mailbox, lot_width, lot_height))
        if (accepts(candidate)) return candidate;
    return std::nullopt;
}

} // namespace odh
