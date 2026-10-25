# Native shared wake queue

The actual FreeSO `VMScheduler` calls this bridge to the C++ `odh_simulation`
queue. There is no alternate managed scheduling implementation at runtime.
The adapter owns entity execution, thread wake metadata, presentation idle frames,
deferred deletion and RNG changes. Native code owns tick buckets, ordered lists,
current-list alias, insertion/removal decisions and queue resource accounting.

Build a platform-appropriate native library first, then pass an explicit absolute
`ODHNativeSimulationLibrary` property when building or publishing the host:

```sh
DOTNET_ROLL_FORWARD=Major dotnet run \
  --project managed/OpenDollhouse.Simulation.Tests -c Release \
  -p:ODHNativeSimulationLibrary=/absolute/path/to/libodh_simulation.so
```

Use the unversioned loader basename: `libodh_simulation.so`,
`libodh_simulation.dylib`, or `odh_simulation.dll`. The SDK copies this explicit
asset transitively to build and publish outputs. No platform path is guessed;
missing library, missing entry point and ABI mismatch fail without fallback.
The roll-forward setting above lets a net9.0 test executable run on the installed
.NET 10 host; it does not change the netstandard2.0/C# 7.3 bridge target.

## Identity, mutation and ownership

The boundary uses opaque queue handles, fixed-width primitives and Cdecl
callbacks. A monotonic nonzero `uint64` token identifies an entity instance, never
its numeric ObjectID. The managed registry uses reference identity. This matches
production `VMEntity`; equivalence for arbitrary types overriding equality is not
claimed. The ObjectID callback reads the entity's current signed 16-bit ID for
individual binary-search comparisons. Existing lists are never re-sorted after
ID mutation, so the same token can even acquire multiple list occurrences.

Native retain/release callbacks count actual list occurrences, including those in
multiple tick buckets and a detached current-list alias. The registry contains
strong references only while such occurrences exist. A candidate is registered
before a native call; an unused candidate is removed after deduplication or error.
A linked pending-release chain avoids allocations inside retain/release callbacks.
Selectors must be pure reads: they must not allocate or reenter the queue.
Selector exceptions are caught at the boundary and propagated after native return.

`CurrentCount` and `CurrentAt` read the current native alias every time. Reentrant
host execution can replace that alias, and the outer loop observes the replacement.
`Reset` and completion erase scheduled buckets while preserving the current alias;
a later lookup replaces it, including with null on a miss. Missing-current and
missing-future-bucket errors remain explicit. Completion receives the host's latest
mutable tick ID after callbacks. No native queue pointer or identity token is saved:
the existing VM load path resets the queue, schedules restored entities at tick 1,
and migrates those entries when the restored clock begins.

A `SafeHandle` owns the native queue, callback delegates and registry through
native destruction. No global callback closure or GCHandle roots the VM. Replaced
unreachable schedulers can therefore finalize even when entities refer back to the
VM/scheduler. `Dispose` releases deterministically. A nonblocking operation gate
allows serialized main/worker/main handoff used by asynchronous VM restoration,
while rejecting overlapping operations and callback reentry.

## Resource bounds and diagnostics

The FSO host declares safety ceilings of 65,536 buckets, 1,048,576 list occurrences
and 64 MiB of native queue allocations. Capacity failure throws explicitly; entries
are not evicted. These are host ceilings, **not** console budgets or expected usage.
Native bytes exclude managed storage, allocator overhead and stack. Managed live
registry entries are bounded by live native occurrences, plus at most one candidate
during an operation. Dictionary backing capacity and callback/handle overhead are
additional managed memory; registry dictionaries retain their allocated capacity
until the queue is collected. `PeakRegisteredTokens` includes temporary candidates.

With `ODH_VM_DIAGNOSTICS=1`, the real scheduler emits best-effort structural JSON
prefixed `ODH native wake queue: ` after its first completed frame that ticks an
entity, then each 256 completed frames. It reports ABI, tick, processed count,
native live/peak bucket/entry/byte counts and managed retained/peak registered token
counts. Diagnostic IO failure cannot fail a tick.

## Validation and limits

Tests compile the unchanged original scheduler oracle and the actual adapted
scheduler with synthetic VM entities. The gate covers 19 differential scenarios,
including live insertion, signed mutable IDs, duplicate occurrences, ID reuse,
stale aliases, reentrant replacement/missing lookup and reset/tick-1 save rebuild.
Another 13 groups cover ownership, candidate cleanup, resource errors, callback
failures, worker-thread handoff, diagnostic failure, 64 collectible VM cycles and
128 explicit native destructions. Only the unrelated VM API is synthetic; the
scheduler source under test is the real adapted source.

The bridge requires a managed runtime with native interop and reverse callbacks.
Its target framework does not establish a CLR on a console. Native cross-build,
console execution, full simulation extraction and game behavior remain separate
gates. See [the platform decision](../../docs/portability.md).

## Provenance and license

The queue contract derives from FreeSO revision
`7e76c1960252ddd033d8937d5edb47d326f5b69c`, especially
`TSOClient/tso.simantics/Engine/VMScheduler.cs` and `VM.cs: AddToObjList`.
Derived code is covered by MPL-2.0; see [LICENSE.FreeSO.md](../LICENSE.FreeSO.md).
`BaselineVMScheduler.cs` retains the original algorithm with a namespace rename
and import for the synthetic VM types. These tests establish that contract,
not equivalence to every behavior of the original The Sims executable.
