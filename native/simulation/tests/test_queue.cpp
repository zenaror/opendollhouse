#include "odh_simulation.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace {

int checks = 0;
void require(bool condition, const char* label) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", label);
        throw std::runtime_error(label);
    }
}

struct Context {
    std::array<std::int16_t, 32> ids{};
    std::array<std::int32_t, 32> refs{};
    std::uint64_t fail_token = 0;
    bool probe_reentry = false;
    std::int32_t nested_status = -1;
    odh_wake_queue* queue = nullptr;
};

std::int32_t ODH_SIM_CALL lookup(void* context, std::uint64_t token, std::int16_t* id) {
    auto& state = *static_cast<Context*>(context);
    if (state.probe_reentry) {
        std::uint32_t buckets = 1, peak_buckets = 1, lists = 1;
        std::uint32_t entries = 1, peak_entries = 1, alias = 1;
        std::uint64_t bytes = 1, peak_bytes = 1;
        state.nested_status = odh_wake_stats_v1(
            state.queue, &buckets, &peak_buckets, &lists, &entries,
            &peak_entries, &alias, &bytes, &peak_bytes);
        return 1; // Callback failure must leave the insertion unchanged.
    }
    if (token >= state.ids.size() || token == state.fail_token) return 1;
    *id = state.ids[token];
    return 0;
}

void ODH_SIM_CALL retain(void* context, std::uint64_t token) {
    ++static_cast<Context*>(context)->refs[token];
}

void ODH_SIM_CALL release(void* context, std::uint64_t token) {
    --static_cast<Context*>(context)->refs[token];
}

odh_wake_queue* make(Context& context, std::uint32_t lists = 8,
                     std::uint32_t entries = 32, std::uint64_t bytes = 65536) {
    odh_wake_queue* queue = nullptr;
    require(odh_wake_create_v1(&context, lookup, retain, release,
                               lists, entries, bytes, &queue) == ODH_WAKE_OK && queue,
            "create queue");
    context.queue = queue;
    return queue;
}

struct Stats {
    std::uint32_t buckets = 0, peak_buckets = 0, lists = 0;
    std::uint32_t entries = 0, peak_entries = 0, alias = 0;
    std::uint64_t bytes = 0, peak_bytes = 0;
};

Stats stats(odh_wake_queue* queue) {
    Stats value;
    require(odh_wake_stats_v1(queue, &value.buckets, &value.peak_buckets,
                              &value.lists, &value.entries, &value.peak_entries,
                              &value.alias, &value.bytes, &value.peak_bytes) == ODH_WAKE_OK,
            "read stats");
    return value;
}

std::uint64_t current_at(odh_wake_queue* queue, std::uint32_t index) {
    std::uint64_t token = 0;
    require(odh_wake_current_at_v1(queue, index, &token) == ODH_WAKE_OK,
            "read current token");
    return token;
}

void check_order_and_live_ids() {
    Context context;
    context.ids[1] = 10; context.ids[2] = 30;
    context.ids[3] = 20; context.ids[4] = 20;
    auto* queue = make(context);
    require(odh_wake_schedule_v1(queue, 1, 5) == ODH_WAKE_OK, "first schedule");
    require(odh_wake_schedule_v1(queue, 2, 5) == ODH_WAKE_OK, "second schedule");
    std::uint32_t found = 0;
    require(odh_wake_select_current_v1(queue, 5, &found) == ODH_WAKE_OK && found,
            "select current tick");
    require(odh_wake_schedule_current_v1(queue, 3) == ODH_WAKE_OK,
            "insert during current tick");
    require(odh_wake_schedule_current_v1(queue, 4) == ODH_WAKE_OK &&
            context.refs[4] == 0, "duplicate ObjectID retains first reference");
    require(current_at(queue, 0) == 1 && current_at(queue, 1) == 3 &&
            current_at(queue, 2) == 2, "live insertion ordering");
    context.ids[1] = 40;
    require(odh_wake_schedule_v1(queue, 1, 5) == ODH_WAKE_OK,
            "same token can occur twice after mutable ID change");
    require(current_at(queue, 0) == 1 && current_at(queue, 3) == 1 &&
            context.refs[1] == 2, "binary search uses live midpoint IDs without resort");
    auto value = stats(queue);
    require(value.entries == 4 && value.lists == 1 && value.alias == 1,
            "stats count actual repeated occurrences");
    require(odh_wake_destroy_v1(queue) == ODH_WAKE_OK &&
            context.refs[1] == 0 && context.refs[2] == 0 && context.refs[3] == 0,
            "destroy releases all occurrences");
}

