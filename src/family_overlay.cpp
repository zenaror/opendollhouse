#include "odh/family_overlay.hpp"
#include "odh/sha256.hpp"

#include <algorithm>
#include <limits>
#include <map>

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
    std::map<std::uint16_t, std::array<std::uint8_t, 32>> families;
    for (const auto& assignment : overlay.assignments) {
        const auto [family, inserted] = families.emplace(
            assignment.family_chunk_id, assignment.family_identity);
        if (!inserted && family->second != assignment.family_identity)
            throw FormatError("family overlay changes identity within its operation log");
        if (assignment.budget_after_purchase < 0)
            throw FormatError("family overlay budget is negative");
        switch (assignment.action) {
        case FamilyOverlayAction::move_in:
            if (assignment.house_number == 0 || assignment.house_number > 98)
                throw FormatError("family move-in house is outside 1..98");
            if (assignment.purchase_price < 0)
                throw FormatError("family move-in purchase price is negative");
            break;
        case FamilyOverlayAction::move_out:
            if (assignment.house_number != 0 || assignment.purchase_price != 0)
                throw FormatError("family move-out record must have no destination or purchase price");
            break;
        default:
            throw FormatError("family overlay action is unsupported");
        }
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

FamilyOverlay plan_family_move_in_overlay(const std::vector<FamilyEntry>& source,
        const std::array<std::uint8_t, 32>& neighborhood_identity,
        const FamilyOverlay* current_overlay, std::uint16_t family_chunk_id,
        std::uint16_t house_number, std::int32_t purchase_price) {
    if (current_overlay && current_overlay->neighborhood_identity != neighborhood_identity)
        throw FormatError("current family overlay belongs to different Neighborhood.iff data");
    const auto effective = current_overlay ?
        apply_family_overlay(source, neighborhood_identity, *current_overlay) : source;
    const FamilyContext families(effective);
    if (families.by_house(static_cast<std::int16_t>(house_number)))
        throw FormatError("family move-in destination is already occupied");
    const auto* family = families.by_id(family_chunk_id);
    if (!family) throw FormatError("selected family is absent from Neighborhood.iff");

    FamilyOverlay result = current_overlay ? *current_overlay :
        FamilyOverlay{neighborhood_identity, {}};
    const auto source_families = FamilyContext(source);
    const auto* original = source_families.by_id(family_chunk_id);
    if (!original) throw FormatError("selected family is absent from Neighborhood.iff");
    if (house_number == 0 || house_number > 98)
        throw FormatError("family move-in house is outside 1..98");
    if (family->family.member_guids.empty())
        throw FormatError("family move-in requires at least one member");
    if (purchase_price < 0 || family->family.budget < 0 ||
        purchase_price > family->family.budget)
        throw FormatError("family cannot afford the selected house");
    result.assignments.push_back({family_chunk_id, family_overlay_identity(*original),
        house_number, purchase_price, family->family.budget - purchase_price});
    (void)apply_family_overlay(source, neighborhood_identity, result);
    return result;
}

FamilyOverlay plan_family_move_out_overlay(const std::vector<FamilyEntry>& source,
        const std::array<std::uint8_t, 32>& neighborhood_identity,
        const FamilyOverlay* current_overlay, std::uint16_t family_chunk_id) {
    if (current_overlay && current_overlay->neighborhood_identity != neighborhood_identity)
        throw FormatError("current family overlay belongs to different Neighborhood.iff data");
    const auto effective = current_overlay ?
        apply_family_overlay(source, neighborhood_identity, *current_overlay) : source;
    const FamilyContext source_families(source);
    const FamilyContext effective_families(effective);
    const auto* original = source_families.by_id(family_chunk_id);
    const auto* family = effective_families.by_id(family_chunk_id);
    if (!original || !family)
        throw FormatError("selected family is absent from Neighborhood.iff");
    const auto original_house = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(original->family.house_number) & 0xffffU);
    if (original_house == 0 || original_house > 98)
        throw FormatError("move-out is only supported for a family housed in source data");
    if (current_overlay) {
        for (const auto& operation : current_overlay->assignments)
            if (operation.family_chunk_id == family_chunk_id &&
                operation.action == FamilyOverlayAction::move_in)
                throw FormatError("move-out after a player move-in is unsupported until ValueInArch updates are verified");
    }
    const auto current_house = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(family->family.house_number) & 0xffffU);
    if (current_house == 0 || current_house > 98)
        throw FormatError("family has no selectable house to move out of");
    if (family->family.value_in_arch < 0 || family->family.budget < 0)
        throw FormatError("family move-out has invalid refund or budget data");
    const auto next_budget = static_cast<std::int64_t>(family->family.budget) +
        family->family.value_in_arch;
    if (next_budget > std::numeric_limits<std::int32_t>::max())
        throw FormatError("family move-out refund overflows its budget field");
    FamilyOverlay result = current_overlay ? *current_overlay :
        FamilyOverlay{neighborhood_identity, {}};
    result.assignments.push_back({family_chunk_id, family_overlay_identity(*original),
        0, 0, static_cast<std::int32_t>(next_budget), FamilyOverlayAction::move_out});
    (void)apply_family_overlay(source, neighborhood_identity, result);
    return result;
}

