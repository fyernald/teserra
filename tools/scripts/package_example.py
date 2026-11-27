#!/usr/bin/env python3
"""Package the examples of a Release build as zips for testers and releases (docs/process/release-process.md).

    python tools/scripts/package_example.py --build-dir build/windows-msvc-release --example rhi_triangle \\
        --version 0.1.0-alpha.5 --feedback-url https://github.com/fyernald/teserra/issues/N --out dist
    python tools/scripts/package_example.py --build-dir build/windows-msvc-release --all-examples \\
        --version 0.1.0 --out dist

--example writes <out>/teserra-<version>-windows-x64-<example>.zip, whose single top-level folder of the same
name holds:

- the example's executable and the DLLs next to it in the build (GLFW, spdlog, fmt, yyjson);
- the example's assets/ folder, if it has one (textured_quad reads its image from there);
- the Visual C++ runtime DLLs it needs, copied app-locally so that testers need no installer. They come from
  --vc-runtime, or else from the Microsoft.VC*.CRT folder under %VCToolsRedistDir%\\x64 (set by the Visual
  Studio developer environment);
- LICENSE, THIRD_PARTY_NOTICES.md, and a README.txt made from tools/packaging/README.txt.in.

--all-examples writes <out>/teserra-<version>-windows-x64-examples.zip, the release artifact: one folder per
example in PACKAGED_EXAMPLES, each with the files above except the licenses and README, and LICENSE,
THIRD_PARTY_NOTICES.md and a README.txt from tools/packaging/README-examples.txt.in at the top. release.yml
makes it for every version tag.

Every DLL in a package other than the Visual C++ runtime must be listed in THIRD_PARTY_NOTICES.md (through
NOTICED_DLLS), so a new dependency cannot ship without its notice. Only Release and RelWithDebInfo builds are
packaged: debug runtimes cannot be redistributed. The script does not build anything; build the Release preset
first.
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
EXAMPLES_README_TEMPLATE = Path("tools/packaging/README-examples.txt.in")
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
class Package:
    path: Path
    files: tuple[str, ...]  # names inside the top-level folder, in archive order


Entries = list[tuple[str, Path | str]]  # name inside the top-level folder, and a file or the text to write


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


def _windows_text(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\n", "\r\n")  # Notepad-friendly line endings


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
    return _windows_text(text)


def examples_readme(root: Path, examples: tuple[str, ...], version: str, feedback_url: str) -> str:
    title = f"Teserra {version} - examples"
    sections = []
    for example in examples:
        heading = f"{example}\\teserra_{example}.exe"
        description = DESCRIPTIONS.get(example, f"The {example} example of Teserra.")
        sections.append(f"{heading}\n{'-' * len(heading)}\n{description}")
    template = string.Template((root / EXAMPLES_README_TEMPLATE).read_text(encoding="utf-8"))
    text = template.substitute(
        title=title,
        underline="=" * len(title),
        version=version,
        examples="\n\n".join(sections),
        feedback_url=feedback_url,
    )
    return _windows_text(text)


def _check_arguments(root: Path, build_dir: Path, version: str, templates: tuple[Path, ...]) -> None:
    if not _VERSION.match(version):
        raise PackageError(f"'{version}' is not a version like 0.1.0 or 0.1.0-alpha.5")
    kind = build_type(build_dir)
    if kind not in PACKAGED_BUILD_TYPES:
        raise PackageError(f"{build_dir} is a '{kind or 'unknown'}' build; package a Release build")
    for required in ROOT_FILES + tuple(str(template) for template in templates):
        if not (root / required).is_file():
            raise PackageError(f"{root / required} is missing")


def _example_entries(build_dir: Path, example: str, runtime: Path, noticed: str) -> Entries:
    """The executable, its DLLs, its assets and the runtime of one example, relative to the example's folder."""
    if not _EXAMPLE.match(example):
        raise PackageError(f"'{example}' is not an example name")
    example_dir = build_dir / "examples" / example
    executable = example_dir / f"teserra_{example}.exe"
    if not executable.is_file():
        raise PackageError(f"{executable} does not exist; build the example first")
    entries: Entries = [(executable.name, executable)]
    for dll in sorted(example_dir.glob("*.dll"), key=lambda p: p.name.lower()):
        component = NOTICED_DLLS.get(dll.name.lower())
        if component is None or f"[{component}]" not in noticed:
            raise PackageError(
                f"{dll.name} would ship without a notice: add its library to {NOTICES} and its DLL to "
                "NOTICED_DLLS in package_example.py")
        entries.append((dll.name, dll))
    # Files the example loads at run time, from the assets/ folder its build copies next to the executable.
    assets = example_dir / "assets"
    entries += [(file.relative_to(example_dir).as_posix(), file)
                for file in sorted(assets.rglob("*"), key=lambda p: p.as_posix().lower()) if file.is_file()]
    entries += [(dll, runtime / dll) for dll in VC_RUNTIME_DLLS]
    return entries


