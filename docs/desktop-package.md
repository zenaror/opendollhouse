# Start on PC

You need a complete installation of The Sims Complete Collection and the
OpenDollhouse package for your computer. The package does not include the
commercial game data.

| System | Folder to place inside the installation | File to open |
| --- | --- | --- |
| Linux | `odl-launcher` | `opendollhouse` |
| Windows | `odl-launcher` | `OpenDollhouse.cmd` |
| macOS Intel or Apple Silicon | `odh-launcher` | `OpenDollhouse.command` |

Extract the package and place **the whole folder**, including all libraries,
beside `GameData`, `UIGraphics` and `UserData`. Do not move only the executable.
Then open the file listed in the table **without parameters**.

```text
The Sims/
  GameData/
  UIGraphics/
  UserData/
  odl-launcher/     (Linux and Windows)
```

On Mac, use `odh-launcher` instead of `odl-launcher`. Choose the correct Intel
or Apple Silicon package. The first launch asks for your game language and
remembers it. Open `ChooseLanguage.command` to choose again; no arguments are
needed and no language is forced.
Execution of these packages still needs confirmation on a Mac.

## Your saves

The program uses the original neighborhoods as a starting point and saves its
own copy in another folder:

| System | Saves | Logs and cache |
| --- | --- | --- |
| Linux | `~/.local/share/opendollhouse` | `~/.cache/opendollhouse` |
| Windows | `%APPDATA%\opendollhouse\saves` | `%APPDATA%\opendollhouse\cache` |
| macOS | `~/Library/Application Support/opendollhouse/saves` | `~/Library/Caches/opendollhouse` |

These are the default paths. Custom settings can change them. Copy your saves
before updating, and never use the original `UserData` folder as the destination
for OpenDollhouse saves.

To update, close the game and replace the launcher folder with the new package,
keeping your game data and saves. Read the [steps for each system](installation.md)
for permissions, dependencies and troubleshooting. On Linux, also see the
[package guide](linux-package.md).