void check_identity_and_multiple_buckets() {
    Context context;
    context.ids[1] = 20; context.ids[2] = 20;
    auto* queue = make(context);
    require(odh_wake_schedule_v1(queue, 1, 5) == ODH_WAKE_OK &&
            odh_wake_schedule_v1(queue, 2, 5) == ODH_WAKE_OK &&
            context.refs[2] == 0, "same ID does not replace queued reference");
    require(odh_wake_schedule_v1(queue, 1, 9) == ODH_WAKE_OK &&
            context.refs[1] == 2, "same reference may occupy two tick buckets");
    require(odh_wake_deschedule_v1(queue, 2, 9, 5) == ODH_WAKE_OK &&
            context.refs[1] == 2, "deschedule other identity leaves original");
    require(odh_wake_deschedule_v1(queue, 1, 9, 5) == ODH_WAKE_OK &&
            context.refs[1] == 1, "deschedule removes one occurrence only");
    std::uint32_t found = 0;
    require(odh_wake_select_current_v1(queue, 5, &found) == ODH_WAKE_OK &&
            found && current_at(queue, 0) == 1, "older bucket still retains identity");
    require(odh_wake_destroy_v1(queue) == ODH_WAKE_OK && context.refs[1] == 0,
            "identity refs released");
}

void check_migration_alias_and_reset() {
    Context context;
    context.ids[1] = 20; context.ids[2] = 30;
    auto* queue = make(context);
    require(odh_wake_schedule_v1(queue, 1, 1) == ODH_WAKE_OK &&
            odh_wake_schedule_v1(queue, 2, 100) == ODH_WAKE_OK,
            "schedule resume and destination");
    std::uint32_t found = 0;
    require(odh_wake_select_current_v1(queue, 100, &found) == ODH_WAKE_OK && found,
            "retain destination as current alias");
    require(odh_wake_begin_v1(queue, 100, 0) == ODH_WAKE_OK,
            "tick zero migrates and overwrites destination");
    auto value = stats(queue);
    require(value.buckets == 1 && value.lists == 2 && value.entries == 2 &&
            context.refs[2] == 1, "overwritten destination survives detached alias");
    require(odh_wake_select_current_v1(queue, 100, &found) == ODH_WAKE_OK &&
            found && current_at(queue, 0) == 1 && context.refs[2] == 0,
            "next lookup replaces detached alias");
    require(odh_wake_complete_v1(queue, 100) == ODH_WAKE_OK &&
            odh_wake_reset_v1(queue) == ODH_WAKE_OK,
            "complete and reset detach current list");
    value = stats(queue);
    require(value.buckets == 0 && value.lists == 1 && value.entries == 1 &&
            value.alias == 1 && context.refs[1] == 1,
            "detached current counts against limits");
    require(odh_wake_select_current_v1(queue, 101, &found) == ODH_WAKE_OK &&
            !found && context.refs[1] == 0, "missing lookup clears stale alias");
    require(odh_wake_destroy_v1(queue) == ODH_WAKE_OK, "destroy after reset");
}

void check_interrupt_and_reentrant_current() {
    Context context;
    context.ids[1] = 10; context.ids[2] = 20; context.ids[3] = 5;
    auto* queue = make(context);
    require(odh_wake_schedule_v1(queue, 1, 5) == ODH_WAKE_OK &&
            odh_wake_schedule_v1(queue, 2, 9) == ODH_WAKE_OK &&
            odh_wake_schedule_v1(queue, 3, 9) == ODH_WAKE_OK,
            "schedule interrupt scenario");
    std::uint32_t found = 0, target = 99;
    require(odh_wake_select_current_v1(queue, 5, &found) == ODH_WAKE_OK && found,
            "current list present");
    require(odh_wake_prepare_interrupt_v1(queue, 2, 9, 5, 10, &target) == ODH_WAKE_OK &&
            target == ODH_WAKE_TARGET_CURRENT, "later ID interrupts this frame");
    require(odh_wake_schedule_current_v1(queue, 2) == ODH_WAKE_OK,
            "interrupt inserts this frame");
    require(odh_wake_prepare_interrupt_v1(queue, 3, 9, 5, 10, &target) == ODH_WAKE_OK &&
            target == ODH_WAKE_TARGET_NEXT,
            "earlier ID interrupts next frame");
    require(odh_wake_schedule_v1(queue, 3, 6) == ODH_WAKE_OK,
            "next-frame scheduling remains caller action");
    require(current_at(queue, 0) == 1 && current_at(queue, 1) == 2,
            "outer list before nested selection");
    require(odh_wake_select_current_v1(queue, 6, &found) == ODH_WAKE_OK &&
            found && current_at(queue, 0) == 3,
            "nested selection replaces current list");
    require(odh_wake_complete_v1(queue, 6) == ODH_WAKE_OK &&
            stats(queue).buckets == 2,
            "complete mutable tick leaves original bucket");
    require(odh_wake_destroy_v1(queue) == ODH_WAKE_OK,
            "destroy reentrant scenario");
}

