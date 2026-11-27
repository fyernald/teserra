#!/usr/bin/env python3
"""Package the examples of a Release build for testers and releases (docs/process/release-process.md).

    python tools/scripts/package_example.py --build-dir build/windows-msvc-release --example rhi_triangle \\
        --version 0.1.0-alpha.5 --feedback-url https://github.com/fyernald/teserra/issues/N --out dist
    python tools/scripts/package_example.py --build-dir build/windows-msvc-release --all-examples \\
        --version 0.1.0 --out dist
    python tools/scripts/package_example.py --platform linux-x64 --build-dir build/linux-clang-release \\
        --all-examples --version 0.2.0 --out dist

--example writes <out>/teserra-<version>-<platform>-<example>.zip (.tar.gz on Linux), whose single top-level
folder of the same name holds:

- the example's executable and, on Windows, the DLLs next to it in the build (GLFW, spdlog, fmt, yyjson);
- the example's assets/ folder, if it has one (textured_quad reads its image from there);
- on Windows, the Visual C++ runtime DLLs it needs, copied app-locally so that testers need no installer. They
  come from --vc-runtime, or else from the Microsoft.VC*.CRT folder under %VCToolsRedistDir%\\x64 (set by the
  Visual Studio developer environment);
- LICENSE, THIRD_PARTY_NOTICES.md, and a README.txt made from tools/packaging/README.txt.in
  (README-linux.txt.in on Linux).

--all-examples writes <out>/teserra-<version>-<platform>-examples.zip (.tar.gz on Linux), the release artifact:
one folder per example in PACKAGED_EXAMPLES, each with the files above except the licenses and README, and
LICENSE, THIRD_PARTY_NOTICES.md and a README.txt from tools/packaging/README-examples.txt.in
(README-examples-linux.txt.in) at the top. release.yml makes both for every version tag.

--platform linux-x64 (#317) packages a Linux build: no DLLs and no runtime, a .tar.gz that keeps the
executables' mode 755, and Unix line endings. The executables may only link the system's C and C++ runtimes
(LINUX_SYSTEM_LIBRARIES, checked with ldd): everything else is static or opened at run time, so the archive runs
on any desktop of a supported distribution.

Every DLL in a Windows package other than the Visual C++ runtime must be listed in THIRD_PARTY_NOTICES.md
(through NOTICED_DLLS), so a new dependency cannot ship without its notice. Only Release and RelWithDebInfo
builds are packaged: debug runtimes cannot be redistributed. The script does not build anything; build the
Release preset first.
"""

from __future__ import annotations