std::vector<std::uint8_t> encode_family_overlay(const FamilyOverlay& overlay) {
    validate_overlay(overlay);
    std::vector<std::uint8_t> out;
    out.reserve(header_size + record_size * overlay.assignments.size());
    out.insert(out.end(), {'O','D','H','F'});
    put16(out, 2); put16(out, 0);
    out.insert(out.end(), overlay.neighborhood_identity.begin(), overlay.neighborhood_identity.end());
    put32(out, static_cast<std::uint32_t>(overlay.assignments.size()));
    for (const auto& assignment : overlay.assignments) {
        put16(out, assignment.family_chunk_id);
        out.insert(out.end(), assignment.family_identity.begin(), assignment.family_identity.end());
        put16(out, assignment.house_number);
        put32(out, static_cast<std::uint32_t>(assignment.purchase_price));
        put32(out, static_cast<std::uint32_t>(assignment.budget_after_purchase));
        put32(out, static_cast<std::uint32_t>(assignment.action));
    }
    return out;
}

FamilyOverlay decode_family_overlay(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < header_size || bytes.size() > header_size + record_size * max_assignments)
        throw FormatError("family overlay size is outside the supported range");
    if (bytes[0] != 'O' || bytes[1] != 'D' || bytes[2] != 'H' || bytes[3] != 'F')
        throw FormatError("invalid family overlay signature");
    const auto version = get16(bytes, 4);
    if ((version != 1 && version != 2) || get16(bytes, 6) != 0)
        throw FormatError("unsupported family overlay version or flags");
    const auto count = get32(bytes, 40);
    if (count > max_assignments || bytes.size() != header_size + record_size * count)
        throw FormatError("family overlay record count or extent is invalid");
    FamilyOverlay result;
    std::copy_n(bytes.begin() + 8, result.neighborhood_identity.size(), result.neighborhood_identity.begin());
    result.assignments.reserve(count);
    std::size_t at = header_size;
    for (std::uint32_t i = 0; i < count; ++i, at += record_size) {
        const auto action = get32(bytes, at + 44);
        if ((version == 1 && action != 0) || (version == 2 && action > 1))
            throw FormatError("family overlay action or reserved word is invalid");
        FamilyOverlayAssignment assignment;
        assignment.family_chunk_id = get16(bytes, at);
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(at + 2), assignment.family_identity.size(),
                    assignment.family_identity.begin());
        assignment.house_number = get16(bytes, at + 34);
        assignment.purchase_price = signed32(get32(bytes, at + 36));
        assignment.budget_after_purchase = signed32(get32(bytes, at + 40));
        assignment.action = version == 1 ? FamilyOverlayAction::move_in :
            static_cast<FamilyOverlayAction>(action);
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
    std::map<std::uint16_t, std::uint16_t> occupied;
    std::map<std::uint16_t, bool> move_out_supported;
    for (std::size_t i = 0; i < result.size(); ++i)
        if (!by_id.emplace(result[i].chunk_id, i).second)
            throw FormatError("ambiguous duplicate family chunk ID");
        else {
            const auto key = static_cast<std::uint16_t>(static_cast<std::uint32_t>(result[i].family.house_number) & 0xffffU);
            move_out_supported.emplace(result[i].chunk_id, key >= 1 && key <= 98);
            if (key >= 1 && key <= 98 && !occupied.emplace(key, result[i].chunk_id).second)
                throw FormatError("ambiguous multiple families occupy one source house");
        }
    for (const auto& assignment : overlay.assignments) {
        const auto found = by_id.find(assignment.family_chunk_id);
        if (found == by_id.end()) throw FormatError("family overlay references a missing FAMI chunk");
        auto& family = result[found->second].family;
        if (family_overlay_identity(source[found->second]) != assignment.family_identity)
            throw FormatError("family overlay identity is stale");
        const auto current_house = static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(family.house_number) & 0xffffU);
        if (assignment.action == FamilyOverlayAction::move_in) {
            if (current_house != 0)
                throw FormatError("family must be moved out before it can move into a new house");
            if (family.member_guids.empty())
                throw FormatError("family move-in requires at least one member");
            if (occupied.count(assignment.house_number))
                throw FormatError("family overlay destination is currently occupied");
            if (assignment.purchase_price > family.budget ||
                assignment.budget_after_purchase != family.budget - assignment.purchase_price)
                throw FormatError("family overlay budget does not match the purchase price");
            family.house_number = assignment.house_number;
            family.budget = assignment.budget_after_purchase;
            occupied.emplace(assignment.house_number, assignment.family_chunk_id);
            move_out_supported[assignment.family_chunk_id] = false;
        } else {
            if (!move_out_supported[assignment.family_chunk_id])
                throw FormatError("family move-out has no verified source ValueInArch");
            if (current_house == 0 || current_house > 98)
                throw FormatError("family move-out requires a currently occupied selectable house");
            if (occupied.erase(current_house) != 1)
                throw FormatError("family move-out source house is not occupied by this family");
            if (family.value_in_arch < 0 || family.budget < 0)
                throw FormatError("family move-out has invalid refund or budget data");
            const auto next_budget = static_cast<std::int64_t>(family.budget) + family.value_in_arch;
            if (next_budget < 0 || next_budget > std::numeric_limits<std::int32_t>::max() ||
                assignment.budget_after_purchase != next_budget)
                throw FormatError("family move-out budget does not match its architecture refund");
            family.budget = static_cast<std::int32_t>(next_budget);
            family.value_in_arch = 0;
            family.house_number = 0;
            move_out_supported[assignment.family_chunk_id] = false;
        }
    }
    return result;
}

} // namespace odh
