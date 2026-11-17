# Changelog

Notable changes for players and contributors, following
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## Unreleased

### Changed

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
- Autonomous action scoring follows the original minimum and personality
  calculation. Actions whose checks supply their effects can now be considered;
  other differences in autonomous behavior remain under review.
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
