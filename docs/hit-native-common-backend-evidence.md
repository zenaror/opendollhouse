# Native HIT: common backend loading and control lifetime

Static evidence for the external `../referencias/The Sims/Sims.exe`, SHA-256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
The source was read only and never executed. This unit changes evidence files
only. It does not implement or validate an audio adapter or original-game audio.

## Reproduction and scope

From the repository root:

```sh
python3 reports/hit-native-common-backend/extract.py
```

The [extractor](../reports/hit-native-common-backend/extract.py) checks the full
source hash, PE32/i386 headers, image base and section mapping; obtains bounded
`objdump -d -Mintel` slices; verifies every displayed instruction/continuation
byte against the source and requires contiguous coverage. The
[manifest](../reports/hit-native-common-backend/manifest.json) records 20 ranges,
4,320 verified bytes, VA/RVA/raw offsets and per-range SHA-256. The field
`instruction_byte_lines` includes objdump continuation lines; it is not a count
of semantic instructions. Vtable targets and PE import names are separately
recorded in [vtables.json](../reports/hit-native-common-backend/vtables.json) and
[resource-routing.json](../reports/hit-native-common-backend/resource-routing.json).
No whole-program disassembly or fresh Ghidra analysis was needed. Existing
Ghidra exports for `005f95ef` and `005f87ba` were reused before the bounded audit.
The manifest's ODH revision identifies the starting investigation baseline,
not a claim that concurrent implementation work remains at that revision.

This extends [binding outcomes](hit-native-binding-outcomes-evidence.md) and
[control formulas](hit-native-control-formulas-evidence.md). The latter remains
the authority for controller A/B arithmetic and wrapper float conversion.
Addresses below refer to this exact EXE; property IDs are decimal and offsets
are hexadecimal. `TZ` denotes signed division truncated toward zero; `S32`
denotes signed interpretation of the low 32 bits.

## Source mode is not the resulting streaming flag

Patch initialization `0057fe8f..0057ffe0` reads Patch ID 38. Nonzero chooses
factory source mode 1; zero chooses mode 2. With Patch ID 26 zero, engine slot
`+5c` reaches `00603d43`, constructing the common backend with vtable `00676094`.
Source configuration reaches `005f86fd` / `005f8725`, copies the filename,
stores the mode at backend `+7c`, clears loop latch `+24`, and sets dirty `+2c`.

| Source mode | Resulting backend streaming flag `+25` |
| --- | --- |
| 1 | Set unconditionally |
| 2 | Result of virtual classifier `+d8 = 005f988d` |
| Other | Cleared; outside the Patch initializer's 1/2 selection |

The classifier calls `OpenFile(raw_filename, &OFSTRUCT, 0)`. Failure `-1`
returns false. Otherwise it calls `GetFileSize(handle, NULL)`, saves that DWORD,
closes with **`_lclose`**, and compares the unsigned size against the mutable
DWORD at `006d7c60`. It returns true when size is **greater than or equal to**
the threshold. Its initial bytes encode 262,144 (256 KiB); `005f839a` reads it
and `005f83a0` writes it. `GetFileSize` failure `0xffffffff` has no separate
failure branch and therefore compares as a large unsigned size.

The PE imports establish `OpenFile` at IAT `006662b4`, `GetFileSize` at
`0066624c`, and `_lclose` at `00666140`. The zero OpenFile mode is read mode.
This classifier uses a physical filename API. Patch ID 38 zero does **not**
guarantee nonstreaming, and a resource's archive entry length must **not** be
substituted for this physical-file result. Classifier-open failure can choose
nonstreaming while a later resource-manager open succeeds.

A narrow physical-XA admission gate can require common/nonspatial class,
source mode 2, independently established successful physical classification,
and physical size below the current threshold. Supporting virtual/archive
resources requires explicit classifier-result provenance; their entry sizes
alone do not establish this gate.

## Nonstreaming XA loading uses a different opener

`005f8909..005f8b7f` requires a nonempty stored filename. It extracts the last
four characters, normalizes that temporary string through `005d4d82`, then
compares its last two characters with `"XA"`. This records the observed helper
calls without asserting complete locale/case-conversion behavior. Other
branches compare `".WAV"` and `".MP"`; those decoders are outside this unit.

