#include "odh/vm.hpp"
#include <iostream>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
odh::RoutineKey key() { return odh::routine_key(4096); }
odh::RoutineSet routines() {
    return {{key(), {0x8002, {{1, 1, 1, {}}, {1, 254, 255, {}}}, {},
                     odh::Bhav::Metadata{0, 4, 1, 0}}}};
}
odh::Vm machine(odh::PrimitiveOutcome outcome) {
    return odh::Vm(routines(), {}, {},
        [outcome](const auto&, const auto&, const auto&, const auto&) { return outcome; });
}
void rejects(odh::PrimitiveOutcome outcome) {
    auto vm = machine(outcome);
    vm.start(key(), {}, {17});
    vm.temps()[0] = 42;
    const auto before = vm.temps();
    require(vm.run(1) == odh::VmStatus::fault, "invalid output faults");
    require(vm.temps() == before, "rejected output preserves every temp");
    require(vm.stack().size() == 1 && vm.stack()[0].pc == 0 &&
            vm.stack()[0].arguments[0] == 17 && vm.stack()[0].locals[0] == 0,
            "rejected output preserves frame and branch");
    require(!vm.pending(), "rejected output creates no wait");
}
}
int main() {
    try {
        for (const auto disposition : {odh::PrimitiveDisposition::goto_true,
                                       odh::PrimitiveDisposition::goto_false}) {
            for (const std::int16_t value : {std::int16_t{0}, std::int16_t{1},
                                           std::int16_t{-32768}}) {
                odh::PrimitiveOutcome outcome{disposition, {}};
                outcome.temp_write = odh::VmTempWrite{0, value};
                auto vm = machine(outcome);
                vm.start(key()); vm.temps()[0] = 42; vm.temps()[19] = 73;
                require(vm.run(1) == odh::VmStatus::budget_exhausted &&
                        vm.stack()[0].pc == 1 && vm.temps()[0] == value && vm.temps()[19] == 73,
                        "branch publishes zero, nonzero and signed temp without touching neighbors");
                require(vm.run(1) == (disposition == odh::PrimitiveDisposition::goto_true ?
                        odh::VmStatus::returned_true : odh::VmStatus::returned_false),
                        "temp output preserves selected return branch");
            }
        }
        odh::PrimitiveOutcome combined{odh::PrimitiveDisposition::goto_true, {}};
        combined.write = odh::VmFrameWrite{odh::VmFrameBank::arguments, 0, 99};
        combined.temp_write = odh::VmTempWrite{19, -7};
        auto valid = machine(combined); valid.start(key(), {}, {17});
        require(valid.run(1) == odh::VmStatus::budget_exhausted &&
                valid.stack()[0].arguments[0] == 99 && valid.temps()[19] == -7,
                "combined frame and final temp publish together");
        combined.temp_write = odh::VmTempWrite{20, 9};
        rejects(combined);
        combined.temp_write = odh::VmTempWrite{0, 9};
        combined.write = odh::VmFrameWrite{odh::VmFrameBank::arguments, 4, 99};
        rejects(combined);
        combined.write = odh::VmFrameWrite{static_cast<odh::VmFrameBank>(99), 0, 99};
        rejects(combined);
        for (const auto disposition : {odh::PrimitiveDisposition::unsupported,
                                       odh::PrimitiveDisposition::wait, odh::PrimitiveDisposition::call}) {
            odh::PrimitiveOutcome rejected{disposition, {}};
            rejected.temp_write = odh::VmTempWrite{0, 9};
            rejected.write = odh::VmFrameWrite{odh::VmFrameBank::arguments, 0, 99};
            if (disposition == odh::PrimitiveDisposition::call)
                rejected.call = odh::VmCall{key(), {}, {}};
            rejects(rejected);
        }
        std::cout << checks << " checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
