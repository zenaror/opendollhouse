# OpenDollhouse

<a href="https://www.buymeacoffee.com/zenaro147" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" ></a>

An independent **C/C++** engine for playing The Sims 1 with files from your own
installation. The project aims to preserve the look and experience of the
original PC game, including its seven expansion packs.

**This is the first experimental version, milestone 1.** It is playable on
Linux, with bugs and differences still present. Simitone is the functional
reference for this milestone; compatibility is not complete. See the
[current status and limitations](docs/status.md).

## Start playing

You need the files from a complete **The Sims Complete Collection** installation.
They are not included with OpenDollhouse. Use an installed or extracted game
folder: an ISO alone is not enough. The engine does not require the original
executable or an inserted CD. Full Legacy Collection compatibility has not yet
been confirmed.

1. Obtain the test package for your system. Packages prepared in a development
   checkout are in `build/releases/debug/<platform>/`; these generated files
   are not included in a clone of the source repository.
2. On Linux and Windows, extract the entire `odl-launcher` folder into the game
   installation, next to `GameData` and `UIGraphics`. On macOS, keep the
   `odh-launcher` folder name used by the package.
3. Open the package launcher without arguments. For Android and consoles,
   follow the platform instructions below.

**[Installation instructions for each platform →](docs/installation.md)**

Experimental packages exist for Linux, Windows, macOS Intel/Apple Silicon,
Android, New 3DS, Vita and Switch. Testing varies by platform; a package does
not guarantee performance or every feature on that device. There is no stable
public release yet, and no game package for Wii U or Old 3DS.
See [which package to use](docs/testing-builds.md).

## Playing and getting help

- [Installation, language and saved games](docs/installation.md).
- [Keyboard shortcuts](docs/keyboard-controls.md).
- [Console and Android controls](docs/controls.md).
- [Troubleshooting and reporting a problem](docs/troubleshooting.md).
- [Recent changes](CHANGELOG.md).

OpenDollhouse saves are separate from the original game files. Back up your
saved games before updating a test build.

## Contributing

The engine code is in `engine/sims3ds/`; platform adapters are in
`engine/platform/`. To build the project or contribute with an AI agent,
see the [contributor guides](docs/ai_agents/README.md).
Playing a prepared package does not require a compiler or .NET.

## Credits and license

The project uses a Sims3DS codebase and references Simitone, FreeSims and FreeSO.
We preserve their authors' credits and licenses. Authorized original
contributions use [MPL-2.0](LICENSE); other components retain their own terms.
See [credits](docs/references.md) and [license notices](NOTICE.md).
The Sims and its expansions belong to Maxis/Electronic Arts; their data is not
distributed here.
