# Native migration work inventory

Inventory date: 2026-10-26, native base `6890bf141250164cc06198bdbac8f87f427c59ee`.
This is a structural source inventory and delivery plan, not a completion report.

## What this inventory proves

[source-inventory.csv](source-inventory.csv) enumerates every tracked `.cs`,
`.csproj`, `.fx`, `.hlsl`, `.glsl`, `.props` and `.targets` file in the two pinned
reference trees: Simitone alexjyong `e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab`
and FreeSO `7e76c1960252ddd033d8937d5edb47d326f5b69c`.
Each row carries repository/revision/blob identity, routing family, existing
native candidate paths where known, lexical type hints and semantic-audit status.
`inventory-summary.json` supplies counts and the CSV checksum.

Reproduce with `python3 docs/migration/generate_inventory.py`. It reads committed
Git objects, not modified reference files. It neither compiles nor changes C#.
Routing covers all 2,451 selected files. All semantic rows remain `not_audited`:
**routing completeness is not semantic completeness or port completion**.
Type names are regex hints, including possible comment/string false positives;
they are not a compiler-derived class or method graph. Native candidates are
verified existing files, not a statement that the corresponding feature works.

The CSV deliberately includes server, editor, installer and third-party code so
it cannot disappear from consideration. SERVICE_REVIEW/TOOL_REVIEW/ASSET_TOOLS
usually concern reference/development tooling, not the shipped TS1 runtime;
verify actual TS1 dependencies before excluding a file. LIBRARY_REVIEW/SHARED
require licensing and dependency decisions. JIT_REPLACE points to the existing
native interpreter, not a plan to ship Roslyn/CLR. TSO_CONTENT_REVIEW is not
permission to import TSO behavior into TS1. No binary/commercial assets are copied.

This is a structural inventory and a delivery backlog, not a complete method-by-
method audit. Conditional TS1 execution, shared dependencies and external BHAV
scripts still require targeted semantic review. Most individual object behavior
comes from game data: reuse those scripts through the native engine instead of
rewriting each object in C++. The seven expansion packs are cross-cutting and
cannot be inferred from seven source directories.

## Reference-family map

Client paths are relative to `Client/Simitone/Simitone.Client/`; core paths are
relative to FreeSO `TSOClient/`. The CSV provides complete individual paths.

| Family | Reference owners | Native candidates / planning boundary |
| --- | --- | --- |
| APP/HOST | SimitoneGame, GameController, LoadingGameScreen; desktop/shared hosts | linux_game_entry/window, bootstrap; separate platform services now. |
| UI/SHARED_UI | UI/Controls, Panels, Screens; tso.client and FSO.UI | neighborhood_screen, person, motive/dialog/text hosts; provisional UI is partial. |
| DATA/PROVIDERS | tso.files formats/FAR; tso.content TS1/providers/codecs | readers, character_catalog, object_runtime_resources, resource manifest; verify overrides/identity. |
| VM | tso.simantics Engine/Entities/Primitives/Utils | vm/world/object construction; audit each primitive and error/profile. |
| SIM | Model/TS1Platform and TS1 primitives | motives, relationship, inventory, clock; read/display is not mutation coverage. |
| ROUTE | Model/Routing, slots, placement and portals | rect_router/world_attempt_walk/household_arrival; object placement remains distinct. |
| BUILD | VMArchitecture*, architecture commands, UIObjectHolder/LotControls | prepared/saved architecture; reading/rendering does not implement editing. |
| SAVE | Marshals, TS1GameScreen, TS1NeighbourProvider | session_bundle, household_transfer, occupancy_projection, journal; separate property authority. |
| DRAW | tso.world components/model/effects/lighting/RC/facades | saved_lot/object/scene renderers; complete camera, roofs, lighting and depth behavior. |
| POSE | tso.vitaboy.model/engine and avatar components | pose/skeleton/animation/appearance; carry frame0 is a bounded implemented profile. |
| AUDIO | tso.sound, HIT/XA/UTK, TS1Audio, WeatherSounds | game_audio, hit controller, sound threads, PCM/XA; streaming/loops/spatial still partial. |
| Cross-cutting expansions | TS1 primitives/providers, UI destinations/catalogs, Content/TS1Patch and original BHAVs | E01–E07 below; no full-pack completion claim. |

