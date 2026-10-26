# Native runtime-created object visuals

The saved-lot renderer can now bind artwork for supported objects created by
the live native runtime and position objects held in avatar slots using the
same animated pose that draws the avatar. Rendering borrows the current World;
this work does not change World state, VM execution, gameplay placement or save
authority to make an object visible.

## Runtime artwork identity

`SavedLotRenderer::set_runtime_objects` stages a replacement binding map before
publishing it. Each binding supplies runtime ID, generation, GUID and exact
resource provenance: source, asset and OBJD chunk ID. Validation requires a
live non-ghost object, the matching World generation/GUID, a unique indexed
definition and the supported singleton object profile. Duplicate IDs, resource
ambiguity and malformed identities reject the complete replacement.

Bindings are bounded to4096 objects and256KiB of resource names. A new runtime
binding shadows a saved row with the same numeric ID after host-verified
removal/reuse; the old row's generation and terrain-group metadata cannot
authorize or position the new object. Removed objects stop drawing through
live identity checks. A reused ID requires a new explicit binding. Restoring a
current session rebuilds the bindings from its authenticated GEXP creation
origins rather than guessing artwork from an old saved row.

Runtime-created sprite flags, logical position, graphic, direction and
visibility are read from the live World for each capture. Original saved
dynamic sprite flags remain record snapshots; this feature does not claim to
make every imported rendering field live. Pixel picks are validated against
the current entity generation, GUID and visibility, so stale artwork or old
pixel ownership cannot select a replacement object merely sharing its ID.

## Carry pose and slot anchors

The native pose sampler applies the ordinary animation first and an optional
carry clip second. The supported carry layer samples frame0 with weight1 and
overrides only enabled local translation/rotation channels. Bones or channels
not supplied by the carry clip retain the ordinary pose. The final absolute
bone positions are computed after those layers.

`SceneRenderer::render` can return `ScenePoseAnchors` from that exact pose.
Slots0,1,2 resolve R_FINGER0, HEAD and PELVIS respectively. Missing anchors stay
absent and duplicate matching bones reject. With final model-space bone
position b, avatar facing theta, visual tile position P and supported scale1,
the reference-equivalent anchor is:

```text
h = b / 3
slot = RotateZ((h.x, h.z, h.y), theta + pi) + P - (0.5, 0.5, 0)
```

The native renderer's model Y rotation by pi minus theta, followed by the
existing axis permutation and division by3, yields that same coordinate
transform. The hand is not approximated by the avatar center, a fixed height
or the rest-pose hand. Bone orientation is not inherited by the held sprite;
the child retains its own object direction.

The host wraps each supplied anchor in `SavedLotAvatarSlotAnchor`, including
actor ID, generation, current logical position and slot. The renderer checks
that identity, the reciprocal parent/child slot link and finite visual
coordinates before resolving a held object. Anchors are already in the
terrain-adjusted visual coordinate system used to render that avatar; terrain
height must not be added a second time.

The setter is atomic and bounded to64 avatars times3 slots. ID, generation and
logical position catch stale/reused actors and movement, but do not by
themselves encode an animation timestamp. The host must refresh anchors from
every relevant avatar's current rendered pose on every capture, including
captures where only animation changes. Reusing an anchor just because the
logical position stayed constant violates this contract.

## Host capture path

`person.cpp` samples the primary resident and every supported additional
resident with its own ordinary animation, carry clip, direction and visual
position. Each resident's frame and exported anchors come from the same
`SceneRenderer` invocation. The host then refreshes live runtime artwork
bindings and the full anchor set before saved-lot composition and picking.
This path is shared by window captures and diagnostic/status frame captures.

The existing depth compositor remains responsible for occlusion. Held objects
use their actual animated anchor, ordinary object sprite direction and depth;
they are not assigned an unconditional front layer. Renderer caches and
resource bindings may change, but the capture path does not mutate simulation
position, attachments, animation playback or VM state.

## Pinned source basis

The references below are committed FreeSO
`7e76c1960252ddd033d8937d5edb47d326f5b69c`, the core revision used by Simitone
`e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab`. No different upstream revision is
used as authority for these transforms.

- `TSOClient/tso.simantics/Entities/VMAvatar.cs:1085-1117` attaches the child,
  starts the default right-arm carry clip for slot0, sets its visual container
  and makes contained objects dynamic. Its logical position follows the actor.
- `VMAvatar.cs:600-604` checks carry status without advancing its frame.
  `Model/VMAnimationState.cs:20-30` initializes a new forward state at frame0.
  This does not make imported nonzero carry frames equivalent to frame0.
- `VMAvatar.cs:739-746` applies ordinary animation, then carry at fraction0 and
  weight1, before recomputing the skeleton. `tso.vitaboy.engine/Animator.cs:93-135`
  changes only motion-enabled local channels; disabled channels remain intact.
