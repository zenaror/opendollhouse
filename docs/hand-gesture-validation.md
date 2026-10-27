# Timed hand gestures and visual selection

Primary Playback now applies `lefthand` and `righthand` timed properties to
typed visual state: idle=0, pointing=1, fist=2, none=3. New primary clips and
posture reset set both sides idle only after resource validation succeeds.
Gesture state persists after clip completion/cancellation, and its accessor
rejects a stale VM run or removed/reused actor. Immutable Playback samples
include the current hands. PersonData22/23 and saved banks are not overwritten.

The renderer can retain all six hand variants and select the appropriate
meshes before skinning. `ScenePart::hand` marks side/gesture;
`SceneState::hands` selects each side, and none omits its tagged meshes.
Untagged parts retain the previous rendering behavior. ProbeView forwards
Playback's hand state, including the state retained after animation completion.
The constructed person host reports `animation_hands` when animation sources
are enabled.

## Reference contract

Pinned FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0,
was read locally:

- `tso.simantics/Entities/VMAvatar.cs:443–465`: timed xevt and right/left
  hand handling; hand values parse as shorts and update avatar visual state.
- `tso.vitaboy.model/Animation.cs:326–341`: keyed properties return the first
  matching value. ODH applies this rule to each hand key, before observers run.
- `tso.simantics/Primitives/VMAnimateSim.cs:52–53,118–119,136–137`:
  reset and newly started primary animations reset both gestures to idle.
- `tso.vitaboy.engine/SimAvatar.cs:179–216,253–256,494–500`: gesture enum,
  appearance selection, None omission, and the TS1 right-hand texture quirk.
- `tso.vitaboy.model/Outfit.cs:99–116,153–163`: STR# 17..22 map to left/right
  idle, left/right pointing, left/right fist. Appearance precedes the comma;
  texture follows the equals sign.

ODH accepts ASCII whitespace, optional sign and decimal values 0..3. Invalid
syntax or unsupported numeric values reject during clip preparation before
changing clip/gestures. This is an explicit bounded supported domain, not a
claim to reproduce all .NET integer parsing or undefined enum values.
Unknown property services, carrying and other previous limitations still fail.

`SavedAppearanceResources::resolve_gestures(person, body)` preserves body,
head, current/base animation evidence and resolves the six BodySTR hand
variants. It uses a separate bounded single-binding hand-record parser rather
than interpreting raw saved hand strings as a group. Missing variants reject
without idle substitution. Left pointing/fist use their own textures. Right
pointing/fist use the right idle texture while keeping their own appearance,
matching the pinned reference quirk. This remains unconfirmed against the
original executable. Raw saved records are preserved; a saved gesture is not
inferred from PersonData or an appearance name.

All variants count toward the existing 16-part, geometry and texture budgets.
State enum validation occurs before CFP decoding. Switching gestures does not
expand or evict the resident meshes/textures. Reported resource sizes are
logical decoded sizes, not RSS or console performance.

## Validation on 2026-10-14

Debug and ASAN/UBSAN pass 137 Playback checks, 90 saved-appearance checks, and
74 hand-renderer checks. Tests include timed changes, xevt coexistence,
unchanged raw banks, immutable samples, completion/cancellation persistence,
stale identity rejection, backwards timing, first-key selection, reset
atomicity, numeric syntax, six resource mappings, right texture selection,
missing/invalid groups, budgets, None omission and unchanged untagged behavior.
The renderer compares all 16 side combinations against untagged configurations
containing only the selected parts, pixel/depth and sample counts exactly.
This tests selection using the same rasterizer, not an independent raster oracle.
The full suite configured 91 tests: 90 passed, X11 skipped inside the sandbox in
both builds. Build logs are warning-free; leak detection is disabled here.

### Native Main progression

The same constructed User00000/House07 family probe from
`person-animation-validation.md`, seed 1 and two explicit animation FARs, now
passes the previous `lefthand` gate. At tick214 it is waiting in the 24-frame
`a2o-idle-neutral-trans-lstand-lhips`, frame1.2. Two real time-properties are
observed at ticks225/243, time466, both with left/right value2 (fist).
The 256-tick probe is still waiting: 665 total instructions, no runtime fault.
The 4096-budget probe reaches a new explicit fault after 669 total instructions
and 296 ticks: opcode23, PersonGlobals8219 pc27, operands
`[99,1,0,0,0,0,0,0]`. The reference registers opcode23 as `play_sound` in
`VMContext.cs:237–241`. Host failure is false, and RNG is
2017197920513298031. Main has not completed.

All three native JSON outputs are byte-identical Debug/ASAN. Seven original
input hashes match the preceding checkpoint before and after these probes.
Evidence: ignored an archived validation record (not distributed).
This constructs actor1; saved105/137 frames/queues/clock remain dormant, without
reinitialization. No animated Main pixels or audio device playback is claimed.

### Native appearance/render probe

The probe takes body/head and six hand strings from the validated constructed
actor's BodySTR descriptor and reads the original base Animation/Textures FARs.
It renders bind pose with no CFP, independently of Main. The eight resident
parts contain 684 vertices, 840 triangles and 425984 RGBA texture bytes.
Six resolved variants are:

| Side/gesture | Appearance | Texture |
| --- | --- | --- |
| Left idle | hmlo | HUAOlgt.bmp |
| Right idle | hmro | HUAOlgt.bmp |
| Left pointing | hmlp | HUAPlgt.bmp |
| Right pointing | hmrp | HUAOlgt.bmp |
| Left fist | hmlc | HUAClgt.bmp |
| Right fist | hmrc | HUAOlgt.bmp |

All 16 native 320x240 frame selections match their untagged subset controls
in RGB and depth. Four parts render with both hands visible, three with one
None, two with both None. Idle/idle shades6754 samples; None/None shades6573.
The complete ordered RGB/depth dump matches Debug/ASAN byte-for-byte, SHA256
`69aa627ad1dd55dfd884f8648acfbadc8715753cea2a059b39e8fb5fc509c4fa`.
Idle/None PNG exports were visually inspected; both show the complete avatar,
with hand meshes absent in the latter. Textures FAR SHA256 remains
`d4848bdf01c8e4e01067204d4a5c89011948c7ce7c1bf97b39202eafd002993e`.
Ignored native helper/logs/dumps are an archived validation record (not distributed); commercial
resources/images are not versioned.

Next: resolve the actual sound service and its resource/backend contract;
connect resource catalogs, CFP/rig sampling and these graphics states to the
interactive Linux lot; restore saved continuation without reinitialization.
