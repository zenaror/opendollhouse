#include "odh/family_overlay.hpp"
#include <iostream>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F&& action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const odh::FormatError&) { rejected = true; }
    require(rejected, message);
}
odh::FamilyEntry family(std::uint16_t id, std::int32_t house, std::int32_t budget,
                        std::vector<std::uint32_t> members, std::int32_t value_in_arch = 0) {
    odh::Family value;
    value.house_number = house;
    value.family_number = static_cast<std::int32_t>(id + 20);
    value.budget = budget;
    value.value_in_arch = value_in_arch;
    value.member_guids = std::move(members);
    return {id, std::move(value)};
}
odh::FamilyOverlayAssignment assignment(const odh::FamilyEntry& entry,
        std::uint16_t house, std::int32_t budget) {
    return odh::plan_family_move_in(entry, house, entry.family.budget - budget);
}
std::uint32_t le32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes.at(at)) |
        (static_cast<std::uint32_t>(bytes.at(at + 1)) << 8) |
        (static_cast<std::uint32_t>(bytes.at(at + 2)) << 16) |
        (static_cast<std::uint32_t>(bytes.at(at + 3)) << 24);
}
}

int main() {
    try {
        std::array<std::uint8_t, 32> source_id{};
        source_id[0] = 0x51; source_id[31] = 0xa7;
        const auto cas = family(12, 0, 20000, {0xffffffffU, 0x80000000U});
        const auto resident = family(18, 5, 12000, {0x12345678U}, 6000);
        odh::FamilyOverlay overlay{source_id, {odh::plan_family_move_in(cas, 7, 5500)}};
        require(overlay.assignments[0].budget_after_purchase == 14500,
                "move-in plan charges the supplied lot purchase price once");
        rejects([&] { (void)odh::plan_family_move_in(cas, 7, 20001); }, "unaffordable move-in rejects");
        rejects([&] { (void)odh::plan_family_move_in(cas, 0, 0); }, "house zero cannot be a destination");
        rejects([&] { (void)odh::plan_family_move_in(cas, 99, 0); }, "house 99 cannot be a destination");
        rejects([&] { (void)odh::plan_family_move_in(resident, 7, 5500); },
                "housed family must be evicted before move-in planning");
        auto empty_family = family(22, 0, 20000, {});
        rejects([&] { (void)odh::plan_family_move_in(empty_family, 7, 0); }, "empty family cannot move in");

        const auto bytes = odh::encode_family_overlay(overlay);
        require(bytes.size() == 92 && le32(bytes, 80) == 5500 && le32(bytes, 84) == 14500,
                "version 2 golden extent and purchase/budget byte offsets");
        const auto decoded = odh::decode_family_overlay(bytes);
        require(decoded.neighborhood_identity == source_id && decoded.assignments.size() == 1,
                "round trip retains source and assignment count");
        require(decoded.assignments[0].family_chunk_id == 12 &&
                decoded.assignments[0].house_number == 7 &&
                decoded.assignments[0].purchase_price == 5500 &&
                decoded.assignments[0].budget_after_purchase == 14500 &&
                decoded.assignments[0].family_identity == odh::family_overlay_identity(cas),
                "round trip retains family identity, destination and charged budget");

        const auto applied = odh::apply_family_overlay({resident, cas}, source_id, decoded);
        require(applied[0].family.house_number == 5 && applied[1].family.house_number == 7,
                "reordered source chunks resolve by stable chunk identity");
        require(applied[1].family.budget == 14500 && cas.family.house_number == 0 &&
                cas.family.budget == 20000, "applies to owned copy and preserves source data");

        const auto moved_out = odh::plan_family_move_out_overlay(
            {resident, cas}, source_id, nullptr, resident.chunk_id);
        const auto moved_out_effective = odh::apply_family_overlay(
            {resident, cas}, source_id, moved_out);
        require(moved_out.assignments.size() == 1 &&
                moved_out.assignments[0].action == odh::FamilyOverlayAction::move_out &&
                moved_out_effective[0].family.house_number == 0 &&
                moved_out_effective[0].family.budget == 18000 &&
                moved_out_effective[0].family.value_in_arch == 0,
                "move-out refunds the source ValueInArch, clears it, and releases the house");
        const auto rehomed = odh::plan_family_move_in_overlay(
            {resident, cas}, source_id, &moved_out, resident.chunk_id, 9, 3000);
        const auto rehomed_effective = odh::apply_family_overlay(
            {resident, cas}, source_id, rehomed);
        require(rehomed.assignments.size() == 2 &&
                rehomed_effective[0].family.house_number == 9 &&
                rehomed_effective[0].family.budget == 15000 &&
                rehomed_effective[0].family.value_in_arch == 0,
                "rehome replays move-out then charges the new lot against the refunded budget");
        rejects([&] { (void)odh::plan_family_move_out_overlay(
                    {resident, cas}, source_id, &rehomed, resident.chunk_id); },
                "rehome cannot invent a second architecture refund after a player move-in");
        rejects([&] { (void)odh::plan_family_move_out_overlay(
                    {resident, cas}, source_id, &decoded, cas.chunk_id); },
                "a family without a source house cannot receive an unverified architecture refund");
        rejects([&] { (void)odh::plan_family_move_out_overlay(
                    {resident, cas}, source_id, nullptr, cas.chunk_id); },
                "a newly moved family cannot receive a source architecture refund");
        auto negative_refund = family(25, 4, 1000, {7}, -1);
        rejects([&] { (void)odh::plan_family_move_out_overlay(
                    {negative_refund}, source_id, nullptr, negative_refund.chunk_id); },
                "negative source architecture value rejects move-out");
        const auto forged_negative_refund = odh::FamilyOverlayAssignment{
            negative_refund.chunk_id, odh::family_overlay_identity(negative_refund),
            0, 0, 999, odh::FamilyOverlayAction::move_out};
        rejects([&] { (void)odh::apply_family_overlay(
                    {negative_refund}, source_id,
                    odh::FamilyOverlay{source_id, {forged_negative_refund}}); },
                "replay rejects a forged move-out with a negative architecture refund");
        auto overflow_refund = family(26, 4, 2147483000, {7}, 1000);
        rejects([&] { (void)odh::plan_family_move_out_overlay(
                    {overflow_refund}, source_id, nullptr, overflow_refund.chunk_id); },
                "move-out refund cannot overflow the family budget field");

        const auto household = family(13, 0, 9000, {0x01020304U});
        const auto expanded = odh::plan_family_move_in_overlay(
            {resident, cas, household}, source_id, &decoded, household.chunk_id, 8, 4000);
        const auto expanded_effective = odh::apply_family_overlay(
            {resident, cas, household}, source_id, expanded);
        require(expanded.assignments.size() == 2 &&
                expanded_effective[1].family.house_number == 7 &&
                expanded_effective[2].family.house_number == 8 &&
                expanded_effective[2].family.budget == 5000,
                "move-in transaction preserves prior moves and applies latest family/house state");
        rejects([&] { (void)odh::plan_family_move_in_overlay(
                    {resident, cas, household}, source_id, &decoded,
                    household.chunk_id, 7, 4000); },
                "move-in transaction rejects a destination occupied in current overlay");
        rejects([&] { (void)odh::plan_family_move_in_overlay(
                    {resident, cas, household}, source_id, &decoded,
                    household.chunk_id, 8, 10000); },
                "move-in transaction rejects unaffordable purchase");
        auto stale_overlay = decoded;
        stale_overlay.assignments[0].house_number = 8;
        rejects([&] { (void)odh::plan_family_move_in_overlay(
                    {resident, cas, household}, source_id, &stale_overlay,
                    cas.chunk_id, 9, 1000); },
                "move-in transaction rejects a conflicting current overlay");

        auto stale_source = source_id; stale_source[1] = 1;
        rejects([&] { (void)odh::apply_family_overlay({cas, resident}, stale_source, decoded); },
                "changed Neighborhood identity fails closed");
        auto changed = cas; changed.family.member_guids[0] ^= 1U;
        rejects([&] { (void)odh::apply_family_overlay({changed, resident}, source_id, decoded); },
                "changed family roster fails closed");
        changed = cas; changed.family.budget++;
        rejects([&] { (void)odh::apply_family_overlay({changed, resident}, source_id, decoded); },
                "changed source budget fails closed");
        rejects([&] { (void)odh::apply_family_overlay({resident}, source_id, decoded); },
                "missing referenced FAMI fails closed");

        const auto occupied = family(19, 7, 3000, {0x99887766U});
        rejects([&] { (void)odh::apply_family_overlay({cas, occupied}, source_id, decoded); },
                "occupied destination rejects assignment");
        const auto low16_occupied = family(20, 65543, 3000, {0x88776655U});
        rejects([&] { (void)odh::apply_family_overlay({cas, low16_occupied}, source_id, decoded); },
                "destination aliases in the original signed-low16 house profile reject");
        const auto moving = family(21, 5, 20000, {0x01020304U});
        odh::FamilyOverlayAssignment invalid_move{moving.chunk_id,
            odh::family_overlay_identity(moving), 7, 5500, 14500};
        rejects([&] { (void)odh::apply_family_overlay({moving}, source_id,
                    odh::FamilyOverlay{source_id, {invalid_move}}); },
                "overlay cannot move a housed family without eviction accounting");
        auto duplicate = overlay;
        duplicate.assignments.push_back(assignment(family(13, 0, 500, {9}), 7, 500));
        rejects([&] { (void)odh::apply_family_overlay(
                    {resident, cas, family(13, 0, 500, {9})}, source_id, duplicate); },
                "duplicate destination rejects when replayed against current occupancy");
        duplicate = overlay;
        duplicate.assignments.push_back(assignment(family(12, 0, 500, {9}), 8, 400));
        rejects([&] { (void)odh::apply_family_overlay({cas}, source_id, duplicate); },
                "duplicate family operations reject when replayed against current state");
        duplicate = overlay;
        duplicate.assignments[0].house_number = 99;
        rejects([&] { (void)odh::encode_family_overlay(duplicate); }, "house 99 outside supported range");
        duplicate = overlay;
        duplicate.assignments[0].budget_after_purchase = 20001;
        rejects([&] { (void)odh::apply_family_overlay({cas}, source_id, duplicate); },
                "purchase cannot increase source budget");

        for (std::size_t n = 0; n < bytes.size(); ++n) {
            auto short_bytes = bytes; short_bytes.resize(n);
            rejects([&] { (void)odh::decode_family_overlay(short_bytes); },
                    "every truncated overlay prefix rejects");
        }
        auto malformed = bytes; malformed[0] = 'X';
        rejects([&] { (void)odh::decode_family_overlay(malformed); }, "bad signature rejects");
        malformed = bytes; malformed[4] = 3;
        rejects([&] { (void)odh::decode_family_overlay(malformed); }, "unknown version rejects");
        auto legacy = bytes; legacy[4] = 1;
        const auto decoded_legacy = odh::decode_family_overlay(legacy);
        require(decoded_legacy.assignments.size() == 1 &&
                decoded_legacy.assignments[0].action == odh::FamilyOverlayAction::move_in,
                "version 1 purchase-only save remains readable as a move-in");
        malformed = bytes; malformed.push_back(0);
        rejects([&] { (void)odh::decode_family_overlay(malformed); }, "trailing bytes reject");
        malformed = bytes; malformed[40] = 0xff;
        rejects([&] { (void)odh::decode_family_overlay(malformed); }, "impossible record count rejects");
        malformed = bytes; malformed[88] = 1;
        rejects([&] { (void)odh::decode_family_overlay(malformed); }, "nonzero reserved field rejects");

        odh::Family created_family;
        created_family.version = 9; created_family.unknown = 24;
        created_family.family_number = 39; created_family.budget = 20000;
        created_family.member_guids = {0x10000001U, 0x10000002U};
        odh::NeighborRecord first{};
        first.version = 10; first.unknown3 = 9; first.name = "iffname";
        first.mystery_zero = 0; first.person_mode = 9;
        first.person_data = std::vector<std::int16_t>(256, 0);
        (*first.person_data)[2] = 500; (*first.person_data)[61] = 0;
        first.neighbor_id = 100; first.guid = created_family.member_guids[0];
        first.unknown_neg_one = -1;
        auto second = first; second.neighbor_id = 101;
        second.guid = created_family.member_guids[1];
        odh::FamilyOverlayCreation creation{0, "New family",
            odh::encode_simitone_create_family_fami(created_family), {first, second}};
        auto original_neighbor = first; original_neighbor.neighbor_id = 1;
        original_neighbor.guid = 0x12345678U;
        original_neighbor.unknown3 = 17; original_neighbor.name = "Ada";
        original_neighbor.mystery_zero = 23; original_neighbor.unknown_neg_one = -7;
        original_neighbor.relationships = {{1, 19, {5, -1, 0x12345678}}};
        odh::Nbrs neighbors{55, 63, {{0, std::nullopt}, {1, original_neighbor}}, 0};
        const auto neighbor_bytes = odh::encode_nbrs(neighbors);
        const auto created = odh::plan_family_creation_overlay(
            {resident, cas}, neighbors, source_id, nullptr, creation);
        const auto v3 = odh::encode_family_overlay(created);
        require(v3[4] == 3 && v3[44] == 2 && le32(v3, 40) == 1,
                "creation uses tagged length-prefixed v3 with one creation");
        const auto created_decoded = odh::decode_family_overlay(v3);
        require(odh::encode_family_overlay(created_decoded) == v3 &&
                created_decoded.creations[0].family_name == "New family",
                "v3 canonical roundtrip preserves name and exact payloads");
        const auto created_effective = odh::apply_family_overlay({resident, cas}, source_id, created_decoded);
        require(created_effective.size() == 3 && created_effective[2].chunk_id == 0 &&
                created_effective[2].family.family_number == 39 &&
                created_effective[2].display_name == "New family" &&
                created_effective[2].family.member_guids == created_family.member_guids,
                "free FAMI zero appends a named owned household without changing original order");
        const auto appended = odh::apply_neighbor_overlay(neighbors, source_id, created_decoded);
        auto original_prefix = appended; original_prefix.entries.resize(neighbors.entries.size());
        require(odh::encode_nbrs(original_prefix) == neighbor_bytes && appended.entries.size() == 4 &&
                appended.entries[2].record->person_data == first.person_data &&
                appended.entries[2].record->relationships.empty() &&
                appended.entries[2].record->unknown3 == first.unknown3 &&
                appended.entries[2].record->mystery_zero == 0 &&
                appended.entries[2].record->unknown_neg_one == -1 && neighbors.entries.size() == 2,
                "neighbor replay preserves original bytes, placeholders, fields and source ownership");

        const auto archived = odh::plan_family_archive_overlay(
            {resident, cas}, neighbors, source_id, &created, 0);
        const auto v4 = odh::encode_family_overlay(archived);
        require(v4[4] == 4 && le32(v4, 40) == 2,
                "logical family archive uses v4 with its creation and archive records");
        const auto archived_decoded = odh::decode_family_overlay(v4);
        require(odh::encode_family_overlay(archived_decoded) == v4 &&
                archived_decoded.creations.size() == 1 && archived_decoded.archives.size() == 1,
                "v4 roundtrip retains the family payload and archive tombstone");
        const auto archived_families = odh::apply_family_overlay(
            {resident, cas}, source_id, archived_decoded);
        const auto archived_neighbors = odh::apply_neighbor_overlay(
            neighbors, source_id, archived_decoded);
        require(archived_families.size() == 2 && archived_families[0].chunk_id == resident.chunk_id &&
                archived_families[1].chunk_id == cas.chunk_id &&
                archived_neighbors.entries.size() == neighbors.entries.size() &&
                odh::encode_nbrs(archived_neighbors) == neighbor_bytes &&
                odh::encode_nbrs(neighbors) == neighbor_bytes,
                "archive hides only player-created family records and leaves source NBRS unchanged");
        rejects([&] { (void)odh::plan_family_archive_overlay(
                    {resident, cas}, neighbors, source_id, &archived, 0); },
                "already archived family cannot be archived again");
        rejects([&] { (void)odh::plan_family_archive_overlay(
                    {resident, cas}, neighbors, source_id, &created, resident.chunk_id); },
                "original commercial family cannot be archived");
        const auto created_with_history = odh::plan_family_move_in_overlay(
            {resident, cas}, source_id, &created, 0, 7, 5000);
        rejects([&] { (void)odh::plan_family_archive_overlay(
                    {resident, cas}, neighbors, source_id, &created_with_history, 0); },
                "family with move history cannot be archived");
        auto forged_archive = archived;
        forged_archive.archives[0].family_identity[0] ^= 1;
        rejects([&] { (void)odh::encode_family_overlay(forged_archive); },
                "archive identity must match the retained creation record");

        const auto source_delete = family(30, 0, 8000, {0x20000001U, 0x20000002U});
        auto delete_first = first; delete_first.neighbor_id = 20;
        delete_first.guid = 0x20000001U; delete_first.relationships = {{1, 22, {60, -15}}};
        auto delete_second = first; delete_second.neighbor_id = 21;
        delete_second.guid = 0x20000002U;
        auto survivor = original_neighbor; survivor.neighbor_id = 22;
        survivor.guid = 0x30000001U;
        survivor.relationships = {{1, 20, {60, -15}}, {1, 21, {55, -25}}, {1, 1, {10, 10}}};
        odh::Nbrs deletion_neighbors{0, 73,
            {{1, delete_first}, {1, delete_second}, {1, survivor}}, 0};
        const auto deletion_source_bytes = odh::encode_nbrs(deletion_neighbors);
        const auto deleted_overlay = odh::plan_family_delete_overlay(
            {resident, cas, source_delete}, deletion_neighbors, source_id, nullptr,
            source_delete.chunk_id);
        const auto v5 = odh::encode_family_overlay(deleted_overlay);
        require(v5[4] == 5 && le32(v5, 40) == 1,
                "source-family deletion uses tagged v5 operation record");
        const auto deleted_decoded = odh::decode_family_overlay(v5);
        require(odh::encode_family_overlay(deleted_decoded) == v5 &&
                deleted_decoded.deletions.size() == 1 &&
                deleted_decoded.deletions[0].members.size() == 2,
                "v5 roundtrip preserves source-family tombstone and member identities");
        auto empty_deletion = deleted_decoded;
        empty_deletion.deletions[0].members.clear();
        rejects([&] { (void)odh::encode_family_overlay(empty_deletion); },
                "source-family deletion requires a nonempty exact member roster");
        const auto remaining_families = odh::apply_family_overlay(
            {resident, cas, source_delete}, source_id, deleted_decoded);
        const auto remaining_neighbors = odh::apply_neighbor_overlay(
            deletion_neighbors, source_id, deleted_decoded);
        require(remaining_families.size() == 2 &&
                std::none_of(remaining_families.begin(), remaining_families.end(),
                    [&](const auto& entry) { return entry.chunk_id == source_delete.chunk_id; }) &&
                remaining_neighbors.entries.size() == 1 &&
                remaining_neighbors.entries[0].record->guid == survivor.guid &&
                remaining_neighbors.entries[0].record->relationships.size() == 1 &&
                remaining_neighbors.entries[0].record->relationships[0].key == 1 &&
                odh::encode_nbrs(deletion_neighbors) == deletion_source_bytes,
                "delete hides source FAMI/NBRS members and their links without changing source data");
        rejects([&] { (void)odh::plan_family_delete_overlay(
                    {resident, cas, source_delete}, deletion_neighbors, source_id,
                    &deleted_decoded, source_delete.chunk_id); },
                "already deleted source family cannot be deleted again");
        rejects([&] { (void)odh::plan_family_delete_overlay(
                    {resident, cas}, deletion_neighbors, source_id, nullptr, resident.chunk_id); },
                "housed source family cannot be deleted from the unhoused-family picker");
        const auto moved_source = odh::plan_family_move_in_overlay(
            {resident, cas, source_delete}, source_id, nullptr, source_delete.chunk_id, 9, 1000);
        rejects([&] { (void)odh::plan_family_delete_overlay(
                    {resident, cas, source_delete}, deletion_neighbors, source_id,
                    &moved_source, source_delete.chunk_id); },
                "source family with move history cannot be deleted");
        auto missing_delete_member = deletion_neighbors;
        missing_delete_member.entries.erase(missing_delete_member.entries.begin());
        rejects([&] { (void)odh::plan_family_delete_overlay(
                    {resident, cas, source_delete}, missing_delete_member, source_id,
                    nullptr, source_delete.chunk_id); },
                "source-family deletion requires every member to resolve in NBRS");

        std::vector<std::int16_t> cas_person_data(88,0);
        cas_person_data[2]=500;cas_person_data[3]=500;cas_person_data[5]=500;
        const auto built_creation=odh::make_simitone_family_creation(
            {resident,cas},neighbors,source_id,&created,"Next family",
            {{0x10000003U,cas_person_data}});
        require(built_creation.family_chunk_id==1&&built_creation.members.size()==1&&
                built_creation.members[0].neighbor_id==2&&built_creation.members[0].guid==0x10000003U,
                "CAS family builder allocates free FAMI and neighbor IDs across source and overlay");
        require(built_creation.members[0].person_data->size()==256&&
                (*built_creation.members[0].person_data)[2]==500&&
                (*built_creation.members[0].person_data)[61]==1&&
                std::all_of(built_creation.members[0].person_data->begin()+88,
                    built_creation.members[0].person_data->end(),[](std::int16_t v){return v==0;}),
                "CAS family builder preserves 88 inputs and applies the observed family word/zero extension");
        const auto appended_creation=odh::plan_family_creation_overlay(
            {resident,cas},neighbors,source_id,&created,built_creation);
        const auto appended_families=odh::apply_family_overlay({resident,cas},source_id,appended_creation);
        require(appended_families.size()==4&&appended_families.back().family.family_number==40&&
                appended_families.back().display_name=="Next family",
                "CAS family builder adds the next family to the current player overlay");
        auto invalid_person_data=cas_person_data;invalid_person_data.pop_back();
        rejects([&]{(void)odh::make_simitone_family_creation({resident,cas},neighbors,source_id,
            &created,"bad",{{0x10000004U,invalid_person_data}});},"CAS family builder rejects incomplete person data");
        auto tailed = neighbors; tailed.remaining_bytes = 19;
        require(odh::apply_neighbor_overlay(tailed, source_id, created).remaining_bytes == 19,
                "neighbor replay preserves unknown source tail extent without claiming its bytes");
        const auto created_move = odh::plan_family_move_in_overlay(
            {resident, cas}, source_id, &created, 0, 7, 5000);
        const auto created_moved = odh::apply_family_overlay({resident, cas}, source_id,
            odh::decode_family_overlay(odh::encode_family_overlay(created_move)));
        require(created_moved.back().family.house_number == 7 && created_moved.back().family.budget == 15000,
                "v3 applies created household before subsequent move-in assignment");
        rejects([&] { (void)odh::apply_neighbor_overlay(neighbors, stale_source, created); },
                "neighbor replay checks exact neighborhood identity");
        auto bad_creation = creation;
        const auto reject_creation = [&](const odh::FamilyOverlayCreation& value) {
            rejects([&] { (void)odh::plan_family_creation_overlay(
                {resident, cas}, neighbors, source_id, nullptr, value); }, "invalid created metadata rejects");
        };
        bad_creation.family_chunk_id = resident.chunk_id; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].neighbor_id = 1; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].neighbor_id = 0; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].neighbor_id = -1; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[1].neighbor_id = first.neighbor_id; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[1].guid = first.guid; reject_creation(bad_creation);
        bad_creation = creation; std::swap(bad_creation.members[0], bad_creation.members[1]); reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members.pop_back(); reject_creation(bad_creation);
        bad_creation = creation; bad_creation.fami_payload[4] = 8; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.fami_payload.back() = 1; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.fami_payload.pop_back(); reject_creation(bad_creation);
        bad_creation = creation; bad_creation.family_name.clear(); reject_creation(bad_creation);
        bad_creation = creation; bad_creation.family_name.assign(257, 'x'); reject_creation(bad_creation);
        bad_creation = creation; bad_creation.family_name = std::string("a\0b", 3); reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].name.assign(257, 'x'); reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].person_data->push_back(0); reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].relationships = {{1, 19, {5}}}; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].version = 11; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].unknown3.reset(); reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].name = "Even"; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].unknown3 = 17; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].mystery_zero = 1; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].unknown_neg_one = 0; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].person_mode = 1; reject_creation(bad_creation);
        bad_creation = creation; (*bad_creation.members[0].person_data)[61] = 39; reject_creation(bad_creation);
        bad_creation = creation; (*bad_creation.members[0].person_data)[88] = 1; reject_creation(bad_creation);
        bad_creation = creation; (*bad_creation.members[0].person_data)[255] = 1; reject_creation(bad_creation);
        bad_creation = creation; bad_creation.members[0].person_data.reset(); reject_creation(bad_creation);
        auto wrong_number = created_family;
        wrong_number.family_number = resident.family.family_number;
        bad_creation = creation;
        bad_creation.fami_payload = odh::encode_simitone_create_family_fami(wrong_number);
        reject_creation(bad_creation);
        wrong_number.family_number = 40;
        bad_creation.fami_payload = odh::encode_simitone_create_family_fami(wrong_number);
        reject_creation(bad_creation);
        wrong_number.family_number = -1;
        bad_creation.fami_payload = odh::encode_simitone_create_family_fami(wrong_number);
        reject_creation(bad_creation);
        auto maximum_number = resident; maximum_number.family.family_number = 2147483647;
        rejects([&] { (void)odh::apply_family_overlay({maximum_number}, source_id, created); },
                "FamilyNumber increment overflow fails closed");
        rejects([&] { (void)odh::apply_family_overlay({}, source_id, created); },
                "empty source cannot define reference families.Max");
        auto townie = resident; townie.family.family_number = -1;
        auto from_townie = creation; auto zero_number = created_family; zero_number.family_number = 0;
        from_townie.fami_payload = odh::encode_simitone_create_family_fami(zero_number);
        require(odh::plan_family_creation_overlay({townie}, neighbors, source_id, nullptr,
                from_townie).creations.size() == 1, "townie sentinel -1 produces first FamilyNumber zero");
        bad_creation = creation; bad_creation.family_chunk_id = 65535;
        bad_creation.members[0].neighbor_id = 32767;
        for (auto& member : bad_creation.members) (*member.person_data)[61] = -1;
        require(odh::plan_family_creation_overlay({resident, cas}, neighbors, source_id, nullptr,
            bad_creation).creations[0].family_chunk_id == 65535,
            "maximum FAMI and positive signed neighbor IDs remain representable");
        auto next_creation = bad_creation; auto next_family = created_family;
        next_family.family_number = 40;
        next_family.member_guids = {0x20000001U, 0x20000002U};
        next_creation.fami_payload = odh::encode_simitone_create_family_fami(next_family);
        for (std::size_t i = 0; i < next_creation.members.size(); ++i) {
            next_creation.members[i].guid = next_family.member_guids[i];
            next_creation.members[i].neighbor_id = static_cast<std::int16_t>(200 + i);
        }
        require(odh::plan_family_creation_overlay({resident, cas}, neighbors, source_id, &created,
                next_creation).creations.size() == 2, "subsequent creation increments prior FamilyNumber");
        next_family.family_number = 39;
        next_creation.fami_payload = odh::encode_simitone_create_family_fami(next_family);
        rejects([&] { (void)odh::plan_family_creation_overlay({resident, cas}, neighbors,
                source_id, &created, next_creation); }, "duplicate created FamilyNumber rejects");
        auto full_neighbors = neighbors; full_neighbors.entries.resize(4096, {0, std::nullopt});
        rejects([&] { (void)odh::apply_neighbor_overlay(full_neighbors, source_id, created); },
                "appended members cannot exceed cumulative NBRS entry limit");
        std::vector<odh::FamilyEntry> full_families;
        for (std::uint16_t i = 1; i <= 4096; ++i) full_families.push_back(family(i, 0, 0, {}));
        rejects([&] { (void)odh::apply_family_overlay(full_families, source_id, created); },
                "appended household cannot exceed cumulative family runtime limit");
        rejects([&] { (void)odh::plan_family_creation_overlay(
            {resident, cas}, neighbors, source_id, &created, creation); }, "duplicate creation rejects");
        auto guid_collision = creation; auto collision_family = created_family;
        collision_family.member_guids[0] = resident.family.member_guids[0];
        guid_collision.members[0].guid = collision_family.member_guids[0];
        guid_collision.fami_payload = odh::encode_simitone_create_family_fami(collision_family);
        reject_creation(guid_collision);
        collision_family.member_guids[0] = original_neighbor.guid;
        guid_collision.members[0].guid = original_neighbor.guid;
        guid_collision.fami_payload = odh::encode_simitone_create_family_fami(collision_family);
        reject_creation(guid_collision);
        auto eight = creation; auto eight_family = created_family;
        eight.members.clear(); eight_family.member_guids.clear();
        for (int i = 0; i < 8; ++i) {
            auto member = first; member.guid = static_cast<std::uint32_t>(1000 + i);
            member.neighbor_id = static_cast<std::int16_t>(200 + i);
            eight.members.push_back(member); eight_family.member_guids.push_back(member.guid);
        }
        eight.fami_payload = odh::encode_simitone_create_family_fami(eight_family);
        require(odh::plan_family_creation_overlay({resident, cas}, odh::Nbrs{0, 73, {}, 0}, source_id, nullptr,
                eight).creations.size() == 1, "eight-member household is accepted");
        eight.members.push_back(first); eight_family.member_guids.push_back(first.guid);
        eight.fami_payload = odh::encode_simitone_create_family_fami(eight_family); reject_creation(eight);
        bad_creation = creation; bad_creation.members.clear(); reject_creation(bad_creation);
        for (std::size_t n = 0; n < v3.size(); ++n) {
            auto truncated = v3; truncated.resize(n);
            rejects([&] { (void)odh::decode_family_overlay(truncated); }, "every truncated v3 prefix rejects");
        }
        auto corrupt_v3 = v3; corrupt_v3[44] = 99;
        rejects([&] { (void)odh::decode_family_overlay(corrupt_v3); }, "unknown v3 tag rejects");
        corrupt_v3 = v3; corrupt_v3[46] = 0xff; corrupt_v3[47] = 0xff;
        corrupt_v3[48] = 0xff; corrupt_v3[49] = 0xff;
        rejects([&] { (void)odh::decode_family_overlay(corrupt_v3); }, "overflowing v3 record extent rejects");
        corrupt_v3 = v3; corrupt_v3.push_back(0);
        rejects([&] { (void)odh::decode_family_overlay(corrupt_v3); }, "trailing v3 data rejects");
        corrupt_v3 = v3; corrupt_v3.resize(4 * 1024 * 1024 + 1);
        rejects([&] { (void)odh::decode_family_overlay(corrupt_v3); }, "v3 cumulative file bound rejects");
        auto large_creation = creation;
        large_creation.members.clear(); auto large_family = created_family;
        large_family.member_guids.clear();
        for (int i = 0; i < 8; ++i) {
            auto member = first; member.guid = static_cast<std::uint32_t>(1000 + i);
            member.neighbor_id = static_cast<std::int16_t>(200 + i);
            large_creation.members.push_back(member); large_family.member_guids.push_back(member.guid);
        }
        large_creation.fami_payload = odh::encode_simitone_create_family_fami(large_family);
        const auto large_bytes = odh::encode_family_overlay(
            odh::FamilyOverlay{source_id, {}, {large_creation}});
        require(large_bytes.size() > 4 * 1024 && large_bytes.size() < 64 * 1024 &&
                odh::encode_family_overlay(odh::decode_family_overlay(large_bytes)) == large_bytes,
                "eight-member exact NBRS profile roundtrips below record byte cap");
        large_creation.members[0].relationships.push_back({1, 1024, {}});
        rejects([&] { (void)odh::encode_family_overlay(
            odh::FamilyOverlay{source_id, {}, {large_creation}}); },
                "newborn profile rejects relationships in encoder");
        corrupt_v3 = v3; corrupt_v3.resize(50 + 65537, 0);
        corrupt_v3[46] = 1; corrupt_v3[47] = 0; corrupt_v3[48] = 1; corrupt_v3[49] = 0;
        rejects([&] { (void)odh::decode_family_overlay(corrupt_v3); },
                "complete creation record one byte beyond cap rejects before parsing");
        auto too_many = created; too_many.creations.resize(4097, creation);
        rejects([&] { (void)odh::encode_family_overlay(too_many); }, "operation budget rejects before copying payloads");

        // Native profile: checkpoint the live budget between purchases; never
        // synthesize architecture refund from the lot's purchase price.
        std::array<std::uint8_t, 32> ticket{}; ticket[0] = 0x83;
        auto managed = odh::plan_family_managed_move_in_overlay(
            {resident, cas}, source_id, &created, 0, 7, 5500);
        auto managed_replay = odh::replay_family_overlay({resident, cas}, source_id, managed);
        require(managed_replay.states[2].occupancy_epoch == 1 &&
                managed_replay.states[2].managed && managed_replay.states[2].value_in_arch == 0,
                "created occupancy starts epoch one and retains zero architecture value");
        managed = odh::plan_family_checkpoint_overlay(
            {resident, cas}, source_id, &managed, 0, 7, 1, 12345, ticket);
        const auto checkpointed = managed;
        managed = odh::plan_family_managed_move_out_overlay(
            {resident, cas}, source_id, &managed, 0);
        managed_replay = odh::replay_family_overlay({resident, cas}, source_id, managed);
        require(managed_replay.states[2].house_number == 0 &&
                managed_replay.states[2].budget == 12345 &&
                managed_replay.states[2].checkpoint_sha256 == ticket &&
                managed_replay.states[2].checkpoint_house == 7 &&
                managed_replay.states[2].checkpoint_epoch == 1,
                "move-out retains checkpoint carry ticket and live budget without purchase refund");
        managed = odh::plan_family_managed_move_in_overlay(
            {resident, cas}, source_id, &managed, 0, 8, 2000);
        const auto v6 = odh::encode_family_overlay(managed);
        require(v6[4] == 6 && odh::encode_family_overlay(odh::decode_family_overlay(v6)) == v6,
                "v6 extended chronological operations roundtrip canonically");
        managed_replay = odh::replay_family_overlay({resident, cas}, source_id, odh::decode_family_overlay(v6));
        require(managed_replay.states[2].house_number == 8 &&
                managed_replay.states[2].occupancy_epoch == 2 && managed_replay.states[2].budget == 10345 &&
                managed_replay.states[2].checkpoint_epoch == 1 &&
                managed_replay.families[2].family.member_guids == created_family.member_guids,
                "second occupancy charges checkpoint budget and retains roster and earlier carry origin");
        rejects([&] { (void)odh::plan_family_checkpoint_overlay(
            {resident, cas}, source_id, &managed, 0, 8, 1, 10345, ticket); }, "old epoch checkpoint rejects");
        rejects([&] { (void)odh::plan_family_checkpoint_overlay(
            {resident, cas}, source_id, &managed, 0, 7, 2, 10345, ticket); }, "old house checkpoint rejects");
        rejects([&] { (void)odh::plan_family_checkpoint_overlay(
            {resident, cas}, source_id, &managed, 0, 8, 2, -1, ticket); }, "negative checkpoint budget rejects");
        rejects([&] { (void)odh::plan_family_checkpoint_overlay(
            {resident, cas}, source_id, &managed, 0, 8, 2, 10345, {}); }, "zero checkpoint digest rejects");
        auto corrupt_managed = checkpointed;
        ++corrupt_managed.assignments.back().state->budget_before;
        rejects([&] { (void)odh::apply_family_overlay({resident, cas}, source_id, corrupt_managed); },
                "stale checkpoint before-budget rejects");
        corrupt_managed = checkpointed;
        corrupt_managed.assignments.back().state->value_in_arch_after = 5500;
        rejects([&] { (void)odh::apply_family_overlay({resident, cas}, source_id, corrupt_managed); },
                "checkpoint cannot fabricate architecture refund");
        const auto retained_source = odh::plan_family_managed_move_out_overlay(
            {resident, cas}, source_id, nullptr, resident.chunk_id);
        require(odh::replay_family_overlay({resident, cas}, source_id, retained_source).states[0].budget == 18000,
                "managed source move-out retains verified source architecture refund");
        auto adopted = odh::plan_family_checkpoint_overlay(
            {resident, cas}, source_id, &overlay, cas.chunk_id, 7, 1, 14000, ticket);
        const auto adopted_bytes = odh::encode_family_overlay(adopted);
        require(adopted_bytes[44] == 1 && le32(adopted_bytes, 46) == 48 &&
                adopted_bytes[98] == 5 && le32(adopted_bytes, 100) == 104,
                "v6 retains legacy tag1 and appends a 104-byte tag5 checkpoint");
        require(odh::replay_family_overlay({resident, cas}, source_id,
            odh::plan_family_managed_move_out_overlay({resident, cas}, source_id, &adopted, cas.chunk_id))
                .states[1].budget == 14000, "legacy occupancy can explicitly adopt managed checkpoint profile");
        auto corrupt_v6 = adopted_bytes; corrupt_v6[154] = 2;
        rejects([&] { (void)odh::decode_family_overlay(corrupt_v6); }, "unknown extended flags reject");
        corrupt_v6 = adopted_bytes; corrupt_v6[154] = 0;
        rejects([&] { (void)odh::decode_family_overlay(corrupt_v6); }, "absent digest requires zero bytes");
        corrupt_v6 = adopted_bytes; corrupt_v6[100] = 103;
        rejects([&] { (void)odh::decode_family_overlay(corrupt_v6); }, "extended record exact extent enforced");
        corrupt_v6 = adopted_bytes; corrupt_v6[4] = 5;
        rejects([&] { (void)odh::decode_family_overlay(corrupt_v6); }, "extended operation cannot enter legacy codec");
        for (std::size_t n = 0; n < adopted_bytes.size(); ++n) {
            auto prefix = adopted_bytes; prefix.resize(n);
            rejects([&] { (void)odh::decode_family_overlay(prefix); }, "every truncated v6 prefix rejects");
        }

        std::cout << "family_overlay: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