- `tso.vitaboy.model/Skeleton.cs:212-219` defines AbsolutePosition as the
  transformed origin of AbsoluteMatrix. The position is sufficient for the
  slot translation; the full bone orientation is not applied to the object.
- `tso.world/Components/AvatarComponent.cs:36-46` supplies the slot bone names
  and coordinate transform, including scale and the half-tile offset.
- `Components/EntityComponent.cs:103-113` resolves contained visual position
  dynamically. `ObjectComponent.cs:504-522` refreshes the DGRP position while
  drawing a contained object; its own direction remains the DGRP direction.
- `tso.world/World.cs:721-722` draws avatar and object passes; `WorldEntities.cs:125-144`
  uses the Z-sprite depth channel and normal dynamic-object ordering. The
  avatar-container exclusion near ObjectComponent line636 is in commented
  shadow code, not an active rule to hide held objects.

These are behavioral anchors, not copied reference implementation bodies or
proof of original-executable equivalence.

## Selected Sim continuity

The graphical regression also exposed a separate persisted-UI gap. Scheduling
the snack for actor105 selected that Sim, but reopening restored the primary
runtime actor137 as the UI selection. Gameplay sections and world artwork
matched;95 numeric-HUD pixels differed. Cropping the HUD would hide a real
resume difference.

Optional UIST v1 stores selected avatar ID, generation and GUID in16 bytes.
The codec requires a live non-ghost avatar with exact household identity;
legacy single-resident state must name HOST's resident. It does not replace
HOST's primary VM identity. The host applies selection after execution restore,
before UI rendering, and saves it when non-primary or when a loaded UIST exists.
Older/default-primary saves keep their absent UIST and zero-tick byte shape.
Old readers may ignore this optional section.

## Supported profile and verified evidence

The current profile supports explicitly bound singleton runtime objects,
native newly created carry state at frame0 and avatar scale1. Imported
nonzero carry frames, non-unit avatar scaling, head-seek or other unsupported
partial-pose layers, ambiguous/missing slot bones and unsupported container
chains are outside that claim. Missing pose evidence must reject rather than
fall back to an arbitrary center anchor. General object placement, dropping
held children during eviction and original saved-flag mutation remain separate
features.

Final installed-data evidence is
`reports/native-runtime-object-visuals/attempt-5/result.json`, produced by:

```sh
python3 reports/native-runtime-object-visuals/validate_real.py \
  --output reports/native-runtime-object-visuals/new-attempt
```

The runner refuses existing output. It uses commit base6719794 plus this
checkpoint's native changes, host SHA256
`a02ef9c37dfde50a51831d69e3716d558805cf4c6c95b8d3c17cf049c906d9ba`.

The snack interaction creates food180/GUID4076987253, attached to actor105's
hand at tick512. The existing320x240 status view has no visible food ownership
pixels; that view obscures the hand. A fresh actual800x600 X11 window restores
the same checkpoint, presents tick513, and shows the food:528 owned pixels and
an exact renderer-pick identity match. No forced front layer, arbitrary
placement, wall removal or camera rotation was needed. Root inspected the
capture. Pausing and closing without saving preserves checkpoint bytes.

Zero-tick reopen preserves the complete archive. A one-tick continuation to513
matches fresh continuous513 in all13 sections, including UIST, actor, globals,
RNG, tick and the entire PNG. At4096, food180 is absent from the runtime registry
and saved World. Actor105's persisted controller has no active, suspended or
waiting action and retains successful completion at tick643. The removal
diagnostic identifies object180.

All116 protected original files retain their hashes. Six game processes exit0
and are waited/absent; Xvfb exits and both temporary profiles are removed. The
executable stays unchanged before/after every process. Reports retain metadata
and five PNGs; archives stay temporary.

`ctest.log` records17 passing bundle/restore/journal, pose/carry, scene, saved-lot
and entry/travel/relocation checks. Synthetic regressions cover resource
provenance, atomic rejection, sprite flags, object slots, animated avatar
anchors, stale parents, ID reuse, stale framebuffer picks and restored binding.
UIST tests cover non-primary roundtrip, legacy absence and malformed/stale
identity. Independent reviews found no outstanding blocker in this profile.

Earlier attempts remain diagnostic history:1 exposed avatar containment;2
used a small occluded view as an inadequate visibility gate;3 proved GUI
visibility but exposed lost selected-Sim state;4 passed product checks but its
last assertion read an ephemeral new-request diagnostic after restore. Attempt5
checks the persisted controller/World instead, retaining full-image equality
and the positive GUI visibility requirement.

Gate session archives remain in the isolated temporary profile; retained
reports should contain metadata and observations rather than executable,
commercial asset or full player-save payloads. The visual projection changes
no World codec; UIST is separate optional session metadata. None of this proves
console/handheld memory fit, original-game visual equivalence or hardware support.
