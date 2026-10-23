#include "odh/family_overlay.hpp"
#include "odh/sha256.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace odh {
namespace {
constexpr std::size_t max_assignments = 4096;
constexpr std::size_t header_size = 44;
constexpr std::size_t record_size = 48;

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
std::uint16_t get16(const std::vector<std::uint8_t>& in, std::size_t at) {
    return static_cast<std::uint16_t>(in[at] | (static_cast<std::uint16_t>(in[at + 1]) << 8));
}
std::uint32_t get32(const std::vector<std::uint8_t>& in, std::size_t at) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(in[at + i]) << (i * 8);
    return value;
}
std::int32_t signed32(std::uint32_t value) {
    return static_cast<std::int32_t>(value < 0x80000000U ? static_cast<std::int64_t>(value) :
        static_cast<std::int64_t>(value) - 0x100000000LL);
}
void validate_overlay(const FamilyOverlay& overlay) {
    if (overlay.assignments.size() > max_assignments)
        throw FormatError("family overlay exceeds assignment limit");
    std::set<std::uint16_t> families, houses;
    for (const auto& assignment : overlay.assignments) {
        if (assignment.house_number == 0 || assignment.house_number > 98)
            throw FormatError("family overlay house is outside 1..98");
        if (!families.insert(assignment.family_chunk_id).second)
            throw FormatError("family overlay repeats a family chunk");
        if (!houses.insert(assignment.house_number).second)
            throw FormatError("family overlay repeats a destination house");
        if (assignment.purchase_price < 0)
            throw FormatError("family overlay purchase price is negative");
        if (assignment.budget_after_purchase < 0)
            throw FormatError("family overlay budget is negative");
    }
}
}

std::array<std::uint8_t, 32> family_overlay_identity(const FamilyEntry& entry) {
    Sha256 hash;
    const std::uint8_t id[]{static_cast<std::uint8_t>(entry.chunk_id),
                            static_cast<std::uint8_t>(entry.chunk_id >> 8)};
    hash.update(id, sizeof(id));
    const auto add32 = [&](std::uint32_t value) {
        std::uint8_t bytes[4]{};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
        hash.update(bytes, sizeof(bytes));
    };
    add32(static_cast<std::uint32_t>(entry.family.family_number));
    add32(static_cast<std::uint32_t>(entry.family.house_number));
    add32(static_cast<std::uint32_t>(entry.family.budget));
    add32(static_cast<std::uint32_t>(entry.family.member_guids.size()));
    for (const auto guid : entry.family.member_guids) add32(guid);
    return hash.finish();
}

FamilyOverlayAssignment plan_family_move_in(const FamilyEntry& family,
        std::uint16_t house_number, std::int32_t purchase_price) {
    if (house_number == 0 || house_number > 98)
        throw FormatError("family move-in house is outside 1..98");
    if (family.family.member_guids.empty())
        throw FormatError("family move-in requires at least one member");
    const auto current_house = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(family.family.house_number) & 0xffffU);
    if (current_house != 0)
        throw FormatError("family must be evicted before it can move into a new house");
    if (purchase_price < 0 || family.family.budget < 0 || purchase_price > family.family.budget)
        throw FormatError("family cannot afford the selected house");
    return {family.chunk_id, family_overlay_identity(family), house_number,
            purchase_price, family.family.budget - purchase_price};
}

std::vector<std::uint8_t> encode_family_overlay(const FamilyOverlay& overlay) {
    validate_overlay(overlay);
    std::vector<std::uint8_t> out;
    out.reserve(header_size + record_size * overlay.assignments.size());
    out.insert(out.end(), {'O','D','H','F'});
    put16(out, 1); put16(out, 0);
    out.insert(out.end(), overlay.neighborhood_identity.begin(), overlay.neighborhood_identity.end());
    put32(out, static_cast<std::uint32_t>(overlay.assignments.size()));
    for (const auto& assignment : overlay.assignments) {
        put16(out, assignment.family_chunk_id);
        out.insert(out.end(), assignment.family_identity.begin(), assignment.family_identity.end());
        put16(out, assignment.house_number);
        put32(out, static_cast<std::uint32_t>(assignment.purchase_price));
        put32(out, static_cast<std::uint32_t>(assignment.budget_after_purchase));
        put32(out, 0);
    }
    return out;
}

