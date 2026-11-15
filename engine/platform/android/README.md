# Experimental Android adapter

This adapter uses the same C++ engine and single-screen presentation as the
desktop application. SDL2 handles the window, touch, keyboard, mouse and audio.
Commercial game data is not included. The initial target is ARM64, Android 8 or
later; Retroid Pocket 5 is the planned reference device, not yet physically tested.

## Building the APK on Linux

Prepare the Android SDK with a platform, build-tools and NDK; also install JDK,
CMake, Ninja and zip. Use the official SDL2 2.30.0 sources, including its Java
classes. The script does not download SDKs. To retain Android 8/API26 support,
it prepares GNU libiconv 1.18 when missing, downloading the official file with a
pinned hash (about 5.6 MiB):

```sh
export ANDROID_HOME="/path/to/android-sdk"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/installed-version"
export ODH_SDL_SOURCE="/path/to/SDL2-2.30.0"
engine/cmake/build-android-package.sh
```

If `~/Tools/android-env.sh` exists, the script can load that environment.
By default, it looks for SDL2 in `~/Tools/odh-dependencies/sources/SDL2-2.30.0`.
The output is `build/android-arm64-app/OpenDollhouse-arm64-debug.apk`, signed
with a local debug key. It is not a store package or public release. The script
records input files and refuses to accept a build whose sources changed during
compilation; run it again in that case to rebuild only what is needed.

## Installing and supplying game data

With USB debugging authorized on the device:

```sh
adb install -r build/android-arm64-app/OpenDollhouse-arm64-debug.apk
adb shell am start -n org.opendollhouse/.LauncherActivity
```

Open the application once to create its directory and see its path. Copy your
installation's contents into the `game` subdirectory, keeping `GameData`,
`UserData` and expansion directories at the same level. For example, if the
screen shows the usual primary-storage path:

```sh
adb push "/path/to/copy/The Sims/." /sdcard/Android/data/org.opendollhouse/files/game/
```

Confirm the path shown on your device before copying. Android restricts this
directory in some file managers; the USB command avoids relying on them. The
application does not request broad storage access and does not save progress to
the commercial data copy. Saves use the application's internal storage, with
cache kept separately. Uninstalling removes these areas; back up saves first.

The debug package lets you copy saves to the computer without uninstalling:

```sh
adb exec-out run-as org.opendollhouse tar -C files -cf - saves > opendollhouse-saves.tar
adb logcat -d > opendollhouse-android.log
```

These commands require an installed application and a connected device. They
do not include the external commercial data. Inspect the engine log directory
with `adb shell run-as org.opendollhouse ls cache/engine`.

Choose a language and press **Play**. Missing text may fall back to English;
localization is still being adapted. The landscape window preserves the
original 800×600 layout's proportions. Touch selects directly; a mouse and
keyboard can help with testing. A controller recognized by SDL uses the
[common mapping](../../../docs/controls.md): sticks for camera/cursor, R2 for
clicking, L2 for next Sim and centering, L/R for rotation and face buttons for
game modes. Touch remains available without a controller and after disconnecting
it. Commands passed computer tests; physical models, gestures, resume and
performance require their own testing.

Building and checking the APK signature does not establish that the game runs
on Android. Emulator and device execution must be recorded separately.

## Text conversion on Android 8

Android's own `iconv` service is available only from API28. This version retains
API26 using **shared libiconv**, built by the NDK for ARM64/API26. The GNU header
uses `libiconv_*` symbols to avoid accidentally depending on unavailable system
functions.

To prepare only this dependency:

```sh
engine/cmake/prepare-android-iconv.sh
```

By default, it is placed in `~/Tools/odh-dependencies/android-arm64/iconv`.
`ODH_ANDROID_ICONV_ROOT` accepts another compatible prefix. The package loads
`libiconv.so` before the engine and includes its LGPL notices. The original
`libiconv-1.18.tar.gz` source file and `prepare-android-iconv.sh` recipe accompany
the APK. Preserve them when distributing the package. Only the library is
built; the command-line utility is not included. To replace the library,
prepare an alternate prefix and regenerate/sign the APK with the public script;
this does not depend on another person's signing key.

The official source is [GNU libiconv 1.18](https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz).
The `tests/iconv` project can check the integration without building the whole
game: it compiles the real codec and requires all symbols to resolve using the
ARM64/API26 toolchain. This checks compilation and linking, not Android
execution or correct translation of every screen.
