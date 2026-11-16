# Choose a package

Choose the file for your system under **Assets** on the [v0.0.1 release page](https://github.com/zenaror/opendollhouse/releases/tag/v0.0.1).
These builds are experimental. Read the package instructions before updating.

## Find the package

The folder column is for people working from a source checkout. Local builds
are stored under `build/releases/debug/`.

| System | Folder | File to install or extract |
| --- | --- | --- |
| Linux x86_64 | `linux` | `OpenDollhouse-Linux-debug.tar.gz` |
| Windows x86_64 | `windows` | `OpenDollhouse-windows-x86_64.tar.gz` |
| macOS Intel | `macos-intel` | `OpenDollhouse-macos-x86_64-launcher.tar.gz` |
| macOS Apple Silicon | `macos-arm64` | Separate test build; no arm64 Mac download in v0.0.1 |
| Android ARM64 | `android` | `OpenDollhouse-arm64-debug.apk` |
| New Nintendo 3DS | `new3ds` | `OpenDollhouse-new3ds.zip` |
| PlayStation Vita | `psvita` | `OpenDollhouse.vpk` |
| Nintendo Switch | `switch` | `OpenDollhouse-Switch-debug.tar.gz` |

The Switch archive contains **one installable file**:
`switch/OpenDollhouse/opendollhouse.nro`. Files marked `SUPORTE-relink` contain
source code and support material for developers. They are not another version
of the game to install.

Each platform has been tested to a different extent. Check the
[current status](status.md#other-platforms) before choosing a device to
play on. Old 3DS support is desired, but no game package is available yet.

## Install or update

Follow the [installation guide](installation.md). The packages do not include
The Sims data: you must supply your own complete installation.

Close the game before updating, keep the package files together and preserve
your saves. Do not use the build delivery folder as a save folder.
Also keep the licenses and corresponding source code supplied with the build.
