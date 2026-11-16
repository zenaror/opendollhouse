# Current status

OpenDollhouse is at **milestone 1**, its first experimental version. It is
playable on Linux: open a family, perform actions, buy and build, save, exit and
continue. Those playthroughs were tested, but they do not cover every possible
situation in a saved game.

This milestone aims to combine functionality from the Simitone references
while preserving the PC The Sims experience. **Bugs and differences remain;
this version is not a complete reproduction of the original game.** New
expansions and a mod programming interface are not part of the current delivery.

## What to expect when playing

- Neighborhoods, families, home life, buying and building are available, with
  limitations in combinations of actions, objects and lots.
- Expansion travel and activities were exercised in specific cases. This does
  not guarantee that every activity behaves as in the original game.
- The interface uses original game resources, but differences in scale,
  portraits, icons, help and behavior may remain.
- Language selection depends on the text and fonts available in your data.
  Some screens may still contain untranslated text.
- Classic custom content may work, but compatibility must be checked
  individually. There is no public mod API.
- Back up your saves. Do not assume universal compatibility with the original
  executable or with other engine versions.

If you encounter a problem, see [how to report it](troubleshooting.md#report-a-problem).
A fix in one build does not mean an older package for another platform already
includes it; check the package date.

## Other platforms

This table describes the available test packages. An emulator result does not
prove performance on the real device.

| Platform | Experimental package status |
| --- | --- |
| Linux x86_64 | Playable in local tests, including saving and continuing. Bugs and differences remain. |
| Windows x86_64 | The current build opened the neighborhood and Create-a-Sim in Wine and exited normally. Native Windows testing is still needed. |
| macOS Intel and Apple Silicon | Packages include a first-run language choice and remember it; no fixed language. Compilation and package checks do not replace a Mac runtime test, which is still needed. |
| Android ARM64 | An earlier build opened on a device. The current APK includes external controller support and periodic performance logs; it still needs another device test. |
| New Nintendo 3DS | Hardware logs show live gameplay. The exact package used was not recorded. |
| PlayStation Vita | A player entered an occupied lot, saved and exited on the console. Play remained below the 30 FPS target. |
| Nintendo Switch | A player entered an occupied lot, opened the action menu, saved and exited on the console. |
| Old Nintendo 3DS (desired) | No game package available to install; New 3DS results do not establish support. |

Current packages are listed in [which package to use](testing-builds.md).
The gameplay target is **at least 30 FPS** on each device. In a physical test
of the same occupied house, five-second readings while the simulation was
running ranged from **54–59 FPS on Switch** and **7–11 FPS on Vita**. Both
devices saved and exited. The logs did not identify the exact build; other
lots can run differently, and these readings can hide brief stalls.

## Limitations affecting tests

- Vita players may experience long waits entering lots and saving. Audio
  smoothness and input response still need checking during longer sessions.
- Switch controllers have a limitation when physically disconnected. Release
  buttons before disconnecting; restart the game if an input remains active.
- Player reports include gaps in cutaway walls and startup music playing
  when opening a Studio Town lot for editing. Both await comparison with
  the original Complete Collection.

See [installation](installation.md) and [controls](controls.md) for usage steps.
This page describes the current version; it does not promise dates or complete
support for every system.
