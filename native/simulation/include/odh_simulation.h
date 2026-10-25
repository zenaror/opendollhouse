#ifndef ODH_SIMULATION_H
#define ODH_SIMULATION_H

#include <stdint.h>

#if defined(_WIN32)
#  define ODH_SIM_CALL __cdecl
#  if defined(ODH_SIMULATION_STATIC)
#    define ODH_SIM_API
#  elif defined(ODH_SIMULATION_SHARED_BUILD)
#    define ODH_SIM_API __declspec(dllexport)
#  else
#    define ODH_SIM_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && !defined(ODH_SIMULATION_STATIC)
#  define ODH_SIM_CALL
#  define ODH_SIM_API __attribute__((visibility("default")))
#else
#  define ODH_SIM_CALL
#  define ODH_SIM_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odh_wake_queue odh_wake_queue;

typedef int32_t (ODH_SIM_CALL *odh_wake_lookup_id_fn)(
    void* context, uint64_t token, int16_t* object_id);
typedef void (ODH_SIM_CALL *odh_wake_retain_fn)(void* context, uint64_t token);
typedef void (ODH_SIM_CALL *odh_wake_release_fn)(void* context, uint64_t token);

enum {
    ODH_WAKE_OK = 0,
    ODH_WAKE_INVALID_ARGUMENT = 1,
    ODH_WAKE_NO_CURRENT = 2,
    ODH_WAKE_NOT_FOUND = 3,
    ODH_WAKE_CAPACITY = 4,
    ODH_WAKE_CALLBACK_FAILED = 5,
    ODH_WAKE_INTERNAL = 6,
    ODH_WAKE_REENTRANT = 7
};

enum {
    ODH_WAKE_TARGET_NONE = 0,
    ODH_WAKE_TARGET_CURRENT = 1,
    ODH_WAKE_TARGET_NEXT = 2
};

ODH_SIM_API uint32_t ODH_SIM_CALL odh_wake_abi_version(void);

/* Tokens are stable, nonzero reference identities, never ObjectIDs. The caller
 * must never reuse a token for a different entity during this queue lifetime.
 * Callbacks must not throw, allocate, or reenter this queue. lookup_id returns
 * zero on success and reads the entity's CURRENT signed 16-bit ObjectID.
 * retain/release are paired for each actual list occurrence. The caller owns
 * context (which may be null) and callbacks until destroy returns; no entity
 * pointer crosses ABI. A queue is single-thread owned; concurrent calls are
 * unsupported. max_buckets bounds distinct live lists, including a detached
 * current-list alias, rather than only keys in the scheduled map.
 * max_bytes bounds queue-owned C++ allocations (including list control blocks),
 * not caller memory, allocator metadata, stack, or the managed token registry.
 */
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_create_v1(
    void* context, odh_wake_lookup_id_fn lookup_id,
    odh_wake_retain_fn retain, odh_wake_release_fn release,
    uint32_t max_buckets, uint32_t max_entries, uint64_t max_bytes,
    odh_wake_queue** out_queue);
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_destroy_v1(odh_wake_queue* queue);

ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_schedule_v1(
    odh_wake_queue* queue, uint64_t token, uint32_t tick);
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_schedule_current_v1(
    odh_wake_queue* queue, uint64_t token);
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_deschedule_v1(
    odh_wake_queue* queue, uint64_t token, uint32_t scheduled_end, uint32_t current_tick);
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_prepare_interrupt_v1(
    odh_wake_queue* queue, uint64_t token, uint32_t scheduled_end,
    uint32_t current_tick, int16_t current_object_id, uint32_t* target);
/* prepare_interrupt only chooses a target and removes the previous occurrence.
 * A later caller-issued schedule is a separate operation and can fail its cap.
 */
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_begin_v1(
    odh_wake_queue* queue, uint32_t tick, uint32_t previous_tick);
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_select_current_v1(
    odh_wake_queue* queue, uint32_t tick, uint32_t* found);
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_current_count_v1(
    odh_wake_queue* queue, uint32_t* count);
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_current_at_v1(
    odh_wake_queue* queue, uint32_t index, uint64_t* token);
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_complete_v1(
    odh_wake_queue* queue, uint32_t tick);
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_reset_v1(odh_wake_queue* queue);

/* live_lists counts distinct list objects, including a detached current alias;
 * live_entries counts actual token occurrences in those lists. current_alias
 * is 1 when the mutable current list exists, even when detached from schedule.
 * Outputs reset to zero on entry where pointers are present. All are required.
 */
ODH_SIM_API int32_t ODH_SIM_CALL odh_wake_stats_v1(
    odh_wake_queue* queue, uint32_t* scheduled_buckets,
    uint32_t* peak_scheduled_buckets, uint32_t* live_lists,
    uint32_t* live_entries, uint32_t* peak_entries, uint32_t* current_alias,
    uint64_t* live_bytes, uint64_t* peak_bytes);

#ifdef __cplusplus
}
#endif

#endif
