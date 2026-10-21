#!/usr/bin/env python3
"""Check or apply clang-format on Teserra sources.

    python tools/scripts/check_format.py            # report files that need formatting, exit 1 if any
    python tools/scripts/check_format.py --fix      # format them in place

Formats C++ files under the source directories below, skipping third-party and generated code. Files
are taken from git (tracked and untracked-but-not-ignored), so build trees are never touched. Used by
the `format` / `format-check` CMake targets and by the CI `checks` job (docs/guides/build-and-ci.md).
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIRS = ("engine", "examples", "tests", "tools", "benchmarks", "editor")
EXTENSIONS = {".cpp", ".hpp", ".h", ".inl"}
EXCLUDED_PARTS = {"third_party"}


def list_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", *SOURCE_DIRS],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    files = []
    for line in sorted(set(result.stdout.splitlines())):
        path = Path(line)
        if path.suffix in EXTENSIONS and not EXCLUDED_PARTS.intersection(path.parts):
            if (REPO_ROOT / path).is_file():  # skip deleted-but-tracked files
                files.append(path)
    return files


def find_clang_format(explicit: str | None) -> str:
    if explicit:
        return explicit
    found = shutil.which("clang-format")
    if found:
        return found
    # Visual Studio's "C++ Clang tools for Windows" component, outside a developer prompt.
    vc_dir = os.environ.get("VCINSTALLDIR")
    if vc_dir:
        candidate = Path(vc_dir) / "Tools" / "Llvm" / "x64" / "bin" / "clang-format.exe"
        if candidate.is_file():
            return str(candidate)
    sys.exit("check_format: clang-format not found; install LLVM or pass --clang-format <path>")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fix", action="store_true", help="format files in place instead of checking")
    parser.add_argument("--clang-format", metavar="PATH", help="clang-format executable to use")
    args = parser.parse_args()

    clang_format = find_clang_format(args.clang_format)
    version = subprocess.run([clang_format, "--version"], capture_output=True, text=True, check=True)
    print(f"check_format: {version.stdout.strip()}")

    files = list_files()
    if not files:
        print("check_format: no files to check")
        return 0

    if args.fix:
        # Chunked to stay below the Windows command-line length limit.
        for start in range(0, len(files), 50):
            chunk = [str(path) for path in files[start : start + 50]]
            subprocess.run([clang_format, "-i", "--style=file", *chunk], cwd=REPO_ROOT, check=True)
        print(f"check_format: formatted {len(files)} files")
        return 0

    unformatted = []
    for path in files:
        check = subprocess.run(
            [clang_format, "--dry-run", "--Werror", "--style=file", str(path)],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        if check.returncode != 0:
            unformatted.append(path)
            sys.stdout.write(check.stderr)
    if unformatted:
        print(f"check_format: {len(unformatted)} of {len(files)} files need formatting:")
        for path in unformatted:
            print(f"  {path.as_posix()}")
        print("Run `cmake --build --preset <preset> --target format` or "
              "`python tools/scripts/check_format.py --fix`.")
        return 1
    print(f"check_format: {len(files)} files are formatted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
