# Maxis XA audio decoding

`odh::read_xa(View, XaAudioLimits)` now decodes bounded Maxis XA resources to
owned signed PCM16 samples, interleaved L/R for stereo, with source sample rate,
channel count and remaining-byte count. `odh-xa-audio INPUT_XA OUTPUT_WAV`
exports a PCM WAV and reports decode metadata. It does not submit sound events,
select HIT samples, mix audio or play a device.

## Supported profile

The decoder accepts signatures `XAI\0`, `XAJ\0` and `XA\0\0`, PCM tag1,
mono/stereo, 16 bits, rates8000..192000, consistent alignment/byte rate and whole
declared PCM frames. Predictor codes0..3 use coefficient pairs (0,0), (240,0),
(460,-208), (392,-220). Others reject explicitly. The pinned reference is
FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0,
`tso.files/XA/XAFile.cs`, mono/stereo routines and coefficient table.
Double-NUL signature support comes from real candidate resources and their
independent FFmpeg decoding, rather than the reference's narrower header comment.

Each complete compressed block is 15 bytes/channel and yields 28 frames.
History is independent per channel and survives blocks. Signed arithmetic,
rounding and saturation use defined C++17 operations, without negative signed
left shifts or implementation-dependent signed right shifts. All required
blocks/predictors validate before PCM allocation. The final full block is
consumed, but its padded samples beyond the declared frame count are omitted.
Bytes after the required blocks are reported and not decoded.

Defaults bound the entire input view to16 MiB and decoded PCM to4 MiB. These
are buffer limits, not RSS measurements or New3DS playback/performance proof.
Streaming, other sample codecs, resampling and the device backend remain pending.
The CLI refuses input/output path, symlink and hardlink aliasing; malformed
decoding does not truncate an existing output. Commercial resources remain
external and are not versioned.

## Validation on 2026-10-14

1519 synthetic checks pass in Debug and ASAN/UBSAN: signed nibbles, all supported
predictors/scales, clipping and clipped history, channel separation/interleaving,
cross-block state, partial final frames, truncations, backing-stream truncation,
budgets, header consistency, integer extents and unsupported predictors. Python
`wave` independently checks CLI WAV metadata/sample bytes and input preservation.
Leak detection is disabled in this environment; no leak-check result is claimed.

Twelve real XA candidates for `hungry_sated_vox` were independently selected and
extracted from external `SoundData/SoundData.far` by a Python FAR directory
reader. All are mono22050Hz/PCM16. Every decoded sample exactly matches the
declared-size prefix of installed FFmpeg6.1.1's `adpcm_ea_maxis_xa` output.
Debug/ASAN outputs also match. FFmpeg produces0..50 extra block-padding bytes
on these files; they are explicitly excluded from the comparison and WAV export.
Patch3212 declares108592 PCM bytes; FFmpeg returns108640 (48 padding bytes).
Ten additional synthetic mono/stereo cases, with distinct predictors and
1/28/29/55/56 frames, match FFmpeg's declared prefix exactly. Native stereo
resources were not validated in this unit.

Ignored reproducible evidence includes an archived validation record (not distributed),
an archived validation record (not distributed), an archived validation record (not distributed), exported
XA/WAV under an archived validation record (not distributed), and build/test logs. The static inventory
found124 standalone SoundData TV XA files, all mono11025; this is an inventory,
not a claim that they were all decoded or played.

The full suite configured93 tests:92 passed and the X11 backend test was skipped
inside the sandbox in each build. Decoder builds were warning-free.

## The actual pending Main sound

Independent IFF/FAR reading confirms FWAV355 is absent from User00000.iff and
PersonGlobals.iff, and present in Global.iff as `hungry_sated_vox\0`:
IFF offset145508, absolute FAR chunk offset454218, payload offset454294,
93-byte chunk. The Global.iff FAR entry starts at308710.

Static reference/resource chain:

- `SimsGeneratedHitSource.hot:5036` maps the event to track3979.
- Track3979 at HOT5778 has volume1024, `kArgsIdVolPan`, `kGroupVox`,
  `@tkd_hungry_sated_vox`, patch0.
- HSM1469–1472 / HOT648–649 map track-data901 to HIT13699 (`0x3583`),
  and902 to HIT13741 (`0x35ad`).
- HIT entry13699 reads source field20, compares50 and branches. Values>=50
  select male/female/child hitlists1206/1207/1208; values<50 select1291/1292/1293.
  Both enter `generic_fm_smartlists`, TrackData259 → HIT36 (`0x24`).
- Generic code selects a list by gender, chooses a patch randomly, assigns
  patch variable50, then NoteOn/WaitSamp/End. Initial hitlist901 is overridden;
  its members must not be mistaken for the actual sample candidates.

Important source correction: `HITInterpreter.GetSrcDataField` at680–693 reads
`ReadVar(10010 + FieldVar)`; `HITThread.ReadVar` at478–481 subtracts10010.
Thus field20 directly reads ObjectVar20. `VMAvatar.SubmitHITVars` at186 sets
that slot to NeatPersonality/10; age is a separate slot14, unused by this branch.
The same mapping exists in Simitone's pinned FreeSO submodule
`f86bbd34b2112296ed786741f640a0535886d180`. The initial age interpretation was
incorrect and withdrawn. Original TS1 field20 semantics remain unknown; there
is no HSM field definition establishing them.

The reference also maps gender/child to objectvar0. The recorded constructed
actor has NeatPersonality7=300 and gender65=0, so this static FreeSO interpretation
uses field20=30 and selects candidate list1291:

| Patch | FAR entry | Offset | Length |
| --- | --- | --- | --- |
| 3212 | Voice\mood\VOX_COMFORT_SIGHM4.XA |109753498|29124|
| 2756 | Voice\interactions\VOX_HUGGEE_GOODM1.XA |96596593|22929|
| 2757 | Voice\interactions\VOX_HUGGEE_GOODM3.XA |96619522|27624|
| 3981 | Voice\utterances\Vox_Burpm4.XA |121923895|5199|
| 2249 | Voice\utterances\VOX_BURPM1.XA |121903948|9969|

Other validated candidates are female3982/3983/3984/1833/3985 and child2481/2482.
The >=50 male list1206 contains3212/2756/2757; female/child lists are unchanged
across this field20 branch. Actual random selection is not established: the
reference uses a separate `new Random()` in `HITThread.HitlistChoose`, not the
BHAV host RNG. Original TS1 selection/mixer behavior remains unverified.

Primary reference locations also include `VMPlaySound.cs:18–47`,
`TS1Audio.cs:160–195,204–215,272–276`, `Hot.cs:153–177`,
`HITInterpreter.cs` and `HITThread.cs`. This is static tracing of the local
resources/reference, not execution of a new ODH HIT interpreter.

The original SoundData FAR SHA256 remains
`14d98dce3bd75b445beea9f575fa2ebe8736e3c5542cb429d2b30a57fae7c5c0`.
Patch3212 XA SHA256 is
`e68b39e613b2500e40796346fb6cef6343ae97dda5eea2ae0debc3f9f6dcbd64`.
Main still has the opcode23 gate; this unit supplies a verified codec prerequisite.
Next: bounded HOT/HSM/HIT resources and event execution, explicit sample choice,
and real Linux audio submission before claiming that the requested sound played.
