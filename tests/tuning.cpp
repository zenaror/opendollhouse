#include "odh/tuning.hpp"
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}
std::int16_t query(unsigned table, unsigned index = 0) {
    const auto bits = table * 128 + index;
    return static_cast<std::int16_t>(bits < 32768 ? static_cast<int>(bits) : static_cast<int>(bits) - 65536);
}
odh::Bcon bcon(std::initializer_list<std::uint16_t> words) { return {0, words, 0}; }
odh::TuningResources resources() {
    odh::TuningResources data;
    data.globals[256] = bcon({0, 0x8000, 0xffff, 0x7fff});
    data.owners[11].private_tables[4096] = bcon({11});
    data.owners[22].private_tables[4096] = bcon({22});
    data.owners[11].private_tables[8192] = bcon({111});
    data.owners[22].private_tables[8192] = bcon({222});
    data.owners[11].semiglobal_state = odh::TuningSemiState::absent;
    data.owners[22].semiglobal_state = odh::TuningSemiState::loaded;
    data.owners[22].semiglobal_tables[8193] = bcon({223});
    data.owners[33].private_tables[8192] = bcon({333});
    return data;
}
odh::Bhav routine(std::initializer_list<odh::Instruction> code) {
    return {0x8002, code, {}, odh::Bhav::Metadata{0, 4, 4, 0}};
}
odh::Instruction read_tuning(unsigned table) {
    const auto bits = static_cast<std::uint16_t>(query(table));
    return {2, 254, 255, {0, 0, static_cast<std::uint8_t>(bits & 255),
        static_cast<std::uint8_t>(bits >> 8), 0, 5, 8, 26}};
}
}

