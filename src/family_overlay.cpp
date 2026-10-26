#include "odh/family_overlay.hpp"
#include "odh/sha256.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace odh {
namespace {
constexpr std::size_t max_assignments = 4096;
constexpr std::size_t header_size = 44;
constexpr std::size_t record_size = 48;
constexpr std::size_t managed_record_size = 104;
constexpr std::size_t max_overlay_bytes = family_overlay_max_bytes;
constexpr std::size_t max_creation_bytes = 64 * 1024;
constexpr NbrsLimits creation_limits{max_creation_bytes, 32768, 8, 256, 2048, 1024, 4096};

// Validate storage extents before making any owned copy. encode_nbrs then
// validates profile consistency and tighter cumulative serialized budgets.
void bound_neighbor_storage(const NeighborRecord& record) {
    if (record.name.size() > 256 || (record.person_data && record.person_data->size() > 256) ||
        record.relationships.size() > 16384)
        throw FormatError("neighbor storage exceeds bounded copy limits");
    std::size_t values = 0;
    for (const auto& relationship : record.relationships) {
        if (relationship.values.size() > 65536 - values)
            throw FormatError("neighbor relationship storage exceeds bounded copy limits");
        values += relationship.values.size();
    }
}

Nbrs creation_neighbors(const FamilyOverlayCreation& creation) {
    Nbrs result{0, 73, {}, 0};
    for (const auto& member : creation.members) {
        bound_neighbor_storage(member);
        result.entries.push_back({1, member});
    }
    return result;
}
Family creation_family(const FamilyOverlayCreation& creation) {
    if (creation.family_name.empty() || creation.family_name.size() > 256 ||
        creation.family_name.find('\0') != std::string::npos)
        throw FormatError("created family name must contain 1..256 non-NUL bytes");
    if (creation.members.empty() || creation.members.size() > 8 || creation.fami_payload.size() > 88)
        throw FormatError("created household must contain 1..8 members");
    std::string raw(creation.fami_payload.begin(), creation.fami_payload.end());
    std::istringstream stream(raw); View view(stream, raw.size());
    auto family = read_fami(view, FamiLimits{88, 8});
    family.remaining_bytes = 0;
    if (encode_simitone_create_family_fami(family, FamiLimits{88, 8}) != creation.fami_payload ||
        family.budget < 0 || family.family_number < 0 || family.member_guids.size() != creation.members.size())
        throw FormatError("created family does not match explicit FAMI profile or roster");
    std::set<std::uint32_t> guids;
    std::set<std::int16_t> ids;
    for (std::size_t i = 0; i < creation.members.size(); ++i) {
        const auto& member = creation.members[i];
        const auto family_id = static_cast<std::int16_t>(creation.family_chunk_id < 0x8000U ?
            static_cast<std::int32_t>(creation.family_chunk_id) :
            static_cast<std::int32_t>(creation.family_chunk_id) - 65536);
        if (member.version != 10 || member.unknown3 != 9 || member.name != "iffname" ||
            member.name_padding || member.mystery_zero != 0 || member.person_mode != 9 ||
            member.unknown_neg_one != -1 || !member.relationships.empty() ||
            !member.person_data || member.person_data->size() != 256 ||
            (*member.person_data)[61] != family_id ||
            std::any_of(member.person_data->begin() + 88, member.person_data->end(),
                [](std::int16_t value) { return value != 0; }))
            throw FormatError("created member does not match Simitone AddNeighbor profile or family ID");
        if (member.guid != family.member_guids[i] || member.neighbor_id <= 0 ||
            !guids.insert(member.guid).second || !ids.insert(member.neighbor_id).second)
            throw FormatError("created member roster or identity is invalid");
    }
    (void)encode_nbrs(creation_neighbors(creation), creation_limits);
    return family;
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
std::uint64_t get64(const std::vector<std::uint8_t>& in, std::size_t at) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(in[at + i]) << (i * 8);
    return value;
}
bool zero_digest(const std::array<std::uint8_t, 32>& value) {
    return std::all_of(value.begin(), value.end(), [](auto byte) { return byte == 0; });
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
    if (overlay.assignments.size() > max_assignments ||
        overlay.creations.size() > max_assignments - overlay.assignments.size() ||
        overlay.archives.size() > max_assignments - overlay.assignments.size() -
            overlay.creations.size() ||
        overlay.deletions.size() > max_assignments - overlay.assignments.size() -
            overlay.creations.size() - overlay.archives.size())
        throw FormatError("family overlay exceeds assignment limit");
    std::set<std::uint16_t> created_ids;
    std::set<std::uint32_t> created_guids;
    std::set<std::int16_t> neighbor_ids;
    std::set<std::int32_t> family_numbers;
    std::size_t creation_bytes = 0;
    for (const auto& creation : overlay.creations) {
        const auto family = creation_family(creation);
        if (!family_numbers.insert(family.family_number).second)
            throw FormatError("duplicate created FamilyNumber");
        if (!created_ids.insert(creation.family_chunk_id).second)
            throw FormatError("duplicate created FAMI ID");
        for (const auto& member : creation.members)
            if (!created_guids.insert(member.guid).second || !neighbor_ids.insert(member.neighbor_id).second)
                throw FormatError("duplicate created neighbor identity");
        const auto size = encode_nbrs(creation_neighbors(creation), creation_limits).size() +
            creation.fami_payload.size() + creation.family_name.size() + 32;
        if (size > max_overlay_bytes - creation_bytes)
            throw FormatError("family creation cumulative byte budget exceeded");
        creation_bytes += size;
    }
    std::map<std::uint16_t, std::array<std::uint8_t, 32>> families;
    for (const auto& assignment : overlay.assignments) {
        const auto [family, inserted] = families.emplace(
            assignment.family_chunk_id, assignment.family_identity);
        if (!inserted && family->second != assignment.family_identity)
            throw FormatError("family overlay changes identity within its operation log");
        if (assignment.budget_after_purchase < 0)
            throw FormatError("family overlay budget is negative");
        if (assignment.state) {
            const auto& state = *assignment.state;
            if (state.expected_house > 98 || state.budget_before < 0 ||
                state.value_in_arch_before < 0 || state.value_in_arch_after < 0 ||
                (state.session_sha256 && zero_digest(*state.session_sha256)))
                throw FormatError("invalid managed family operation state");
            if (assignment.action != FamilyOverlayAction::checkpoint && state.session_sha256)
                throw FormatError("only checkpoints may publish a session digest");
        }
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
        case FamilyOverlayAction::checkpoint:
            if (!assignment.state || !assignment.state->session_sha256 ||
                assignment.house_number == 0 || assignment.house_number > 98 || assignment.purchase_price != 0)
                throw FormatError("invalid family checkpoint profile");
            break;
        default:
            throw FormatError("family overlay action is unsupported");
        }
    }
    std::set<std::uint16_t> archived_ids;
    for (const auto& archive : overlay.archives) {
        if (!archived_ids.insert(archive.family_chunk_id).second)
            throw FormatError("family overlay archives the same family more than once");
        const auto creation = std::find_if(overlay.creations.begin(), overlay.creations.end(),
            [&](const auto& value) { return value.family_chunk_id == archive.family_chunk_id; });
        if (creation == overlay.creations.end())
            throw FormatError("family overlay can only archive player-created families");
        const auto family = creation_family(*creation);
        const FamilyEntry entry{creation->family_chunk_id, family, creation->family_name};
        if (family_overlay_identity(entry) != archive.family_identity)
            throw FormatError("family archive identity does not match its creation record");
        if (families.count(archive.family_chunk_id))
            throw FormatError("family with move history cannot be archived");
    }
    std::set<std::uint16_t> deleted_ids;
    std::set<std::uint32_t> deleted_guids;
    std::set<std::int16_t> deleted_neighbor_ids;
    for (const auto& deletion : overlay.deletions) {
        if (!deleted_ids.insert(deletion.family_chunk_id).second ||
            archived_ids.count(deletion.family_chunk_id) || created_ids.count(deletion.family_chunk_id))
            throw FormatError("family overlay deletes the same family more than once");
        if (deletion.members.empty() || deletion.members.size() > 8)
            throw FormatError("family deletion roster is empty or exceeds the eight-member limit");
        if (families.count(deletion.family_chunk_id))
            throw FormatError("family with move history cannot be deleted");
        for (const auto& member : deletion.members) {
            if (!member.guid || member.neighbor_id <= 0 ||
                !deleted_guids.insert(member.guid).second ||
                !deleted_neighbor_ids.insert(member.neighbor_id).second)
                throw FormatError("family deletion contains an invalid or duplicate member");
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
            purchase_price, family.family.budget - purchase_price, FamilyOverlayAction::move_in, std::nullopt};
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
        FamilyOverlay{neighborhood_identity, {}, {}};
    const auto source_families = FamilyContext(current_overlay ?
        apply_family_overlay(source, neighborhood_identity,
            FamilyOverlay{neighborhood_identity, {}, current_overlay->creations}) : source);
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
        house_number, purchase_price, family->family.budget - purchase_price,
        FamilyOverlayAction::move_in, std::nullopt});
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
        FamilyOverlay{neighborhood_identity, {}, {}};
    result.assignments.push_back({family_chunk_id, family_overlay_identity(*original),
        0, 0, static_cast<std::int32_t>(next_budget), FamilyOverlayAction::move_out, std::nullopt});
    (void)apply_family_overlay(source, neighborhood_identity, result);
    return result;
}

