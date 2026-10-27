# Supplied comparison sample: property liquidation static audit

This is static evidence from the supplied comparison sample, not a claim about a pristine original executable or an observed gameplay run. No executable was run or unpacked. No copied decompilation or binary body is included in this summary.

## Identity and scope

- Input: `../referencias/The Sims/Sims.exe`.
- SHA-256 before and after: `6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
- Analyzed program `/Sims.exe`, x86 little endian 32-bit, image base `00400000`. Embedded program SHA matches the input.
- Ghidra 12.1.3 queried using `-readOnly -noanalysis -process Sims.exe`, at most two CPUs and 2 GiB heap. Script decompilation was bounded to 20 seconds per selected function. All new scripts, logs, settings, temporary directories and local inspection output are under this report directory.
- No production edits, builds, tests, reference checkout updates or forced project locks.

## Findings supported by the comparison sample

1. The ASCII `evict lot` string is at `006c26d0`, with references in `004273c8` and `004278ea`. The neighborhood eviction UI resource string is at `006cdd48`.
2. UI handler `00552c63` distinguishes eviction from demolition. Eviction reaches wrapper `004e2b07` and implementation `004273c8` with the second argument zero; demolition supplies one. Additional UI handlers reference the same resource, but were not treated as separate rules.
3. Normal eviction iterates lot entities and calls predicate `0043557a`, then deletion helper `0044127f` for qualifying entities. It does not delete every entity unconditionally. The predicate excludes entities whose flag word at entity offset `0xa0` has bit `0x10`; otherwise it accepts type-2 entities, or objects whose effective definition field `0x8a` is zero. The later bit-`0x8` check does not alter this truth table after the initial bit-`0x10` exclusion.
4. The pinned FreeSO OBJD layout puts ObjectType at byte offset `0x12` and BuildModeType at `0x8a`, matching these accesses. Its movement enum names bit `0x10` StaysAfterEvict. This is corroborating layout/semantic evidence, not a complete recovered comparison-engine C++ structure or proof of every possible engine version. The predicate therefore supports preserving build-mode objects and explicitly retained objects while removing ordinary buy objects and resident actors. Do not substitute an indiscriminate deletion of every non-family object.
5. Demolition takes an additional branch with object/location predicates and architecture clearing; its exceptions should not be reused as the normal eviction rule without further study.
6. Family record offset `0x110` is the occupied-house field in this path. Eviction clears it before the lot reload; on relevant failure it restores the house. At `00427678`, current runtime cash is copied into family offset `0x118`. Later `004276be` calls `00410304`, which forwards the family to `0042730f`; its result overwrites offset `0x118`, then `004276c9` clears offset `0x11c`.
7. `0042730f` returns the sum of family offsets `0x118` and `0x11c` when that family is not the currently active occupied family. Thus eviction adds a cached asset amount once and clears that cache. This corroborates the Budget/ValueInArch interpretation used by pinned Simitone, while showing that a permanently zero cache would omit assets represented in this sample.
8. Neighborhood-save function `00425316` refreshes family offset `0x11c` when its occupied-house field is nonzero and matches the loaded-house identifier. The value is the integer conversion of a multiplier times runtime lot-stat offset `0xa4`, plus runtime stat offsets `0x9c` and `0xa0`.
9. `0042730f` uses the same live calculation, plus runtime cash, for a family still occupying the active house. The architecture multiplier at `006e4a38` is runtime initialized; its on-disk zero is not its runtime policy. Initializer `00424260` reads the named setting `architecture_value_multiplier`, with float32 default at `00666cd4` equal to `0.699999988079071` (approximately 0.7). A configured override remains possible. The exact names, update paths and rounding behavior of every live lot-stat accumulator were not independently reconstructed here.
10. `004286d1`, reached after eviction, invokes house serialization function `00416153`; that function contains the `HouseTemp.iff` path and house/lot chunk serialization calls. This supports writing the filtered lot, rather than merely changing neighborhood occupancy. Full failure atomicity and all writer side effects were not audited.

## Native implications and remaining gates

Keep same-occupancy save/resume persistence separate from eviction. The comparison sample supports both a financial asset refund and physical filtering: ordinary buy objects are removed, while architecture/build objects and a retained-object class remain. It does not support carrying all furniture to another house or blindly preserving the entire old lot after sale. Pinned Simitone's zero-ValueInArch profile is a partial intermediate profile, not evidence for zero original refund.

Before claiming original parity, verify the native mapping of saved movement flags and effective master/build-mode OBJD data, the exact valuation accumulator contents and their update timing, deletion behavior for held or contained objects, house serialization failure handling, and neighborhood transaction ordering. A disposable isolated original before/after experiment remains pending; no original runtime experiment was performed by this audit.

## Integrity and publication boundary

`integrity.json`, `project-before.json`, and `project-after.json` record verification. The executable SHA and all eight existing project file hashes/sizes are unchanged. Despite `-readOnly`, Ghidra refreshed mtimes for `idata/~index.dat`, `idata/~index.bak`, `versioned/~index.dat`, and `versioned/~index.bak`. No attempt was made to restore those timestamps. Therefore project content is identical, but project metadata is not byte-for-byte filesystem-state identical.

The local `scan`, `helpers`, `values`, `initializer`, and instruction inspection outputs are private audit intermediates containing code representations; do not publish or commit those bodies. This metadata/semantic summary is the shareable finding. One optional inspection script initially failed to compile due to an unavailable memory API; a corrected bounded retry completed. No result from the failed invocation was used.
