#include "odh/entry_point5_readonly.hpp"
#include "odh/world_snapshot_codec.hpp"

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

    World ep5_world;
    require(ep5_world.create(10, EntityKind::avatar, {2}) &&
            ep5_world.create(20, EntityKind::object, {2}) &&
            ep5_world.define_type(20, tested_guid), "generic EP5 caller and stack object");
    const VmFrame ep5_frame{routine_key(4096), 0, {10, 10, 20, 0}, {}, {}};
    require(ep5_world.write(ep5_frame, 0, 0, 44) && ep5_world.write(ep5_frame, 1, 0, 77),
            "generic EP5 caller and stack-object attributes");
    const auto ep5_snapshot = ep5_world.snapshot();
    const auto ep5_before = encode_world_snapshot(ep5_snapshot);
    Instruction caller_attribute_test{};
    caller_attribute_test.opcode = 2; caller_attribute_test.on_true = 1; caller_attribute_test.on_false = 0xff;
    caller_attribute_test.operands = {0, 0, 44, 0, 0, 2, 0, 7};
    Instruction stack_attribute_test{};
    stack_attribute_test.opcode = 2; stack_attribute_test.on_true = 2; stack_attribute_test.on_false = 0xff;
    stack_attribute_test.operands = {0, 0, 77, 0, 0, 2, 1, 7};
    Instruction stack_id_argument_test{};
    stack_id_argument_test.opcode = 2; stack_id_argument_test.on_true = 0xfe; stack_id_argument_test.on_false = 0xff;
    stack_id_argument_test.operands = {0, 0, 20, 0, 0, 2, 9, 7};
    const auto stack_attribute = Bhav{0x8002,
        {caller_attribute_test, stack_attribute_test, stack_id_argument_test}, {}, Bhav::Metadata{0, 4, 0, 4}};
    RoutineSet ep5_owner{{routine_key(4108), stack_attribute}};
    auto generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 20, {}, 8);
    if(generic.state!=ReadOnlyBhavState::evaluated_true)
        std::cerr<<"generic EP5 state="<<static_cast<int>(generic.state)<<" reason="
                 <<static_cast<int>(generic.reason)<<" steps="<<generic.steps<<" diagnostic="
                 <<generic.diagnostic<<'\n';
    require(generic.state == ReadOnlyBhavState::evaluated_true && generic.steps == 6,
            "generic EP5 receives Caller, StackObject and the stack-object ID argument");
    require(encode_world_snapshot(ep5_snapshot) == ep5_before &&
            encode_world_snapshot(ep5_world.snapshot()) == ep5_before,
            "generic EP5 sandbox leaves saved and live World unchanged");

    Instruction ghost_test{};
    ghost_test.opcode = 2; ghost_test.on_true = 0xfe; ghost_test.on_false = 0xff;
    ghost_test.operands = {1, 0, 1, 0, 0, 2, 9, 7};
    auto ghost_bhav = Bhav{0x8002, {ghost_test}, {}, Bhav::Metadata{0, 4, 0, 4}};
    ep5_owner[routine_key(4108)] = ghost_bhav;
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 20, 1, 8);
    require(generic.state == ReadOnlyBhavState::evaluated_true,
            "generic EP5 receives the known ghost argument");
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 20, {}, 8);
    require(generic.state == ReadOnlyBhavState::unsupported &&
            generic.diagnostic.find("unknown ghost argument") != std::string::npos,
            "unknown ghost state cannot be guessed when the BHAV depends on it");
    ghost_test.operands[0] = 0;
    ghost_test.operands[2] = 1;
    ep5_owner[routine_key(4108)] = Bhav{0x8002, {ghost_test}, {}, Bhav::Metadata{0, 4, 0, 4}};
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 20, {}, 8);
    require(generic.state == ReadOnlyBhavState::evaluated_false,
            "unknown ghost state is accepted only when both possibilities agree");
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 99, {}, 8);
    require(generic.state == ReadOnlyBhavState::unsupported &&
            generic.reason == ReadOnlyBhavReason::invalid_snapshot,
            "missing EP5 stack object remains unsupported");
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4000, 10, 20, {}, 8);
    require(generic.state == ReadOnlyBhavState::unsupported &&
            generic.reason == ReadOnlyBhavReason::invalid_resources,
            "generic EP5 rejects non-private root owners");
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 20, 2, 8);
    require(generic.state == ReadOnlyBhavState::unsupported &&
            generic.reason == ReadOnlyBhavReason::invalid_resources,
            "generic EP5 rejects invalid ghost flags");
    require(encode_world_snapshot(ep5_snapshot) == ep5_before &&
            encode_world_snapshot(ep5_world.snapshot()) == ep5_before,
            "all generic EP5 variants leave the live World unchanged");

    std::cout << "Read-only EntryPoint 5 BHAV checks passed\n";
}