std::vector<std::uint8_t> encode_family_overlay(const FamilyOverlay& overlay) {
    validate_overlay(overlay);
    const bool managed = std::any_of(overlay.assignments.begin(), overlay.assignments.end(),
        [](const auto& operation) { return operation.state.has_value(); });
    if (managed || !overlay.archives.empty() || !overlay.creations.empty() || !overlay.deletions.empty()) {
        const auto version = managed ? 6U : !overlay.deletions.empty() ? 5U :
            (overlay.archives.empty() ? 3U : 4U);
        std::vector<std::uint8_t> out{'O','D','H','F'};
        put16(out, static_cast<std::uint16_t>(version)); put16(out, 0);
        out.insert(out.end(), overlay.neighborhood_identity.begin(), overlay.neighborhood_identity.end());
        put32(out, static_cast<std::uint32_t>(overlay.creations.size() +
            overlay.assignments.size() + overlay.archives.size() + overlay.deletions.size()));
        const auto append = [&](std::uint16_t tag, const std::vector<std::uint8_t>& payload) {
            if (tag == 2 && payload.size() > max_creation_bytes)
                throw FormatError("family creation record exceeds byte limit");
            if (payload.size() + 6 > max_overlay_bytes - out.size())
                throw FormatError("family overlay byte budget exceeded");
            put16(out, tag); put32(out, static_cast<std::uint32_t>(payload.size()));
            out.insert(out.end(), payload.begin(), payload.end());
        };
        for (const auto& creation : overlay.creations) {
            std::vector<std::uint8_t> payload;
            put16(payload, creation.family_chunk_id);
            put16(payload, static_cast<std::uint16_t>(creation.family_name.size()));
            payload.insert(payload.end(), creation.family_name.begin(), creation.family_name.end());
            put32(payload, static_cast<std::uint32_t>(creation.fami_payload.size()));
            payload.insert(payload.end(), creation.fami_payload.begin(), creation.fami_payload.end());
            const auto nbrs = encode_nbrs(creation_neighbors(creation), creation_limits);
            payload.insert(payload.end(), nbrs.begin(), nbrs.end());
            append(2, payload);
        }
        for (const auto& assignment : overlay.assignments) {
            // All operation profiles share the same first 48 bytes.
            std::vector<std::uint8_t> payload;
            put16(payload, assignment.family_chunk_id);
            payload.insert(payload.end(), assignment.family_identity.begin(), assignment.family_identity.end());
            put16(payload, assignment.house_number);
            put32(payload, static_cast<std::uint32_t>(assignment.purchase_price));
            put32(payload, static_cast<std::uint32_t>(assignment.budget_after_purchase));
            put32(payload, static_cast<std::uint32_t>(assignment.action));
            if (assignment.state) {
                const auto& state = *assignment.state;
                put16(payload, state.expected_house); put16(payload, state.session_sha256 ? 1 : 0);
                put64(payload, state.expected_occupancy_epoch);
                put32(payload, static_cast<std::uint32_t>(state.budget_before));
                put32(payload, static_cast<std::uint32_t>(state.value_in_arch_before));
                put32(payload, static_cast<std::uint32_t>(state.value_in_arch_after));
                const auto digest = state.session_sha256.value_or(std::array<std::uint8_t, 32>{});
                payload.insert(payload.end(), digest.begin(), digest.end());
            }
            append(assignment.state ? 5 : 1, payload);
        }
        for (const auto& archive : overlay.archives) {
            std::vector<std::uint8_t> payload;
            put16(payload, archive.family_chunk_id);
            payload.insert(payload.end(), archive.family_identity.begin(), archive.family_identity.end());
            append(3, payload);
        }
        for (const auto& deletion : overlay.deletions) {
            std::vector<std::uint8_t> payload;
            put16(payload, deletion.family_chunk_id);
            payload.insert(payload.end(), deletion.family_identity.begin(), deletion.family_identity.end());
            put16(payload, static_cast<std::uint16_t>(deletion.members.size()));
            for (const auto& member : deletion.members) {
                put32(payload, member.guid);
                put16(payload, static_cast<std::uint16_t>(member.neighbor_id));
            }
            append(4, payload);
        }
        return out;
    }
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
    if (bytes.size() < header_size || bytes.size() > max_overlay_bytes)
        throw FormatError("family overlay size is outside the supported range");
    if (bytes[0] != 'O' || bytes[1] != 'D' || bytes[2] != 'H' || bytes[3] != 'F')
        throw FormatError("invalid family overlay signature");
    const auto version = get16(bytes, 4);
    if ((version != 1 && version != 2 && version != 3 && version != 4 && version != 5 && version != 6) || get16(bytes, 6) != 0)
        throw FormatError("unsupported family overlay version or flags");
    const auto count = get32(bytes, 40);
    if (count > max_assignments || ((version == 1 || version == 2) &&
        bytes.size() != header_size + record_size * count))
        throw FormatError("family overlay record count or extent is invalid");
    FamilyOverlay result;
    std::copy_n(bytes.begin() + 8, result.neighborhood_identity.size(), result.neighborhood_identity.begin());
    if (version == 3 || version == 4 || version == 5 || version == 6) {
        std::size_t at = header_size;
        bool saw_assignment = false;
        bool saw_archive = false;
        bool saw_deletion = false;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (bytes.size() - at < 6) throw FormatError("truncated tagged family overlay record header");
            const auto tag = get16(bytes, at);
            const auto length = get32(bytes, at + 2); at += 6;
            if (length > bytes.size() - at) throw FormatError("truncated tagged family overlay record");
            const auto end = at + length;
            if (tag == 1 || (tag == 5 && version == 6)) {
                if (saw_archive || saw_deletion) throw FormatError("family assignments must precede archives and deletions");
                if (length != (tag == 5 ? managed_record_size : record_size)) throw FormatError("invalid family operation extent");
                saw_assignment = true;
                FamilyOverlayAssignment assignment;
                assignment.family_chunk_id = get16(bytes, at);
                std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(at + 2), 32, assignment.family_identity.begin());
                assignment.house_number = get16(bytes, at + 34);
                assignment.purchase_price = signed32(get32(bytes, at + 36));
                assignment.budget_after_purchase = signed32(get32(bytes, at + 40));
                const auto action = get32(bytes, at + 44);
                if (action > (tag == 5 ? 2U : 1U)) throw FormatError("invalid family operation action");
                assignment.action = static_cast<FamilyOverlayAction>(action);
                if (tag == 5) {
                    FamilyOverlayOperationState state;
                    state.expected_house = get16(bytes, at + 48);
                    const auto flags = get16(bytes, at + 50);
                    if (flags > 1) throw FormatError("invalid managed family operation flags");
                    state.expected_occupancy_epoch = get64(bytes, at + 52);
                    state.budget_before = signed32(get32(bytes, at + 60));
                    state.value_in_arch_before = signed32(get32(bytes, at + 64));
                    state.value_in_arch_after = signed32(get32(bytes, at + 68));
                    std::array<std::uint8_t, 32> digest{};
                    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(at + 72), 32, digest.begin());
                    if (flags) state.session_sha256 = digest;
                    else if (!zero_digest(digest)) throw FormatError("absent session digest must be zero");
                    assignment.state = state;
                }
                result.assignments.push_back(std::move(assignment));
            } else if (tag == 2) {
                if (saw_assignment || saw_archive || saw_deletion || length > max_creation_bytes || length < 8)
                    throw FormatError("invalid v3 creation ordering or extent");
                FamilyOverlayCreation creation;
                creation.family_chunk_id = get16(bytes, at);
                const auto name_size = get16(bytes, at + 2); at += 4;
                if (!name_size || name_size > 256 || name_size > end - at)
                    throw FormatError("invalid v3 family name extent");
                creation.family_name.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                    bytes.begin() + static_cast<std::ptrdiff_t>(at + name_size)); at += name_size;
                if (end - at < 4) throw FormatError("truncated v3 FAMI extent");
                const auto fami_size = get32(bytes, at); at += 4;
                if (fami_size > 88 || fami_size > end - at) throw FormatError("invalid v3 FAMI extent");
                creation.fami_payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                    bytes.begin() + static_cast<std::ptrdiff_t>(at + fami_size)); at += fami_size;
                std::string raw(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(end));
                std::istringstream stream(raw); View view(stream, raw.size());
                const auto nbrs = read_nbrs(view, creation_limits);
                if (nbrs.reserved || nbrs.version != 73 || nbrs.remaining_bytes)
                    throw FormatError("invalid v3 NBRS container profile");
                for (const auto& entry : nbrs.entries) {
                    if (entry.marker != 1 || !entry.record) throw FormatError("v3 creation has placeholder member");
                    creation.members.push_back(*entry.record);
                }
                result.creations.push_back(std::move(creation));
            } else if (tag == 3 && (version == 4 || version == 5 || version == 6)) {
                if (saw_deletion || length != 34) throw FormatError("invalid family archive ordering or extent");
                saw_archive = true;
                FamilyOverlayArchive archive;
                archive.family_chunk_id = get16(bytes, at);
                std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(at + 2),
                    archive.family_identity.size(), archive.family_identity.begin());
                result.archives.push_back(archive);
            } else if (tag == 4 && (version == 5 || version == 6)) {
                if (length < 36) throw FormatError("truncated v5 family deletion");
                saw_deletion = true;
                FamilyOverlayDeletion deletion;
                deletion.family_chunk_id = get16(bytes, at);
                std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(at + 2),
                    deletion.family_identity.size(), deletion.family_identity.begin());
                const auto member_count = get16(bytes, at + 34);
                if (member_count > 8 || length != 36 + static_cast<std::size_t>(member_count) * 6)
                    throw FormatError("invalid v5 family deletion member extent");
                deletion.members.reserve(member_count);
                std::size_t member_at = at + 36;
                for (std::uint16_t member = 0; member < member_count; ++member, member_at += 6)
                    deletion.members.push_back({get32(bytes, member_at),
                        static_cast<std::int16_t>(get16(bytes, member_at + 4))});
                result.deletions.push_back(std::move(deletion));
            } else throw FormatError("unknown v3 record tag");
            at = end;
        }
        if (at != bytes.size()) throw FormatError("trailing v3 bytes");
        validate_overlay(result);
        return result;
    }
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

