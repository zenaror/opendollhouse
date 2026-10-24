#include "odh/vm_snapshot_codec.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template <typename Function>
void rejects(Function&& function, const char* message) {
    bool threw = false;
    try { function(); } catch (const odh::FormatError&) { threw = true; }
    require(threw, message);
}
void same(const odh::VmSnapshot& left, const odh::VmSnapshot& right) {
    require(left.status == right.status && left.diagnostic == right.diagnostic &&
        left.steps == right.steps && left.ticket_sequence == right.ticket_sequence &&
        left.run_generation == right.run_generation && left.stack.empty() && right.stack.empty() &&
        left.suspended_pending.empty() && right.suspended_pending.empty() &&
        !left.pending && !right.pending && !left.interaction_return && !right.interaction_return &&
        !left.external_frame_return && !right.external_frame_return, "snapshot fields round-trip");
    require(left.temps == right.temps && left.temp_xl == right.temp_xl,
        "temporary registers round-trip");
}
}

int main() {
    try {
        odh::VmSnapshot snapshot;
        snapshot.status = odh::VmStatus::fault;
        snapshot.diagnostic = "fixture: bounded diagnostic";
        snapshot.steps = 0x1020304050607080ull;
        snapshot.ticket_sequence = 0x8877665544332211ull;
        snapshot.run_generation = 0x0102030405060708ull;
        for (std::size_t i = 0; i < snapshot.temps.size(); ++i)
            snapshot.temps[i] = static_cast<std::int16_t>(i * 97u) - 800;
        snapshot.temp_xl = {70000, -70000};

        const auto encoded = odh::encode_vm_snapshot(snapshot);
        same(snapshot, odh::decode_vm_snapshot(encoded));
        auto legacy = encoded;
        const auto temp_xl_offset = 2u + 1u + 4u + snapshot.diagnostic.size() + 3u * 8u + 20u * 2u;
        legacy.erase(legacy.begin() + static_cast<std::ptrdiff_t>(temp_xl_offset),
                     legacy.begin() + static_cast<std::ptrdiff_t>(temp_xl_offset + 8));
        legacy[0] = 1; legacy[1] = 0;
        const auto old_snapshot = odh::decode_vm_snapshot(legacy);
        require(old_snapshot.temp_xl == std::array<std::int32_t, 2>{},
            "version 1 snapshots restore with zeroed TempXL values");
        const auto pending_flag_offset = 2u + 1u + 4u + snapshot.diagnostic.size() +
            3u * 8u + 20u * 2u + 2u * 4u + 4u;
        for (std::size_t size = 0; size < encoded.size(); ++size) {
            auto truncated = encoded;
            truncated.resize(size);
            rejects([&] { (void)odh::decode_vm_snapshot(truncated); }, "every truncated prefix rejected");
        }
        auto changed = encoded;
        changed[0] = 3;
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "unsupported version rejected");
        changed = encoded;
        changed[2] = 0xff;
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "invalid status rejected");
        changed = encoded;
        changed[pending_flag_offset] = 2;
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "invalid pending flag rejected");
        changed = encoded;
        changed[pending_flag_offset + 5u] = 2;
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "invalid interaction flag rejected");
        changed = encoded;
        changed[pending_flag_offset + 6u] = 2;
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "invalid external return flag rejected");
        changed = encoded;
        changed.push_back(0);
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "trailing bytes rejected");

        auto limits = odh::VmSnapshotCodecLimits{};
        limits.max_bytes = encoded.size() - 1;
        rejects([&] { (void)odh::decode_vm_snapshot(encoded, limits); }, "byte limit enforced on decode");
        limits = {};
        limits.max_diagnostic_bytes = snapshot.diagnostic.size() - 1;
        rejects([&] { (void)odh::encode_vm_snapshot(snapshot, limits); }, "diagnostic limit enforced on encode");

        odh::VmSnapshot empty;
        auto too_small = odh::VmSnapshotCodecLimits{};
        too_small.max_bytes = 81;
        rejects([&] { (void)odh::encode_vm_snapshot(empty, too_small); }, "minimum output size enforced");

        std::cout << checks << " VM snapshot codec checks passed (synthetic values only)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
