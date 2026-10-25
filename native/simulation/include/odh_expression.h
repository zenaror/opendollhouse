#ifndef ODH_EXPRESSION_H
#define ODH_EXPRESSION_H

#include "odh_simulation.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (ODH_SIM_CALL *odh_expression_read_fn)(
    void* context, uint8_t scope, int16_t index, int32_t* value);
typedef int32_t (ODH_SIM_CALL *odh_expression_write_fn)(
    void* context, uint8_t scope, int16_t index, int32_t value, uint32_t* accepted);
typedef int32_t (ODH_SIM_CALL *odh_expression_levels_fn)(
    void* context, int32_t* caller_level, int32_t* callee_level, int32_t* callee_level_offset);

enum {
    ODH_EXPRESSION_OK = 0,
    ODH_EXPRESSION_INVALID_ARGUMENT = 1,
    ODH_EXPRESSION_CALLBACK_FAILED = 2,
    ODH_EXPRESSION_UNKNOWN_OPERATOR = 3,
    ODH_EXPRESSION_OVERFLOW = 4,
    ODH_EXPRESSION_INTERNAL = 5
};

ODH_SIM_API uint32_t ODH_SIM_CALL odh_expression_abi_version(void);

/* Execute TS1 Expression operators 0..20, never TSO list operations.
 * operand is exactly 8 bytes: LE int16 lhs_index, LE int16 rhs_index,
 * uint8 is_signed (preserved but unused by the reference executor), uint8 op,
 * uint8 lhs_scope, uint8 rhs_scope. read/write and goto_true are required.
 * goto_true is reset to 0 on entry and becomes 0/1 on success; it is a boolean,
 * NOT the numeric VMPrimitiveExitCode (whose GOTO_TRUE is zero).
 * Callbacks return zero on success. accepted is observed only for Assign,
 * SetFlag, ClearFlag and AssignSqrtRHS; it must then be 0 or 1. Other operators
 * ignore that boolean exactly as the reference executor does.
 * levels may be null: it is required/called lazily ONLY for op LessThan(1) or
 * GreaterThanOrEqualTo(14), lhs scope MyObject(3), lhs index Room(29), RHS1024,
 * AFTER both memory reads. Read/write errors preserve all preceding effects;
 * there is no rollback (Inc/Dec write lhs before reading rhs).
 * Context may be null; callbacks/context/operand are borrowed only until return.
 * Callbacks must not throw across the C ABI. Reentrant calls are permitted;
 * the executor allocates no heap memory and retains no state or callbacks.
 */
ODH_SIM_API int32_t ODH_SIM_CALL odh_expression_execute_ts1_v1(
    const uint8_t* operand, uint32_t operand_length, void* context,
    odh_expression_read_fn read, odh_expression_write_fn write,
    odh_expression_levels_fn levels, uint32_t* goto_true);

#ifdef __cplusplus
}
#endif

#endif
