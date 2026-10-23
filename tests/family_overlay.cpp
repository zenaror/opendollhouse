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

        std::cout << "family_overlay: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
