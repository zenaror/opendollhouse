#!/usr/bin/env bash
# Cross-build the same desktop application and collect its runtime dependencies.
set -euo pipefail
if [[ $# -ne 1 || $1 == --help ]]; then
  printf 'Usage: engine/cmake/build-desktop-package.sh windows-x86_64|macos-x86_64|macos-arm64\n'
  exit 0
fi
odh_target=$1
case "$odh_target" in windows-x86_64|macos-x86_64|macos-arm64) ;; *) printf 'Unknown target: %s\n' "$odh_target" >&2; exit 2;; esac
odh_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
odh_tools=${ODH_TOOLS_ROOT:-"$HOME/Tools"}
odh_deps=${ODH_DEPS_ROOT:-"$odh_tools/odh-dependencies"}/$odh_target
odh_build=${ODH_CROSS_BUILD_ROOT:-"$odh_root/build/cross"}/$odh_target
odh_jobs=${ODH_BUILD_JOBS:-2}
if [[ ! -f "$odh_deps/share/licenses/SDL2.txt" ]]; then
  printf 'Prepare target dependencies first: engine/cmake/prepare-desktop-deps.sh %s\n' "$odh_target" >&2
  exit 1
fi
odh_options=(-DODH_BUILD_DESKTOP=ON -DBUILD_TESTING=OFF "-DCMAKE_PREFIX_PATH=$odh_deps")
if [[ $odh_target == windows-x86_64 ]]; then
  odh_options+=("-DODH_TARGET_DEPS=$odh_deps")
else
  # Keep LLVM tools available during installation too: OSXCross's
  # install_name_tool wrapper needs llvm-install-name-tool to fix runtime paths.
  if [[ -f "$odh_tools/osxcross-env.sh" ]]; then source "$odh_tools/osxcross-env.sh"; fi
  odh_min=10.15
  if [[ $odh_target == macos-arm64 ]]; then odh_min=11.0; fi
  odh_options+=("-DCMAKE_FIND_ROOT_PATH=$odh_deps" "-DCMAKE_OSX_DEPLOYMENT_TARGET=$odh_min"
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
    '-DCMAKE_INSTALL_RPATH=@executable_path/../lib')
fi
"$odh_root/engine/cmake/configure-core.sh" "$odh_target" "${odh_options[@]}"
odh_source_manifest() {
python3 - "$odh_root" <<'PY'
import hashlib,json,pathlib,sys
root=pathlib.Path(sys.argv[1])
# The recovered PC services live outside engine/ too. Record their readers,
# headers and presentation sources so this package cannot silently mix revisions.
files=[root/'CMakeLists.txt']+[p for directory in ('engine','src','include','tools')
    for p in (root/directory).rglob('*')
    if p.is_file() and (p.suffix in ('.cpp','.c','.h','.hpp','.inl','.inc','.cmake','.sh') or p.name=='CMakeLists.txt')]
print(json.dumps({str(p.relative_to(root)):
    hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)},indent=2))
PY
}
odh_source_manifest > "$odh_build/source-inputs.json"
cmake --build "$odh_build" --target opendollhouse --parallel "$odh_jobs"
odh_source_manifest > "$odh_build/source-inputs-after.json"
if ! cmp -s "$odh_build/source-inputs.json" "$odh_build/source-inputs-after.json"; then
  printf 'Sources changed during compilation. Rerun to produce a consistent package.\n' >&2
  exit 1
fi
odh_launcher_folder=odl-launcher
if [[ $odh_target != windows-x86_64 ]]; then odh_launcher_folder=odh-launcher; fi
odh_package="$odh_build/$odh_launcher_folder"
cmake --install "$odh_build" --prefix "$odh_package"
mkdir -p "$odh_package/share/licenses/dependencies"
cp -a "$odh_deps/share/licenses/." "$odh_package/share/licenses/dependencies/"
cp "$odh_build/source-inputs.json" "$odh_package/SOURCE-INPUTS.json"
if [[ $odh_target == windows-x86_64 ]]; then
  cp "$odh_deps/bin/SDL2.dll" "$odh_deps/bin/libzlib.dll" "$odh_package/bin/"
  for odh_dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
    odh_runtime=$(x86_64-w64-mingw32-g++-posix -print-file-name="$odh_dll")
    if [[ ! -f "$odh_runtime" ]]; then printf 'Missing MinGW runtime: %s\n' "$odh_dll" >&2; exit 1; fi
    cp "$odh_runtime" "$odh_package/bin/"
  done
  # Debian/Ubuntu MinGW runtimes: retain their complete packaging notices,
  # including the GCC Runtime Library Exception, and the referenced GPL text.
  for odh_notice in /usr/share/doc/gcc-mingw-w64-x86-64-posix/copyright /usr/share/doc/mingw-w64-common/copyright; do
    if [[ ! -f "$odh_notice" ]]; then
      printf 'Missing runtime notices: %s. Supply equivalent notices before packaging.\n' "$odh_notice" >&2; exit 1
    fi
    odh_name=$(basename "$(dirname "$odh_notice")")
    cp "$odh_notice" "$odh_package/share/licenses/dependencies/$odh_name.txt"
  done
  cp /usr/share/common-licenses/GPL-3 "$odh_package/share/licenses/dependencies/GPL-3.txt"