FamilyOverlayReplay replay_family_overlay(const std::vector<FamilyEntry>& source,
        const std::array<std::uint8_t, 32>& neighborhood_identity, const FamilyOverlay& overlay) {
    validate_overlay(overlay);
    if (overlay.neighborhood_identity != neighborhood_identity)
        throw FormatError("family overlay belongs to different Neighborhood.iff data");
    (void)FamilyContext(source); // Bound before copying member arrays.
    auto result = source;
    std::set<std::uint16_t> source_ids;
    std::set<std::uint32_t> guids;
    for (const auto& entry : source) {
        source_ids.insert(entry.chunk_id);
        guids.insert(entry.family.member_guids.begin(), entry.family.member_guids.end());
    }
    for (const auto& creation : overlay.creations) {
        auto family = creation_family(creation);
        if (result.empty())
            throw FormatError("created FamilyNumber requires a nonempty source family snapshot");
        const auto highest = std::max_element(result.begin(), result.end(),
            [](const FamilyEntry& left, const FamilyEntry& right) {
                return left.family.family_number < right.family.family_number;
            })->family.family_number;
        if (highest == std::numeric_limits<std::int32_t>::max() ||
            family.family_number != static_cast<std::int64_t>(highest) + 1)
            throw FormatError("created FamilyNumber must follow source and prior creations Max+1");
        if (!source_ids.insert(creation.family_chunk_id).second)
            throw FormatError("created FAMI chunk ID is already used");
        for (const auto guid : family.member_guids)
            if (!guids.insert(guid).second) throw FormatError("created GUID is already used");
        result.push_back({creation.family_chunk_id, std::move(family), creation.family_name});
    }
    (void)FamilyContext(result);
    const auto originals = result;
    std::vector<FamilyOverlayFamilyState> states;
    for (const auto& entry : result) {
        const auto house = static_cast<std::uint16_t>(static_cast<std::uint32_t>(entry.family.house_number) & 0xffffU);
        states.push_back({entry.chunk_id, family_overlay_identity(entry), house,
            (house >= 1 && house <= 98) ? 1U : 0U, entry.family.budget, entry.family.value_in_arch, false, std::nullopt, 0, 0});
    }
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
        if (family_overlay_identity(originals[found->second]) != assignment.family_identity)
            throw FormatError("family overlay identity is stale");
        const auto current_house = static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(family.house_number) & 0xffffU);
        auto& state = states[found->second];
        if (assignment.state) {
            const auto& before = *assignment.state;
            if (before.expected_house != current_house || before.expected_occupancy_epoch != state.occupancy_epoch ||
                before.budget_before != family.budget || before.value_in_arch_before != family.value_in_arch)
                throw FormatError("managed family operation before-state is stale");
            state.managed = true;
        } else if (state.managed) {
            throw FormatError("managed family history cannot return to legacy operations");
        }
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
            if (state.occupancy_epoch == std::numeric_limits<std::uint64_t>::max())
                throw FormatError("family occupancy epoch overflows");
            if (assignment.state && assignment.state->value_in_arch_after != family.value_in_arch)
                throw FormatError("managed move-in must retain ValueInArch");
            ++state.occupancy_epoch;
            family.house_number = assignment.house_number;
            family.budget = assignment.budget_after_purchase;
            occupied.emplace(assignment.house_number, assignment.family_chunk_id);
            move_out_supported[assignment.family_chunk_id] = false;
        } else if (assignment.action == FamilyOverlayAction::checkpoint) {
            if (!assignment.state || current_house == 0 || current_house > 98 ||
                assignment.house_number != current_house ||
                assignment.state->value_in_arch_after != family.value_in_arch)
                throw FormatError("family checkpoint must retain current occupancy and ValueInArch");
            family.budget = assignment.budget_after_purchase;
            state.checkpoint_sha256 = assignment.state->session_sha256;
            state.checkpoint_house = current_house;
            state.checkpoint_epoch = state.occupancy_epoch;
        } else {
            if (!assignment.state && !move_out_supported[assignment.family_chunk_id])
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
            if (assignment.state && assignment.state->value_in_arch_after != 0)
                throw FormatError("managed move-out must clear ValueInArch");
            family.budget = static_cast<std::int32_t>(next_budget);
            family.value_in_arch = 0;
            family.house_number = 0;
            move_out_supported[assignment.family_chunk_id] = false;
        }
        state.house_number = static_cast<std::uint16_t>(family.house_number);
        state.budget = family.budget;
        state.value_in_arch = family.value_in_arch;
    }
    std::set<std::uint16_t> archived_ids;
    for (const auto& archive : overlay.archives) archived_ids.insert(archive.family_chunk_id);
    for (const auto& deletion : overlay.deletions) {
        const auto original = std::find_if(source.begin(), source.end(), [&](const auto& entry) {
            return entry.chunk_id == deletion.family_chunk_id;
        });
        if (original == source.end() ||
            family_overlay_identity(*original) != deletion.family_identity ||
            original->family.member_guids.size() != deletion.members.size())
            throw FormatError("family deletion identity or roster is stale");
        for (std::size_t i = 0; i < deletion.members.size(); ++i)
            if (original->family.member_guids[i] != deletion.members[i].guid)
                throw FormatError("family deletion roster differs from source FAMI");
    }
    std::set<std::uint16_t> deleted_ids;
    for (const auto& deletion : overlay.deletions) deleted_ids.insert(deletion.family_chunk_id);
    result.erase(std::remove_if(result.begin(), result.end(), [&](const auto& entry) {
        return archived_ids.count(entry.chunk_id) != 0 || deleted_ids.count(entry.chunk_id) != 0;
    }), result.end());
    states.erase(std::remove_if(states.begin(), states.end(), [&](const auto& entry) {
        return archived_ids.count(entry.family_chunk_id) != 0 || deleted_ids.count(entry.family_chunk_id) != 0;
    }), states.end());
    return {std::move(result), std::move(states)};
}

