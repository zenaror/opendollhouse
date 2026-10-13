#include "odh/random.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
odh::Instruction instruction(std::uint16_t destination_scope = 8, std::int16_t destination = 0,
                             std::uint16_t range_scope = 7, std::int16_t range = -1) {
    odh::Instruction i{8, 254, 255, {}};
    const std::array<std::uint16_t, 4> words{static_cast<std::uint16_t>(destination), destination_scope,
                                           static_cast<std::uint16_t>(range), range_scope};
    for (std::size_t j = 0; j < words.size(); ++j) {
        i.operands[j * 2] = static_cast<std::uint8_t>(words[j] & 255);
        i.operands[j * 2 + 1] = static_cast<std::uint8_t>(words[j] >> 8);
    }
    return i;
}
odh::RoutineSet routines(odh::Instruction i) {
    return {{odh::routine_key(4096), {0x8002, {i}, {}, odh::Bhav::Metadata{0, 4, 1, 1}}}};
}
void generator() {
    // Independently evaluated fixed Python arbitrary-integer vectors, masked
    // after the left shift and multiplication. These catch absent uint64 wrap.
    struct Vector {std::uint64_t input, state, product;};
    const std::array<Vector, 5> vectors{{
        {0, 0, 0}, {1, UINT64_C(0x2000001), UINT64_C(0x47e4ce4b896cdd1d)},
        {UINT64_MAX, UINT64_C(0xfff0001ffe000000), UINT64_C(0xf92cc9e5c6000000)},
        {UINT64_C(0x8000000000000000), UINT64_C(0x8008001001000000), UINT64_C(0xf1053ead1d000000)},
        {UINT64_C(0x123456789abcdef0), UINT64_C(0x69cf1fdabed68fcd), UINT64_C(0xb7fb0288c5ee4339)}
    }};
    for (const auto& v : vectors) {
        for (const auto range : {UINT64_C(0), UINT64_C(1), UINT64_C(2), UINT64_C(65535), UINT64_MAX}) {
            odh::RandomState rng(v.input);
            const auto result = rng.next(range);
            require(result == (range ? v.product % range : 0), "fixed masked product and modulo oracle");
            require(rng.seed() == (range ? v.state : v.input), "zero range never advances state");
        }
    }
    odh::RandomState rng(1);
    const std::array<std::uint64_t, 5> states{UINT64_C(0x2000001), UINT64_C(0x4004000802801),
        UINT64_C(0xd004003202803), UINT64_C(0x200340a005480e81), UINT64_C(0x81fd0210eb25e00)};
    const std::array<std::uint64_t, 5> results{31930,38927,53803,8973,39788};
    for (std::size_t j = 0; j < results.size(); ++j) {
        require(rng.next(65535) == results[j] && rng.seed() == states[j], "multi-step known sequence");
    }
    auto copy = rng;
    require(copy.next(17) == rng.next(17) && copy.seed() == rng.seed(), "copy resumes identical stream");
    copy.next(17); require(copy.seed() != rng.seed(), "copy owns independent state");
    rng.reset(1); require(rng.next(65535) == 31930, "explicit reset restores sequence");
}
void rejection() {
    odh::VmFrame frame{odh::routine_key(4096), 0, {}, {12}, {13}};
    std::array<std::int16_t, 20> temps{};
    odh::RandomState rng(1);
    int records = 0, writes = 0;
    const odh::VmMemory memory{
        [](const auto&, auto, auto) -> std::optional<std::int16_t> {return std::nullopt;},
        [&writes](const auto&, auto, auto, auto) {++writes; return false;}
    };
    auto handler = odh::random_primitive(rng, memory, [&](const auto&) {++records;});
    for (const auto& i : {instruction(256), instruction(8,0,256), instruction(7), instruction(10),
        instruction(8,-1), instruction(8,20), instruction(9,1), instruction(25,-1),
        instruction(8,0,8,-1), instruction(8,0,8,20), instruction(8,0,9,1),
        instruction(8,0,25,-1), instruction(8,0,14,5), instruction(14,5)}) {
        require(handler(frame, i, temps, {nullptr,0}).disposition == odh::PrimitiveDisposition::unsupported,
                "unsupported read or destination rejected");
        require(rng.seed() == 1 && records == 0, "rejection preserves RNG and observer");
    }
    require(writes == 1, "external rejected write invoked once");
    for (const auto& i : {instruction(8,20), instruction(8,0,9,20), instruction(14,5)}) {
        odh::Vm vm(routines(i), {}, {}, handler);
        vm.start(odh::routine_key(4096), {}, {17}); vm.temps()[0] = 91;
        require(vm.run(1) == odh::VmStatus::fault && vm.temps()[0] == 91 &&
                vm.stack().size() == 1 && vm.stack()[0].pc == 0 &&
                vm.stack()[0].arguments[0] == 17 && rng.seed() == 1 && records == 0,
                "actual VM rejects without temp, frame, branch or RNG mutation");
    }
    auto unbound = odh::random_primitive(rng);
    require(unbound(frame, instruction(14,5), temps, {nullptr,0}).disposition == odh::PrimitiveDisposition::unsupported,
            "missing writer unsupported");
    auto unknown = instruction(); unknown.opcode = 1;
    require(handler(frame, unknown, temps, {nullptr,0}).disposition == odh::PrimitiveDisposition::unsupported && rng.seed() == 1,
            "other opcode does not advance RNG");
}
void vm_outputs() {
    for (const std::uint16_t scope : {std::uint16_t{8}, std::uint16_t{9}, std::uint16_t{25}}) {
        for (const std::int16_t range : {std::int16_t{0}, std::int16_t{1}, std::int16_t{-1}}) {
            odh::RandomState rng(1);
            std::vector<odh::RandomRecord> records;
            odh::Vm vm(routines(instruction(scope,0,7,range)), {}, {},
                odh::random_primitive(rng, {}, [&](const auto& record) {records.push_back(record);}));
            vm.start(odh::routine_key(4096), {11,12,13,0}, {17});
            require(vm.run(1) == odh::VmStatus::returned_true, "Random Number returns true through VM");
            const auto expected = range == -1 ? 31930 : 0;
            require(records.size() == 1 && records[0].result == expected && records[0].context.caller == 11 &&
                    records[0].destination_scope == scope && records[0].seed_before == 1 &&
                    records[0].seed_after == rng.seed(), "typed observer retains committed output and context");
            if (scope == 8) require(vm.temps()[0] == expected, "temp output published by VM");
            // Frame outputs are verified before return discards the frame below.
        }
    }
    for (const std::uint16_t scope : {std::uint16_t{9}, std::uint16_t{25}}) {
        auto i = instruction(scope); i.on_true = 0;
        odh::RandomState rng(1);
        odh::Vm vm(routines(i), {}, {}, odh::random_primitive(rng));
        vm.start(odh::routine_key(4096), {}, {17});
        require(vm.run(1) == odh::VmStatus::budget_exhausted, "frame output looping fixture pauses");
        require((scope == 9 ? vm.stack()[0].arguments[0] : vm.stack()[0].locals[0]) == 31930,
                "validated argument and local output published by VM");
    }
    odh::RandomState rng(1);
    odh::Vm alias(routines(instruction(8,0,8,0)), {}, {}, odh::random_primitive(rng));
    alias.start(odh::routine_key(4096)); alias.temps()[0] = 100;
    require(alias.run(1) == odh::VmStatus::returned_true && alias.temps()[0] == 65,
            "alias range read occurs before temp write");
    require(rng.seed() == UINT64_C(0x2000001), "alias consumes exactly one cycle");
    alias.start(odh::routine_key(4096)); alias.temps()[0] = 1;
    alias.run(1); require(rng.seed() == UINT64_C(0x4004000802801), "VM restart does not reseed supplied state");
}
void scopes_and_owners() {
    odh::RandomState rng(1);
    std::int16_t external = -1;
    const odh::VmMemory memory{
        [&](const auto& frame, auto scope, auto index) -> std::optional<std::int16_t> {
            return scope == 14 && index == 5 && frame.context.code_owner == 7 ?
                std::optional<std::int16_t>{external} : std::nullopt;
        },
        [&](const auto& frame, auto scope, auto index, auto value) {
            if (scope != 14 || index != 5 || frame.context.code_owner != 7) return false;
            external = value; return true;
        }
    };
    auto handler = odh::random_primitive(rng,memory);
    odh::VmFrame frame{odh::routine_key(4096),0,{1,2,-1,7},{-1},{-1}};
    std::array<std::int16_t,20> temps{};
    for (const std::uint16_t scope : {std::uint16_t{7},std::uint16_t{8},std::uint16_t{9},std::uint16_t{10},std::uint16_t{25},std::uint16_t{14}}) {
        rng.reset(1); external = -1; temps[0] = -1;
        const auto out = handler(frame,instruction(8,0,scope,scope == 7 ? -1 : (scope == 14 ? 5 : 0)),temps,{nullptr,0});
        require(out.temp_write && out.temp_write->value == 31930, "all supported range reads preserve unsigned 65535 maximum");
    }
    rng.reset(1);
    // Native Init operand layout writes explicit external scope14/index5.
    const auto out = handler(frame,instruction(14,5,8,0),temps,{nullptr,0});
    require(out.disposition == odh::PrimitiveDisposition::goto_true && external == 31930 && !out.temp_write && !out.write,
            "native external output succeeds only through supplied memory");
    rng.reset(1); external = -1;
    odh::Vm native(routines(instruction(14,5,8,0)), {}, {}, handler,
                  {{7,routines(instruction(14,5,8,0))}});
    native.start(odh::routine_key(4096), {1,2,3,7}); native.temps()[0] = -1;
    require(native.run(1) == odh::VmStatus::returned_true && external == 31930 &&
            native.temps()[0] == -1, "native external write executes through actual VM");
    rng.reset(1);
    const auto first = instruction();
    odh::RoutineOwners owners{{7,routines(first)}, {8,routines(first)}};
    odh::Vm a(routines(first),{}, {},handler,owners), b(routines(first),{}, {},handler,owners);
    a.start(odh::routine_key(4096),{0,0,0,7}); b.start(odh::routine_key(4096),{0,0,0,8});
    require(a.run(1) == odh::VmStatus::returned_true && b.run(1) == odh::VmStatus::returned_true,
            "two VM code owners share explicit RNG serially");
    require(a.temps()[0] == 31930 && b.temps()[0] == -26609 && rng.seed() == UINT64_C(0x4004000802801),
            "shared stream and signed result wrapping preserved");
    rng.reset(0);
    auto zero = handler(frame,instruction(),temps,{nullptr,0});
    require(zero.temp_write && zero.temp_write->value == 0 && rng.seed() == 0, "zero seed accepted by primitive");
}
}
int main() {
    try {
        generator(); rejection(); vm_outputs(); scopes_and_owners();
        std::cout << checks << " checks passed\n";
    } catch (const std::exception& error) {std::cerr << error.what() << '\n'; return 1;}
}
