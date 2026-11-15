# Install and launch

This guide covers ready-made OpenDollhouse packages. The milestone 1 version is
experimental: Linux is playable, but differences and bugs remain. Confirmation
on other systems varies. Check the
[platform status](status.md#other-platforms) before installing.

Use the package provided by the project for your system. In a checkout used to
prepare builds, local test packages are kept in
`build/releases/debug/<platform>`. Each folder has a `README.md` with instructions
for that package. See also [test builds](testing-builds.md).
To compile it yourself, see the [build guide](ai_agents/building.md).

## Prepare the game files

You must supply a complete installation of **The Sims Complete Collection**.
These files do not come with OpenDollhouse. Full compatibility with Legacy
Collection has not yet been confirmed.

Copy the installation contents without changing the folder structure. Do not
copy only `GameData` or only `UserData`. The structure includes:

```text
The Sims/
  GameData/
  UIGraphics/
  UserData/
    Neighborhood.iff
    Houses/
    Characters/
  ExpansionPack/
  ExpansionPack2/
  ...other installation files and folders...
```

On devices that use a folder called `game`, place these folders directly inside
it: `game/GameData`, `game/UIGraphics`, `game/UserData`, and so on.
Avoid an extra layer, such as `game/The Sims/GameData`.

An ISO alone cannot be used yet: use files from an extracted or existing
installation. The engine does not run the original program or require an
inserted CD. On first launch, it may prepare copies of the neighborhoods for
its own saves and create cache files.

## Linux

Use the **Linux x86_64** package.

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

If the copy lost its execute permissions:

```sh
chmod +x opendollhouse bin/opendollhouse
```

- **Saves:** `~/.local/share/opendollhouse`.
- **Log:** `~/.cache/opendollhouse/opendollhouse.log`.

The current build requires glibc 2.38 or later and libraries such as SDL2 and
Pango/Cairo. See dependencies and language selection in the
[Linux guide](linux-package.md).

## Windows

Use the **Windows x86_64** package.

1. Extract the package until you have the `odl-launcher` folder.
2. Place that entire folder inside the game installation, beside `GameData`.
3. Keep the DLLs and other files in `bin` together.
4. Open `odl-launcher\OpenDollhouse.cmd` without parameters.

To open in Brazilian Portuguese, run this from Command Prompt inside
`odl-launcher`:

```bat
OpenDollhouse.cmd --language pt-BR
```

- **Saves:** `%APPDATA%\opendollhouse\saves`.
- **Log:** `%APPDATA%\opendollhouse\cache\opendollhouse.log`.

The current package opened the neighborhood and Create a Family in Wine.
Execution on native Windows still needs confirmation.

## macOS: Apple Silicon and Intel

Choose the package matching your computer:

| Mac | Package |
| --- | --- |
| Apple Silicon, such as M1, M2 and later | `OpenDollhouse-macos-arm64-launcher.tar.gz` |
| Intel | `OpenDollhouse-macos-x86_64-launcher.tar.gz` |

1. Extract the package.
2. Place the entire **`odh-launcher`** folder inside the game installation.
3. Open `odh-launcher/OpenDollhouse.command` and choose the game language on
   first launch. Your choice is remembered.

On Mac, the name is `odh-launcher`; Linux and Windows use `odl-launcher`.
Keep the folder name and all its contents. No language is forced. To change
your saved choice later, open `odh-launcher/ChooseLanguage.command`.

To launch from Terminal:

```sh
cd "/path/to/The Sims/odh-launcher"
./OpenDollhouse.command
```

If the copy lost its permissions:

```sh
chmod +x OpenDollhouse.command ChooseLanguage.command bin/opendollhouse
```

- **Saves:** `~/Library/Application Support/opendollhouse/saves`.
- **Language preference:** `~/Library/Application Support/opendollhouse/config`.
  An explicit `XDG_CONFIG_HOME` overrides this location.
- **Logs:** `~/Library/Caches/opendollhouse/launcher.log` and
  `~/Library/Caches/opendollhouse/opendollhouse.log`.

The packages have not yet been run on a Mac and are not notarized by Apple.
If the system blocks launch, keep the displayed message for your report.

## Android

Use `OpenDollhouse-arm64-debug.apk`, for **Android 8 or later on ARM64**.

1. Transfer the APK to the device and open it to install. If prompted, allow
   installation by the app opening that file.
2. Open OpenDollhouse once. The start screen creates and displays the data folder.
3. Copy the complete installation contents into the indicated `game` folder.
4. Return to the app, select `pt-BR` or another language and tap **Play**.

A common path is:

```text
/sdcard/Android/data/org.opendollhouse/files/game/
  GameData/
  UIGraphics/
  UserData/
  ...other installation files and folders...
```

Use the path displayed on your device. If the file manager restricts access to
`Android/data`, you can copy from a computer with ADB and USB debugging already
authorized:

```sh
adb install -r OpenDollhouse-arm64-debug.apk
adb push "/path/to/The Sims/." /sdcard/Android/data/org.opendollhouse/files/game/
```

This example assumes that the app displayed the same path. The `-r` option
updates an existing installation when its signature is compatible.

Saves are kept in the internal `files/saves` folder; the log is at
`cache/engine/opendollhouse.log`. With the test APK, you can copy them to a
computer:

```sh
adb exec-out run-as org.opendollhouse tar -C files -cf - saves > opendollhouse-saves.tar
adb exec-out run-as org.opendollhouse cat cache/engine/opendollhouse.log > opendollhouse-android.log
```

**Do not uninstall to update without backing up your saves:** uninstalling can
remove the app's data and the game copy in its external folder.

Touch remains available. The current build also includes the
[controller mapping](controls.md#switch-vita-and-android-with-a-controller), which still
needs to be tested again on the device.

## PlayStation Vita

The Vita needs a homebrew environment with **VitaShell** available.

1. Transfer `OpenDollhouse.vpk` to the device.
2. Install the VPK through VitaShell. To update, install over the previous
   version, keeping your data and saves.
3. Copy the game installation contents to `ux0:/data/OpenDollhouse/game`.
4. Open OpenDollhouse from the home screen.

```text
ux0:/data/OpenDollhouse/
  game/
    GameData/
    UIGraphics/
    UserData/
      Neighborhood.iff
      Houses/
      Characters/
    ...other installation files and folders...
  saves/
  cache/
```

VitaShell may show the same location as `ux0:data/OpenDollhouse/game`.

- **Saves:** `ux0:/data/OpenDollhouse/saves`.
- **Log:** `ux0:/data/OpenDollhouse/cache/opendollhouse.log`.

An earlier build opened the neighborhood on the device. The current build
corrects the 4:3 fit and includes front touch and triggers on the rear touchpad
halves; see [controls](controls.md#vita-rear-touchpad). These changes and
performance still need confirmation on Vita. This adapter does not offer an
initial language choice.

The source files and `SUPORTE-relink` files supplied with the VPK support
development and redistribution. You do not need to install them to play;
keep them if you share the complete package.

## New Nintendo 3DS

Use `OpenDollhouse-new3ds.zip`. The device needs a homebrew environment with
**Homebrew Launcher** available.

1. Extract the ZIP and copy the `3ds` folder to the SD card root.
2. Copy the complete game data to `3ds/OpenDollhouse/game`.
3. Open `opendollhouse.3dsx` through Homebrew Launcher.

```text
SD:/3ds/OpenDollhouse/
  opendollhouse.3dsx
  opendollhouse.smdh
  game/
    GameData/
    UIGraphics/
    UserData/
    ...other installation files and folders...
  saves/
  cache/
```

- **Saves:** `SD:/3ds/OpenDollhouse/saves`.
- **Log:** create an empty file called `debug`, without an extension, in
  `SD:/3ds/OpenDollhouse/`. The next run writes
  `SD:/3ds/OpenDollhouse/cache/opendollhouse.log`.

The current version keeps two screens and the
[3DS controls](controls.md#new-nintendo-3ds). The package does not contain a CIA
installer. In the emulator, a house opened and it was possible to return to the
map, but a warning appeared on exit. Operation and speed on the physical device
still need confirmation.

## Nintendo Switch

Use **`OpenDollhouse-Switch-debug.tar.gz`**. It contains one game to install;
`SUPORTE-relink` is a development supplement.

1. Use a homebrew environment capable of opening `.nro` files.
2. Extract the package's `switch/OpenDollhouse` folder to the SD card root.
3. Copy the complete game data to `SD:/switch/OpenDollhouse/game`.
4. Open `opendollhouse.nro` from the homebrew menu.

```text
SD:/switch/OpenDollhouse/
  opendollhouse.nro
  game/
    GameData/
    UIGraphics/
    UserData/
    ...other installation files and folders...
  saves/
  cache/
```

- **Saves:** `SD:/switch/OpenDollhouse/saves`.
- **Log:** `SD:/switch/OpenDollhouse/cache/opendollhouse.log`.

An earlier build opened the neighborhood on the device. The current build
corrects the stretched image and includes controls, touch and performance logs;
these changes still need to be tested again on Switch. The emulator attempt
ended before the map and does not confirm gameplay for this build.

See the [controls](controls.md#switch-vita-and-android-with-a-controller) and
[performance reporting guide](console-diagnostics.md). Release the triggers
before disconnecting the controller. Docked mode, suspend and resume also need
their own confirmation.

## Old Nintendo 3DS

Old 3DS support is desired, but there is no game package for it yet.
The New 3DS package does not prove compatibility with Old 3DS. Check the
[platform status](status.md#other-platforms) before trying to install.

## Update and report problems

Close the game before replacing a build and back up your saves.
On PCs, replace the launcher folder; on Android and Vita, update over the
compatible version. On console cards, keep `game`, `saves` and `cache`.

If an error says `UserData` is missing, check that `Neighborhood.iff`, `Houses`
and `Characters` are in the path listed for your platform. If they are present,
report the error with the log; the message does not necessarily mean that the
files were copied incorrectly.

When reporting a problem, include:

- Your system or device and the installed package name.
- Steps leading to the error and the last screen displayed.
- The message shown and `opendollhouse.log`, when available.

Each player supplies the commercial game data separately. When redistributing
a build, keep the licenses, corresponding source code and support kits supplied
with it.