The XA branch allocates `0xf6c` bytes and constructs the reader at `005fbcf8`
(vtable `0067665c`). Reader `+4 = 005fbd8e` receives the **original stored
filename**, not the suffix temporary. It calls singleton `005b8e25`, then
resource manager `005b936f(filename, false)`. That manager performs name
normalization and catalog lookup; it has an entry-backed resource-handle
path and a file-class fallback. This is a distinct opening mechanism from
`OpenFile` in the streaming classifier. The false flag is the observed
read-side argument; this bounded evidence does not close every eventual OS
opening flag, prove a particular catalog entry is FAR, or equate entry length
with physical file size.

The reader checks the `X`, `A` signature and format tag 1, obtains decoded
length and format metadata, and exposes them via `+18` and `+1c`. The loader
stores decoded length at backend `+a4`, copies the 18-byte WAVEFORMATEX to
`+a8`, and passes it to buffer creation `+e8 = 005f92a4`. It locks the decoded
length, invokes reader `+10` to fill the PCM buffer, unlocks, closes and frees
the reader. This path loads a complete decoded resource rather than retaining
a streaming reader. Codec conformance, malformed-resource behavior and the
streaming decoder are not claimed here.

## Preload does not settle the first-play buffer

Patch ID 43 nonzero causes initialization to call Patch `+60 = 00580072`.
That method increments Patch resource count `+d4`; the transition to one calls
backend `+60 = 005f87ba`. Its return does not gate Patch initialization/binding
completion. Backend load returns success immediately when audio is disabled,
or when a nonstreaming buffer already exists; otherwise it dispatches to the
appropriate loader.

Source configuration sets dirty `+2c = 1`. Preload, the nonstreaming loader and
buffer creation do not clear it. On enabled `play` (`005f95ef`), the backend
stores the requested gain at `+28`; if dirty, it first clears `+2c` and calls
`+64 = 005f884f`, releasing existing buffers. A successful preload can therefore
be discarded on the first play. Nonstreaming play then tests buffer presence
via `+5c = 005f88fd`, calls `+60` if absent, and returns false if that load fails.
When audio is disabled, play returns success before these mutations.

| Control | No-buffer behavior | Retention/reapplication |
| --- | --- | --- |
| Pan `005f93f9` | Returns false | No pan field is stored; loader/play does not reapply it |
| Frequency `005f9471` | Enabled: false; disabled: true | No requested-frequency field is stored; loader/play does not reapply it |
| Gain `005f94c1` | Returns success | Signed clamp 0..1024 is stored at `+28`, even without a buffer |

Enabled play reapplies gain via `+78` at `005f969a` on the active/restart branch,
or `005f96ec` before issuing a new DirectSound Play. Gain setter results are
ignored there. No corresponding pan/frequency application appears in the
bounded nonstream loader, buffer creation or play. Header sample rate is
supplied in the WAVEFORMATEX passed to CreateSoundBuffer. These facts establish
absence of EXE-side reapplication; they do not measure the implementation's
DirectSound default pan/frequency or rendered PCM.

Consequently, applying pending pan/pitch automatically after every lazy load
would introduce behavior absent from these paths. Even controls successfully
applied to a preload buffer can be lost when dirty play replaces that buffer.
A future adapter must separate requested wrapper controls from controls
actually applied to the current buffer generation.

## NoteOn reads two different ID 38 sources at different times

Controller NoteOn `0057f88e` snapshots wrapper `controller+1d0`. Missing wrapper
or missing initial `controller+bc` Patch causes a diagnostic and false return.
After those guards, the exact main sequence is:

| VA | Operation |
| --- | --- |
| `0057f8c9` | Controller public getter `+88(38)` |
| `0057f8dd` | If that value is nonzero: wrapper writer `+84(38, 1, 0)` |
| `0057f8ec` | Wrapper NoteOn `+94(literal)` |
| `0057f8f2` | Reload the current `controller+bc` Patch pointer |
| `0057f8f8` | Save wrapper return AL in BL |
| `0057f8fe` | Read that current Patch's getter `+88(38)` |
| `0057f90a`, `0057f911` | If Patch value is nonzero: controller A then B |
| `0057f916` | Return saved wrapper AL |