std::vector<FamilyEntry> apply_family_overlay(const std::vector<FamilyEntry>& source,
        const std::array<std::uint8_t, 32>& identity, const FamilyOverlay& overlay) {
    return replay_family_overlay(source, identity, overlay).families;
}

namespace {
FamilyOverlay append_managed_operation(const std::vector<FamilyEntry>& source,
        const std::array<std::uint8_t, 32>& identity, const FamilyOverlay* current,
        std::uint16_t family_id, FamilyOverlayAction action, std::uint16_t house,
        std::int32_t price, std::optional<std::int32_t> budget,
        std::optional<std::pair<std::uint16_t, std::uint64_t>> expected,
        std::optional<std::array<std::uint8_t, 32>> digest) {
    FamilyOverlay result = current ? *current : FamilyOverlay{identity, {}};
    const auto replay = replay_family_overlay(source, identity, result);
    const auto found = std::find_if(replay.states.begin(), replay.states.end(),
        [&](const auto& state) { return state.family_chunk_id == family_id; });
    if (found == replay.states.end()) throw FormatError("managed operation family is absent");
    if (expected && (expected->first != found->house_number || expected->second != found->occupancy_epoch))
        throw FormatError("family checkpoint occupancy is stale");
    if (action == FamilyOverlayAction::move_out && (found->house_number == 0 || found->house_number > 98))
        throw FormatError("managed move-out requires current occupancy");
    std::int64_t after = budget.value_or(found->budget);
    if (action == FamilyOverlayAction::move_in) after = static_cast<std::int64_t>(found->budget) - price;
    if (action == FamilyOverlayAction::move_out) after = static_cast<std::int64_t>(found->budget) + found->value_in_arch;
    if (after < 0 || after > std::numeric_limits<std::int32_t>::max())
        throw FormatError("managed family operation budget is outside signed32");
    FamilyOverlayAssignment operation{family_id, found->family_identity, house, price,
        static_cast<std::int32_t>(after), action, std::nullopt};
    operation.state = FamilyOverlayOperationState{found->house_number, found->occupancy_epoch,
        found->budget, found->value_in_arch,
        action == FamilyOverlayAction::move_out ? 0 : found->value_in_arch, digest};
    result.assignments.push_back(std::move(operation));
    (void)replay_family_overlay(source, identity, result);
    return result;
}
}
FamilyOverlay plan_family_managed_move_in_overlay(const std::vector<FamilyEntry>& source,
        const std::array<std::uint8_t, 32>& identity, const FamilyOverlay* current,
        std::uint16_t family_id, std::uint16_t house, std::int32_t price) {
    return append_managed_operation(source, identity, current, family_id, FamilyOverlayAction::move_in,
        house, price, {}, {}, {});
}
FamilyOverlay plan_family_managed_move_out_overlay(const std::vector<FamilyEntry>& source,
        const std::array<std::uint8_t, 32>& identity, const FamilyOverlay* current, std::uint16_t family_id) {
    return append_managed_operation(source, identity, current, family_id, FamilyOverlayAction::move_out,
        0, 0, {}, {}, {});
}
FamilyOverlay plan_family_checkpoint_overlay(const std::vector<FamilyEntry>& source,
        const std::array<std::uint8_t, 32>& identity, const FamilyOverlay* current,
        std::uint16_t family_id, std::uint16_t expected_house, std::uint64_t expected_epoch,
        std::int32_t budget_after, const std::array<std::uint8_t, 32>& digest) {
    return append_managed_operation(source, identity, current, family_id, FamilyOverlayAction::checkpoint,
        expected_house, 0, budget_after, std::make_pair(expected_house, expected_epoch), digest);
}

