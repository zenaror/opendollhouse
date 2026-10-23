# Person host primary animation execution

`odh-person` accepts repeated `--animation-source FAR` options and dispatches
opcode 44 through the existing primary Playback controller. The explicit FAR
catalog supplies owned animation headers. Queue and Sleep advance first, then
Playback delivers/advances its state before the host resumes the VM. Without
animation sources, the previous unsupported-opcode behavior and output schema
remain available.

## Resolution contract and references

The reference is local FreeSO revision
`4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0:

- `TSOClient/tso.simantics/Engine/VMMemory.cs:676–709`: Object uses CodeOwner,
  StackObject uses that object's resource, age below 18 selects the child table
  in TS1, and private fallback applies when the selected table is absent.
- `Engine/Scopes/VMAnimationScope.cs`: Object 0, Global 1, PersonStock 2,
  Misc 3, StackObject 65536.
- `Primitives/VMAnimateSim.cs`: IDFromParam remaps Object to StackObject;
  posture reset selects the avatar's stored walk animation.

ODH resolves Object/StackObject from the explicitly supplied owners 1 and 2:
OBJD AnimationTableID plus child offset, then STR# 129/130 fallback. An existing
table with a missing string does not fall back. Global uses STR# 128, PersonStock
130, and Misc 156 adult / 157 child. Selection uses explicit language 1 and the
internal animation name, with ambiguous catalog lookup rejected.

After body initialization, posture reset uses the initialized walk descriptor,
including its private overrides. For an explicit root probe without body
initialization, the supplied global STR# 150/151 provides the reset profile.
Playback retains its existing empty-hand, supported flags/property and bounded
frame/event contracts. Missing services fail explicitly; there is no animation
success stub or synthesized CFP/pose.

Aliasing shared ownership keeps the catalog record alive while Playback holds
its header. The catalog preserves identity across eviction. The silent host
does not decode CFP or bind a skeleton; this execution milestone establishes
VM wait/event/end progression, not animated pixels or audio.

The optional JSON reports animation progress, resolution diagnostics and timed
properties. Event logging is bounded by 10000 records and 256 KiB of logical
record/pair/string bytes. Sources are limited to 16, each source path to 4096
bytes, in addition to the catalog's resource budgets. Accounting is not RSS.

## Synthetic integration validation

### Integrated household posture and Snap, 2026-10-23

Each additional integrated household resident now derives an independent
`AvatarBodyDescriptor` from its pinned character OBJD BodySTR, private STR#150,
and age-selected global walk/swim tables. Derivation does not reinitialize the
saved World banks or consume RNG. Posture reset selects the descriptor by the
frame's Caller; an unavailable additional-resident descriptor remains an
explicit failure. Ordinary animation table selection also uses that caller's
derived adult/child choice. Integrated saved-world descriptors accept adult or
child BodySTR types; fresh constructor initialization retains its adult profile.
Descriptors are also prepared before household playback
continuation is restored. The separate second-avatar fixture retains its
existing restrictions.

Additional integrated residents dispatch opcode46 through the existing
SLOT-backed Snap handler. Controllers are cached by target object and origin VM,
preserving VM binding while sharing target occupancy in the World. Opcodes27/45
remain explicitly unsupported for additional residents. Existing SLOT geometry,
placement, stair and occupancy restrictions remain in force.

The reference consulted for this host integration is Simitone's FreeSO
submodule `f86bbd34b2112296ed786741f640a0535886d180` (MPL-2.0):
`Entities/VMAvatar.cs:364-389` stores per-avatar walk/swim strings with private
overrides, `Primitives/VMAnimateSim.cs:40` uses the caller avatar's walk string,
and `Primitives/VMSnap.cs:73-75` places the caller into the target's slot.

`linux_household_animation_snap_cli` generates two character resources and a
one-slot object entirely synthetically. With either resident selected as the
primary, timed clip properties prove independent private posture names,
adult/child global fallback selection, and ordinary Object animation selection
by descriptor age even when the saved PersonData age differs. The child fixture
has explicit child BodySTR type and age17. Missing resident BodySTR is rejected.
Both resident VMs Snap to the same SLOT-backed object: resident1 returns true,
resident2 returns false because the shared slot is occupied, without a VM
binding fault. Resident1 repeats the Snap successfully through its cached
controller. The CLI asserts the live target slot occupant and both resident
attachments, rather than deriving occupancy from return status alone.
The `snap` C++ test additionally binds two VMs to separate controllers for the
same target, checks occupied rejection without changing either relation or the
second resident's position, explicitly detaches the first resident through
World, and proves the same second VM then succeeds with the exact target slot,
attachment and position. Vacancy/retry is World/Snap evidence, not a household
host interaction or floor/stair Snap milestone.
Separate probes preserve both routing gates. This is headless
Linux host/VM evidence; it does not establish original-game equivalence,
commercial-lot completeness, rendered residents or console behavior.

`tests/person_animation_cli.py` generates only synthetic FAR/IFF/BCF resources.
It tests terminal completion, xevt/footstep observation, Local0 writes through
the false branch and same-clip reentry, primary loops, private owner fallback,
age boundary 17/18, Global/PersonStock/Misc, language selection, queued object2
CodeOwner inheritance, IDFromParam StackObject2 with CodeOwner1, normalized
posture reset, and missing/ambiguous resources. Invalid/missing CFP deliberately
proves that this host resolves headers without eager sample decoding. CLI
source bounds and legacy opt-in behavior are covered.

The full configured suite has 90 tests: 89 passed and the X11 backend was
skipped inside the sandbox in both Debug and ASAN/UBSAN. The new CLI
integration test passed in both builds, and build logs were warning-free.

## Native constructed actor validation, 2026-10-14

The probe uses User00000.iff with `--initialize --activate-family`, House07,
LotZoning, Neighborhood, seed 1, explicit PersonGlobals/Global, and two animation
sources: `GameData/Animation/Animation.far` and
`ExpansionShared/ExpansionShared.far`. This constructs actor 1. It does not
restore or restart saved actor 105 or 137, their frames, queues or clock.

Without the animation options, the same probe stops at 470 total instructions,
tick 2, PersonGlobals 8330 pc 9, unsupported opcode 44. Its operands request
Misc 281, primary blocking playback and Local0 event output.

With the catalog enabled:

| Tick budget | Result for the first 45-frame clip |
| --- | --- |
| 2 | Waiting, frame 0, `a2o-idle-neutral-trans-stand-lstand` |
| 3 | Waiting, frame 1.2 |
| 39 | Waiting, frame 44.4, end false |
| 40 | Waiting, frame 45.6, end true |
| 41 | VM has continued; next clip `a2o-idle-neutral-lstand-look-1a`, frame 0, 26 frames |

At the default 256-tick budget, the VM instead stops after 604 total
instructions / 213 ticks, PersonGlobals 8331 pc 36:
`animation property service not implemented: lefthand`. Host failure is false,
resolution diagnostic is empty, and RNG state is 2017197920513298031. It is a
coherent runtime unsupported-service result, not a completed Main. No timed
events occurred before this gate in the native probe; event delivery evidence
here comes from synthetic integration tests.

All six native outputs are byte-identical between Debug and ASAN/UBSAN. Seven
original input SHA256 values match before and after the probe. Evidence remains
ignored in `reports/person-animation-ticks*-{debug,asan}.json` and
`reports/person-animation-input-sha256.json`. Leak detection is disabled because
of the environment; no leak-check result is claimed. Neither native execution
here nor the FreeSO reference proves original-game equivalence or console use.

Next work includes faithful hand gesture properties and their rendered state,
CFP/rig sampling integration, saved frame/queue/clock continuation without
reinitialization, and connecting simulation to the interactive Linux lot.
