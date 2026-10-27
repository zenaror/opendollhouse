# Bounded host animation catalog

The host can now find an animation by its internal BCF name across explicitly
supplied FAR files, and locate its CFP by normalized basename. This connects
resources split between the base game and expansion archives without guessing
which duplicate resource takes precedence. `odh::host::AnimationCatalog` is a
host library; it is not yet attached to the person executor or saved scene.

## Resource and ownership contract

Files are seekable and read-only. ASCII case folding applies to internal names;
CFP matching additionally normalizes Windows/POSIX separators and takes the
basename. Provenance retains the supplied source path and complete FAR entry.
An ambiguous lookup rejects, even across sources. This deliberately leaves the
original game's resource precedence unresolved.

Only binary BCF with zero skeletons and zero appearances is indexed. Other BCF
profiles are counted as skipped. Malformed supported resources fail explicitly.
Headers and CFP planes load lazily. A missing CFP is visible on the header;
decoding it fails rather than inventing a pose.

Defaults bound sources to 16, entries to 8192 per FAR, cumulative names to 1 MiB,
index records to 2 MiB, live metadata to 1 MiB, live headers/cache records to 16,
and CFP samples to 1,048,576 (four bytes each). Parser limits also bound transient
decoding. Accounting covers logical string lengths and populated record sizes,
not allocator capacity, stream buffers or process RSS. These are not measurements
of New 3DS memory use.

Returned handles own their resources and may outlive the catalog. Eviction does
not release accounting for externally held resources. Exhaustion with pinned
handles rejects. A bounded weak registry preserves live header/decoded identity
after eviction, necessary because Playback uses header identity on reentry.
Registry records count toward the index budget and expire when their header
dies. The catalog is movable and not thread-safe.

## Validation on 2026-10-14

The synthetic executable reports 196 checks in Debug and ASAN/UBSAN. Tests cover
cross-source resolution, both separators, case folding, full provenance, missing
and ambiguous resources, exact budgets, pinned eviction, identity reuse after
eviction, repeated registry pruning, moves, handles surviving destruction,
truncated BCF and lazy malformed CFP rejection. Leak detection is disabled in
this environment; no leak-check result is claimed.

The native probe supplied `GameData/Animation/Animation.far` and
`ExpansionShared/ExpansionShared.far` from the external installation. It indexed
5630 FAR entries, 1275 supported BCF resources, 379 skipped BCF profiles, 3032
animation records and 3018 CFP entries. Initial logical names use 383615 bytes;
index records use 516768 bytes on this Linux build.

Resolving `a2o-idle-neutral-trans-stand-lstand` selects:

- BCF: `ExpansionShared.far`, `Animations\a2o-idle-neutral-lstart.cmx.bcf`.
- CFP: same FAR, `Animations\xskill-a2o-idle-neutral-trans-stand-Lstand.cfp`.

The active registry adds 40 logical bytes. One decoded resource charges 3947
metadata bytes and 5490 CFP samples. BCF and CFP both have zero remaining bytes.
All header fields and all seven raw CFP planes exactly match the prior explicit
entry CLI output in Debug and ASAN/UBSAN. The complete JSON is byte-identical:
SHA256 `baed4510ddbe60f0b193aa53d78e38d43defccade58c9761c3c6a4b379589132`.
This comparison checks catalog selection against an existing decoder; both use
the same ODH format parser, so it is not an independent CFP decoding oracle.
A separate Python FAR directory reader confirms 5630 entries and exactly one
matching CFP basename, preserving its full prefixed entry.

The full suite configured 89 tests: 88 passed and the X11 backend test was
skipped inside the sandbox in each build. The separate native X11 presentation
validation is recorded in `linux-saved-window-validation.md`; it does not
convert this sandbox skip into a full unsandboxed suite run. Both catalog build
logs were warning-free.

Ignored local evidence: an archived validation record (not distributed),
an archived validation record (not distributed), and an archived validation record (not distributed).
No commercial resources are versioned. This establishes host resource lookup
and owned decoding, not Main execution, animated rendering, original-game
equivalence or console execution. Next: connect the Misc STR resolver and
Playback ticks to the person host, then restore saved continuation without
reinitializing the saved actors.
