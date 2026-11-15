# Changelog

Notable changes for players and contributors, following
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
There is no stable public release yet.

## Unreleased

### Added

- Experimental packages for Linux, Windows, macOS Intel and Apple Silicon,
  Android, New 3DS, Vita and Switch, with platform instructions. Testing varies;
  see the [current status](docs/status.md#other-platforms).
- PC launchers that find the game data without arguments, with saves and cache
  separate from the original installation.
- Initial support for languages, audio, inventory, classic custom content and
  expansion activities, still subject to bugs and behavior differences.
- Touch and controller input in the Vita and Switch adapters; external Android
  controllers with touch input preserved. The Vita rear touchpad's left and right
  halves act as L2 and R2.
- Loading, performance and failure logs, including periodic performance
  measurements on Vita, Switch, Android and New 3DS in `opendollhouse.log`.
- MPL-2.0 for authorized original contributions, with credits, notices and
  corresponding sources in the packages.

### Changed

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

- Portable resource readers keep a limited number of files open during previews.
  File-open failures now include the path and system error in the log. Preview
  discovery avoids unnecessary file checks while retaining deep custom content.
- Reduced repeated interface, portrait and text-table file reads, bitmap scaling
  work and action-menu image copies on Vita and Switch, preserving the original
  layout. Added measurements for the first frame when entering a house.
  Physical-device performance still needs confirmation with the updated builds.
- Reduced repeated HUD image conversions and copies while preserving its pixels,
  portrait button states, needs arrows and language-specific layout.
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