int main() {
    try {
        const unsigned raw_tables[] = {0, 63, 64, 127, 128, 191, 192, 511};
        const unsigned ids[] = {4096, 4159, 8192, 8255, 256, 319, 4288, 4607};
        const odh::RoutineScope scopes[] = {
            odh::RoutineScope::private_object, odh::RoutineScope::private_object,
            odh::RoutineScope::semiglobal, odh::RoutineScope::semiglobal,
            odh::RoutineScope::global, odh::RoutineScope::global,
            odh::RoutineScope::private_object, odh::RoutineScope::private_object};
        auto edges = resources();
        for (unsigned n = 0; n < 8; ++n) {
            auto words = odh::Bcon{0, std::vector<std::uint16_t>(128, static_cast<std::uint16_t>(n + 1)), 0};
            auto& destination = scopes[n] == odh::RoutineScope::global ? edges.globals : edges.owners[11].private_tables;
            destination[static_cast<std::uint16_t>(ids[n])] = words;
        }
        odh::BconTuning boundaries(edges);
        for (unsigned n = 0; n < 8; ++n) for (unsigned index : {0U, 127U}) {
            const auto location = odh::BconTuning::address(query(raw_tables[n], index));
            require(location.table == ids[n] && location.scope == scopes[n] && location.index == index,
                    "unsigned query scope and address boundaries");
            const auto result = boundaries.read(11, query(raw_tables[n], index));
            require(result && result->value == static_cast<int>(n + 1), "edge table and key lookup");
            require(result->semiglobal_fallback == (scopes[n] == odh::RoutineScope::semiglobal), "fallback provenance");
        }
        require(query(511, 127) == -1, "negative query retains all unsigned bits");
        const auto data = resources();
        odh::BconTuning tuning(data);
        require(tuning.read(11, query(0))->value == 11 && tuning.read(22, query(0))->value == 22,
                "private tuning stays isolated by owner");
        require(!tuning.read(99, query(0)), "unknown private owner unsupported");
        require(tuning.read(99, query(128))->value == 0 && tuning.read(99, query(128))->owner == 99,
                "shared global lookup independent of owner with zero retained");
        require(tuning.read(99, query(128, 1))->value == -32768 && tuning.read(99, query(128, 2))->value == -1 &&
                tuning.read(99, query(128, 3))->value == 32767, "unsigned BCON words interpreted as signed values");
        require(!tuning.read(11, query(1)) && !tuning.read(11, query(0, 1)) &&
                !tuning.read(99, query(129)) && !tuning.read(99, query(128, 4)), "missing table and index are nullopt");
        require(tuning.read(11, query(64))->value == 111 && tuning.read(11, query(64))->semiglobal_fallback,
                "explicit absent semiglobal uses same ID in private cache");
        require(!tuning.read(22, query(64)) && !tuning.read(33, query(64)),
                "loaded missing and unknown semiglobal never fall back");
        require(tuning.read(22, query(65))->value == 223 && !tuning.read(22, query(65))->semiglobal_fallback,
                "loaded semiglobal lookup");
        require(!tuning.read(99, query(64)), "unknown semiglobal owner unsupported");
        auto mutable_data = resources();
        odh::BconTuning snapshot(mutable_data);
        mutable_data.owners[11].private_tables[4096].constants[0] = 999;
        mutable_data.globals.clear();
        require(snapshot.read(11, query(0))->value == 11 && snapshot.read(11, query(128))->value == 0,
                "snapshot owns an isolated copy of source data");
        odh::BconTuning moved(resources());
        require(moved.read(11, query(0))->value == 11, "moved source is owned");
        for (const auto state : {odh::TuningSemiState::absent, odh::TuningSemiState::unknown}) {
            auto inconsistent = resources();
            inconsistent.owners[22].semiglobal_state = state;
            rejects([&] { odh::BconTuning bad(inconsistent); }, "semiglobal state contradicting tables rejected");
        }
        auto invalid_state = resources();
        invalid_state.owners[22].semiglobal_state = static_cast<odh::TuningSemiState>(99);
        rejects([&] { odh::BconTuning bad(invalid_state); }, "invalid semiglobal state rejected");
        // Seven tables and ten words across shared and owner banks, including unknown owner.
        odh::BconTuning exact(data, {3, 7, 10});
        require(exact.read(22, query(65))->value == 223, "aggregate limits accept exact bounds");
        rejects([&] { odh::BconTuning bad(data, {2, 7, 10}); }, "owner bound enforced");
        rejects([&] { odh::BconTuning bad(data, {3, 6, 10}); }, "aggregate table bound enforced");
        rejects([&] { odh::BconTuning bad(data, {3, 7, 9}); }, "aggregate constant bound enforced");
        odh::TuningResources default_bounds;
        for (unsigned owner = 0; owner < 64; ++owner) default_bounds.owners[owner] = {};
        for (unsigned table = 0; table < 1024; ++table)
            default_bounds.globals[static_cast<std::uint16_t>(table)] =
                {0, std::vector<std::uint16_t>(64, 7), 0};
        odh::BconTuning default_exact(default_bounds);
        require(default_exact.read(999, query(128))->value == 7,
                "default 64 owner 1024 table 65536 constant bounds accepted exactly");
        auto excess = default_bounds;
        excess.owners[64] = {};
        rejects([&] { odh::BconTuning bad(excess); }, "default owner bound rejects 65");
        excess = default_bounds;
        excess.globals[1024] = bcon({});
        rejects([&] { odh::BconTuning bad(excess); }, "default table bound rejects 1025 including empty table");
        excess = default_bounds;
        excess.globals[0].constants.push_back(7);
        rejects([&] { odh::BconTuning bad(excess); }, "default word bound rejects 65537");
        odh::BconTuning empty({}, {0, 0, 0});
        require(!empty.read(0, 0), "empty bank accepts zero limits");
        auto large = odh::TuningResources{};
        large.globals[256] = {0, std::vector<std::uint16_t>(255, 7), 0};
        odh::BconTuning largest(large, {0, 1, 255});
        require(largest.read(0, query(128, 127))->value == 7, "legal byte count may exceed query key width");
        large.globals[256].constants.resize(256);
        rejects([&] { odh::BconTuning bad(large); }, "unrepresentable BCON byte count rejected");
        odh::RoutineSet routines{{odh::routine_key(256), routine({read_tuning(0)})}};
        odh::RoutineOwners owners{{11, {{odh::routine_key(4096), routine({{256, 254, 255, {}}})}}},
                                 {22, {{odh::routine_key(4096), routine({{256, 254, 255, {}}})}}}};
        odh::VmMemory memory{[&](const auto& frame, auto scope, auto operand) -> std::optional<std::int16_t> {
            if (scope != 26) return std::nullopt;
            const auto value = tuning.read(frame.context.code_owner, operand);
            return value ? std::optional<std::int16_t>(value->value) : std::nullopt;
        }, {}};
        odh::Vm vm(routines, memory, {}, {}, owners);
        for (const auto owner : {11U, 22U}) {
            vm.start(odh::routine_key(4096), {1, 2, 2, owner});
            require(vm.run(2) == odh::VmStatus::returned_true && vm.temps()[0] == static_cast<int>(owner),
                    "global routine memory read preserves calling private code owner");
        }
        std::cout << checks << " BCON tuning checks passed (synthetic fixtures only)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