def _write_zip(path: Path, name: str, entries: Entries) -> Package:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for entry, source in entries:
            if isinstance(source, Path):
                archive.write(source, f"{name}/{entry}")
            else:
                archive.writestr(f"{name}/{entry}", source)
    return Package(path=path, files=tuple(entry for entry, _ in entries))


def package(root: Path, build_dir: Path, example: str, version: str, out_dir: Path,
            feedback_url: str = DEFAULT_FEEDBACK_URL, vc_runtime: Path | None = None) -> Package:
    """One example in its own zip, for a feedback build of that example."""
    if not _EXAMPLE.match(example):
        raise PackageError(f"'{example}' is not an example name")
    _check_arguments(root, build_dir, version, (README_TEMPLATE,))
    runtime = find_vc_runtime(vc_runtime)
    noticed = (root / NOTICES).read_text(encoding="utf-8")

    entries = _example_entries(build_dir, example, runtime, noticed)
    entries += [(file, root / file) for file in ROOT_FILES]
    entries.append(("README.txt", readme(root, example, version, f"teserra_{example}.exe", feedback_url)))
    name = f"teserra-{version}-{PLATFORM}-{example}"
    return _write_zip(out_dir / f"{name}.zip", name, entries)


def package_examples(root: Path, build_dir: Path, version: str, out_dir: Path,
                     feedback_url: str = DEFAULT_FEEDBACK_URL, vc_runtime: Path | None = None,
                     examples: tuple[str, ...] = PACKAGED_EXAMPLES) -> Package:
    """The release zip: every example in `examples` in a folder of its own."""
    _check_arguments(root, build_dir, version, (EXAMPLES_README_TEMPLATE,))
    runtime = find_vc_runtime(vc_runtime)
    noticed = (root / NOTICES).read_text(encoding="utf-8")

    entries: Entries = [("README.txt", examples_readme(root, examples, version, feedback_url))]
    entries += [(file, root / file) for file in ROOT_FILES]
    for example in examples:
        entries += [(f"{example}/{entry}", source)
                    for entry, source in _example_entries(build_dir, example, runtime, noticed)]
    name = f"teserra-{version}-{PLATFORM}-examples"
    return _write_zip(out_dir / f"{name}.zip", name, entries)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, required=True, help="a Release build directory")
    which = parser.add_mutually_exclusive_group(required=True)
    which.add_argument("--example", help="one example in its own zip, e.g. rhi_triangle")
    which.add_argument("--all-examples", action="store_true",
                       help="the release zip with every example that has a window")
    parser.add_argument("--version", required=True, help="package version, e.g. 0.1.0-alpha.5")
    parser.add_argument("--out", type=Path, default=Path("dist"), help="output directory (default: dist)")
    parser.add_argument("--feedback-url", default=DEFAULT_FEEDBACK_URL, help="link printed in the README")
    parser.add_argument("--vc-runtime", type=Path, help="folder with the Visual C++ runtime DLLs")
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        if args.all_examples:
            made = package_examples(args.root, args.build_dir, args.version, args.out, args.feedback_url,
                                    args.vc_runtime)
        else:
            made = package(args.root, args.build_dir, args.example, args.version, args.out, args.feedback_url,
                           args.vc_runtime)
    except PackageError as error:
        print(f"package_example: {error}", file=sys.stderr)
        return 1
    print(f"package_example: {made.path} ({len(made.files)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
