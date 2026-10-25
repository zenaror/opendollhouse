#!/usr/bin/env python3
"""Exercise the production revision helper without reconfiguring the fixture."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil
import tempfile


def run(*args, cwd=None):
    result = subprocess.run(args, cwd=cwd, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise RuntimeError(f"Command failed: {args!r}\n{result.stdout}")
    return result.stdout.strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--module", required=True, type=Path)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--generator", required=True)
    args = parser.parse_args()
    if shutil.which("git") is None:
        print("build revision: skipped; Git is required for the synthetic repository")
        return 77
    # Do not let caller-provided Git overrides redirect the synthetic repository.
    for key in list(os.environ):
        if key.startswith("GIT_"):
            del os.environ[key]
    with tempfile.TemporaryDirectory(prefix="odh build revision ") as temp:
        root = Path(temp)
        source = root / "source with spaces"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.16)\n'
            'project(RevisionFixture LANGUAGES CXX)\n'
            'file(APPEND "${CMAKE_BINARY_DIR}/configure-count" "configured\\n")\n'
            f'include([[{args.module.resolve().as_posix()}]])\n'
            'add_executable(revision main.cpp)\n'
            'odh_attach_build_revision(revision "${CMAKE_CURRENT_SOURCE_DIR}")\n')
        (source / "main.cpp").write_text(
            '#include "odh_build_revision.hpp"\n'
            '#include <iostream>\n'
            'int main() { std::cout << ODH_BUILD_REVISION; }\n')
        tracked = source / "tracked.txt"
        tracked.write_text("initial\n")
        run("git", "init", "--quiet", str(source))

        def git(*command):
            return run("git", "-C", str(source), *command)

        def commit(message):
            git("-c", "user.name=Revision Test", "-c", "user.email=revision@example.invalid",
                "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", message)
            return git("rev-parse", "HEAD")

        git("add", ".")
        head = commit("Initial synthetic fixture")

        def configure(src, build):
            run(args.cmake, "-S", str(src), "-B", str(build), "-G", args.generator,
                f"-DCMAKE_CXX_COMPILER={args.compiler}")

        def check(build, expected):
            run(args.cmake, "--build", str(build), "--config", "Debug", "--target", "revision")
            executable = build / "revision"
            if not executable.exists():
                executable = build / "Debug" / "revision"
            actual = run(str(executable))
            assert actual == expected, (actual, expected)
            assert (build / "configure-count").read_text() == "configured\n"
            return executable

        build = root / "build with spaces"
        configure(source, build)
        executable = check(build, head)
        header = build / "generated/revision/odh_build_revision.hpp"
        objects = list(build.rglob("main.cpp.o")) + list(build.rglob("main.cpp.obj"))
        assert objects, "Fixture object file not found"
        observed = [header, executable, *objects]

        def check_unchanged():
            before = [path.stat().st_mtime_ns for path in observed]
            check(build, head)
            assert before == [path.stat().st_mtime_ns for path in observed], "Unchanged build rewrote output"

        check_unchanged()
        override_keys = ("GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE")
        try:
            for key in override_keys:
                os.environ[key] = str(root / "deliberately missing override")
            check(build, head)
        finally:
            for key in override_keys:
                os.environ.pop(key, None)
        git("config", "core.filemode", "true")
        original_mode = tracked.stat().st_mode
        config_overrides = (
            {"GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "core.filemode",
             "GIT_CONFIG_VALUE_0": "false"},
            {"GIT_CONFIG_PARAMETERS": "'core.filemode=false'"},
        )
        for overrides in config_overrides:
            try:
                tracked.chmod(original_mode | 0o100)
                os.environ.update(overrides)
                assert git("config", "--get", "core.filemode") == "false"
                check(build, head + "-dirty")
            finally:
                tracked.chmod(original_mode)
                for key in overrides:
                    os.environ.pop(key, None)
            check(build, head)
        (source / "untracked.txt").write_text("untracked files do not make a dirty build\n")
        check_unchanged()
        tracked.write_text("unstaged change\n")
        check(build, head + "-dirty")
        git("checkout", "--", "tracked.txt")
        check(build, head)
        tracked.write_text("staged change\n")
        git("add", "tracked.txt")
        check(build, head + "-dirty")
        head = commit("New tracked revision")
        check(build, head)
        check_unchanged()

        worktree = root / "linked worktree with spaces"
        git("worktree", "add", "--quiet", "--detach", str(worktree), "HEAD")
        assert (worktree / ".git").is_file()
        worktree_build = root / "worktree build"
        configure(worktree, worktree_build)
        check(worktree_build, head)

        # This archive deliberately lives beneath a repository with a valid HEAD.
        archive = source / "nested source archive"
        archive.mkdir()
        for name in ("CMakeLists.txt", "main.cpp"):
            (archive / name).write_bytes((source / name).read_bytes())
        archive_build = root / "archive build"
        configure(archive, archive_build)
        check(archive_build, "unknown")
    print("build revision: clean, unstaged, staged, new commit, unchanged output, "
          "Git environment overrides, untracked files, linked worktree and nested archive passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