FamilyOverlay decode_family_overlay(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < header_size || bytes.size() > header_size + record_size * max_assignments)
        throw FormatError("family overlay size is outside the supported range");
    if (bytes[0] != 'O' || bytes[1] != 'D' || bytes[2] != 'H' || bytes[3] != 'F')
        throw FormatError("invalid family overlay signature");
    if (get16(bytes, 4) != 1 || get16(bytes, 6) != 0)
        throw FormatError("unsupported family overlay version or flags");
    const auto count = get32(bytes, 40);
    if (count > max_assignments || bytes.size() != header_size + record_size * count)
        throw FormatError("family overlay record count or extent is invalid");
    FamilyOverlay result;
    std::copy_n(bytes.begin() + 8, result.neighborhood_identity.size(), result.neighborhood_identity.begin());
    result.assignments.reserve(count);
    std::size_t at = header_size;
    for (std::uint32_t i = 0; i < count; ++i, at += record_size) {
        if (get32(bytes, at + 44) != 0) throw FormatError("family overlay reserved word is nonzero");
        FamilyOverlayAssignment assignment;
        assignment.family_chunk_id = get16(bytes, at);
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(at + 2), assignment.family_identity.size(),
                    assignment.family_identity.begin());
        assignment.house_number = get16(bytes, at + 34);
        assignment.purchase_price = signed32(get32(bytes, at + 36));
        assignment.budget_after_purchase = signed32(get32(bytes, at + 40));
        result.assignments.push_back(assignment);
    }
    validate_overlay(result);
    return result;
}

std::vector<FamilyEntry> apply_family_overlay(const std::vector<FamilyEntry>& source,
        const std::array<std::uint8_t, 32>& neighborhood_identity, const FamilyOverlay& overlay) {
    validate_overlay(overlay);
    if (overlay.neighborhood_identity != neighborhood_identity)
        throw FormatError("family overlay belongs to different Neighborhood.iff data");
    auto result = source;
    std::map<std::uint16_t, std::size_t> by_id;
    std::set<std::uint16_t> initially_occupied;
    for (std::size_t i = 0; i < result.size(); ++i)
        if (!by_id.emplace(result[i].chunk_id, i).second)
            throw FormatError("ambiguous duplicate family chunk ID");
        else {
            const auto key = static_cast<std::uint16_t>(static_cast<std::uint32_t>(result[i].family.house_number) & 0xffffU);
            if (key >= 1 && key <= 98 && !initially_occupied.insert(key).second)
                throw FormatError("ambiguous multiple families occupy one source house");
        }
    for (const auto& assignment : overlay.assignments) {
        const auto found = by_id.find(assignment.family_chunk_id);
        if (found == by_id.end()) throw FormatError("family overlay references a missing FAMI chunk");
        auto& family = result[found->second].family;
        if (family_overlay_identity(result[found->second]) != assignment.family_identity)
            throw FormatError("family overlay identity is stale");
        const auto current_house = static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(family.house_number) & 0xffffU);
        if (current_house != 0)
            throw FormatError("family must be evicted before it can move into a new house");
        if (initially_occupied.count(assignment.house_number))
            throw FormatError("family overlay destination was not vacant in source data");
        if (assignment.purchase_price < 0 || assignment.purchase_price > family.budget ||
            assignment.budget_after_purchase != family.budget - assignment.purchase_price)
            throw FormatError("family overlay budget does not match the purchase price");
        family.house_number = assignment.house_number;
        family.budget = assignment.budget_after_purchase;
    }
    std::map<std::int16_t, std::uint16_t> occupied;
    for (const auto& entry : result) {
        const auto key = static_cast<std::uint16_t>(static_cast<std::uint32_t>(entry.family.house_number) & 0xffffU);
        if (key == 0 || key > 98) continue; // Preserve non-selectable source profiles unchanged.
        if (!occupied.emplace(key, entry.chunk_id).second)
            throw FormatError("family overlay creates an occupied destination conflict");
    }
    return result;
}

} // namespace odh