import argparse
import io
import os
import re
import string
import subprocess
import sys
import tarfile
import time
import zipfile
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
NOTICES = "THIRD_PARTY_NOTICES.md"
ROOT_FILES = ("LICENSE", NOTICES)
# The Visual C++ runtime the engine's executables and DLLs import (the Universal CRT is part of Windows 10+).
VC_RUNTIME_DLLS = ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
# The DLLs a Release build puts next to the examples, and the name of their row in THIRD_PARTY_NOTICES.md.
NOTICED_DLLS = {
    "glfw3.dll": "GLFW",
    "spdlog.dll": "spdlog",
    "fmt.dll": "{fmt}",
    "yyjson.dll": "yyjson",
}
PACKAGED_BUILD_TYPES = frozenset({"Release", "RelWithDebInfo"})
DEFAULT_FEEDBACK_URL = "https://github.com/fyernald/teserra/issues/new/choose"
_VERSION = re.compile(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$")
_EXAMPLE = re.compile(r"^[a-z][a-z0-9_]*$")

# The examples of the release zip, in the order its README lists them: the ones with a window. The console
# examples (CONSOLE_EXAMPLES) only print, which a double-click shows as a flashing console window.
PACKAGED_EXAMPLES = (
    "demo_game",
    "sound",
    "pixel_perfect",
    "tilemap",
    "scene_sprites",
    "sprite_stress",
    "textured_quad",
    "rhi_triangle",
    "rhi_clear",
    "window_input",
)
CONSOLE_EXAMPLES = ("hello", "core_sandbox", "ecs_sandbox")

# What testers should see, by example; shown in the README.
DESCRIPTIONS = {
    "demo_game": (
        "What you should see: a small top-down cave game. Press Enter on the menu, collect every coin, then\n"
        "reach the door; spikes and slimes send you back to the start. WASD or the arrows move, F1 shows the\n"
        "colliders, M mutes the music, Escape goes back to the menu and quits from there. You should hear a\n"
        "tune, and a sound for each coin, each restart, the door opening and the win."
    ),
    "sound": (
        "What you should hear: a looping tune from the start. Space plays a blip; Left and Right move it\n"
        "between your ears, Up and Down raise or lower it. T stops or starts the tune, 1 and 2 turn the music\n"
        "down or up, B starts a buzz that circles around you, M mutes everything. The window stays black;\n"
        "the backtick key opens a window with the state."
    ),
    "pixel_perfect": (
        "What you should see: a pixel-art field with a hero, scaled up in sharp square pixels with black bars\n"
        "when the window's shape differs. WASD or the arrows move the hero; a minimap stays in a corner. F1 shows\n"
        "debug outlines, F2 switches to the window's own resolution."
    ),
    "tilemap": (
        "What you should see: a large tile map that scrolls and zooms on its own, with outlines around the\n"
        "chunks near the center. WASD or the arrows take over, Q and E zoom, Space plants flowers, F1 hides the\n"
        "outlines. The title shows how many chunks were drawn."
    ),
    "scene_sprites": (
        "What you should see: a hero on a field of grass with a ring of bobbing coins. WASD or the arrows move\n"
        "the hero, and the camera follows."
    ),
    "rhi_triangle": (
        "What you should see: a window with a dark gray background and a triangle in the middle: red at the\n"
        "top, green at the bottom left, blue at the bottom right, with the colors blending in between.\n"
        "Resizing the window keeps the triangle in the middle of it."
    ),
    "rhi_clear": "What you should see: a window whose color slowly cycles through the hues.",
    "sprite_stress": (
        "What you should see: colored discs moving and spinning on a black window, and a small red square in\n"
        "the top-left corner. The title shows the sprite count, FPS, draw calls and batches. Press 1 or 2 for\n"
        "1000 or 10 000 sprites; WASD or the arrows pan; the mouse wheel zooms; R resets; Escape quits."
    ),
    "textured_quad": (
        "What you should see: a square picture in the middle of a dark window: a blue tile with a dark border,\n"
        "a red block in its top-left corner and a large upright letter F. Press Space to switch between sharp\n"
        "pixels (nearest) and smooth blending (linear); the window title shows which."
    ),
    "window_input": (
        "What you should see: an empty window whose title shows the FPS, the cursor position and a position\n"
        "that WASD moves. Keys, mouse buttons and typed text are written to teserra.log."
    ),
}


class PackageError(Exception):
    """A reason the package cannot be made; the message is meant for the user."""


@dataclass(frozen=True)
class Platform:
    """How the packages of one target system look."""

    name: str  # in the package names: teserra-<version>-<name>-<example>.<archive>
    executable_suffix: str
    archive: str  # "zip" or "tar.gz"
    readme: Path
    examples_readme: Path
    separator: str  # between folder and executable in the README's headings


WINDOWS = Platform("windows-x64", ".exe", "zip", Path("tools/packaging/README.txt.in"),
                   Path("tools/packaging/README-examples.txt.in"), "\\")
LINUX = Platform("linux-x64", "", "tar.gz", Path("tools/packaging/README-linux.txt.in"),
                 Path("tools/packaging/README-examples-linux.txt.in"), "/")
PLATFORMS = {platform.name: platform for platform in (WINDOWS, LINUX)}

# The libraries a Linux executable may link dynamically: the C and C++ runtimes every distribution has. GLFW,
# the OpenGL loader and miniaudio open X11, Wayland, libGL and the sound libraries at run time, and vcpkg links
# everything else statically, so the archive needs no shared libraries of its own.
LINUX_SYSTEM_LIBRARIES = frozenset({
    "linux-vdso.so.1", "ld-linux-x86-64.so.2", "libc.so.6", "libm.so.6", "libstdc++.so.6", "libgcc_s.so.1",
    "libdl.so.2", "libpthread.so.0", "librt.so.1",
})


@dataclass(frozen=True)
class Package:
    path: Path
    files: tuple[str, ...]  # names inside the top-level folder, in archive order


Entries = list[tuple[str, Path | str]]  # name inside the top-level folder, and a file or the text to write
LinkedLibraries = Callable[[Path], list[str]]


def linked_libraries(executable: Path) -> list[str]:
    """The shared libraries `executable` links, by name, from ldd."""
    try:
        output = subprocess.run(["ldd", str(executable)], check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        raise PackageError(f"ldd {executable} failed: {error}") from error
    names = []
    for line in output.splitlines():
        words = line.split()
        if words:
            names.append(Path(words[0]).name)
    return names


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


def _line_endings(text: str, platform: Platform) -> str:
    text = text.replace("\r\n", "\n")
    return text.replace("\n", "\r\n") if platform is WINDOWS else text  # Notepad-friendly on Windows


def executable_name(example: str, platform: Platform = WINDOWS) -> str:
    return f"teserra_{example}{platform.executable_suffix}"


def readme(root: Path, example: str, version: str, executable: str, feedback_url: str,
           platform: Platform = WINDOWS) -> str:
    title = f"Teserra {version} - {example}"
    template = string.Template((root / platform.readme).read_text(encoding="utf-8"))
    text = template.substitute(
        title=title,
        underline="=" * len(title),
        version=version,
        example=example,
        executable=executable,
        description=DESCRIPTIONS.get(example, f"The {example} example of Teserra."),
        feedback_url=feedback_url,
    )
    return _line_endings(text, platform)


def examples_readme(root: Path, examples: tuple[str, ...], version: str, feedback_url: str,
                    platform: Platform = WINDOWS) -> str:
    title = f"Teserra {version} - examples"
    sections = []
    for example in examples:
        heading = f"{example}{platform.separator}{executable_name(example, platform)}"
        description = DESCRIPTIONS.get(example, f"The {example} example of Teserra.")
        sections.append(f"{heading}\n{'-' * len(heading)}\n{description}")
    template = string.Template((root / platform.examples_readme).read_text(encoding="utf-8"))
    text = template.substitute(
        title=title,
        underline="=" * len(title),
        version=version,
        examples="\n\n".join(sections),
        feedback_url=feedback_url,
    )
    return _line_endings(text, platform)


def _check_arguments(root: Path, build_dir: Path, version: str, templates: tuple[Path, ...]) -> None:
    if not _VERSION.match(version):
        raise PackageError(f"'{version}' is not a version like 0.1.0 or 0.1.0-alpha.5")
    kind = build_type(build_dir)
    if kind not in PACKAGED_BUILD_TYPES:
        raise PackageError(f"{build_dir} is a '{kind or 'unknown'}' build; package a Release build")
    for required in ROOT_FILES + tuple(str(template) for template in templates):
        if not (root / required).is_file():
            raise PackageError(f"{root / required} is missing")


def _example_entries(build_dir: Path, example: str, platform: Platform, runtime: Path | None, noticed: str,
                     ldd: LinkedLibraries | None) -> Entries:
    """The executable, its libraries, its assets and the runtime of one example, relative to its folder."""
    if not _EXAMPLE.match(example):
        raise PackageError(f"'{example}' is not an example name")
    example_dir = build_dir / "examples" / example
    executable = example_dir / executable_name(example, platform)
    if not executable.is_file():
        raise PackageError(f"{executable} does not exist; build the example first")
    entries: Entries = [(executable.name, executable)]
    if platform is WINDOWS:
        for dll in sorted(example_dir.glob("*.dll"), key=lambda p: p.name.lower()):
            component = NOTICED_DLLS.get(dll.name.lower())
            if component is None or f"[{component}]" not in noticed:
                raise PackageError(
                    f"{dll.name} would ship without a notice: add its library to {NOTICES} and its DLL to "
                    "NOTICED_DLLS in package_example.py")
            entries.append((dll.name, dll))
    else:
        bundled = sorted(file.name for file in example_dir.glob("*.so*") if file.is_file())
        if bundled:
            raise PackageError(f"{example} has shared libraries next to it ({', '.join(bundled)}); the Linux "
                               "archive ships none: link them statically, or open them at run time")
        foreign = sorted(set((ldd or linked_libraries)(executable)) - LINUX_SYSTEM_LIBRARIES)
        if foreign:
            raise PackageError(
                f"{executable.name} links {', '.join(foreign)}, which a Linux desktop may not have: link it "
                "statically, or open it at run time (LINUX_SYSTEM_LIBRARIES in package_example.py lists the "
                "allowed ones)")
    # Files the example loads at run time, from the assets/ folder its build copies next to the executable.
    assets = example_dir / "assets"
    entries += [(file.relative_to(example_dir).as_posix(), file)
                for file in sorted(assets.rglob("*"), key=lambda p: p.as_posix().lower()) if file.is_file()]
    if runtime is not None:
        entries += [(dll, runtime / dll) for dll in VC_RUNTIME_DLLS]
    return entries


def _write_zip(path: Path, name: str, entries: Entries) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for entry, source in entries:
            if isinstance(source, Path):
                archive.write(source, f"{name}/{entry}")
            else:
                archive.writestr(f"{name}/{entry}", source)


def _write_tar(path: Path, name: str, entries: Entries, executables: frozenset[str]) -> None:
    """A .tar.gz whose executables have mode 755 and other files 644, owned by root, whatever the build's."""
    with tarfile.open(path, "w:gz") as archive:
        for entry, source in entries:
            data = source.read_bytes() if isinstance(source, Path) else source.encode("utf-8")
            info = tarfile.TarInfo(f"{name}/{entry}")
            info.size = len(data)
            info.mode = 0o755 if entry in executables else 0o644
            info.mtime = int(source.stat().st_mtime) if isinstance(source, Path) else int(time.time())
            archive.addfile(info, io.BytesIO(data))


def _write_archive(out_dir: Path, name: str, entries: Entries, platform: Platform,
                   executables: frozenset[str]) -> Package:
    path = out_dir / f"{name}.{platform.archive}"
    path.parent.mkdir(parents=True, exist_ok=True)
    if platform.archive == "zip":
        _write_zip(path, name, entries)
    else:
        _write_tar(path, name, entries, executables)
    return Package(path=path, files=tuple(entry for entry, _ in entries))


def package(root: Path, build_dir: Path, example: str, version: str, out_dir: Path,
            feedback_url: str = DEFAULT_FEEDBACK_URL, vc_runtime: Path | None = None,
            platform: Platform = WINDOWS, ldd: LinkedLibraries | None = None) -> Package:
    """One example in its own archive, for a feedback build of that example."""
    if not _EXAMPLE.match(example):
        raise PackageError(f"'{example}' is not an example name")
    _check_arguments(root, build_dir, version, (platform.readme,))
    runtime = find_vc_runtime(vc_runtime) if platform is WINDOWS else None
    noticed = (root / NOTICES).read_text(encoding="utf-8")

    entries = _example_entries(build_dir, example, platform, runtime, noticed, ldd)
    entries += [(file, root / file) for file in ROOT_FILES]
    executable = executable_name(example, platform)
    entries.append(("README.txt", readme(root, example, version, executable, feedback_url, platform)))
    name = f"teserra-{version}-{platform.name}-{example}"
    return _write_archive(out_dir, name, entries, platform, frozenset({executable}))


def package_examples(root: Path, build_dir: Path, version: str, out_dir: Path,
                     feedback_url: str = DEFAULT_FEEDBACK_URL, vc_runtime: Path | None = None,
                     examples: tuple[str, ...] = PACKAGED_EXAMPLES, platform: Platform = WINDOWS,
                     ldd: LinkedLibraries | None = None) -> Package:
    """The release archive: every example in `examples` in a folder of its own."""
    _check_arguments(root, build_dir, version, (platform.examples_readme,))
    runtime = find_vc_runtime(vc_runtime) if platform is WINDOWS else None
    noticed = (root / NOTICES).read_text(encoding="utf-8")

    entries: Entries = [("README.txt", examples_readme(root, examples, version, feedback_url, platform))]
    entries += [(file, root / file) for file in ROOT_FILES]
    for example in examples:
        entries += [(f"{example}/{entry}", source)
                    for entry, source in _example_entries(build_dir, example, platform, runtime, noticed, ldd)]
    executables = frozenset(f"{example}/{executable_name(example, platform)}" for example in examples)
    name = f"teserra-{version}-{platform.name}-examples"
    return _write_archive(out_dir, name, entries, platform, executables)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, required=True, help="a Release build directory")
    which = parser.add_mutually_exclusive_group(required=True)
    which.add_argument("--example", help="one example in its own archive, e.g. rhi_triangle")
    which.add_argument("--all-examples", action="store_true",
                       help="the release archive with every example that has a window")
    parser.add_argument("--version", required=True, help="package version, e.g. 0.1.0-alpha.5")
    parser.add_argument("--platform", choices=sorted(PLATFORMS), default=WINDOWS.name,
                        help="the system the build is for (default: windows-x64)")
    parser.add_argument("--out", type=Path, default=Path("dist"), help="output directory (default: dist)")
    parser.add_argument("--feedback-url", default=DEFAULT_FEEDBACK_URL, help="link printed in the README")
    parser.add_argument("--vc-runtime", type=Path, help="folder with the Visual C++ runtime DLLs (Windows)")
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    platform = PLATFORMS[args.platform]
    try:
        if args.all_examples:
            made = package_examples(args.root, args.build_dir, args.version, args.out, args.feedback_url,
                                    args.vc_runtime, platform=platform)
        else:
            made = package(args.root, args.build_dir, args.example, args.version, args.out, args.feedback_url,
                           args.vc_runtime, platform=platform)
    except PackageError as error:
        print(f"package_example: {error}", file=sys.stderr)
        return 1
    print(f"package_example: {made.path} ({len(made.files)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