Nbrs apply_neighbor_overlay(const Nbrs& source,
        const std::array<std::uint8_t, 32>& neighborhood_identity, const FamilyOverlay& overlay) {
    validate_overlay(overlay);
    if (overlay.neighborhood_identity != neighborhood_identity)
        throw FormatError("neighbor overlay belongs to different Neighborhood.iff data");
    if (source.entries.size() > NbrsLimits{}.entries)
        throw FormatError("source NBRS exceeds entry limit");
    std::size_t decoded = 0, relationships = 0, values = 0;
    for (const auto& entry : source.entries) if (entry.record) {
        const auto& member = *entry.record;
        bound_neighbor_storage(member);
        if (member.relationships.size() > 16384 - relationships)
            throw FormatError("source NBRS relationship copy budget exceeded");
        relationships += member.relationships.size();
        const auto bytes = member.name.size() + (member.person_data ? member.person_data->size() * 2 : 0);
        if (bytes > 512 * 1024 - decoded)
            throw FormatError("source NBRS decoded copy budget exceeded");
        decoded += bytes;
        for (const auto& relationship : member.relationships) {
            if (relationship.values.size() > 65536 - values ||
                relationship.values.size() * 4 > 512 * 1024 - decoded)
                throw FormatError("source NBRS value copy budget exceeded");
            values += relationship.values.size(); decoded += relationship.values.size() * 4;
        }
    }
    // Validate known source fields without discarding unpreserved tail extent.
    auto validation = source; validation.remaining_bytes = 0;
    (void)encode_nbrs(validation);
    std::set<std::int16_t> ids;
    std::set<std::uint32_t> guids;
    for (const auto& entry : source.entries) if (entry.record) {
        ids.insert(entry.record->neighbor_id); guids.insert(entry.record->guid);
    }
    auto result = source;
    for (const auto& creation : overlay.creations)
        for (const auto& member : creation.members) {
            if (!ids.insert(member.neighbor_id).second || !guids.insert(member.guid).second)
                throw FormatError("created neighbor ID or GUID is already used");
            result.entries.push_back({1, member});
        }
    std::set<std::uint32_t> archived_guids;
    for (const auto& archive : overlay.archives) {
        const auto creation = std::find_if(overlay.creations.begin(), overlay.creations.end(),
            [&](const auto& value) { return value.family_chunk_id == archive.family_chunk_id; });
        if (creation == overlay.creations.end())
            throw FormatError("family archive lost its player-created records");
        for (const auto& member : creation->members) archived_guids.insert(member.guid);
    }
    std::set<std::int16_t> deleted_neighbor_ids;
    for (const auto& deletion : overlay.deletions) {
        for (const auto& member : deletion.members) {
            const auto found = std::find_if(source.entries.begin(), source.entries.end(),
                [&](const NeighborEntry& entry) {
                    return entry.record && entry.record->neighbor_id == member.neighbor_id &&
                        entry.record->guid == member.guid;
                });
            if (found == source.entries.end())
                throw FormatError("family deletion member is absent from source NBRS");
            const auto duplicates = std::count_if(source.entries.begin(), source.entries.end(),
                [&](const NeighborEntry& entry) {
                    return entry.record && (entry.record->neighbor_id == member.neighbor_id ||
                        entry.record->guid == member.guid);
                });
            if (duplicates != 1)
                throw FormatError("family deletion member is ambiguous in source NBRS");
            archived_guids.insert(member.guid);
            deleted_neighbor_ids.insert(member.neighbor_id);
        }
    }
    result.entries.erase(std::remove_if(result.entries.begin(), result.entries.end(),
        [&](const NeighborEntry& entry) {
            return entry.record && archived_guids.count(entry.record->guid) != 0;
        }), result.entries.end());
    for (auto& entry : result.entries) if (entry.record) {
        auto& relationships = entry.record->relationships;
        relationships.erase(std::remove_if(relationships.begin(), relationships.end(),
            [&](const NeighborRelationship& relationship) {
                return relationship.key_count == 1 &&
                    relationship.key >= std::numeric_limits<std::int16_t>::min() &&
                    relationship.key <= std::numeric_limits<std::int16_t>::max() &&
                    deleted_neighbor_ids.count(static_cast<std::int16_t>(relationship.key));
            }), relationships.end());
    }
    validation = result; validation.remaining_bytes = 0;
    (void)encode_nbrs(validation); // Cumulative source + overlay limits.
    return result;
}

