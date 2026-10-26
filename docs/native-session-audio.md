# Native session sound output

The native game host now sends BHAV FWAV requests and animation time-property
sounds into the existing HIT/XA/PCM path. GameAudio owns bounded metadata,
per-request execution, sample notes and transport; a PcmSink is injected.
Filesystem discovery stays in tools/, and Linux device creation stays in person.
The portable simulation does not depend on SDL, a Linux audio API, or wall-clock
waiting. This is the first session integration of the existing sound components,
not full original audio parity.

## Game and platform boundary

Windowed integrated sessions enable audio by default. `--no-audio` disables it.
Bounded headless sessions remain silent unless `--audio` is supplied.
`--audio-drain-ms 1..5000` optionally pumps already queued PCM after simulation.
It never advances World, VM, Playback or HIT. PCM may be fully consumed while
HIT still waits for its next regular simulation tick; thread counters report that
state separately. Device startup errors are logged and leave gameplay running
with explicit skipped-no-backend outcomes. No output device is silently claimed.

One HIT poll occurs at each existing simulation boundary. Window pause pumps only
PCM. The caller closes sound instances before transport and transport before the
borrowed sink. Removing an entity invalidates its generation-bound sounds;
StopAllSounds selects StackObject only when its operand flag equals 1, otherwise
Caller. The current single-owner path stops that owner's instances immediately.
Sounds are independent of the animation that originally emitted them.

Audio is presentation state and starts fresh on load. Saved Playback cursors
retain their simulation position, so consumed animation events are not replayed.
No HIT/PCM sound snapshot or exact mid-note continuation is promised. The audio
chooser has its own deterministic random stream and never consumes gameplay RNG.

## Resource admission and limits

Discovery pairs case-folded HSM/HOT/HIT stems within the same directory and uses
the Simitone-local parser profile. It skips symlink files/directories, retains
missing/malformed group diagnostics, and reads resources without extraction.
Sound/SoundData FARs and loose XA files form the sample catalog. Event names that
appear in multiple groups are rejected explicitly; load order is not guessed.
Duplicate sample basenames likewise retain the catalog's rejection policy.

Limits include 131072 scanned entries, 64 metadata groups, 32 MiB aggregate
serialized metadata, 4 MiB discovery names, 65536 event rows/2 MiB event names,
8192 XA sources, 64 live sound instances, 64 notes per instance and 4 MiB PCM.
Existing instruction/event/trace/decode limits remain enforced. Runtime names
are limited to 256 bytes; receipts and diagnostics have 256 entries and retained
diagnostic text is limited to 1024 bytes. Host sound history additionally has a
256 KiB budget. These are logical resource budgets, not a measured RSS claim.

The current profile is one source per nonlooping type-1 HIT bytecode event,
with XA samples, centered unity-Q15 mixing and explicit raw request metadata.
Global event sharing, loudest-owner variable arbitration, interruptable sharing,
spatial pan/zoom conversion, native volume/rate scales, loops, music/TV,
WAV/MP3 and arbitrary HIT instructions remain incomplete. Unsupported requests
or runtime instructions produce bounded diagnostics and do not change the
nonblocking true branch of PlaySound or invent sample playback.

## Animation and execution semantics

Playback emits actor and VM run generations with each event. The shared
`animation_sound_event` bridge uses the first exact `sound` property, validates
the actor incarnation, captures the existing 29 avatar source variables and
submits pan/zoom true and loop false. Optional output absence, invalid names,
missing variables, stale owners and backend rejection have distinct outcomes.
Unknown metadata retains the prior no-effect rule; xevt and hand events still
execute. `dress` and `undress` remain unsupported services.

FWAV lookup follows the exact code owner's resource, then Globals. Pinned
VMStackFrame.ScopeResource and VMThread.ExecuteSubRoutine preserve CodeOwner
across global/semiglobal routine calls; routine number alone does not select a
different FWAV bank. Dynamic object action owners now resolve their own FWAVs.

