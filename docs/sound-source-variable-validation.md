# Sound source variables from the Simitone TS1 integration

PlaySound now captures owned source variables from its selected World entity
before calling the resolver/backend. This bridges entity data to offline HIT
selection. It does not run a sound thread, resume saved Main or submit Linux audio.

## Reference and bounded contract

Primary reference: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, using its
FreeSO submodule `f86bbd34b2112296ed786741f640a0535886d180` (MPL-2.0). Paths are
relative to the submodule's `TSOClient/`:

- `tso.simantics/Primitives/VMPlaySound.cs:30–55`: the owner submits HIT variables.
- `tso.simantics/Entities/VMAvatar.cs:169–190`:29 fields from PersonData.
- `tso.simantics/Entities/VMEntity.cs:420–427`: attributes prefixed by zero.
- `tso.simantics/Entities/VMEntity.cs:402–406`: TickSounds refreshes avatar data
  when the sound accepts that owner's volume update.
- `tso.sound/HITInterpreter.cs:680–692` / `HITThread.cs:456–483`: this reference
  reads ObjectVar for GetSrcDataField, ignoring the source register value.

`sound_source_variables(Entity, field_limit=256)` returns an owned vector with
a named reference profile and entity generation. Generation identifies an
incarnation, not a bank revision. Incomplete avatar banks and over-budget object
banks return no snapshot; no avatar fields are fabricated or attributes truncated.

| HIT field | Avatar PersonData and transformation |
|---|---|
| 0 | age58: child2 if age<18 and age!=0; otherwise raw gender65 |
| 2 / 5 / 7 / 11 | cooking10 / creativity15 / mechanical12 / body17, divided by10, upper cap100 |
| 14 | raw age58 |
| 15–20 | nice2 / active3 / generous4 / playful5 / outgoing6 / neat7, divided by10 |
| remaining | zero in the reference29-field initialization |

Signed division truncates toward zero. Skills have an upper cap only;
personalities are not capped. Gender bitfields are retained. Age0 uses gender;
negative ages follow the exact reference child predicate. Avatars require
PersonData through65 and output budget>=29. Objects produce
`[0, attribute0, attribute1, ...]`: default limit permits255 attributes, and
an empty bank produces one zero. Unsupported entity kinds return no snapshot.

`SoundRequest::source_variables` reaches sink and observer. Flag2 selects
StackObject, otherwise Caller. Capture before callbacks prevents resolver
removal from invalidating this copy. Later bank mutation does not alter old
requests; new submissions refresh them. Missing snapshots leave the existing
nonblocking return unchanged and do not establish a usable HIT source. A future
thread must handle absence, generation changes and ongoing refresh explicitly.

Default field payload is bounded to1KiB/request. The CLI's4096 retained-request
limit bounds this additional int32 payload to4MiB, excluding container overhead
and existing event-name storage. This is logical accounting, not measured RSS
or New3DS validation; a future platform policy can reduce field_limit.

## Checks and native-resource fixture

Sound tests pass400 checks (previously357). Additions cover every mapped field
and zero slot, signed extremes/truncation/caps, age boundaries and pet bits,
immutability, exact/default budgets, incomplete banks, both owner flags, backend
delivery, refreshed submissions, ID reuse and removal inside the resolver.
Full Debug and ASAN/UBSAN suites each configure96 tests:95 pass, with one X11
backend skip in the sandbox. Builds have no warnings. LSan is disabled in this
environment; leak validation is not claimed.

An ignored C++ verifier imports the88-word actor bank from
`reports/person-animation-ticks256-debug.json`, SHA256
`eba41a42b9397e52c6bf869997d27afe3e8ed85ac15878bdd06a24359c8191b6`.
That receipt is `ODH_native_BHAV_constructed_person_host`: imported neighbor
PersonData in a constructed actor, not original execution or resumed saved
simulation. It selects real FWAV355 in Global.iff/Global.far, invokes one
constructed PlaySound frame and passes the captured29 fields to the explicit
literal32 offline HIT profile with choices0,0:

```text
[0,0,30,0,0,0,0,20,0,0,0,0,0,0,27,60,40,0,60,60,30,0,0,0,0,0,0,0,0]
```

An independent Python mapping agrees on all fields. `hungry_sated_vox` selects
patch3212, NoteOnPC81,19 instructions/two choices. The callback returns false;
`audio_submitted=false`. No acceptance/playback is claimed. Debug/ASAN output
is byte-identical, SHA256
`8c2774f846d6bd59057ca01c6127b79ab47565baf532b246e82fdb43dc74e0ed`.

Ignored artifacts: `reports/sound-vars-native.cpp`, verifier binaries,
`sound-vars-native-{debug,asan}.json`, `sound-vars-native-bank.json`,
`sound-vars-input-identities.json`, build/full-test logs. External input hashes:

| Resource | SHA256 |
|---|---|
| Global.far | `25fe73945f89f82b1f49d2c2f53eb7c4bc31db1bc1dd575b1778a8cf72aaca59` |
| generated HSM | `ca6bef035809b2cdacdc2c14a98582cd4c4702c53cc6640beceaa548c06d55b6` |
| generated HOT | `cf324a77668c04f15ec56e78e905e0b8b3219807ce959c60bca68b9e81afb21d` |
| generated HIT | `a2d23b96490b6a0b8f29f61931c4fc7a5b66fa012e43bdb23cdd0c0356353c85` |

## Static binary follow-up and remaining work

Read-only Ghidra investigation continues on the distinct supplied `Sims.exe`,
SHA256 `6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
The operator-reported original `.bak` remains a separate self-modifying input;
its post-transformation image and equivalence remain unproven.

TrackData callback VA `0x0057b109` calls `0x0057bb98`, whose call site
`0x0057bc3c` invokes `0x0057d169`. That function looks up/inserts an ID in the
map at instance+`0x168`, then assigns instance+`0xa4` plus the supplied offset.
`0x00582457` searches a chained hash bucket and inserts absent keys via
`0x005825c5`; its value is copied from the supplied pair. Jump lookup
`0x0057d0a9` supplies initial value zero, so this inspected path inserts/returns
zero for absence. TrackData assignment overwrites existing IDs. ODH retains
explicit failure for missing/repeated mappings; full load order and base-pointer
initialization remain pending. Pseudocode was checked against instruction exports.

Opcode72 reads both source and field values at VAs `0x0057f608`/`0x0057f615`,
then forwards them and a result pointer at `0x0057f623`–`0x0057f62a` to wrapper
`0x0057c0e7`, which invokes its instance+`0x100` callback. Instruction exports
and objdump confirm these paths; the installed callback and original field20
are still unidentified. All addresses here are VAs; in this supplied PE's
`.text`, RVA/file offset equal VA minus `0x00400000`, per its section layout.

Opcode69 reads its register operand before selecting a sequence group in this
binary; the offline reference profile records an unapplied raw-byte request.
Thus the literal-Jump probe remains a combination of bounded reference semantics
and one verified operand rule. Do not apply its sequence-group effects to live
sounds before resolving that contract.

Follow-up: [the registered source callback](hit-source-callback-validation.md)
is now identified in the distinct supplied executable. ODH has an explicit
source-field reader; reference snapshots above remain Simitone semantics.
Register initialization and full field context are still pending. Next steps:
sound-thread lifecycle/WaitSamp and live refresh, then connect XA to Linux audio.
Full variable-length object HIT integration, saved threads/clock continuation, continuous
lot simulation and original comparison remain required. No dummy backend is
registered to bypass Main in this checkpoint.