There is no post-NoteOn Patch null guard. A/B runs even when wrapper NoteOn
returned false; the gate is the post-call Patch value, not play success. The
pre-call getter belongs to the controller and the post-call getter to the
Patch. They must not be folded into one cached value. The wrapper writer
must also remain an observable ordered operation; this unit does not reinterpret
writing its bank ID 38 as a physical-backend stream switch.

Wrapper NoteOn's main path (`0058045d..005804aa`) obtains backend `+74` gain,
seeks zero through backend `+98` (ignoring seek's return), reads **Patch ID 44**
at `00580491`, and calls backend `+20(gain, loop, 1)` at `005804a7`. `SETNE AL`
normalizes the low byte of loop, which the common backend reads as a byte;
the upper bytes of the pushed register are not necessarily zero. The opcode's
literal NoteOn argument is not used for this loop decision.

A callback matrix for a portable global adapter remains future work: it must
represent controller getter 38, wrapper writer 38, ordered backend calls and
live post-NoteOn Patch getter 38, including their effects. A single Boolean
play callback plus precomputed metadata does not express this entire contract.

## Loop is latched within a configured source

On the nonstreaming play path, `005f965e` tests the low byte of loop. A true
value writes backend `+24 = 1` at `005f9663`; false does not clear it. Later
Play flags use this latched field. Source configuration clears it at
`005f8763`; the inspected clear/release functions do not reset it.

HIT passes play mode 1. If already active, this path seeks to zero and reapplies
gain, without issuing another DirectSound Play. Setting the latch then need
not update the running buffer's existing Play flags until a subsequent new
Play. Thus repeated NoteOn with false after true does not simply disable
looping for that configured source.

## Numerical foundation and implementation limits

The exact EXE-side common controls are:

- Pan: signed clamp `p` to 0..1024, then `TZ(p*20000,1024)-10000`, passed to
  DirectSound SetPan.
- Frequency: when enabled with a buffer, signed clamp to 100..100000 Hz and
  pass to SetFrequency. The wrapper's preceding x87/power conversion is
  documented separately; no claim of `std::pow` bit identity is made.
- Gain: signed clamp `g` to 0..1024 and retain it; with a buffer read global
  gain `G`, compute `v = TZ(S32(g*G),1024)`, then volume `-10000` for `v=0`,
  `0` for `v=1024`, otherwise `TZ(S32(v*4000),1024)-4000`.

For ordinary bounded inputs `g,G,p` in 0..1024 the listed products fit signed
32 bits and the resulting DirectSound volume/pan values are in their normal
ranges. Pitch admission additionally needs a finite, representable conversion
result before the common frequency clamp. That is an adapter-domain policy,
not proof for arbitrary NaNs, infinities, x87 precision or integer overflow.

Microsoft documents SetVolume/SetPan in hundredths of a decibel and cumulative
attenuation. A mathematical mixer interpretation is amplitude `10^(V/2000)`;
for pan `P`, left/right factors are `10^((V-max(P,0))/2000)` and
`10^((V+min(P,0))/2000)`. These equations interpret the API units; they do not
prove DirectSound's exact internal gain, rounding, clipping, interpolation,
Q15 conversion or a bit-identical power function. Treat exact-zero mute at
-10000 and numerical approximations as explicit portable-mixer policy.
Frequency gives a nominal source-frame step of applied-Hz/output-Hz, again
without establishing the original resampler's interpolation law. See official
[SetPan](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/mt708938%28v%3Dvs.85%29),
[SetVolume](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/mt708939%28v%3Dvs.85%29)
and [DirectSound node ordering](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/directsound-node-ordering-requirements).

This closes static resource routing, preload/dirty lifetime, control retention,
NoteOn ordering and nonstream loop latch for the selected class. Streaming,
spatial-class aliasing, complete resource-catalog/FAR resolution, actual
DirectSound defaults and playback/codec equivalence remain outside the claim.
