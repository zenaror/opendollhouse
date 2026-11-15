# Choose a package

The available test builds are **experimental**. They may contain bugs and run
at different speeds on different devices. There is no stable public release yet.

## Find the package

If you received a build, choose the file for your system and read its included
`README.md`. It gives the package date and its known limits.

In a checkout used to prepare builds, current packages are kept under
`build/releases/debug/`, with one folder per platform. These are build outputs
and do not come with a clone of the repository. This guide does not announce a
public download link.

| System | Folder | File to install or extract |
| --- | --- | --- |
| Linux x86_64 | `linux` | `OpenDollhouse-Linux-debug.tar.gz` |
| Windows x86_64 | `windows` | `OpenDollhouse-windows-x86_64.tar.gz` |
| macOS Intel | `macos-intel` | `OpenDollhouse-macos-x86_64-pt-BR-launcher.tar.gz` |
| macOS Apple Silicon | `macos-arm64` | `OpenDollhouse-macos-arm64-pt-BR-launcher.tar.gz` |
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
