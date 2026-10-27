# Native household seated continuation

The C++ household session can save several residents after completed Sit actions,
reopen in a fresh process, and let each resident Stand independently. Seated and
standing residents can coexist in the same saved household. Input selection does
not determine which resident owns a seat binding.

## Portable save and resource contract

The required SEAT section uses version 3 for a bounded list ordered by actor ID.
Each entry retains the existing version-2 identity, reciprocal containment,
completed Sit boundary, exact object definition, linked semiglobal provenance,
and Sit/Stand closure fingerprints. Legacy version 1 and 2 singular records
remain readable through the existing single-resident restore profile. A singular
record cannot coexist with the household list. Controller codec v5 is unchanged.

Household records require the complete HOST/HHRD roster and GEXP execution data.
Every seated runtime actor needs exactly one record; standing actors have none.
The matching actor generation, target generation, position, posture, containing
object and occupant index must agree. Duplicate, unordered, missing and stale
records are rejected. The currently supported boundary has an idle interaction
queue after a successful Sit; it does not cover an unfinished Sit or Stand.

The household uses its existing per-resident resource manifest for executable
banks. SEAT has no second copy of legacy closure owners. Resource identity is
checked against the seat's exact object source, asset, definition, tree table and
semiglobal link. The loaded-bank fingerprint and the Sit/Stand closure fingerprints
serve different purposes: a complete bank may contain more than one action.
All banks must be installed into the correct fresh resident VM before its seat
verification. Every seated resident then verifies the exact Sit and Stand code.
Playback and VM execution values are restored only after all resident/VM proofs
are present. Partial proof or proof for a different VM cannot publish a session.

The host captures completed seat bindings for every resident, restores those
bindings before input, and retains each actor's own resources and queue identity.
The integrated diagnostic option `--headless-resident-stand ACTOR TICK` accepts
up to eight distinct saved seated residents, with positive nondecreasing ticks
relative to the loaded session. Equal ticks preserve argument order. Other
scripted action schedules cannot be mixed with that diagnostic sequence.
Normal window controls continue using the selected resident's own seat.

Stand outcome tracking uses actor/generation/Main run/native UID. Another
completion cannot supply its result. Disappearance without a matching completion
is reported as dropped, without a fabricated false result. Pending records survive
history trimming. Resource/tuning registration failure prevents submission.

## Animation metadata encountered by the real scenario

House05 continuation exposed a separate animation fault in resident 16: the
original clip contains `foo` metadata. Pinned Simitone/FreeSO revision
`7e76c1960252ddd033d8937d5edb47d326f5b69c`,
`TSOClient/tso.simantics/Entities/VMAvatar.cs:443–506`, queries only `xevt`,
`righthand`, `lefthand`, `sound`, `dress` and `undress`; other keys have no effect.
Its animation tick consumes the event in file order in both directions.
The native player now keeps unknown metadata within the existing property/event
budgets and diagnostics, while applying only the implemented keys. At that checkpoint, known missing
services `sound`, `dress` and `undress` still failed explicitly. The later
[native session audio](native-session-audio.md) adds the optional sound side
effect; `dress` and `undress` accessory services remain incomplete. Tests verify metadata alongside real xevt
and hand changes, and rejection of each known missing service.

## Validation scope

Evidence is native Linux execution with copied user profiles and external game
resources. It does not establish original-game parity, a final interface, console
support, hardware behavior, arbitrary postures, or saves during all interactions.


## Verified checkpoint

Final executable SHA256:
`249988e4724a8a65bbde0b67db05b25d303a17aa7ec76d6c2388dbfed4eea6f3`.

- an archived validation record (not distributed) is the final host
  link and playback-test build after the metadata fix. `final-focused-build.log`
  and `final-20-ctest.log` record the rebuilt focused targets and 20 passing CTests.
  The earlier `build.log` built the host but stopped on a nonexistent extra target;
  the first core test build exposed a misplaced test brace, repaired before the
  passing runs. Intermediate reports are not final-binary evidence.
- an archived validation record (not distributed): twelve fresh
  processes exercise House07 Sims 105/137 with chairs 128/127. Both Sit requests
  complete true before saving at tick1024. Zero-tick reopen preserves all 13
  sections. Both Stand requests at relative tick1 succeed independently, leaving
  posture0 and no attachment. Splitting that run at tick1536 and continuing to
  tick2048 matches an uninterrupted loaded run in all 12 sections and actor,
  global, RNG and tick fields. Each mixed case first stands one Sim, reopens with
  all 13 sections equal while the other remains seated, then stands the other.
- `linked-final/result.json`: three processes exercise the full House05 household,
  Sit21/bench173, zero-tick reopen with all 12 sections identical, and successful
  Stand21 through tick2048. This uses the exact linked semiglobal seat resources.
  Before the metadata fix, `linked-1/` correctly recorded the unrelated resident16
  `foo` animation fault and refused to overwrite the prior save.
- an archived validation record (not distributed): six
  processes repeat legacy single-resident House05 bench173 and House07 chair128
  Sit/save/reopen/Stand, with byte-identical zero-tick archives and final posture0
  without attachment. These preserve the singular save profile.
- `rejections-final/result.json`: five malformed diagnostic sequences are rejected
  with exit2: absent saved seat, repeated actor, backward ticks, mixed schedules,
  and more than eight actors.

Every final report names the executable hash above. All 21 successful game
processes and five parser rejection processes ended; their temporary profiles
were removed. House05/legacy gates verified 26 original file hashes; House07
verified its 25-file set. No virtual display was created for these gates.
Independent review covered code, source semantics and final report inspection;
it did not repeat the test runs.
