# Native TS1 expression execution

This is an execution step in the complete C/C++ engine migration. The Simitone
code is the behavioral oracle and a temporary host bridge; the final engine must
not depend on C#/.NET. This component implements all 21 TS1 Expression operators
from FreeSO `Primitives/VMExpression.cs`, revision
`7e76c1960252ddd033d8937d5edb47d326f5b69c`. TSO list operations are not included:
operator 18 is TS1 OR and 19 is TS1 XOR. Derived semantics are MPL-2.0; see
`managed/LICENSE.FreeSO.md`.

`odh_expression.h` adds two exports to `odh_simulation`, leaving wake ABI 1 and
its code unchanged. `odh_expression_abi_version()` returns 1. The synchronous
`odh_expression_execute_ts1_v1` borrows an exactly eight-byte operand and memory
callbacks for the duration of the call. It owns no mutable global state, retains
no callback and allocates no heap memory. Calls can reenter through callbacks.

Operand layout is little-endian signed 16-bit LHS index, signed 16-bit RHS index,
then four bytes: IsSigned, operator, LHS scope, RHS scope. As in the oracle,
IsSigned is not used by the executor. Scope resolution and host entities remain
temporary callback services; native code decides which reads/writes occur and
their ordering. Callback errors stop execution without undoing earlier effects.

Status and branch result are separate: status zero means success; `goto_true`
is boolean 0/1, **not** the numeric `VMPrimitiveExitCode`. It is zeroed on errors.
Status 1 means bad arguments, 2 callback failure, 3 unknown operator, 4 arithmetic
overflow and 5 internal failure. Context may be null; read/write callbacks and
the result pointer are required. A levels callback is optional except when the
Room special case actually needs it. Callback exceptions must not cross the ABI.

The native implementation preserves these source details:

- Inc/Dec read LHS, write it, then read RHS. Comparison still uses the local
  int32 value, even if the host setter narrows the stored value.
- Assignment, SetFlag, ClearFlag and Sqrt return the setter's boolean. Other
  modifying operators ignore that boolean while propagating callback errors.
- Add/subtract/multiply and the modulo intermediate sum wrap to signed 32 bits
  through unsigned arithmetic, with no C++ signed-overflow dependency.
- Division by zero writes -1. Modulo by zero writes the unchanged LHS. Both
  division and remainder of INT32_MIN by -1 report overflow before any write.
- Modulo performs both source remainders with the wrapping addition in between;
  it is not replaced with a mathematical positive-modulo formula.
- Flag shifts mask the count to five bits. IsFlagSet compares the signed
  bitwise result with zero, so selecting bit 31 can return false.
- Sqrt is floor square root for nonnegative int32 input, followed by signed
  low-16-bit conversion. Negative input writes zero. This matches the measured
  cold and warmed .NET 9.0.21/.NET 10.0.13 x64 oracle; the integer implementation
  avoids floating conversion/domain differences on the native targets.
- Only LessThan and GreaterThanOrEqualTo with MyObject scope 3, Room index 29
  and RHS value 1024 request caller/callee levels. That request occurs after
  both ordinary reads. LevelOffset is the host's unsigned ushort widened to
  int32, preserving values through 65535.

Host validation:

```sh
cmake -S native/simulation -B reports/portability/native-expression/host-build \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build reports/portability/native-expression/host-build
ctest --test-dir reports/portability/native-expression/host-build --output-on-failure
```

The C and C++ ABI tests run alongside the unchanged wake tests.
`tests/expression_cases.hpp` supplies the same allocation-free source suite to
desktop and ARM runners: `odh_expression_run_cases()` returns zero on success,
and `odh_expression_case_check_count()` reports its 792 checks. It covers all
operators, numerical edges, effect order, rejected writes, lazy levels, callback
errors/reentry, operand bounds, signed indices and unknown operators. Host tests
also pass AddressSanitizer/UndefinedBehaviorSanitizer with warnings as errors.
Evidence and hashes are under `reports/portability/native-expression/`.

These native tests do not establish full-game equivalence or hardware execution.
The integration gate must prove this implementation is called by actual BHAV
execution, preserve save/reopen state and replay the same source on the ARM target.
