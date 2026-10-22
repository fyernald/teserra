#!/usr/bin/env python3
"""Package an example of a Release build as a zip for testers (docs/process/development-process.md#feedback-loop).

    python tools/scripts/package_example.py --build-dir build/windows-msvc-release --example rhi_triangle \\
        --version 0.1.0-alpha.5 --feedback-url https://github.com/fyernald/teserra/issues/N --out dist

Writes <out>/teserra-<version>-windows-x64-<example>.zip (the naming of docs/process/release-process.md). Its
single top-level folder of the same name holds:

- the example's executable and the DLLs next to it in the build (GLFW, spdlog, fmt);
- the Visual C++ runtime DLLs it needs, copied app-locally so that testers need no installer. They come from
  --vc-runtime, or else from the Microsoft.VC*.CRT folder under %VCToolsRedistDir%\\x64 (set by the Visual
  Studio developer environment);
- LICENSE, THIRD_PARTY_NOTICES.md, and a README.txt made from tools/packaging/README.txt.in.

Only Release and RelWithDebInfo builds are packaged: debug runtimes cannot be redistributed. The script does not
build anything; build the Release preset first. Used for the feedback builds until release.yml (#122) packages
releases.
"""

from __future__ import annotations

import argparse
import os
import re
import string
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PLATFORM = "windows-x64"
README_TEMPLATE = Path("tools/packaging/README.txt.in")
ROOT_FILES = ("LICENSE", "THIRD_PARTY_NOTICES.md")
# The Visual C++ runtime the engine's executables and DLLs import (the Universal CRT is part of Windows 10+).
VC_RUNTIME_DLLS = ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
PACKAGED_BUILD_TYPES = frozenset({"Release", "RelWithDebInfo"})
DEFAULT_FEEDBACK_URL = "https://github.com/fyernald/teserra/issues/new/choose"
_VERSION = re.compile(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$")
_EXAMPLE = re.compile(r"^[a-z][a-z0-9_]*$")

# What testers should see, by example; shown in the README.
DESCRIPTIONS = {
    "rhi_triangle": (
        "What you should see: a window with a dark gray background and a triangle in the middle: red at the\n"
        "top, green at the bottom left, blue at the bottom right, with the colors blending in between.\n"
        "Resizing the window keeps the triangle in the middle of it."
    ),
    "rhi_clear": "What you should see: a window whose color slowly cycles through the hues.",
}


class PackageError(Exception):
    """A reason the package cannot be made; the message is meant for the user."""


@dataclass(frozen=True)
class Package:
    path: Path
    files: tuple[str, ...]  # names inside the top-level folder, in archive order


def build_type(build_dir: Path) -> str:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        raise PackageError(f"{build_dir} is not a CMake build directory (no CMakeCache.txt)")
    match = re.search(r"^CMAKE_BUILD_TYPE:\w+=(.*)$", cache.read_text(encoding="utf-8", errors="replace"), re.M)
    return match.group(1).strip() if match else ""


def find_vc_runtime(explicit: Path | None) -> Path:
    if explicit is not None:
        candidates = [explicit]
    else:
        redist = os.environ.get("VCToolsRedistDir")
        if not redist:
            raise PackageError(
                "No Visual C++ runtime folder: pass --vc-runtime, or run from a Visual Studio developer "
                "environment (VCToolsRedistDir)")
        candidates = sorted((Path(redist) / "x64").glob("Microsoft.VC*.CRT"), reverse=True)
    for candidate in candidates:
        if all((candidate / name).is_file() for name in VC_RUNTIME_DLLS):
            return candidate
    raise PackageError(f"No folder with {', '.join(VC_RUNTIME_DLLS)} among: "
                       + (", ".join(str(c) for c in candidates) or "(none found)"))


def readme(root: Path, example: str, version: str, executable: str, feedback_url: str) -> str:
    title = f"Teserra {version} - {example}"
    template = string.Template((root / README_TEMPLATE).read_text(encoding="utf-8"))
    text = template.substitute(
        title=title,
        underline="=" * len(title),
        version=version,
        example=example,
        executable=executable,
        description=DESCRIPTIONS.get(example, f"The {example} example of Teserra."),
        feedback_url=feedback_url,
    )
    return text.replace("\r\n", "\n").replace("\n", "\r\n")  # Notepad-friendly line endings


def package(root: Path, build_dir: Path, example: str, version: str, out_dir: Path,
            feedback_url: str = DEFAULT_FEEDBACK_URL, vc_runtime: Path | None = None) -> Package:
    if not _EXAMPLE.match(example):
        raise PackageError(f"'{example}' is not an example name")
    if not _VERSION.match(version):
        raise PackageError(f"'{version}' is not a version like 0.1.0 or 0.1.0-alpha.5")
    kind = build_type(build_dir)
    if kind not in PACKAGED_BUILD_TYPES:
        raise PackageError(f"{build_dir} is a '{kind or 'unknown'}' build; package a Release build")
    example_dir = build_dir / "examples" / example
    executable = example_dir / f"teserra_{example}.exe"
    if not executable.is_file():
        raise PackageError(f"{executable} does not exist; build the example first")
    for required in ROOT_FILES + (str(README_TEMPLATE),):
        if not (root / required).is_file():
            raise PackageError(f"{root / required} is missing")
    runtime = find_vc_runtime(vc_runtime)

    name = f"teserra-{version}-{PLATFORM}-{example}"
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{name}.zip"
    entries: list[tuple[str, Path | str]] = [(executable.name, executable)]
    entries += [(dll.name, dll) for dll in sorted(example_dir.glob("*.dll"), key=lambda p: p.name.lower())]
    entries += [(dll, runtime / dll) for dll in VC_RUNTIME_DLLS]
    entries += [(file, root / file) for file in ROOT_FILES]
    entries.append(("README.txt", readme(root, example, version, executable.name, feedback_url)))

    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for entry, source in entries:
            if isinstance(source, Path):
                archive.write(source, f"{name}/{entry}")
            else:
                archive.writestr(f"{name}/{entry}", source)
    return Package(path=path, files=tuple(entry for entry, _ in entries))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, required=True, help="a Release build directory")
    parser.add_argument("--example", required=True, help="example name, e.g. rhi_triangle")
    parser.add_argument("--version", required=True, help="package version, e.g. 0.1.0-alpha.5")
    parser.add_argument("--out", type=Path, default=Path("dist"), help="output directory (default: dist)")
    parser.add_argument("--feedback-url", default=DEFAULT_FEEDBACK_URL, help="link printed in the README")
    parser.add_argument("--vc-runtime", type=Path, help="folder with the Visual C++ runtime DLLs")
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        made = package(args.root, args.build_dir, args.example, args.version, args.out, args.feedback_url,
                       args.vc_runtime)
    except PackageError as error:
        print(f"package_example: {error}", file=sys.stderr)
        return 1
    print(f"package_example: {made.path} ({len(made.files)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
