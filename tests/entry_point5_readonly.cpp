#include "odh/entry_point5_readonly.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

odh::Bhav flamingo_like_bhav() {
    odh::Instruction type_test{};
    type_test.opcode = 32;
    type_test.on_true = 1;
    type_test.on_false = 0xff;
    type_test.operands = {0x56, 0x6b, 0x71, 0x43, 0, 0, 10, 0};

    odh::Instruction attribute_test{};
    attribute_test.opcode = 2;
    attribute_test.on_true = 0xfe;
    attribute_test.on_false = 0xff;
    attribute_test.operands = {16, 0, 1, 0, 0, 2, 1, 7};

    odh::Bhav bhav;
    bhav.version = 0x8002;
    bhav.metadata = odh::Bhav::Metadata{0, 0, 0, 2};
    bhav.instructions = {type_test, attribute_test};
    return bhav;
}

odh::Bhav urnstone_bhav() {
    odh::Instruction person_type{};
    person_type.opcode = 2; person_type.on_true = 1; person_type.on_false = 0xff;
    person_type.operands = {9, 0, 2, 0, 0, 2, 21, 7};
    odh::Instruction same_neighbor{};
    same_neighbor.opcode = 2; same_neighbor.on_true = 0xfe; same_neighbor.on_false = 0xff;
    same_neighbor.operands = {31, 0, 1, 0, 0, 2, 19, 0};
    odh::Bhav bhav;
    bhav.version = 0x8002;
    bhav.metadata = odh::Bhav::Metadata{0, 1, 0, 3};
    bhav.instructions = {person_type, same_neighbor};
    return bhav;
}

odh::Bhav dining_table_bhav() {
    odh::Instruction verify_person{};
    verify_person.opcode = 355; verify_person.on_true = 1; verify_person.on_false = 0xff;
    verify_person.operands.fill(0xff);
    odh::Instruction flag_two{};
    flag_two.opcode = 2; flag_two.on_true = 0xfe; flag_two.on_false = 2;
    flag_two.operands = {65, 0, 2, 0, 0, 8, 19, 7};
    odh::Instruction flag_three{};
    flag_three.opcode = 2; flag_three.on_true = 0xfe; flag_three.on_false = 0xff;
    flag_three.operands = {65, 0, 3, 0, 0, 8, 19, 7};
    odh::Bhav bhav;
    bhav.version = 0x8002;
    bhav.metadata = odh::Bhav::Metadata{0, 0, 0, 1};
    bhav.instructions = {verify_person, flag_two, flag_three};
    return bhav;
}
}

int main() {
    using namespace odh;
    constexpr std::uint32_t tested_guid = 0x43716b56;
    constexpr std::uint32_t other_guid = 0x36500000;
    auto bhav = flamingo_like_bhav();

    auto result = evaluate_read_only_entry_point5(4108, bhav, tested_guid, {}, 1);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_true && result.steps == 2,
            "matching object type and attribute 16 should return true");

    result = evaluate_read_only_entry_point5(4108, bhav, other_guid, {}, {});
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 1,
            "nonmatching type should short-circuit false without needing attribute 16");

    result = evaluate_read_only_entry_point5(4108, bhav, tested_guid, {}, 0);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 2,
            "matching type with attribute 16 clear should return false");

    result = evaluate_read_only_entry_point5(4108, bhav, tested_guid, {}, {});
    require(result.state == ReadOnlyEntryPoint5State::unsupported && result.steps == 0,
            "missing required attribute must not be inferred as zero");

    auto mutating = bhav;
    mutating.instructions[1].operands[5] = 5;
    result = evaluate_read_only_entry_point5(4108, mutating, tested_guid, {}, 1);
    require(result.state == ReadOnlyEntryPoint5State::unsupported && result.steps == 0,
            "expression assignment must fail the read-only allowlist");

    auto extended = bhav;
    extended.instructions.push_back(extended.instructions.back());
    result = evaluate_read_only_entry_point5(4108, extended, tested_guid, {}, 1);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "unexpected instructions must fail the fixed two-instruction shape");

    result = evaluate_read_only_entry_point5(4000, bhav, tested_guid, {}, 1);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "non-private routine IDs must fail the entry-point allowlist");

    const auto urn = urnstone_bhav();
    result = evaluate_read_only_entry_point5(4105, urn, 0, {}, {}, 2, 32, 30);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 2,
            "Legacy UrnStone allows only a Sim whose PersonData[31] matches its Attribute[1]");
    result = evaluate_read_only_entry_point5(4105, urn, 0, {}, {}, 2, 30, 30);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_true && result.steps == 2,
            "UrnStone rule returns true when the two saved neighbor values match");
    result = evaluate_read_only_entry_point5(4105, urn, 0, {}, {}, 3, {}, {});
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 1,
            "non-person objects short-circuit UrnStone's second read");
    result = evaluate_read_only_entry_point5(4105, urn, 0, {}, {}, 2, {}, 30);
    require(result.state == ReadOnlyEntryPoint5State::unsupported && result.steps == 1,
            "missing person data remains unknown rather than being guessed");
    result = evaluate_read_only_entry_point5(4105, urn, 0, {}, {}, {}, 32, 30);
    require(result.state == ReadOnlyEntryPoint5State::unsupported && result.steps == 0,
            "missing object definition remains unknown rather than being guessed");
    auto altered_urn = urn;
    altered_urn.instructions[1].operands[2] = 2;
    result = evaluate_read_only_entry_point5(4105, altered_urn, 0, {}, {}, 2, 32, 30);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "altered UrnStone behavior remains outside the exact allowlist");
    altered_urn = urn;
    altered_urn.metadata->tree_version++;
    result = evaluate_read_only_entry_point5(4105, altered_urn, 0, {}, {}, 2, 32, 30);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "unverified UrnStone tree versions remain outside the exact allowlist");

    const auto dining = dining_table_bhav();
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, true, 1);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 3,
            "DiningTables returns false for a live Sim without either tested flag");
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, true, 2);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_true,
            "DiningTables recognizes flag number 2 in PersonData[65]");
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, true, 4);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_true,
            "DiningTables recognizes flag number 3 in PersonData[65]");
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, false, {});
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 1,
            "Global355 short-circuits when the stack object is not a live avatar");
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, true, {});
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "missing PersonData[65] remains unknown");
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, {}, 1);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "missing live-avatar enumeration remains unknown");
    auto changed_dining = dining;
    changed_dining.instructions[1].operands[2] = 4;
    result = evaluate_read_only_entry_point5(4122, changed_dining, 0, {}, {}, {}, {}, {}, true, 2);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "changed DiningTables logic remains outside the exact allowlist");

    std::cout << "Read-only EntryPoint 5 BHAV checks passed\n";
}
