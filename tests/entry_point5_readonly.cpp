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

    std::cout << "Read-only EntryPoint 5 BHAV checks passed\n";
}
