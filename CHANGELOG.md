# Changelog

Notable changes for players and contributors, following
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## Unreleased

### Changed

- Newly arriving neighbors use the original initialization order, profile
  fields and starting needs. This also removes an online-game state change
  that does not belong in The Sims 1. Broader autonomy differences remain
  under comparison.
- Character Detail changes reach Sims outside the current view as well as
  visible Sims, preserving the original material-setting behavior. Full Sim
  lighting is still under comparison.
- Older behavior scripts retain their declared arguments and local variables.
  Saves from earlier debug builds can still load the affected actions.
- Sims stop repeating the same route when they make no progress around a
  blocking object. In the Novato household, this lets a visitor continue
  to the next destination.
- Restoring an incomplete neighborhood reports failed copies and preserves
  older backups.
- Reaching for objects uses the original height and animation-event rules,
  including child and seated animations and pickup/put callbacks. New native
  saves preserve the current step of these actions and require updated builds
  when shared between platforms.
- Fixed loading some saves from earlier experimental builds, preserving their
  object relationships and the loading order needed to continue playing.
- Sims can autonomously join activities already being used by another Sim,
  using the original eligibility checks and selection rules.
- Sims keep their walking animation and direction when passing through doors,
  following the original movement rules. New saves also retain the movement
  history needed to continue these actions. Compatibility with earlier
  experimental saves remains under validation.
- Objects run their original loading routines when a lot opens, including
  newly created expansion helpers. Helpers that do not belong on the current
  lot can leave normally instead of repeatedly failing.
- Animation events and held-object changes follow the original script rules
  more closely. New saves preserve the associated animation state and require
  updated builds when shared between platforms.
- Interaction positions use the original oriented search and weighted destination
  selection, including the chosen chair's entry action. Walking paths also use
  the original weighted regions, corner refinement and final-facing rules.
  Movement and animation timing differences remain under comparison.
- Walking and turning share a movement request that can survive saving and
  reopening a house. New saves require updated builds when transferred between
  platforms. Original-game save behavior is still under comparison.
- Roofs use the original game's piece shapes, texture coordinates and placement,
  including their response to terrain and upper floors. Character Detail follows
  the original material history when roofs change or house pictures are taken.
  Texture filtering remains under comparison.
- The dedicated roof view and the three wall views have separate selections.
  Floor controls respect the current mode, and applying a roof style shows it.
- Saving a changed house refreshes both sizes of its map pictures and their
  placement information. Picture capture uses daytime and high terrain detail,
  then restores the live view without advancing the simulation.
- Ordinary grass uses the original color tables, detail levels and wall-edge
  shading in all four camera directions. Other terrain and scene-lighting
  differences remain under review.
- Floors, walls and ordinary objects use the original room-lighting palette
  and day/night limits. Standard floors and walls also use the game's local
  lamp-light masks. Sim shading, terrain, roofs and special surfaces still
  need further work.
- Studio Town uses separate music for its map and public lot editor. Entering
  an editor no longer restarts the opening music, and switching between its
  Buy and Build modes keeps the current song playing.
- Autonomous action priorities use the original need calculations and order of
  operations, including small gains near the decision threshold. Checks that
  change an action's effects remain supported; other differences in autonomous
  behavior remain under review.
- Entering a residential house from the map fades out the map music instead
  of restarting the opening music. The fade timing is still being compared.
- Autonomous choices use the original lottery among the best eligible actions,
  with the group size affected by Logic. The selected game data supplies the
  tuning values. Other differences in autonomous behavior remain under review.
- Changing between Buy and Build keeps the current song playing until it
  finishes, as observed in the original Complete Collection.
- Music diagnostics record the requested event and each track that actually
  starts, helping identify unexpected songs during screen and mode changes.

### Fixed

- Sims can plan a path while seated and leave the chair to reach another object,
  including a bed.
- Preserve the saved position information used by pathfinding for carried and
  off-lot objects. Prevent a crash when a path check moves or contains an object.
- Saving and reopening preserves the object traversal order and room-manager
  history used by pathfinding. Full original-game continuation remains under
  comparison; sharing new saves requires updated builds on every device.
