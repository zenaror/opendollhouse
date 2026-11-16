# Finding the engine code

The current application links the `odh_engine` library, defined in
[`engine/CMakeLists.txt`](../../engine/CMakeLists.txt). It combines code
incorporated from Sims3DS with reused OpenDollhouse services. The directory name
`sims3ds` records its origin; it does not impose the 3DS interface or screen
limits on the PC version.

## File map

| Where to look | Responsibility |
| --- | --- |
| `engine/sims3ds/src/app/` | Screen navigation, the open house, family creation and interface. |
| `engine/sims3ds/src/content/` | Finding installation resources, characters, families and expansions. |
| `engine/sims3ds/src/files/` | Reading archives, IFF resources and audio data. |
| `engine/sims3ds/src/simantics/` | Running object scripts, actions, autonomy, needs and lot rules. |
| `engine/sims3ds/src/world/` | Composing terrain, walls, objects and the lot view. |
| `engine/sims3ds/src/vitaboy/` | Character appearance, models and animations. |
| `engine/sims3ds/src/hit/` | Interpreting and playing audio events. |
| `engine/platform/` | Windowing, input, audio, files and startup for each system. |
| `engine/original-ui/` | Selecting previous readers and services used by the current presentation. |
| `engine/tests/` and `tests/` | Tests for the engine and reused services. Check the CMake target. |

## Following a player flow

`App::Frame`, in `app/App.h`, coordinates input, screens and application updates.
`LotSession`, in `app/LotSession.h`, maintains the house session. Simulation and
presentation have separate responsibilities: calling `LotSession::Update`
directly does not run a complete application frame.

For an interface fix, find its consumer in `app/` and the presentation service it
uses. For example, `app/ui/Desktop*` and `OriginalPCUI` connect the PC interface
to shared services. The source list in `engine/original-ui/CMakeLists.txt` shows
which files from `src/`, `include/odh/` and `tools/` participate in that path.

For saving, start with `app/front/LotSave`, the neighborhood model and the
`simantics/utils/VMTS1LotSaver` and `VMTS1InteractionCheckpoint` services. Check
both the house file and the character and neighborhood records. A successful
write does not prove that the save will restore correctly.

## Needs and action state

`VMAvatar` keeps current and previous needs as floating point values. Use
`GetMotiveDataFloat` for simulation and scoring. `GetMotiveData` provides the
original rounded integer value for script expressions and interface consumers.
Continuous changes form an ordered list: two changes to the same need can have
different rates and limits, so they must not be collapsed into a single entry.

Classic `ObjM` records store the precise values and changes. The native `ODHI`
record retains the execution graph, with compatible integer projections for
older readers. An optional `ODMP` record binds the precise state and decay phase
to those two records. Restore one validated source of needs; do not combine the
legacy projections and precise values. An older build can rewrite the records
without updating `ODMP`, so stale metadata must not overwrite its new progress.

Room satisfaction has a separate cached logical map in
`TS1EnvironmentGeometry` and `VMContext`. Query it by the Sim's position;
rendering and routing room numbers are not interchangeable with this map.
The original calculation counts repeated visits to tiles, floor and wall
coverage, eligible objects, daylight and artificial light. Reusing unique
rendered floor area gives different results.

For example, removing floor covering must update the logical coverage and
rebuild the room geometry when the edit is committed. Object scripts use the
existing Refresh operations to update contributions. Keep the original wall
type separate from a dynamically allocated drawing resource number. These
caches are reconstructed from game data and edits, without adding save fields.

## Handheld rendering and file access

| When playing | What the engine reuses |
| --- | --- |
| The camera moves across a lot | The renderer keeps three layers of scenery and a 64-pixel margin. It draws newly exposed strips when possible. |
| A Sim moves in front of furniture | Cached scenery supplies both color and depth. Depth determines which part of the Sim the furniture hides. |
| A player enters a house | The engine uses archive indexes to find the residents' outfits and reads the portraits needed for the household panel. |
| An action menu opens on Vita or Switch | The wheel shows the selected resident's cached portrait. The input and simulation loop yields between complete updates after a 14 ms budget. |
| Part of the screen changes on Vita or Switch | The host can upload just the changed bands of pixels. Scrolling or a transition can trigger a full upload. The debug log records the bytes sent for the canvas. |

The 3DS draws the lot at 400×240. Its lower screen can reuse that image on
alternate frames. Vita and Switch currently draw a single 800×600 canvas.
Changing the resolution of the lot independently of the interface requires the
same coordinate conversion for touch, selecting objects, placing furniture and
drawing the crystal over the selected Sim.

## Separating the product from previous tools

The `src/`, `include/odh/`, `native/` and `tools/` directories also contain
components from a previous implementation and inspection programs. Some readers
and presentation services were incorporated into the product; others are
optional with `ODH_BUILD_LEGACY_TOOLS`. A file existing, or a test passing in
that area, does not mean the current application uses that behavior.

Before changing a service, check its CMake target, its callers and the path to
the player's screen or action. Preserve integrated components rather than
creating a second simulation to solve an interface problem.

## Constraints to preserve

- PC uses a single screen. The New 3DS adapter has its own presentation and input.
- Shared rules belong in the engine; system windows, devices and paths belong
  in the platform adapters.
- Game scripts and resources define object behavior. Avoid exceptions based on
  the name of a piece of furniture to hide a general problem.
- Commercial data, saves and cache are separate. Changes to importing or saving
  must preserve source files and explain incompatibilities.
- Internal contracts and classes may change. This section is not a public
  plugin API or a guide to creating new expansions.

Origins and legal notices are in the [engine README](../../engine/README.md#origin-and-licenses)
and [NOTICE.md](../../NOTICE.md).
