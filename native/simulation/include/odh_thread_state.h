#ifndef ODH_THREAD_STATE_H
#define ODH_THREAD_STATE_H

#include "odh_expression.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odh_thread_state odh_thread_state;
typedef struct odh_register_bank odh_register_bank;

enum { ODH_BANK_TEMP16 = 1, ODH_BANK_XL32 = 2 };
enum { ODH_TEMP_COUNT = 20, ODH_TEMP_XL_COUNT = 2 };
enum {
    ODH_STATE_OK = 0,
    ODH_STATE_INVALID_ARGUMENT = 1,
    ODH_STATE_BOUNDS = 2,
    ODH_STATE_WRONG_KIND = 3,
    ODH_STATE_MISSING_BANK = 4,
    ODH_STATE_NO_MEMORY = 5,
    ODH_STATE_REFCOUNT_OVERFLOW = 6,
    ODH_STATE_INTERNAL = 7
};
enum { ODH_EXPRESSION_BANK_BOUNDS = 6, ODH_EXPRESSION_BANK_UNAVAILABLE = 7 };

ODH_SIM_API uint32_t ODH_SIM_CALL odh_state_abi_version(void);
/* initialize_banks must be 0(empty holder) or 1(fresh zero TEMP20 and XL2).
 * Empty holders permit share/clone setup without allocating discarded defaults.
 * No domain is ever implicitly created by a read. */
ODH_SIM_API int32_t ODH_SIM_CALL odh_state_create_v1(uint32_t initialize_banks, odh_thread_state** state);
ODH_SIM_API int32_t ODH_SIM_CALL odh_state_retain_v1(odh_thread_state* state);
ODH_SIM_API int32_t ODH_SIM_CALL odh_state_release_v1(odh_thread_state* state);
/* get returns an OWNED bank reference; caller releases it. Missing returns4.
 * bind retains a nonnull new bank, then releases the old domain. Null clears
 * just that domain; retained old views/other holders remain valid. */
ODH_SIM_API int32_t ODH_SIM_CALL odh_state_get_bank_v1(const odh_thread_state* state, uint32_t kind, odh_register_bank** bank);
ODH_SIM_API int32_t ODH_SIM_CALL odh_state_bind_bank_v1(odh_thread_state* state, uint32_t kind, odh_register_bank* bank);

ODH_SIM_API int32_t ODH_SIM_CALL odh_bank_create_v1(uint32_t kind, odh_register_bank** bank);
ODH_SIM_API int32_t ODH_SIM_CALL odh_bank_retain_v1(odh_register_bank* bank);
ODH_SIM_API int32_t ODH_SIM_CALL odh_bank_release_v1(odh_register_bank* bank);
ODH_SIM_API int32_t ODH_SIM_CALL odh_bank_clone_v1(const odh_register_bank* bank, odh_register_bank** clone);
/* Scalar access is signed32. TEMP writes preserve signed low16 bits; XL writes
 * preserve all32 bits. Negative/out-of-range indices return BOUNDS, never clamp. */
ODH_SIM_API int32_t ODH_SIM_CALL odh_bank_read_v1(const odh_register_bank* bank, int32_t index, int32_t* value);
ODH_SIM_API int32_t ODH_SIM_CALL odh_bank_write_v1(odh_register_bank* bank, int32_t index, int32_t value);
/* Typed copies require EXACT count20 or2 and matching kind. Imports validate
 * before changing anything; exports never partially fill on invalid arguments.
 * Arrays are borrowed only during the call, not retained as live state. */
ODH_SIM_API int32_t ODH_SIM_CALL odh_bank_import16_v1(odh_register_bank* bank, const int16_t* values, uint32_t count);
ODH_SIM_API int32_t ODH_SIM_CALL odh_bank_export16_v1(const odh_register_bank* bank, int16_t* values, uint32_t count);
ODH_SIM_API int32_t ODH_SIM_CALL odh_bank_import32_v1(odh_register_bank* bank, const int32_t* values, uint32_t count);
ODH_SIM_API int32_t ODH_SIM_CALL odh_bank_export32_v1(const odh_register_bank* bank, int32_t* values, uint32_t count);
ODH_SIM_API int32_t ODH_SIM_CALL odh_bank_info_v1(const odh_register_bank* bank,
    uint32_t* kind, uint32_t* count, uint32_t* references, uint64_t* allocation_bytes);

typedef int32_t (ODH_SIM_CALL *odh_expression_resolve_stack_state_fn)(void* context, odh_thread_state** state);
/* Same21-op executor, with direct native bank access for scopes8,11,13,42.
 * Current holder identity is stable during a call, but its independently bound
 * domains are looked up anew at EACH direct access. Scope13 resolves the current
 * StackObject.Thread holder lazily at EACH read/write, never cached across host
 * callbacks. Resolver returns a BORROWED holder valid until execute returns.
 * Current/resolved holders and callback context must remain valid until return;
 * retain/addref on the host protects them from replacement/disposal callbacks.
 * Current may be null: only an actual owned-scope access reports unavailable7.
 * Non-owned scopes use read/write callbacks lazily; absent needed callback is
 * INVALID_ARGUMENT. No bank mutex is held over any host callback; same-thread
 * nesting is allowed. Concurrent mutations must be externally serialized.
 * All seven result pointers are REQUIRED and zeroed on entry. Reads/writes count
 * successful access calls (a setter's false boolean still counts). TempByTemp
 * counts two native physical reads. stack_resolutions counts resolver attempts.
 * negative_sqrt is1 when op20 reads a negative RHS, even if its setter then fails.
 * Status0..5 retain expression meanings;6 bounds;7 missing native domain/holder.
 * Lifetime refs are atomic, but contents/bind are not concurrent-access APIs.
 */
ODH_SIM_API int32_t ODH_SIM_CALL odh_expression_execute_ts1_state_v1(
    const uint8_t* operand, uint32_t operand_length, odh_thread_state* current,
    void* context, odh_expression_read_fn read, odh_expression_write_fn write,
    odh_expression_levels_fn levels, odh_expression_resolve_stack_state_fn resolve_stack,
    uint32_t* goto_true, uint32_t* native_reads, uint32_t* native_writes,
    uint32_t* host_reads, uint32_t* host_writes, uint32_t* stack_resolutions, uint32_t* negative_sqrt);

/* Every create/get/clone/retain success grants one reference; release consumes
 * one. A pointer with no remaining owner is invalid and must not be reused.
 * No managed arrays, entity pointers, or callbacks are stored in these objects.
 * OOM/refcount exhaustion return explicit errors; no eviction or hidden cap.
 */

#ifdef __cplusplus
}
#endif

#endif