- Autonomous action scoring now applies the original room-dependent adjustment
  after an action's checks. Objects in another room no longer advertise the
  same room benefit as nearby objects.

- Newly created objects run their setup in the original order, including
  grouped objects and objects created by another object's script.
- Saving a paused game preserves objects waiting to be removed. Reopening keeps
  their state and finishes removal when play resumes. New saves require the
  updated engine on every device sharing them.
- New walking paths use the original corner sampling, position constraints and
  separate steering and stationary-turn rules. Some pathfinding and final-step
  differences remain under comparison.
- Imported classic saves can resume ordinary walking while preserving queued
  actions and progress. More complex continuation remains under comparison.
  Older native saves keep their recorded motion policy on load.
- Destination checks use the requested footprint and candidate direction, then
  restore the Sim's heading and clear the temporary footprint extension.

- New character animations and walks use the original duration-based clock and
  movement metadata. Animation events keep their owning character layer. Existing
  native saves retain their previous animation and route timing until replaced;
  new saves require an updated engine on every device sharing them. Some walking,
  turning and animation-layer differences remain under comparison.

- Ordinary walking destinations no longer add random score noise that shifts
  later scripted choices.

- The simulation sees the selected Live, Buy, Build or Options mode instead of
  the mode left in the house file. Opening a house also respects its saved Pause
  state; switching panels keeps that preference.

- Lot loading recognizes the original global controller types, including gift
  tokens, and lets their scripts handle residential or public lots. Old catalog
  caches rebuild automatically. Saved native snapshots retain their own objects.

- Random choices during simulation use a saved sequence separate from setup
  and menu checks. Disabled objects no longer advance it without a scripted
  random choice. Existing native saves migrate to the separated states.

- Classic saves load current and previous needs in the original order. Existing
  OpenDollhouse saves retain their values. New saves require an updated engine
  on each platform; older builds refuse them instead of swapping the values.

- Original residential saves restore their saved random state, and saving keeps
  that state up to date. Non-residential reseeding remains under comparison.
- Free Will reaches the simulation when loading or toggling the option. Changing
  unrelated settings preserves the script state used by the original game.

- Scripted object removal waits until the current simulation round finishes,
  allowing eating to finish before its food is removed. Removing a used object
  preserves unrelated pending actions instead of clearing the whole queue.
- Improved nearby positioning, including diagonal requests, facing, wall and
  terrain checks. Full movement parity remains under comparison.
- Repeated terrain edits and saves refresh the terrain data used by movement.
  Current native saves also retain the new routing rules without recreating
  previously removed background objects when reopened.
- Saving retains the full household balance and daily expense history,
  including the tested transition to a new day.
- Sims release objects when their interaction returns, instead of leaving
  stale use counts and occupied space that can block the next action.
- Reopening a saved house no longer rejects an in-progress route just because
  its door connections have not yet been registered during loading.
- New objects receive the current game date. Existing objects retain their
  saved creation date and value when loaded.
- Sims retain their requested and occupied space while walking, turning and
  sitting. Saving and reopening preserves that space and the pending route.
- Job dialogs use the game's localized offers, titles, descriptions and time
  formatting instead of a generated English offer.
- Bills no longer charge for Sims and objects retained outside the lot.
  Their off-lot state also survives saving and reopening a house.
- Loading original-game saves preserves inactive characters and scheduled
  waits instead of immediately running every retained character. Saving and
  reopening keeps the simulation counter and the updated waiting state.
- The first decrease in needs after loading an original-game save uses the
  original simulation counter and initial timing.
- Selecting an unchanged roof style or pitch no longer rebuilds the roof.
- Loading terrain preserves the final byte of literal data and the authored
  edge heights when changing the camera direction or previewing construction.
- Restored the family-friend counter beside the household funds, using the
  original mutual-friendship rule and counter typeface.
- Transparent objects and overlapping lit and unlit parts keep their appearance
  when the renderer rebuilds or reuses their cached images.
