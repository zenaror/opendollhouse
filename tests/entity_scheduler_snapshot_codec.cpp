#include "odh/entity_scheduler_snapshot_codec.hpp"

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
odh::EntityVmSchedulerSnapshot fixture() {
    odh::EntityVmSchedulerSnapshot value;
    value.entity_limit = 4;
    value.entities = {
        {-7, 2, 105, odh::VmStatus::running, true, false, true},
        {11, 9, 221, odh::VmStatus::waiting, false, true, false}
    };
    value.budget_cursor = 11;
    value.last_tick = 5;
    value.instructions_last_tick = 34;
    value.diagnostic = "bounded scheduler checkpoint";
    return value;
}
void same(const odh::EntityVmSchedulerSnapshot& left, const odh::EntityVmSchedulerSnapshot& right) {
    require(left.entity_limit == right.entity_limit && left.entities.size() == right.entities.size() &&
        left.budget_cursor == right.budget_cursor && left.last_tick == right.last_tick &&
        left.instructions_last_tick == right.instructions_last_tick && left.faulted == right.faulted &&
        left.diagnostic == right.diagnostic, "scheduler fields round-trip");
    for (std::size_t i = 0; i < left.entities.size(); ++i) {
        const auto& a = left.entities[i]; const auto& b = right.entities[i];
        require(a.id == b.id && a.run_generation == b.run_generation && a.steps == b.steps &&
            a.status == b.status && a.before == b.before && a.after == b.after &&
            a.interrupt == b.interrupt, "registered entity fields round-trip");
    }
}
}

int main() {
    try {
        const auto snapshot = fixture();
        const auto encoded = odh::encode_entity_vm_scheduler_snapshot(snapshot);
        same(snapshot, odh::decode_entity_vm_scheduler_snapshot(encoded));
        const auto cursor_flag_offset = 14u + snapshot.entities.size() * 22u;
        const auto first_status_offset = 14u + 2u + 8u + 8u;
        const auto fault_flag_offset = cursor_flag_offset + 20u;

        for (std::size_t size = 0; size < encoded.size(); ++size) {
            auto truncated = encoded;
            truncated.resize(size);
            rejects([&] { (void)odh::decode_entity_vm_scheduler_snapshot(truncated); }, "every truncated prefix rejected");
        }
        auto changed = encoded;
        changed[0] = 3;
        rejects([&] { (void)odh::decode_entity_vm_scheduler_snapshot(changed); }, "unsupported version rejected");
        changed = encoded;
        changed[first_status_offset] = 0xff;
        rejects([&] { (void)odh::decode_entity_vm_scheduler_snapshot(changed); }, "invalid VM status rejected");
        changed = encoded;
        changed[cursor_flag_offset] = 2;
        rejects([&] { (void)odh::decode_entity_vm_scheduler_snapshot(changed); }, "invalid cursor flag rejected");
        changed = encoded;
        changed[fault_flag_offset] = 2;
        rejects([&] { (void)odh::decode_entity_vm_scheduler_snapshot(changed); }, "invalid fault flag rejected");
        changed = encoded;
        changed.push_back(0);
        rejects([&] { (void)odh::decode_entity_vm_scheduler_snapshot(changed); }, "trailing bytes rejected");

        auto limits = odh::EntityVmSchedulerSnapshotCodecLimits{};
        limits.max_entities = 1;
        rejects([&] { (void)odh::decode_entity_vm_scheduler_snapshot(encoded, limits); }, "entity limit enforced");
        limits = {};
        limits.max_bytes = encoded.size() - 1;
        rejects([&] { (void)odh::decode_entity_vm_scheduler_snapshot(encoded, limits); }, "byte limit enforced");
        limits = {};
        limits.max_diagnostic_bytes = snapshot.diagnostic.size() - 1;
        rejects([&] { (void)odh::encode_entity_vm_scheduler_snapshot(snapshot, limits); }, "diagnostic limit enforced");

        auto invalid = snapshot;
        std::swap(invalid.entities[0], invalid.entities[1]);
        rejects([&] { (void)odh::encode_entity_vm_scheduler_snapshot(invalid); }, "unsorted entities rejected");
        invalid = snapshot;
        invalid.budget_cursor = 12;
        rejects([&] { (void)odh::encode_entity_vm_scheduler_snapshot(invalid); }, "unregistered cursor rejected");
        invalid = snapshot;
        invalid.entity_limit = odh::EntityVmSchedulerSnapshotCodecLimits{}.max_entities + 1;
        rejects([&] { (void)odh::encode_entity_vm_scheduler_snapshot(invalid); }, "excessive entity capacity rejected");
        invalid = snapshot;
        invalid.faulted = true;
        invalid.diagnostic.clear();
        rejects([&] { (void)odh::encode_entity_vm_scheduler_snapshot(invalid); }, "fault without diagnostic rejected");

        std::cout << checks << " scheduler snapshot codec checks passed (synthetic values only)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