FamilyOverlay plan_family_creation_overlay(const std::vector<FamilyEntry>& source,
        const Nbrs& neighbors, const std::array<std::uint8_t, 32>& neighborhood_identity,
        const FamilyOverlay* current_overlay, const FamilyOverlayCreation& creation) {
    if (current_overlay) {
        validate_overlay(*current_overlay);
        if (current_overlay->neighborhood_identity != neighborhood_identity)
            throw FormatError("current family overlay belongs to different Neighborhood.iff data");
        if (current_overlay->creations.size() + current_overlay->assignments.size() +
            current_overlay->archives.size() >= max_assignments)
            throw FormatError("family overlay exceeds operation limit");
    }
    (void)creation_family(creation); // Bound nested storage before appending a copy.
    FamilyOverlay result = current_overlay ? *current_overlay : FamilyOverlay{neighborhood_identity, {}, {}};
    result.creations.push_back(creation);
    (void)apply_family_overlay(source, neighborhood_identity, result);
    (void)apply_neighbor_overlay(neighbors, neighborhood_identity, result);
    return result;
}

FamilyOverlay plan_family_archive_overlay(const std::vector<FamilyEntry>& source,
        const Nbrs& neighbors, const std::array<std::uint8_t, 32>& neighborhood_identity,
        const FamilyOverlay* current_overlay, std::uint16_t family_chunk_id) {
    if (!current_overlay)
        throw FormatError("only a player-created family can be archived");
    validate_overlay(*current_overlay);
    if (current_overlay->neighborhood_identity != neighborhood_identity)
        throw FormatError("current family overlay belongs to different Neighborhood.iff data");
    const auto creation = std::find_if(current_overlay->creations.begin(),
        current_overlay->creations.end(), [&](const auto& value) {
            return value.family_chunk_id == family_chunk_id;
        });
    if (creation == current_overlay->creations.end())
        throw FormatError("only a player-created family can be archived");
    if (std::any_of(current_overlay->archives.begin(), current_overlay->archives.end(),
        [&](const auto& value) { return value.family_chunk_id == family_chunk_id; }))
        throw FormatError("family is already archived");
    if (std::any_of(current_overlay->assignments.begin(), current_overlay->assignments.end(),
        [&](const auto& value) { return value.family_chunk_id == family_chunk_id; }))
        throw FormatError("family with move history cannot be archived");
    const auto effective = apply_family_overlay(source, neighborhood_identity, *current_overlay);
    const auto found = std::find_if(effective.begin(), effective.end(), [&](const auto& entry) {
        return entry.chunk_id == family_chunk_id;
    });
    if (found == effective.end()) throw FormatError("family is no longer available to archive");
    const auto house = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(found->family.house_number) & 0xffffU);
    if (house != 0) throw FormatError("only an unhoused family can be archived");

    FamilyOverlay result = *current_overlay;
    result.archives.push_back({family_chunk_id, family_overlay_identity(*found)});
    (void)apply_family_overlay(source, neighborhood_identity, result);
    (void)apply_neighbor_overlay(neighbors, neighborhood_identity, result);
    return result;
}