- In-lot audio settings use the original eleven volume positions and gain curve.
  Background simulation preserves the player's selected speed and pause state.
- Lighting preferences do not change room satisfaction. Auto Centering controls
  scripted event focus while preserving explicit camera commands.
- Settings captions support illustrated help, including manual help with Quick
  Tips off. Clicking an open settings category closes it, as in the original.
- Corrected priority, waiting and cancellation in ordinary action queues.
  Cancelling a pending command removes it; a running action finishes its cleanup.
  Internal cancellation commands stay out of the player's action bar.
- Loading an original-game queue no longer marks an action as cancelled merely
  because another command has a higher priority. Saves retain the instruction's
  waiting state in the existing continuation format.
- Autonomous choices respect each Sim's action thresholds and rank needs
  without rounding. Action order, tied scores, species and burning-object
  checks follow the original rules in the corrected selection paths.
- Room satisfaction uses the original floor, wall, lighting and object
  calculations, including diagonal rooms and changes made while playing.
- Scripts receive the original day, dusk, night and dawn periods instead of
  treating every hour as daytime.
- Needs retain fractional values during simulation and saving. Sleep, comfort,
  social needs, eating and mood follow the original calculations and game data.
- Actions preserve simultaneous changes to the same need in their original
  order. Clearing a change no longer leaves progress that can cause a sudden
  jump when another action starts.
- Updated saves preserve precise needs and active changes. Previous native
  saves remain readable, but the new continuation format requires an updated
  engine on every device that opens a newly saved game.

## [0.0.1](https://github.com/zenaror/opendollhouse/releases/tag/v0.0.1) — 2026-11-15

First experimental release. Download availability and the source revisions
used for each platform are listed in the release notes.

### Added