else
  mkdir -p "$odh_package/lib"
  cp -a "$odh_deps/lib/"libSDL2*.dylib "$odh_package/lib/"
fi
if [[ $odh_target == windows-x86_64 ]]; then
  cat > "$odh_package/OpenDollhouse.cmd" <<'BAT'
@echo off
"%~dp0bin\opendollhouse.exe" %*
exit /b %errorlevel%
BAT
else
  cp "$odh_root/engine/platform/desktop/macos-launcher.sh" "$odh_package/OpenDollhouse.command"
  cp "$odh_root/engine/platform/desktop/macos-language-launcher.sh" "$odh_package/ChooseLanguage.command"
  chmod +x "$odh_package/OpenDollhouse.command" "$odh_package/ChooseLanguage.command"
fi
cat > "$odh_package/README.txt" <<'TEXT'
OpenDollhouse — experimental native desktop build

Put the complete launcher folder inside your own The Sims installation.
Windows uses odl-launcher; macOS uses odh-launcher:
  The Sims/
    GameData/
    UIGraphics/
    <launcher-folder>/
      OpenDollhouse.cmd       (Windows)
      OpenDollhouse.command   (macOS)
      ChooseLanguage.command (macOS: choose another language)
      bin/  lib/  share/
Run the launcher for your system without arguments. On Windows, you may also
run bin/opendollhouse.exe directly. To run the macOS binary directly, supply
--data-root /path/to/The\ Sims; using its launcher handles this for you.
The launcher finds the installation independently of the working directory.
No game files are included or downloaded.

Saves and cache use this user's OpenDollhouse storage, outside the installation.
Original UserData is a read source. Keep backups of your OpenDollhouse saves.
For another layout, use --data-root DIR --save-root DIR --cache-dir DIR.
ODH_SAVE_ROOT and ODH_CACHE_ROOT set defaults; explicit command flags win.
Use --help for other options. Missing game data produces an explanatory error.

On macOS, the first launch offers the game language selection and remembers
your choice. Open ChooseLanguage.command to choose again. --language CODE is
a temporary override; the launcher does not force a language. The preference
is saved in ~/Library/Application Support/opendollhouse/config, unless
XDG_CONFIG_HOME explicitly selects another configuration location.

Keep bin, lib (when present), and share together. Windows DLLs stay beside the
executable. The macOS launcher must retain its executable permission; run it
from Terminal if Finder does not allow it. These are unsigned command-line
executables, not notarized .app installers; macOS double-click and current
hardware support have not been validated. Compilation alone is not gameplay
validation. Third-party notices are under share/licenses.
Project sources and public guides are in source-snapshot.tar.gz.
TEXT

# Include the exact local project sources, not only their fingerprints.
python3 - "$odh_root" "$odh_package" <<'PY'
import importlib.util, os, pathlib, sys
repo, output = map(pathlib.Path, sys.argv[1:])
spec = importlib.util.spec_from_file_location('odh_package_sources', repo/'tools/package_linux.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
archive, _ = module.source_snapshot(repo, int(os.environ.get('SOURCE_DATE_EPOCH', '0')))
(output/'source-snapshot.tar.gz').write_bytes(archive)
PY

python3 - "$odh_package" "$odh_target" <<'PY'
import hashlib,json,pathlib,sys
root=pathlib.Path(sys.argv[1])
files={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest()
       for p in sorted(root.rglob('*')) if p.is_file() and p.name!='MANIFEST.json'}
(root/'MANIFEST.json').write_text(json.dumps({'target':sys.argv[2],'sha256':files},indent=2)+'\n')
PY
tar -czf "$odh_build/OpenDollhouse-$odh_target.tar.gz" -C "$odh_build" "$odh_launcher_folder"
printf '\nPackage built (target execution not verified): %s\n' "$odh_build/OpenDollhouse-$odh_target.tar.gz"
