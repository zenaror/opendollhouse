# Project goals and pull request review

Read this before choosing a task or reviewing a contribution. OpenDollhouse
aims to preserve the experience of the original PC The Sims 1 and its seven
expansions in a single native C/C++ engine. It is not a modernization of the
game or a port of every feature added by a reference project.

The [current status](../status.md) describes available builds and limitations.
The milestones below describe product goals, not completed support, deadlines
or permission for an agent to start later work.

## Milestones

| Milestone | Goal |
| --- | --- |
| **1 — Functional parity on Linux** | Cover the original-game functionality present in the selected original Simitone and alexjyong references, with their corresponding FreeSO revisions. Combine compatible capabilities and fixes from both; neither fork alone defines the whole baseline. Preserve the original PC interface and controls. This includes neighborhoods, families, home life and autonomy, buying/building, relationships and careers, pets and events, expansion destinations and activities, saving, languages and classic custom content within that baseline. |
| **1.1 — Other platforms** | Adapt the same engine for Windows, macOS Intel/Apple Silicon, Android, New 3DS, Vita and Switch. Keep platform builds, emulator observations and physical-device results separate. Old 3DS support is desired and remains exploratory, not promised. Platform adaptations must not replace the PC experience. |
| **2.1 — Verify existing features** | Compare concrete behaviors with the DVD and correct functional differences in AI choices and frequency, autonomy, interactions, menus, controls, rendering, sound and settings. Verify the tutorial's learning flow and controls. Behavior in other reference projects does not establish the DVD baseline. |
| **2.2 — Complete missing functionality** | Implement missing systems across the base game and seven expansions, including events, life cycles, travel and saving. Validate their behavior through playable scenarios. |
| **3 — New expansions and mod tooling** | Discuss and design new content, such as apartments, and accessible extension interfaces and tools. These are future work, not current APIs or prerequisites for milestone 1. |

Milestone 1 is a functional parity target, not just a small demonstration.
Passing one action, loading a map or producing a binary does not complete it.
The selected upstream revisions and credits are in [references](../references.md).
FreeSO also contains The Sims Online behavior; do not assume that every shared
routine belongs to The Sims 1.

For milestone 2, use the original PC Complete Collection DVD as the reference.
Compare a full player action in the same lot, mode and starting conditions,
including what happens on entry, during play, and after saving and reopening.
Use the original game data or executable to resolve a specific unanswered
question. Record unverified differences separately. A missing option in an
existing feature belongs to 2.1; a wholly absent system belongs to 2.2.
Accept changes by their behavior and usability in these scenarios. Visual
identity and exact reproduction of the DVD game are outside the acceptance
criteria. Players supply their own game data.

Two scope concessions apply to milestone 1: features missing from both selected
Simitone references may wait until milestone 2, and an action in progress may
restart when loading. Neither permits lost or duplicated residents, possessions,
money or progress, broken travel state or a stuck save. Both concessions must
be resolved by the end of milestone 2. They do not permit regressing working
continuation code or discarding fixes already made.

## Original experience, not reference-specific modernization

Do not import a feature merely because Simitone, a fork or another port labels
it an improvement or quality of life (QoL). Features such as modern catalog
search, extra neighborhood entry/zoom steps or added weather are not part of
the default product scope. A disabled-by-default toggle does not automatically
make such a change acceptable.

On PC, keep the existing single-screen interface and controls, with targeted
usability fixes and missing functionality where needed.
On handhelds, choose rendering detail, internal resolution, input and frame
scheduling to suit the device. For example, a touch menu can show the resident's
portrait while the PC menu turns the resident's head toward the pointer.
The 3DS can use both screens.

Across the interface, reuse artwork and composed controls while their appearance
stays the same. Redraw the affected area when a value, selection, hover state,
animation, language or layout changes. Transparent edges must still show the
current scene underneath. Check a changing screen against a full redraw before
reporting a performance improvement.

Aim for at least 30 FPS during gameplay on each platform. Compare the same lot
and camera on hardware with simulation updates and player input. Record stalls
and loading times separately; a five-second average can hide dropped frames.

Keep essential actions, simulation rules and saved progress shared. A family
saved on one port should reopen on another with its residents, possessions and
progress intact. When changing how a lot loads or saves, test an edited lot
through a save and reload. Record which ports actually read that save.

If a proposed feature changes the original experience, discuss it with the
maintainers before implementing it. Do not merge it as an incidental fix or
move it into an earlier milestone without an explicit decision. Listing future
mod support is not approval to implement a new expansion now.

## Reviewing a pull request

Review scope and behavior as well as code quality. Passing CI alone is not
enough. A human or AI reviewer should check:

1. **Purpose and milestone:** what problem does the player encounter, which
   milestone does the change serve, and is that work currently agreed? Keep
   unrelated fixes, modernization and future expansion design separate.
2. **Reference and evidence:** for milestone 1, identify the relevant pinned
   Simitone/FreeSO behavior; for original fidelity, compare the original game
   in the same context. A screenshot or a function name alone is insufficient.
   State what was observed and what is still inferred or untested.
3. **Player-visible effect:** check interface, controls, AI, timing, gameplay
   consequences and settings where affected. A fix must reach the actual
   application; an isolated helper is not the whole feature.
4. **Regressions and portability:** preserve existing fixes, data ownership,
   save compatibility and localization. Keep shared rules in the engine and
   device services in adapters. Report the platforms actually tested without
   treating a build or emulator run as universal hardware support.
5. **Distribution and clarity:** preserve licenses, credits and corresponding
   sources. Do not include commercial data, private paths or internal reports.
   Update relevant English user docs and the changelog for user-visible changes.

The PR description should briefly state the problem, resulting behavior,
reference used, checks performed and known limits. Follow the
[testing guide](testing.md) for proportionate checks rather than repeating
unrelated full test suites.

A review should explain whether the change fits, needs revision, belongs to a
later milestone or falls outside the project goals. Do not approve a change
solely because an upstream project includes it. Review does not grant an agent
permission to merge, publish or start another milestone; those actions follow
the maintainers' agreed scope and authorization.
