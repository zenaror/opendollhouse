# Native TS1 temporary register state

`odh_thread_state.h` defines a C ABI for native TEMP and TempXL storage. This is
an incremental C++ port component. It has no dependency on C#, CLR, managed
arrays, entities, graphics, or platform services.

A stable reference-counted thread holder has two independently replaceable
bank references: exactly 20 signed 16-bit TEMP registers and exactly two signed
32-bit TempXL registers. The C++ sizes are shared with the native VM through
`include/odh/temporary_registers.hpp`. Reads never create a missing bank. TEMP
writes retain the low 16 bits as a signed value; indices are checked before any
access, including both TempByTemp indices. The ABI reports explicit failures
for missing banks, invalid inputs, allocation failure and exhausted refcounts.

Create, retain, clone and successful get each grant one owned reference. Bind
retains the replacement before releasing the old domain; binding null clears
only that domain. A retained view and another holder sharing the old bank remain
valid after replacement or holder destruction. Clone copies one domain into an
independent bank. Imports and exports require the exact typed count and validate
before copying, so invalid calls do not partially change storage. Arrays passed
to copy calls are borrowed only for that call.

Lifetime counters are atomic. Bank contents and holder bindings require external
serialization when accessed from multiple threads. No internal mutex crosses
callbacks. Bank allocation sizes and reference counts can be inspected through
`odh_bank_info_v1`; there is no implicit pool, memory cap, eviction or off-screen
state removal.

## Expression integration

The original `odh_expression_execute_ts1_v1` ABI remains available. Its 21-operator
body is the same `Execution` used by `odh_expression_execute_ts1_state_v1`.
The new adapter implements native scopes 8 (TEMP), 11 (TempByTemp), 13
(StackObject TEMP), and 42 (TempXL). All other scopes use borrowed host memory
callbacks only when the operator actually needs them. A null current holder is
therefore usable for expressions that only access host scopes.

The current holder is retained until return. Scope 13 calls the stack-holder
resolver separately at every access and retains each distinct resolved holder
until return; an Inc/Dec can resolve three different holders. At most four
holders need pinning: the current holder and the three memory accesses performed
by one Expression. Every native access fetches its domain again, so replacement
inside a host callback is visible to the later setter. The adapter stores no
callback or host pointer after return and permits same-thread reentry.

Status 0 through 5 retain the original Expression meanings; 6 means native
bounds failure and 7 means unavailable native holder/domain. All seven required
outputs are zeroed on entry. Access counters count successful physical accesses;
a successful host setter returning false still counts, and a TempByTemp read
counts two native reads. Resolver attempts have their own counter. Negative
AssignSqrtRHS sets its diagnostic after the RHS read, even if its setter fails.
Earlier writes remain applied when a later access fails, including Inc/Dec.

## Validation and limits

The portable `tests/state_cases.hpp` suite provides `odh_state_run_cases()`
(zero on success) and `odh_state_last_check_count()`. It contains 76 checks for
bank sizes, copies, sharing, cloning, replacement, lifetime, bounds, all 21
operators, native/host counters, dynamic scope 13, callback reentry, room-level
callbacks, negative sqrt and failure side effects. It can be compiled without
platform APIs by desktop and ARM probes. `test_state_c_header.c` checks plain C
consumption and ownership after holder destruction.

On 2026-10-26 the standalone module passed all six CTests in a fresh Linux
Release build and a fresh AddressSanitizer + UndefinedBehaviorSanitizer build.
The previous Expression suite also passed its 792 checks. A separate strict
`-Wall -Wextra -Werror` native-state build passed. Local logs and build manifests
are in `reports/portability/native-state/`; those build products do not replace
an approved game runtime.

These are synthetic native-component results. They do not establish full-game
compatibility, household/save execution, original-game equivalence, console
runtime support, or hardware success. Native VM integration and console probe
results must be reported separately by their owners.