## Dependency and execution policy

Use the dependency-ordered packages below as the roadmap. `Partial` means existing integrated evidence is linked from
[feature matrix](../playable-feature-matrix.md); `Open` means an end-to-end
acceptance gate is missing. No package below is declared complete.

The first implementation wave is P01–P04. In parallel, a second worker can audit
VM01 and a third can map UI01 or resources without editing the same files.
B01 may inspect catalogs in parallel; purchasing waits for valuation, placement
and persistence. Expansion integration waits for the relevant core contracts.
Platform boundaries PORT01 are a prerequisite throughout, not a final rewrite.

Every implementation dispatch must name: package ID, pinned source files,
native files owned, dependencies already satisfied, supported/unsupported cases,
resource budget, exact acceptance flow and required evidence. A discovered defect
belongs to its package; fix immediately only if it blocks the current gate or
threatens state. Otherwise record it and retain the planned sequence.

## Delivery packages

Native paths below are module owners/candidates, not all new filenames. A
package's detailed design must resolve exact interfaces before implementation.
Dependencies refer to this table; work may share established partial components
without waiting for every unrelated requirement of a larger package. VM02 and
SAVE01 are co-developed in bounded slices: first the versioned state contract,
then one supported interaction, then generalized continuation; neither requires
the other entire package to be complete before work begins.

