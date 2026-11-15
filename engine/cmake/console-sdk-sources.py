#!/usr/bin/env python3
"""Build Switch portlibs from pinned official sources in a private Tools prefix.

Fallback for an unavailable devkitPro package server; never modifies the SDK or
pacman database. Needs Python, Mako, make, patch, ninja, bison, m4 and host C/C++.
Only nouveau/EGL for Switch is enabled. No game or emulator is built or launched.
Run: python3 engine/cmake/console-sdk-sources.py
Independent zlib preparation for both targets: add --zlib-only.
For newlib's declared-but-unimplemented timespec_get, explicitly select
--mesa-newlib-timespec-compat. This applies a LOCAL compatibility patch, keeping
Mesa's existing fallback body and naming it privately; no feature probe is faked.
Rollback: remove the dedicated ODH_CONSOLE_DEPS_ROOT after checking its consumers.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

ROOT = Path(os.environ.get("ODH_CONSOLE_DEPS_ROOT", Path.home() / "Tools/opendollhouse-console-deps")).resolve()
SDK = Path(os.environ.get("DEVKITPRO", Path.home() / "Tools/devkitpro")).resolve()
PREFIX = ROOT / "switch"
SRC = ROOT / "sources"
DOWNLOADS = ROOT / "downloads"
RECIPES = ROOT / "official-recipes"
COMMIT = "5c46517a8a1a1f0939a90b8068655ea00fa125b9"
BASE = f"https://raw.githubusercontent.com/devkitPro/pacman-packages/{COMMIT}/"
JOBS = min(2, max(1, int(os.environ.get("ODH_BUILD_JOBS", "2"))))
SOURCES = [
    ("libdrm_nouveau-1.0.1.tar.gz", "https://codeload.github.com/devkitPro/libdrm_nouveau/tar.gz/refs/tags/v1.0.1", "b069bf8029abb120294bd2dd988dc4d6ce2be3f1016c88c26c73c87b6f312136"),
    ("mesa-20.1.0-rc3.tar.xz", "https://archive.mesa3d.org/older-versions/20.x/mesa-20.1.0-rc3.tar.xz", "c90b75ea34302ebde9b81b87c5642fa864c40fe9c4ad34ce0793170c1413168d"),
    ("zlib-1.3.1.tar.xz", "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.xz", "38ef96b8dfe510d42707d9c781877914792541133e1870841463bfa73f883e32"),
    ("SDL2-2.28.5.tar.gz", "https://libsdl.org/release/SDL2-2.28.5.tar.gz", "332cb37d0be20cb9541739c61f79bae5a477427d79ae85e352089afdaf6666e4"),
    ("flex-2.6.4.tar.gz", "https://github.com/westes/flex/releases/download/v2.6.4/flex-2.6.4.tar.gz", "e87aae032bf07c26f85ac0ed3250998c37621d95f8bd748b31f15b33c45ee995"),
    ("meson-1.3.2-py3-none-any.whl", "https://files.pythonhosted.org/packages/" , "0ba4a71fbc060c44721c7b674807598c5af9ea51335073cae7a3e9a95b375c89"),
]
PATCHES = [
    ("switch/mesa/switch-mesa-20.1.0-5.patch", "950f93d3e5b6ae9c5a42c2918623fe9a80d7ee398d92d2e73abec86b62d75916"),
    ("switch/mesa/gl_XML.py.patch", "a9bc326195b3fe29709e079466a8b2162a2ac9409f694eaad5494490155e2dd4"),
    ("switch/mesa/glX_XML.py.patch", "1475defcdf8600690eaddeee28d8f01310635c1273fb541f668e8789714d36ca"),
    ("switch/mesa/OpenGLConfig.cmake", "016b39536fa560b38cbb456e3e9146439401c0ea29f26400ea065bb9d76f2518"),
    ("switch/SDL2/SDL2-2.28.5.patch", "eb2da665a014dc540ad3dfeb62b3f7c5e15ae090ebdd631fe8b78c1f272c7aa0"),
]


def run(args, cwd=ROOT, env=None):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)


def fetch(name, url, expected, directory=DOWNLOADS):
    path = directory / name
    if not path.exists():
        temp = path.with_name(path.name + ".partial")
        urllib.request.urlretrieve(url, temp)
        if hashlib.sha256(temp.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Checksum mismatch: {url}; retained {temp}")
        temp.rename(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise RuntimeError(f"Checksum mismatch: {path}")
    print("verified", path.name, expected, flush=True)
    return path


def unpack(name, dirname):
    target = SRC / dirname
    if not target.exists():
        with tarfile.open(DOWNLOADS / name) as archive:
            archive.extractall(SRC, filter="data")
    return target


def patch(source, names):
    marker = source / ".odh-official-patched"
    if marker.exists():
        return
    # A failed partial patch is intentionally retained for diagnosis; do not
    # retry by discarding edits or silently change upstream source files.
    for name in names:
        run(["patch", "--forward", "-d", source, "-p1", "-i", RECIPES / name])
    marker.write_text(COMMIT + "\n")


def zlibs():
    for directory in (ROOT, SRC, DOWNLOADS):
        directory.mkdir(parents=True, exist_ok=True)
    name, url, sha = next(item for item in SOURCES if item[0].startswith("zlib-"))
    fetch(name, url, sha)
    source = unpack(name, "zlib-1.3.1")
    for target, toolchain, tool, flags in (
        ("switch", "devkitA64", "aarch64-none-elf", "-O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec"),
        ("wiiu", "devkitPPC", "powerpc-eabi", "-O2 -mcpu=750 -meabi -mhard-float -ffunction-sections -fdata-sections"),
    ):
        prefix = ROOT / target
        markers = (".odh-source-switch", ".odh-source-sdl2", ".odh-source-zlib")
        if prefix.exists() and not any((prefix / marker).exists() for marker in markers):
            raise RuntimeError(f"Refusing unrecognized prefix: {prefix}")
        prefix.mkdir(exist_ok=True)
        (prefix / ".odh-source-zlib").touch()
        build = ROOT / f"build-zlib-{target}"
        build.mkdir(exist_ok=True)
        env = os.environ.copy()
        env.update(CC=str(SDK / toolchain / "bin" / f"{tool}-gcc"),
                   AR=str(SDK / toolchain / "bin" / f"{tool}-ar"),
                   RANLIB=str(SDK / toolchain / "bin" / f"{tool}-ranlib"),
                   CFLAGS=flags, CHOST=tool)
        run([source / "configure", f"--prefix={prefix}", "--static"], build, env)
        run(["make", f"-j{JOBS}", "libz.a"], build, env)
        run(["make", "install"], build, env)


def local_timespec_compat(mesa, enabled):
    guards = [
        ("include/c11/threads.h", "46b912c3f4f8afd35cfe0e9925508447248d7ec5508f55296fb5c168e51c998c", "23d1fe7dbde97ca74038cd9103edca55fc73ec7786c5ec53e83747917cd6640f"),
        ("include/c11/threads_posix.h", "f8ad2b69fa472e332b50572c1b2dcc1c8a0fa783a1199aad245398d3df421b4b", "5dea3e05ced8b652a2a34b2aede99c9bede3a1aeaae02b8d5ffd99a81ae76b16"),
    ]
    actual = [hashlib.sha256((mesa / name).read_bytes()).hexdigest() for name, _, _ in guards]
    before, after = [x[1] for x in guards], [x[2] for x in guards]
    if actual not in (before, after):
        raise RuntimeError("Unexpected Mesa C11 header contents; refusing local patch")
    if not enabled:
        if actual == after:
            raise RuntimeError("Mesa contains the local compatibility patch; explicitly pass --mesa-newlib-timespec-compat")
        return
    patchfile = Path(__file__).resolve().parent / "patches/mesa-20.1-newlib-timespec.patch"
    patchsha = hashlib.sha256(patchfile.read_bytes()).hexdigest()
    if patchsha != "03709d02b133a4bb4ad376bb62af80804a77f35d61d23a90bc64f827ba8a29d9":
        raise RuntimeError("Local compatibility patch checksum mismatch")
    if actual == before:
        run(["patch", "--forward", "-d", mesa, "-p1", "-i", patchfile])
    for name, _, expected in guards:
        if hashlib.sha256((mesa / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Post-patch checksum mismatch: {name}")
    (ROOT / "mesa-local-compat.json").write_text(json.dumps(dict(
        kind="LOCAL compatibility patch, not an official unmodified devkitPro build",
        patch=str(patchfile), sha256=patchsha,
        headers=[dict(file=n, before=b, after=a) for n, b, a in guards]), indent=2) + "\n")


def main(compat=False):
    if PREFIX.exists() and not any((PREFIX / marker).exists() for marker in (".odh-source-switch", ".odh-source-zlib")):
        raise RuntimeError(f"Refusing unrecognized prefix: {PREFIX}")
    for path in (ROOT, SRC, DOWNLOADS, RECIPES, PREFIX):
        path.mkdir(parents=True, exist_ok=True)
    (PREFIX / ".odh-source-switch").touch()
    for program in ("make", "patch", "ninja", "bison", "m4", "cc", "c++", "pkg-config"):
        if not shutil.which(program):
            raise RuntimeError(f"Missing host tool: {program}")
    if shutil.disk_usage(ROOT).free < 1024**3:
        raise RuntimeError("At least 1 GiB free space is required")
    provenance = []
    for name, url, sha in SOURCES:
        if name.endswith(".whl"):
            metadata = json.load(urllib.request.urlopen("https://pypi.org/pypi/meson/1.3.2/json"))
            asset = next(item for item in metadata["urls"] if item["filename"] == name)
            if asset["digests"]["sha256"] != sha:
                raise RuntimeError("PyPI Meson release checksum changed")
            url = asset["url"]
        fetch(name, url, sha)
        provenance.append(dict(name=name, url=url, sha256=sha))
    for path, sha in PATCHES:
        fetch(Path(path).name, BASE + path, sha, RECIPES)
        provenance.append(dict(name=path, url=BASE + path, sha256=sha))
    (ROOT / "switch-sources.json").write_text(json.dumps(provenance, indent=2) + "\n")
    with zipfile.ZipFile(DOWNLOADS / "meson-1.3.2-py3-none-any.whl") as wheel:
        wheel.extractall(ROOT / "host-python")
    env = os.environ.copy()
    env.update(DEVKITPRO=str(SDK), DEVKITA64=str(SDK / "devkitA64"))
    env["PATH"] = ":".join(map(str, [ROOT / "host-tools/bin", SDK / "tools/bin", SDK / "devkitA64/bin"])) + ":" + env["PATH"]
    env["PYTHONPATH"] = str(ROOT / "host-python")
    if not shutil.which("flex", path=env["PATH"]):
        flex = unpack("flex-2.6.4.tar.gz", "flex-2.6.4")
        run([flex / "configure", f"--prefix={ROOT / 'host-tools'}", "--disable-shared", "--disable-nls", "--disable-libfl"], flex)
        run(["make", f"-j{JOBS}"], flex)
        run(["make", "install"], flex)
    drm = unpack("libdrm_nouveau-1.0.1.tar.gz", "libdrm_nouveau-1.0.1")
    run(["make", f"-j{JOBS}"], drm, env)
    for directory in ("include", "lib"):
        shutil.copytree(drm / directory, PREFIX / directory, dirs_exist_ok=True)
    wrapper = ROOT / "switch-pkg-config"
    import shlex
    wrapper.write_text("#!/bin/sh\nexport PKG_CONFIG_DIR= PKG_CONFIG_PATH= PKG_CONFIG_SYSROOT_DIR=\n" +
        f"export PKG_CONFIG_LIBDIR={shlex.quote(str(PREFIX / 'lib/pkgconfig'))}\n" +
        f'exec pkg-config --define-variable=prefix={shlex.quote(str(PREFIX))} "$@"\n')
    wrapper.chmod(0o755)
    arch = ["-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft", "-fPIC", "-ftls-model=local-exec"]
    flags = arch + ["-O2", "-ffunction-sections", "-fdata-sections", "-D__SWITCH__", f"-I{PREFIX}/include", "-isystem", str(SDK / "libnx/include")]
    links = arch + [f"-L{PREFIX}/lib", f"-L{SDK}/libnx/lib", "-lnx"]
    cross = ROOT / "switch-cross.ini"
    text = "[binaries]\n"
    for key, tool in (("c", "gcc"), ("cpp", "g++"), ("ar", "gcc-ar"), ("strip", "strip")):
        text += f"{key} = {str(SDK / 'devkitA64/bin' / ('aarch64-none-elf-' + tool))!r}\n"
    text += f"pkgconfig = {str(wrapper)!r}\n[built-in options]\nc_args = {flags!r}\ncpp_args = {flags!r}\nc_link_args = {links!r}\ncpp_link_args = {links!r}\n"
    text += "[host_machine]\nsystem = 'horizon'\ncpu_family = 'aarch64'\ncpu = 'cortex-a57'\nendian = 'little'\n"
    cross.write_text(text)
    mesa = unpack("mesa-20.1.0-rc3.tar.xz", "mesa-20.1.0-rc3")
    patch(mesa, [Path(p).name for p, _ in PATCHES[:3]])
    local_timespec_compat(mesa, compat)
    build = ROOT / "build-mesa-switch"
    if not (build / "build.ninja").exists():
        run([sys.executable, "-m", "mesonbuild.mesonmain", "setup", build, mesa, f"--cross-file={cross}", "--default-library=static", "--buildtype=plain", f"--prefix={PREFIX}", "--libdir=lib", "-Db_ndebug=true", "-Dgallium-drivers=nouveau", "-Ddri-drivers=", "-Dvulkan-drivers=", "-Dplatforms=switch", "-Dllvm=false", "-Dglx=disabled"], env=env)
    run(["ninja", "-C", build, f"-j{JOBS}"], env=env)
    run(["ninja", "-C", build, "install"], env=env)
    egl = PREFIX / "lib/pkgconfig/egl.pc"
    data = egl.read_text()
    if "-ldrm_nouveau" not in data:
        egl.write_text(data.replace("-lEGL", "-lEGL -ldrm_nouveau"))
    glconfig = PREFIX / "lib/cmake/OpenGL"
    glconfig.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(RECIPES / "OpenGLConfig.cmake", glconfig / "OpenGLConfig.cmake")
    zlib = unpack("zlib-1.3.1.tar.xz", "zlib-1.3.1")
    target_env = env | dict(CC="aarch64-none-elf-gcc", AR="aarch64-none-elf-ar", RANLIB="aarch64-none-elf-ranlib", CFLAGS=" ".join(flags), LDFLAGS=" ".join(links), CHOST="aarch64-none-elf")
    run([zlib / "configure", f"--prefix={PREFIX}", "--static"], zlib, target_env)
    run(["make", f"-j{JOBS}", "libz.a"], zlib, target_env)
    run(["make", "install"], zlib, target_env)
    sdl = unpack("SDL2-2.28.5.tar.gz", "SDL2-2.28.5")
    patch(sdl, ["SDL2-2.28.5.patch"])
    target_env.update(PKG_CONFIG=str(wrapper), CPPFLAGS=" ".join(flags), LIBS="-lnx")
    run([sdl / "configure", f"--prefix={PREFIX}", "--host=aarch64-none-elf", "--disable-shared", "--enable-static", "--enable-audio", "--enable-joystick", "--disable-power", "--disable-filesystem", "--enable-cpuinfo", "--enable-pthreads", "--enable-timers", "--enable-video"], sdl, target_env)
    run(["make", f"-j{JOBS}"], sdl, target_env)
    run(["make", "install"], sdl, target_env)
    # The official configure patch embeds the global portlibs include directory
    # in SDL metadata even when --prefix points elsewhere. Relocate metadata to
    # this private target prefix; do not create or populate the global path.
    relocated = []
    for rel in ("lib/pkgconfig/sdl2.pc", "bin/sdl2-config", "lib/cmake/SDL2/sdl2-config.cmake"):
        path = PREFIX / rel
        data = path.read_text()
        old = str(SDK / "portlibs/switch")
        if old in data:
            path.write_text(data.replace(old, str(PREFIX)))
            relocated.append(rel)
    (ROOT / "switch-metadata-relocation.json").write_text(json.dumps(dict(
        reason="official SDL configure embeds SDK portlibs paths despite custom --prefix",
        from_prefix=str(SDK / "portlibs/switch"), to_prefix=str(PREFIX),
        files=relocated), indent=2) + "\n")
    print(f"Switch target libraries ready: {PREFIX}. Compilation is not runtime validation.")


if __name__ == "__main__":
    if sys.argv[1:] == ["--zlib-only"]:
        zlibs()
    elif sys.argv[1:] == ["--mesa-newlib-timespec-compat"]:
        main(compat=True)
    elif sys.argv[1:]:
        sys.exit("Usage: console-sdk-sources.py [--zlib-only|--mesa-newlib-timespec-compat]")
    else:
        main()