- Experimental packages for Linux, Windows, macOS Intel and Apple Silicon,
  Android, New 3DS, Vita and Switch, with platform instructions. Testing varies;
  see the [current status](docs/status.md#other-platforms).
- PC launchers that find the game data without arguments, with saves and cache
  separate from the original installation.
- Initial support for languages, audio, inventory, classic custom content and
  expansion activities, still subject to bugs and behavior differences.
- A first-run language chooser on Vita, Switch and New 3DS. Hold L while
  launching to choose again. Existing neighborhoods keep their saved progress.
- Touch and controller input in the Vita and Switch adapters; external Android
  controllers with touch input preserved. The Vita rear touchpad's left and right
  halves act as L2 and R2.
- Optional Linux tool to prepare game indexes before copying them to a handheld,
  including the selected language's Buy catalog.
- Loading, performance and failure logs, including periodic performance
  measurements on Vita, Switch, Android and New 3DS in `opendollhouse.log`.
- MPL-2.0 for authorized original contributions, with credits, notices and
  corresponding sources in the packages.

### Changed

- The startup picture, music and scrolling messages begin while the game prepares
  the neighborhood. Startup honors custom pictures and music from the installation
  and uses the selected language for the picture, with an English fallback.
  Entering a lot shows an hourglass on PC or a loading notice
  on handhelds. Expansion trips use their original destination artwork.

- When entering a home, the game looks up the outfits its residents wear.
  Custom outfits outside the game index are discovered as needed. The household
  bar reads each resident's face strip independently.
- Switch draws the lot and menus at 800×600 at every zoom level. Vita draws
  the lot at 400×300 at the two closer zooms and keeps the menus at 800×600.
- The engine draws furniture at its original size with less work and reuses
  unchanged control panel artwork. Vita and Switch also update smaller parts of
  the screen when an object moves.
- The handheld Buy catalog reads fewer files when opening a category. Importing
  a neighborhood also skips repeat checks on files the game has already found.
- Documentation organized for milestone 1 players: installation, controls,
  saves, help and limitations. Public contribution instructions are separate
  in `docs/ai_agents/`.
- Public repository documentation and contribution guides are now in English.
  In-game language selection and support for other languages are unchanged.
- Contributor guidance now explains the milestones and PR review criteria,
  keeping the original experience separate from reference-specific QoL changes
  and modernization.
- PC interface restores original panels and resources, including options,
  inventory, interests, needs and help. Presentation still has differences
  and needs further polish.
- Create-a-Sim preview adjusted for adult and child proportions, pose, automatic
  rotation and animation according to the original reference.
- Vita and Switch images adjusted to keep a centered 4:3 aspect ratio without
  stretching or clipping the bottom panel. Device confirmation is still pending.
- Vita and Switch audio continues processing when drawing takes longer.
  This does not confirm that choppy audio on the devices has been resolved.

### Fixed

- Fixed a Vita crash when a sound played from a Sim on a lot.
- Collecting MagiCoins shows their count in magenta. Buying food from a fridge
  shows the price once, while the household pays the usual amount.

- Vita and Switch reuse pixels already on screen when only part of the picture
  changes. The engine also skips drawing triangles for parts of Sims that
  are entirely outside the view.
- Vita and Switch use the resident’s existing portrait in the action wheel.
  Moving the stick no longer checks each object for possible interactions.
- Vita and Switch now use the 3DS limit on simulation time spent between input
  updates. Both start with simpler terrain and Sim shadows off. Existing
  handheld profiles switch to the new defaults once.
- Restored the residents of classic lots that refer to a family's earlier
  character IDs, including the Charming family.
- Fixed saving Old Town lots opened for building when the saved clock was
  partway through a minute.

- Vita and Switch reuse unchanged parts of the lot while Sims move. The
  neighborhood map keeps its roof fades and animated water while drawing
  fewer pixels. New Vita and Switch profiles start with Sim shadows off;
  players can enable them in the graphics options.
- Loading the original cursor art reads each small file once. The family
  selector and control panel reuse prepared artwork, and the handheld portrait
  reader caches appearance names in the private cache directory.

- Portable resource readers keep a limited number of files open during previews.
  File-open failures now include the path and system error in the log. Preview
  discovery avoids unnecessary file checks while retaining deep custom content.
- Reduced repeated interface, portrait and text-table file reads, bitmap scaling
  work and action-menu image copies on Vita and Switch, preserving the original
  layout. Added measurements for the first frame when entering a house.
  Physical-device performance still needs confirmation with the updated builds.
- Vita and Switch reuse unchanged control-panel artwork and send smaller screen
  updates for highlights, portraits and counters. The family picker reuses its
  current page until the contents or selection change. Dialogs in the neighborhood,
  family editor and live mode reuse their original artwork.
- Fixed the magic starter package delivery in translated games: the Mystery Man
  now wears his own costume and keeps his localized name.
- Vita house-loading crash caused by unsupported integer formatting in its
  system library. Log capture now handles both kinds of SDK output streams;
  periodic input measurements help investigate stalls on the device.
- macOS packages no longer force Brazilian Portuguese. They offer the shared
  first-run language choice, remember it and include `ChooseLanguage.command`
  to select another language without command-line arguments.
- Saved lot thumbnails preserve their proportions and reflect yard changes,
  avoiding dark grass bands, clipped trees and fragments of street objects.
- House loading and character records corrected for expansion formats.
  Import preserves existing families, houses and deletions.
- Saving and loading corrected in cases involving active actions, pending
  questions, removed visitors and objects contained in furniture. This does
  not guarantee universal save compatibility with the original game.
- Buying, building and travel received fixes for catalogs, placement, object
  preservation and returning travelers. Some combinations still need testing.
- Portraits, action queues, locale-specific clocks, lighting on mirrored objects
  and needs panels received presentation fixes.
- Magic coins are no longer clipped in family selection; career, personality
  and object information and descriptions were restored from game data.
- Family selection and pet names corrected to preserve identities and
  responses when saving and continuing.
- Characters leaving a lot are no longer brought back to the mailbox. Also
  corrected Drew's departing car movement and Japanese needs-panel spacing.
- Vita and Switch file paths corrected to find player-provided neighborhoods,
  keeping saves separate from game data.

For installation and updates, see the [player guide](docs/README.md).
Builds remain experimental; a fix in one playthrough does not validate every
combination in the game or every device.