| ID / state | Source family → native owner | Delivery and acceptance gate | Depends on |
| --- | --- | --- | --- |
| P01 Open | SAVE/VM/DATA → occupancy_projection, object resources | Verify retention flag, effective master/build type, actors/NPCs and groups. Classify representative installed objects with source-backed per-case results; no indiscriminate delete/retain rule. | Existing source audit |
| P02 Open | BUILD/SAVE → lot values, family_overlay | Compute current asset value, configurable architecture factor and rounding; compare values before/after object changes, save and eviction. No use of original purchase price as an invented refund. | P01 |
| P03 Open | SAVE → session_archive/bundle, family journal | Immutable property payload, baseline pin/revision and ONE overlay CAS for property/vacancy/refund. Exercise conflict, failed preparation, missing/corrupt head and recovery without resurrecting stale property. | P01/P02; design document |
| P04 Open | SAVE/UI → game entry, transfer, renderer | Play/change lot → save → evict → enter with family → reopen. Retain proper building state, dispose/refund correct goods, preserve personal progress, never restore prior family VMs. | P03 |
| VM01 Partial | VM → vm/world | Inventory every TS1 primitive/operand/entry point and implemented rejection; execute representative real scripts and compare effects/errors to pinned source. File existence is insufficient. | DATA01 |
| VM02 Partial | VM → scheduler/controllers | Complete callbacks, immediate calls, object lifecycle and queue cancellation/removal under load; save/reopen active interactions with identical continuation. | VM01 |
| SIM01 Partial | SIM → motives/clock/autonomy | Autonomous day-long household: needs, room score, sleep and object choice advance/persist; bounded deterministic restart and failure logs. | VM02/ROUTE01 |
| SIM02 Partial | SIM → relationships/social | Friendship/romance/family links and social outcomes mutate correctly across save, travel and neighborhood updates. | SIM01/SAVE01 |
| SIM03 Open | SIM/PROVIDERS → careers/skills | Job selection, work schedule, pay/promotion and skills/interests advance and persist, with original-day boundaries. | SIM01/SAVE01 |
| SIM04 Partial | SIM → inventory | Acquire/use/consume items and currencies, persist global neighbor-owned state and prevent duplication across houses. Read-only inventory is not completion. | SAVE01/P03 |
| SIM05 Open | SIM/POSE → age/death/NPC lifecycle | Children/babies, aging rules, death/ghosts, visitors and services follow TS1 behavior and retain correct neighborhood identities. | VM02/SIM02 |
| F01 Partial | UI/PROVIDERS → CAS/private family | Create adults/children, edit names/looks/personality, archive/delete eligible families; enter/play/save/reopen through actual UI. | DATA01/AV01/SAVE01 |
| F02 Open | SAVE/PROVIDERS → family transfer | Import/adopt original families/saves without losing skills, relationships or unsupported state; explicit actionable rejection where not implemented. | P04/F01 |
| SAVE01 Partial | SAVE → codecs/restorers | Cover every supported ongoing action and world mutation, including identity/resource checks, older versions and meaningful recovery. Selected Sim UIST exists; general save coverage is still partial. | VM02/DATA01 |
| ROUTE01 Partial | ROUTE → routing/portals | Household walks through doors/stairs, changes floors and reaches objects; moving obstacles and cancellations do not strand/corrupt actors. | VM01/ARCH01 |
| ROUTE02 Open | ROUTE/BUILD → object placement | Evaluate object walls/floors/slopes/footprints/intersections/slots and placement trees; place, rotate, reject and drop held objects without guessed positions. | ROUTE01/P01 |
| B01 Partial | UI/PROVIDERS → object catalog | Complete filtered/localized catalog with prices/variants/expansion/CC resources and stable item identity; choose actual objects. | DATA01; CC01 acceptance follows B02 |
| B02 Open | BUILD/UI → native buy/sell service | Buy → preview → validate placement → commit money/world → sell/remove → save/reopen, including groups and rollback. | B01/ROUTE02/P02/P03 |
| ARCH01 Partial | BUILD/DATA → prepared architecture | Unified mutable geometry/rooms used by simulation, routing and rendering; saved readers/previews alone do not satisfy it. | DATA01 |
| ARCH02 Open | BUILD/UI → architecture tools | Draw/remove walls/fences, floors/wallpaper, terrain/water, stairs/doors/windows and roofs; correct cost/undo, rooms and path updates. | ARCH01/B02 |
| ARCH03 Open | SAVE/BUILD → architecture payload | Save edited arrays/material aliases/values and reopen after family exit/re-entry with correct disposition. Never replace architecture with HOST clock globals. | ARCH02/P03 |
| AV01 Partial | POSE/PROVIDERS → appearance | Outfit/wardrobe/accessories, body/skin/age, transformations and resource fallbacks work and persist; no silent dropped appearance data. | DATA01/SAVE01 |
| AV02 Partial | POSE → animation/scene | General blending, head seeking, gestures, scales, imported carry frames and attachments share a validated final pose. Native carry frame0 already demonstrated. | AV01/VM02 |
| G01 Partial | DRAW → lot renderer | Camera/zoom/floors/roofs/cutaways/terrain/walls/objects/avatar picking, transparency/depth and live object changes work together. | ARCH01/AV02 |
| G02 Open | DRAW/HOST → graphics backend | Complete lighting/shadows/weather and replaceable accelerated backend with explicit cache/memory/frame-time budgets. Compare representative scenes; CPU preview does not prove console suitability. | G01/PORT01 |
| AU01 Partial | AUDIO → HIT/audio controller | General HIT calls/notes/loops/object ownership, spatial effects, cancellation and streaming music/voices with real playback. | VM02/DATA01 |
| AU02 Open | AUDIO/HOST → audio backend | Device restart/settings, transitions/weather, formats and bounded mixing through injected backend; physical listening distinct from dummy PCM. | AU01/PORT01 |
| UI01 Partial | UI → map/live/family panels | Enumerate every screen/control/dialog and source owner before porting; same-app navigation, selection, options and errors are complete workflows. | F01/P04/G01 |
| UI02 Open | UI → buy/build controls | Integrated buy/build catalog, tool selection, preview/rotate/cancel/undo and money feedback using the same transactional services. | B02/ARCH02 |
| UI03 Open | UI/DATA → original UI | Recreate original layouts/assets/cursors/menus and interactions after functional integration; include all UI01/UI02 controls, not a reskin of a partial HUD. | UI01/UI02 |
| LANG01 Partial | UI/DATA → text/localization | Persist/change language, actual dialogs/catalog/CAS and accents/fallback; quick representative checks of game-provided text, no exhaustive text audit requirement. | UI01/DATA01 |
| DATA01 Partial | DATA/PROVIDERS → readers/catalogs | Format/provider/override matrix with resource identity, malformed input, versions and lookup rules; trace shared dependencies actually used by TS1. | Structural source inventory |
| CC01 Partial | DATA/UI/VM → CC integration | Install/read custom objects/skins/architecture, buy/use/save/reopen and report missing dependencies safely. Catalog listing alone is insufficient. | DATA01/B02/AV01 |
| E01 Open | Cross-cutting → Livin' Large | Identify active TS1 content/services, complete representative expansion object/event with persistent outcome; then audit remaining pack requirements. | VM02/SIM01 |
| E02 Open | Cross-cutting → House Party | Party setup, guests/activities and persistent outcome through actual play; audit pack-specific objects and services. | SIM02/VM02/AU01 |
| E03 Open | Cross-cutting → Hot Date | Home → Downtown with family → social/activity → save/reopen → home, preserving personal/home state. | P04/SIM02/UI01 |
| E04 Open | Cross-cutting → Vacation | Eligible family books/travels/stays, uses vacation activity, resumes and returns; account for visitors and occupancy rules. | E03/SIM04 |
| E05 Open | Cross-cutting → Unleashed | Expanded neighborhood/Old Town, pets acquisition/control/progression and persistent return-home flow, not only map selection. | SIM05/E03 |
| E06 Open | Cross-cutting → Superstar | Studio travel, actual career activity/fame/rewards and save/return; audit Studio catalog and services. | SIM03/E03 |
| E07 Open | Cross-cutting → Makin' Magic | Magic currency/inventory/spell progression and eligible Magic Town travel/move, save/reopen/return without losing residential state. | SIM04/E03/P04 |
| PORT01 Partial | HOST/SHARED → service interfaces | Keep filesystem, window/input, graphics/audio, clocks/threading outside shared simulation. Measure budgets and replace dependencies before desktop assumptions spread. | Applies to every package |
| PORT02 Open | HOST → New 3DS | Native data/VM fixtures exist; validate actual graphics/audio/input/storage and measured memory/CPU on emulator then hardware. No full-game claim from fixtures. | PORT01/G02/AU02 |
| PORT03 Open | HOST → Vita | Toolchain/dependencies, native package, controls/graphics/audio/storage and representative gameplay with measured budgets. | PORT01/G02/AU02 |
| PORT04 Open | HOST → Android | Touch/gamepad, lifecycle/pause/storage/audio and representative save/resume on device. | PORT01/G02/AU02 |
| PORT05 Open | HOST → Switch | Verify viable toolchain/distribution/hardware route, then native backend and gameplay; no support promise before evidence. | PORT01 |
| PORT06 Open | HOST → Windows | Native desktop build/package and actual resource/audio/input/save checks without CLR. | PORT01 |
| PORT07 Open | HOST → macOS ARM/Intel | Both native build/package architectures; execution remains unverified without suitable runners/hardware. | PORT01 |
| PORT08 Open | HOST → Wii U desired | Bounded viability study of toolchain, dependencies, memory and graphics before promising a port. | PORT01 |
| QA01 Partial | All → logs/release gates | Crash logs/recovery, clean first run, installed-data GUI sessions, missing packs/CC, long play and reproducible package checks. Distinguish synthetic, native GUI and hardware evidence. | Per-package gates |

## Delegation and closure

Use disjoint file ownership for parallel changes. Agree shared interfaces first;
serialize integration checks and review stateful changes independently.

Before closing any package require: source/revision + native files; supported
and unsupported behavior; integrated acceptance run; save/reopen and failure
preservation where stateful; resource budgets; independent review; evidence and
remaining gaps linked in the feature matrix. Only mark complete when that whole
package's scope is covered. Full project completion still requires every scope
in the playable criteria, original UI, seven packs, CC and platform claims backed
by their own evidence. No estimate of percentage follows from file counts.
