#include "odh_simulation.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef __3DS__
#include <3ds.h>
#endif

namespace {

struct Context {
    std::array<std::int16_t, 8> ids{};
    std::array<std::int32_t, 8> refs{};
};

std::int32_t ODH_SIM_CALL id_of(void* state, std::uint64_t token, std::int16_t* id) {
    auto& context = *static_cast<Context*>(state);
    if (!id || token >= context.ids.size()) return 1;
    *id = context.ids[token];
    return 0;
}
void ODH_SIM_CALL retain(void* state, std::uint64_t token) {
    ++static_cast<Context*>(state)->refs[token];
}
void ODH_SIM_CALL release(void* state, std::uint64_t token) {
    --static_cast<Context*>(state)->refs[token];
}

odh_wake_queue* create(Context& context) {
    odh_wake_queue* queue = nullptr;
    if (odh_wake_create_v1(&context, id_of, retain, release,
                           8, 32, 65536, &queue) != ODH_WAKE_OK) return nullptr;
    return queue;
}

bool empty_refs(const Context& context) {
    for (const auto count : context.refs) if (count != 0) return false;
    return true;
}

bool live_insertion() {
    Context context;
    context.ids[1] = 10; context.ids[2] = 30; context.ids[3] = 20;
    auto* queue = create(context);
    if (!queue) return false;
    bool okay = odh_wake_schedule_v1(queue, 1, 5) == ODH_WAKE_OK &&
        odh_wake_schedule_v1(queue, 2, 5) == ODH_WAKE_OK;
    std::uint32_t found = 0;
    okay &= odh_wake_select_current_v1(queue, 5, &found) == ODH_WAKE_OK && found;
    std::array<std::uint64_t, 3> order{};
    std::uint32_t seen = 0;
    for (std::uint32_t index = 0; okay; ++index) {
        std::uint32_t count = 0;
        okay &= odh_wake_current_count_v1(queue, &count) == ODH_WAKE_OK;
        if (!okay || index >= count) break;
        std::uint64_t token = 0;
        okay &= odh_wake_current_at_v1(queue, index, &token) == ODH_WAKE_OK;
        if (!okay || seen >= order.size()) { okay = false; break; }
        order[seen++] = token;
        if (token == 1)
            okay &= odh_wake_schedule_current_v1(queue, 3) == ODH_WAKE_OK;
    }
    okay &= seen == 3 && order[0] == 1 && order[1] == 3 && order[2] == 2;
    okay &= odh_wake_complete_v1(queue, 5) == ODH_WAKE_OK;
    okay &= odh_wake_reset_v1(queue) == ODH_WAKE_OK;
    okay &= context.refs[1] == 1 && context.refs[2] == 1 && context.refs[3] == 1;
    okay &= odh_wake_select_current_v1(queue, 6, &found) == ODH_WAKE_OK && !found;
    okay &= odh_wake_destroy_v1(queue) == ODH_WAKE_OK;
    return okay && empty_refs(context);
}

bool reentrant_current_replacement() {
    Context context;
    context.ids[1] = 10; context.ids[2] = 20; context.ids[3] = 30;
    auto* queue = create(context);
    if (!queue) return false;
    bool okay = odh_wake_schedule_v1(queue, 1, 5) == ODH_WAKE_OK &&
        odh_wake_schedule_v1(queue, 2, 5) == ODH_WAKE_OK &&
        odh_wake_schedule_v1(queue, 3, 6) == ODH_WAKE_OK;
    std::uint32_t found = 0;
    okay &= odh_wake_select_current_v1(queue, 5, &found) == ODH_WAKE_OK && found;
    std::array<std::uint64_t, 3> order{};
    std::uint32_t seen = 0;
    for (std::uint32_t index = 0; okay; ++index) {
        std::uint32_t count = 0;
        okay &= odh_wake_current_count_v1(queue, &count) == ODH_WAKE_OK;
        if (!okay || index >= count) break;
        std::uint64_t token = 0;
        okay &= odh_wake_current_at_v1(queue, index, &token) == ODH_WAKE_OK;
        if (!okay || seen >= order.size()) { okay = false; break; }
        order[seen++] = token;
        if (token == 1) {
            okay &= odh_wake_select_current_v1(queue, 6, &found) == ODH_WAKE_OK && found;
            std::uint64_t nested = 0;
            okay &= odh_wake_current_at_v1(queue, 0, &nested) == ODH_WAKE_OK && nested == 3;
            order[seen++] = nested;
            okay &= odh_wake_complete_v1(queue, 6) == ODH_WAKE_OK;
        }
    }
    okay &= seen == 2 && order[0] == 1 && order[1] == 3;
    // Outer Complete uses the mutable tick ID 6, leaving tick 5 scheduled.
    okay &= odh_wake_complete_v1(queue, 6) == ODH_WAKE_OK;
    std::uint32_t buckets = 0, peak_buckets = 0, lists = 0, entries = 0;
    std::uint32_t peak_entries = 0, alias = 0;
    std::uint64_t bytes = 0, peak_bytes = 0;
    okay &= odh_wake_stats_v1(queue, &buckets, &peak_buckets, &lists,
                              &entries, &peak_entries, &alias, &bytes, &peak_bytes) == ODH_WAKE_OK &&
            buckets == 1 && alias == 1;
    okay &= odh_wake_destroy_v1(queue) == ODH_WAKE_OK;
    return okay && empty_refs(context);
}

bool resume_migrate_reset_reconstruct() {
    Context context;
    context.ids[1] = 20; context.ids[2] = 30;
    auto* queue = create(context);
    if (!queue) return false;
    bool okay = odh_wake_schedule_v1(queue, 1, 1) == ODH_WAKE_OK &&
        odh_wake_schedule_v1(queue, 2, 100) == ODH_WAKE_OK;
    std::uint32_t found = 0;
    okay &= odh_wake_select_current_v1(queue, 100, &found) == ODH_WAKE_OK && found;
    okay &= odh_wake_begin_v1(queue, 100, 0) == ODH_WAKE_OK;
    okay &= context.refs[1] == 1 && context.refs[2] == 1; // old destination is detached.
    okay &= odh_wake_select_current_v1(queue, 100, &found) == ODH_WAKE_OK && found;
    std::uint64_t token = 0;
    okay &= odh_wake_current_at_v1(queue, 0, &token) == ODH_WAKE_OK && token == 1;
    okay &= context.refs[2] == 0;
    okay &= odh_wake_complete_v1(queue, 100) == ODH_WAKE_OK &&
        odh_wake_reset_v1(queue) == ODH_WAKE_OK && context.refs[1] == 1;
    okay &= odh_wake_select_current_v1(queue, 101, &found) == ODH_WAKE_OK &&
        !found && context.refs[1] == 0;
    okay &= odh_wake_destroy_v1(queue) == ODH_WAKE_OK && empty_refs(context);

    // Save loading reconstructs scheduling; the native queue has no invented
    // serialized format. A new queue receives the same entity identity.
    queue = create(context);
    if (!queue) return false;
    okay &= odh_wake_schedule_v1(queue, 1, 1) == ODH_WAKE_OK;
    okay &= odh_wake_begin_v1(queue, 200, 0) == ODH_WAKE_OK;
    okay &= odh_wake_select_current_v1(queue, 200, &found) == ODH_WAKE_OK && found;
    okay &= odh_wake_current_at_v1(queue, 0, &token) == ODH_WAKE_OK && token == 1;
    okay &= odh_wake_destroy_v1(queue) == ODH_WAKE_OK;
    return okay && empty_refs(context);
}

bool capacity_and_wrap() {
    Context context;
    context.ids[1] = 10; context.ids[2] = 20;
    odh_wake_queue* queue = nullptr;
    if (odh_wake_create_v1(&context, id_of, retain, release,
                           1, 1, 65536, &queue) != ODH_WAKE_OK) return false;
    bool okay = odh_wake_schedule_v1(queue, 1, 5) == ODH_WAKE_OK;
    std::uint32_t found = 0;
    okay &= odh_wake_select_current_v1(queue, 5, &found) == ODH_WAKE_OK && found;
    okay &= odh_wake_complete_v1(queue, 5) == ODH_WAKE_OK;
    okay &= odh_wake_schedule_v1(queue, 2, 6) == ODH_WAKE_CAPACITY && context.refs[2] == 0;
    okay &= odh_wake_select_current_v1(queue, 6, &found) == ODH_WAKE_OK && !found;
    okay &= odh_wake_schedule_v1(queue, 2, 0) == ODH_WAKE_OK;
    okay &= odh_wake_select_current_v1(queue, 0, &found) == ODH_WAKE_OK && found;
    std::uint64_t token = 0;
    okay &= odh_wake_current_at_v1(queue, 0, &token) == ODH_WAKE_OK && token == 2;
    okay &= odh_wake_destroy_v1(queue) == ODH_WAKE_OK;
    return okay && empty_refs(context);
}

bool run_probe() {
    return odh_wake_abi_version() == 1 && live_insertion() &&
        reentrant_current_replacement() && resume_migrate_reset_reconstruct() &&
        capacity_and_wrap();
}

} // namespace

int main() {
    const bool passed = run_probe();
#ifdef __3DS__
    gfxInitDefault();
    consoleInit(GFX_TOP, nullptr);
    const char* marker = passed
        ? "ODH_3DS_NATIVE_WAKE_V1:PASS\n" : "ODH_3DS_NATIVE_WAKE_V1:FAIL\n";
    svcOutputDebugString(marker, static_cast<s32>(std::strlen(marker)));
#endif
    std::printf("ODH native wake queue: %s\n", passed ? "PASS" : "FAIL");
#ifdef __3DS__
    std::printf("Press START to exit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
#endif
    return passed ? 0 : 1;
}