The session deliberately selects the existing Literal32 Jump profile for TS1
assets. Real generated HIT PC8905 has opcode13 followed by DWORD258
(bytes 02 01 00 00); FreeSO's low-byte heuristic instead reads register2 and
fails on TrackData0. This is an explicit TS1 asset compatibility choice, not
an automatic fallback or an assertion of original executable equivalence.
The synthetic service gate includes a low-byte-2 literal branch to guard it.

The explicitly selected sequence-group backend follows pinned Simitone/FreeSO
`7e76c1960252ddd033d8937d5edb47d326f5b69c`:
`HITInterpreter.cs:648–663`, `HITThread.cs:183–194` and
`tso.files/HIT/HITConstants.cs:67–71`. Group0 stops this thread's active notes;
other groups are the source's no-op TODO. Handles remain queryable and subsequent
NoteOn can start within the lifetime budget. The older explicit-callback backend
remains unchanged. This implements the reference baseline, not a global group
registry or the complete original game's vocal ownership.

## Evidence boundaries

Synthetic tests cover actual discovery/HIT/XA mixing through a small injected
sink, partial acceptance, consumption, cancellation by generation, unrelated
owners, retained budgets, nonblocking animation delivery, event cursor restore,
metadata coexistence, rejected names/groups and absent sources. Real gates use
isolated profiles and SDL dummy, never physical output or a virtual screen.
Raw accepted frames, nonzero sample counts and peak values establish generated
PCM; they do not establish audible playback, original DSP, or console support.


## Verified checkpoint

Final executable SHA256:
`d725c2532dc8c650948759ed36c7165d3da501da18a66ec6bb3e097497386ffa`.

`reports/native-session-audio/attempt-1/final-ctest.log` records 31 passing focused
tests after rebuilding their targets in `focused-build.log`. Host link/core builds
are recorded in `final-build.log`, `meter-build.log`, `diagnostic-build.log`,
`ts1-jump-build.log` and finally `voices-build.log`; the final two changed shared
audio libraries and relinked the same host. No source changed after these gates.

`reports/native-session-audio/real-final/result.json` records six fresh processes
with isolated profiles and SDL dummy. In House07, Sim105 sits in chair128;
voices from Sims105/137 and chair_sit_metal are admitted with zero HIT/PCM faults.
The output accepts and consumes 110080 frames with 218984 nonzero stereo samples
and peak13320. Audio-enabled and silent runs match all 13 saved sections and
actor/global/RNG/tick fields. Zero-tick reopen preserves all 13 sections and
starts with no submitted events/PCM, proving the presentation reset policy.

The restored Sim then Stands successfully with posture0 and no attachment.
Audio and silent versions of that continuation match all 12 final sections and
the same four JSON fields. The audio continuation admits two voice requests,
with zero HIT/PCM faults, 111104 accepted/consumed frames and 220174 nonzero
samples. These measured counts describe this run and its host scheduling.
An unavailable SDL driver is handled without a gameplay fault and preserves all
13 sections compared with the silent run.

All six processes exited0, temporary profiles were removed, and 29 protected
original files kept their hashes, including the selected main sound metadata and
SoundData FAR. No display was created and physical audibility was not tested.
At the pre-destruction report both audio runs still have two HIT instances and
two note handles awaiting a later simulation poll, with zero PCM bytes/active
notes and fully consumed output. They are not reported as completed HIT work;
normal scope teardown closes the backend. Separate synthetic gates check owner
cleanup and retained note query semantics.

Intermediate evidence is retained: real-1 had opaque thread faults; real-2
identified the FreeSO low-byte Jump misinterpretation; real-3 generated chair PCM
but voices still lacked the explicit instance-group handler; real-4 passed the
first four real scenarios before the final Stand comparison was added. The first
synthetic service drain budget was too short for a 1024-frame mixer block through
a two-frame fake sink and was corrected without weakening cleanup assertions.
Independent review checked code, pinned source semantics and final reports
without repeating the builds or tests.
