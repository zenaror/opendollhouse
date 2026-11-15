# OpenDollhouse for Linux

Use the **Linux x86_64** package. It is experimental and needs the data from your
installation of **The Sims Complete Collection**. You do not need a compiler,
Python or .NET to use the ready-made package.

## Install and launch

1. Extract the package.
2. Place the entire `odl-launcher` folder inside the game installation, beside
   `GameData`, `UIGraphics` and `UserData`.
3. Open `odl-launcher/opendollhouse` without parameters.
4. Choose your language when you first open it.

```text
The Sims/
  GameData/
  UIGraphics/
  UserData/
  odl-launcher/
    opendollhouse
    bin/
    licenses/
```

To launch from a terminal:

```sh
cd "/path/to/The Sims/odl-launcher"
./opendollhouse
```

If the copy lost its execute permission:

```sh
chmod +x opendollhouse bin/opendollhouse
```

## Required libraries

The current build requires **glibc 2.38 or later**. Compatibility with older
distributions has not yet been confirmed. It also uses SDL2, zlib, Pango/Cairo
and fonts containing the characters of your chosen language.

On Debian, Ubuntu and Linux Mint, install the main libraries with:

```sh
sudo apt install libsdl2-2.0-0 zlib1g libpangocairo-1.0-0
```

The package's `BUILD.json` lists the exact requirements for that build.
If the program does not open, run it from a terminal and keep the message shown.

## Saves, language and logs

- **Saves:** `~/.local/share/opendollhouse`.
- **Log:** `~/.cache/opendollhouse/opendollhouse.log`.
- **Language:** run `./opendollhouse --choose-language` to choose again.
  To use Portuguese only for this session, run `./opendollhouse --language pt-BR`.

These are the default paths. They change if you set `XDG_DATA_HOME`,
`XDG_CACHE_HOME`, `ODH_SAVE_ROOT` or `ODH_CACHE_ROOT`.

To keep the launcher outside the installation, provide the game data folder:

```sh
./opendollhouse "/path/to/The Sims"
```

Back up your saves before updating. Do not replace the original `UserData` with
OpenDollhouse saves. When reporting an error, include the steps, distribution,
package used and log file.

See the [version status](status.md), [shortcuts](keyboard-controls.md) and
[complete installation guide](installation.md). Keep the supplied licenses and
source code when redistributing a package; commercial game data is supplied
separately.