void check_callback_failure_and_reentry() {
    Context context;
    context.ids[1] = 10; context.ids[2] = 20;
    auto* queue = make(context);
    require(odh_wake_schedule_v1(queue, 1, 5) == ODH_WAKE_OK,
            "seed callback scenario");
    context.fail_token = 2;
    require(odh_wake_schedule_v1(queue, 2, 5) == ODH_WAKE_CALLBACK_FAILED &&
            context.refs[2] == 0 && stats(queue).entries == 1,
            "lookup failure before insertion mutation");
    context.fail_token = 0;
    require(odh_wake_schedule_v1(queue, 2, 9) == ODH_WAKE_OK,
            "seed future callback scenario");
    context.fail_token = 2;
    std::uint32_t target = 99;
    require(odh_wake_prepare_interrupt_v1(queue, 2, 9, 5, 10, &target) ==
            ODH_WAKE_CALLBACK_FAILED && target == ODH_WAKE_TARGET_NONE &&
            context.refs[2] == 1, "interrupt callback failure preserves occurrence");
    context.fail_token = 0;
    context.probe_reentry = true;
    require(odh_wake_schedule_v1(queue, 2, 5) == ODH_WAKE_CALLBACK_FAILED &&
            context.nested_status == ODH_WAKE_REENTRANT && stats(queue).entries == 2,
            "callback cannot reenter same queue");
    context.probe_reentry = false;
    require(odh_wake_destroy_v1(queue) == ODH_WAKE_OK &&
            context.refs[1] == 0 && context.refs[2] == 0,
            "failure scenario does not leak occurrences");
}

void check_caps_and_wrap() {
    Context context;
    context.ids[1] = 10; context.ids[2] = 20;
    auto* queue = make(context, 1, 1);
    require(odh_wake_schedule_v1(queue, 1, 5) == ODH_WAKE_OK,
            "one-list one-entry cap seed");
    require(odh_wake_schedule_v1(queue, 2, 6) == ODH_WAKE_CAPACITY &&
            stats(queue).buckets == 1 && context.refs[2] == 0,
            "list/entry cap fails without empty bucket or retained token");
    std::uint32_t found = 0;
    require(odh_wake_select_current_v1(queue, 5, &found) == ODH_WAKE_OK && found,
            "select capped list");
    require(odh_wake_complete_v1(queue, 5) == ODH_WAKE_OK &&
            odh_wake_schedule_v1(queue, 2, 6) == ODH_WAKE_CAPACITY,
            "detached current still counts against both caps");
    require(odh_wake_select_current_v1(queue, 6, &found) == ODH_WAKE_OK && !found,
            "clear detached cap occupancy");
    require(odh_wake_schedule_v1(queue, 2, 0) == ODH_WAKE_OK &&
            odh_wake_select_current_v1(queue, 0, &found) == ODH_WAKE_OK && found &&
            current_at(queue, 0) == 2, "uint wrap target tick zero is ordinary key");
    require(odh_wake_destroy_v1(queue) == ODH_WAKE_OK &&
            context.refs[1] == 0 && context.refs[2] == 0,
            "cap scenario releases all refs");

    odh_wake_queue* denied = reinterpret_cast<odh_wake_queue*>(1);
    require(odh_wake_create_v1(&context, lookup, retain, release, 1, 1, 1,
                               &denied) == ODH_WAKE_CAPACITY && denied == nullptr,
            "too-small queue byte cap rejected atomically");
    auto* sized = make(context, 1, 1);
    const auto base = stats(sized).bytes;
    require(odh_wake_destroy_v1(sized) == ODH_WAKE_OK, "destroy baseline byte queue");
    auto* tight = make(context, 1, 1, base + 1);
    require(odh_wake_schedule_v1(tight, 1, 5) == ODH_WAKE_CAPACITY &&
            stats(tight).buckets == 0 && stats(tight).entries == 0 &&
            context.refs[1] == 0, "allocator byte cap leaves no empty bucket");
    require(odh_wake_destroy_v1(tight) == ODH_WAKE_OK, "destroy byte-capped queue");
}

} // namespace

int main() {
    try {
        require(odh_wake_abi_version() == 1, "ABI version");
        check_order_and_live_ids();
        check_identity_and_multiple_buckets();
        check_migration_alias_and_reset();
        check_interrupt_and_reentrant_current();
        check_callback_failure_and_reentry();
        check_caps_and_wrap();
        std::printf("PASS %d native wake queue checks\n", checks);
        return 0;
    } catch (...) {
        return 1;
    }
}
