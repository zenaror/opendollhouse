# Native wake queue

`odh_simulation` is a standalone C ABI adaptation of the wake-list algorithm in
the pinned FreeSO source at `refs/work/Simitone-linux/FreeSO/TSOClient/tso.simantics/Engine/VMScheduler.cs`
and `VM.AddToObjList` in `TSOClient/tso.simantics/VM.cs` (revision
`7e76c1960252ddd033d8937d5edb47d326f5b69c`). This adaptation
is covered by the FreeSO MPL-2.0 text in `managed/LICENSE.FreeSO.md`; it is not
claimed as an independent algorithm. It stores stable entity tokens, not entity pointers
or mutable ObjectIDs. The caller supplies a nonthrowing callback to read each
entity's current signed ObjectID during binary-search insertion, and numeric
retain/release callbacks for every actual list occurrence. It never sorts an
existing list after an ObjectID changes.

The queue owns tick buckets and one mutable current-list alias. The caller
re-reads `current_count` and `current_at` for every tick-loop iteration so a
nested tick can replace that list, just as the original scheduler does. `Reset`
and `Complete` remove scheduled buckets while retaining the current alias;
an absent `select_current` clears it. The managed VM still owns entity ticks,
delayed deletion, RNG, UI idle frames and tick arithmetic.

Creation specifies limits for distinct live lists, retained token occurrences,
and tracked queue allocations. `max_bytes` covers the queue object plus its
map nodes, list control blocks and vector storage. It excludes C++ allocator
metadata, caller buffers, stacks and the managed registry. A capacity failure
never silently drops a schedule request. `stats` reports live and peak usage.
The queue is single-thread owned. `max_buckets` counts distinct live lists,
including a detached current alias. `prepare_interrupt` removes a prior
occurrence and chooses a target; the caller's subsequent schedule is a separate
operation that can fail on a capacity limit. Callback retain/release must be
nonthrowing and allocation-free, including during destruction.

Build and test on a desktop host:

```sh
cmake -S native/simulation -B reports/portability/native-simulation/host-build -DCMAKE_BUILD_TYPE=Release
cmake --build reports/portability/native-simulation/host-build
ctest --test-dir reports/portability/native-simulation/host-build --output-on-failure
```

The shared library is named `odh_simulation`; `ODH_SIMULATION_STATIC` suppresses
export annotations for static targets. The API is not a serialized queue
format; existing save loading reconstructs scheduling through the adapter.
