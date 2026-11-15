# Experimental New Nintendo 3DS adapter

This target uses the same C++ engine with the dual-screen libctru adapter.
Building requires New Nintendo 3DS support, devkitARM, libctru and the devkitPro
3DS tools. The script looks for the SDK in `~/Tools/devkitpro`; set `DEVKITPRO`
and `DEVKITARM` if needed.

```sh
engine/cmake/build-new3ds-package.sh
```

The output is `build/new3ds-app/OpenDollhouse-new3ds.zip`, containing `.3dsx`,
`.smdh` and licenses. Extract the `3ds` directory to the SD card root. The script
builds an isolated copy of the current sources and refuses to accept sources
changed during compilation; run it again in that case to rebuild only what
is needed.

Copy your installation data to `3ds/OpenDollhouse/game`, preserving `GameData`,
`UserData`, expansion directories, audio and custom content. The package does
not include these files. Saves are in `3ds/OpenDollhouse/saves` and cache in
`3ds/OpenDollhouse/cache`. Do not delete saves when clearing cache. The game records diagnostics automatically
in `cache/opendollhouse.log`, including performance summaries about every five
seconds. An optional empty `3ds/OpenDollhouse/debug` file enables detailed
loading traces; it is not required for normal performance logs.

Choose the game language on first launch. Use the D-pad or touch a language,
then press A or tap OK. Hold L while starting the app to change this choice.
The preference is stored in `3ds/OpenDollhouse/saves/language.txt`.

Open the `.3dsx` through the Homebrew Launcher. The memory limit depends on the
process used by the launcher; the engine retains the Sims3DS adapter's limits
and diagnostics. Building the package does not establish sufficient memory,
speed, audio, suspension or device playability. A CIA version requires its own
tools and packaging; this script does not generate CIA files.