FamilyOverlay plan_family_delete_overlay(const std::vector<FamilyEntry>& source,
        const Nbrs& neighbors, const std::array<std::uint8_t, 32>& neighborhood_identity,
        const FamilyOverlay* current_overlay, std::uint16_t family_chunk_id) {
    if (current_overlay) {
        validate_overlay(*current_overlay);
        if (current_overlay->neighborhood_identity != neighborhood_identity)
            throw FormatError("current family overlay belongs to different Neighborhood.iff data");
        if (current_overlay->assignments.size() + current_overlay->creations.size() +
            current_overlay->archives.size() + current_overlay->deletions.size() >= max_assignments)
            throw FormatError("family overlay exceeds operation limit");
        if (std::any_of(current_overlay->deletions.begin(), current_overlay->deletions.end(),
            [&](const auto& value) { return value.family_chunk_id == family_chunk_id; }))
            throw FormatError("family is already deleted");
        if (std::any_of(current_overlay->assignments.begin(), current_overlay->assignments.end(),
            [&](const auto& value) { return value.family_chunk_id == family_chunk_id; }))
            throw FormatError("family with move history cannot be deleted");
        if (std::any_of(current_overlay->creations.begin(), current_overlay->creations.end(),
            [&](const auto& value) { return value.family_chunk_id == family_chunk_id; }))
            throw FormatError("player-created families must use the archive operation");
    }
    const FamilyContext source_context(source);
    const auto* original = source_context.by_id(family_chunk_id);
    if (!original) throw FormatError("selected source family is absent from Neighborhood.iff");
    const auto effective = current_overlay ?
        apply_family_overlay(source, neighborhood_identity, *current_overlay) : source;
    const FamilyContext effective_context(effective);
    const auto* family = effective_context.by_id(family_chunk_id);
    if (!family) throw FormatError("selected family is no longer available");
    if ((static_cast<std::uint32_t>(family->family.house_number) & 0xffffU) != 0)
        throw FormatError("only an unhoused family can be deleted from the family picker");
    if (original->family.member_guids.empty() || original->family.member_guids.size() > 8)
        throw FormatError("source family roster is empty or exceeds the supported member limit");

    FamilyOverlayDeletion deletion;
    deletion.family_chunk_id = family_chunk_id;
    deletion.family_identity = family_overlay_identity(*original);
    std::set<std::uint32_t> roster;
    for (const auto guid : original->family.member_guids) {
        if (!guid || !roster.insert(guid).second)
            throw FormatError("source family roster has an invalid or duplicate GUID");
        const auto owners = std::count_if(source.begin(), source.end(), [&](const auto& entry) {
            return std::find(entry.family.member_guids.begin(), entry.family.member_guids.end(), guid) !=
                entry.family.member_guids.end();
        });
        if (owners != 1) throw FormatError("source family member is shared by another family");
        const NeighborRecord* member = nullptr;
        std::size_t matches = 0;
        for (const auto& entry : neighbors.entries) if (entry.record && entry.record->guid == guid) {
            member = &*entry.record;
            ++matches;
        }
        if (matches != 1 || !member || member->neighbor_id <= 0)
            throw FormatError("source family member has no unique positive NBRS identity");
        deletion.members.push_back({guid, member->neighbor_id});
    }

    FamilyOverlay result = current_overlay ? *current_overlay :
        FamilyOverlay{neighborhood_identity, {}, {}, {}};
    result.deletions.push_back(std::move(deletion));
    (void)apply_family_overlay(source, neighborhood_identity, result);
    (void)apply_neighbor_overlay(neighbors, neighborhood_identity, result);
    return result;
}

} // namespace odh
