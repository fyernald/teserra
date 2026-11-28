#!/usr/bin/env python3
"""Run clang-tidy on the C++ files a branch changes and report the findings on the lines it changes.

    python tools/scripts/check_tidy.py -p build/windows-msvc-debug                # changes since origin/main
    python tools/scripts/check_tidy.py -p build/ci-linux-clang --base HEAD^1      # a PR's merge commit (CI)
    python tools/scripts/check_tidy.py -p build/windows-msvc-debug engine/ecs/src/world.cpp   # whole files

Changes are the lines that differ between the merge base of --base and the working tree, plus untracked files,
in C++ files under the source directories of teserra_sources.py (third-party and generated code skipped). A
changed source file in the compile database is analyzed as it is built; a changed header through one
translation unit that includes it directly (its own module's sources first). Only findings on changed lines are
reported, so pre-existing ones do not fail a pull request; compiler errors are always reported, because they
stop the analysis. Files given on the command line are reported whole. Compile-fail tests (`compile_fail/`) are
skipped: they are meant not to compile. Exits 1 if anything is reported. CI runs it on pull requests in the
`linux (clang, ubuntu-26.04)` job (docs/guides/build-and-ci.md#layer-and-leak-checks).
"""

from __future__ import annotations

import argparse
import functools
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

from teserra_sources import CPP_EXTENSIONS, lex_cpp, list_files, read_text

REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_EXTENSIONS = frozenset({".cpp"})
EXPECTED_TO_FAIL = "compile_fail"  # directory of the compile-fail tests (cmake/TeserraTesting.cmake)

# Changed lines of each file; None means the whole file (untracked, or named on the command line).
Changes = dict[Path, "frozenset[int] | None"]

# "path:line:col: severity: message [check]"; notes may have no check.
_DIAGNOSTIC = re.compile(r"^(.+?):(\d+):(\d+): (warning|error|note): (.*?)(?: \[([^\]]+)\])?$")
_SUMMARY = re.compile(r"^\d+ (warnings?|errors?)( and \d+ errors?)? generated\.$")
_HUNK = re.compile(r"^@@ -\S+ \+(\d+)(?:,(\d+))? @@")


@dataclass
class Diagnostic:
    path: str  # relative to the root with forward slashes, or as clang-tidy printed it if outside the root
    line: int
    column: int
    severity: str  # warning or error
    message: str
    check: str
    details: list[str] = field(default_factory=list)  # the source excerpt and the notes that follow

    def key(self) -> tuple[str, int, int, str]:
        return self.path, self.line, self.column, self.message

    def __str__(self) -> str:
        return "\n".join([f"{self.path}:{self.line}:{self.column}: {self.severity}: {self.message} [{self.check}]",
                          *self.details])


def _same_case(path: str) -> str:
    # Windows paths compare without case: clang-tidy may print another drive letter case than git.
    return os.path.normcase(os.path.normpath(path))


def relative(path: str, root: Path) -> str:
    """`path` relative to `root` with forward slashes, if it is inside; else unchanged."""
    try:
        return Path(os.path.normpath(Path(root, path))).relative_to(os.path.normpath(root)).as_posix()
    except ValueError:
        return path


def parse_diff(text: str) -> dict[Path, frozenset[int]]:
    """The added or modified lines (new numbering) per file of a `git diff -U0` output."""
    lines: dict[Path, set[int]] = {}
    current: set[int] | None = None
    for line in text.splitlines():
        if line.startswith("+++ "):
            name = line[4:]
            current = None if name == "/dev/null" else lines.setdefault(Path(name.removeprefix("b/")), set())
        elif current is not None and (hunk := _HUNK.match(line)):
            start, count = int(hunk.group(1)), int(hunk.group(2) or 1)
            current.update(range(start, start + count))  # count 0: lines were only removed
    return {path: frozenset(numbers) for path, numbers in lines.items()}


def changed_lines(root: Path, base: str) -> Changes:
    """The checked C++ files that differ from the merge base of `base` with their changed lines, and untracked ones."""
    def git(*args: str) -> str:
        return subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, check=True).stdout

    merge_base = git("merge-base", base, "HEAD").strip()
    diff = git("diff", "-U0", "--no-color", "--no-ext-diff", "--diff-filter=d", merge_base)
    changes: Changes = dict(parse_diff(diff))
    changes.update((Path(line), None) for line in git("ls-files", "--others", "--exclude-standard").splitlines())
    return {path: changes[path] for path in list_files(root) if path in changes and checked(path)}


def checked(path: Path) -> bool:
    return path.suffix in CPP_EXTENSIONS and EXPECTED_TO_FAIL not in path.parts


def load_database(build_dir: Path) -> dict[str, Path]:
    """The translation units of compile_commands.json, by normalized absolute path."""
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        sys.exit(f"check_tidy: {database} not found; configure the build first (CMAKE_EXPORT_COMPILE_COMMANDS)")
    units = {}
    for entry in json.loads(database.read_text(encoding="utf-8")):
        path = Path(os.path.normpath(Path(entry["directory"], entry["file"])))
        if EXPECTED_TO_FAIL not in path.parts:
            units[_same_case(str(path))] = path
    return units


@functools.cache
def _include_names(unit: Path) -> tuple[str, ...]:
    return tuple(include.header.replace("\\", "/") for include in lex_cpp(read_text(unit)).includes)


def _includes(unit: Path, header: Path) -> bool:
    for name in _include_names(unit):
        if _same_case(str(unit.parent / name)) == _same_case(str(header)):
            return True
        # Through an include directory: <teserra/ecs/world.hpp> ends the path of the header.
        if "/" in name and _same_case(header.as_posix()).endswith(_same_case("/" + name)):
            return True
    return False


def select_units(root: Path, changed: list[Path], units: dict[str, Path]) -> tuple[list[Path], list[Path]]:
    """The translation units that cover `changed` (paths relative to `root`), and the changed files none covers.

    A header is covered by one unit that includes it directly, preferring units of the header's own module (the
    directory above its include/ or src/ directory), then the one with the header's name."""
    selected: dict[str, Path] = {}
    uncovered = []
    headers = []
    for path in changed:
        absolute = Path(os.path.normpath(root / path))
        key = _same_case(str(absolute))
        if key in units:
            selected[key] = units[key]
        elif path.suffix in SOURCE_EXTENSIONS:
            uncovered.append(path)  # not built in this configuration, such as a Windows-only file on Linux
        else:
            headers.append((path, absolute))

    for path, absolute in headers:
        if any(_includes(unit, absolute) for unit in selected.values()):
            continue
        parts = path.parts
        marker = next((part for part in ("include", "src") if part in parts), None)
        module = _same_case(str(root.joinpath(*parts[: parts.index(marker)]))) if marker else None

        def preference(unit: Path) -> tuple[bool, bool, str]:
            in_module = module is not None and _same_case(str(unit)).startswith(module + os.sep)
            return not in_module, unit.stem != path.stem, _same_case(str(unit))

        candidates = sorted(units.values(), key=preference)
        unit = next((unit for unit in candidates if unit.is_file() and _includes(unit, absolute)), None)
        if unit is None:
            uncovered.append(path)  # included only through other headers, or not at all in this configuration
        else:
            selected[_same_case(str(unit))] = unit
    return sorted(selected.values()), uncovered


def parse_output(text: str, root: Path) -> list[Diagnostic]:
    """The warnings and errors in clang-tidy's output, each with the lines and notes that follow it."""
    diagnostics: list[Diagnostic] = []
    current: Diagnostic | None = None
    for line in text.splitlines():
        match = _DIAGNOSTIC.match(line)
        if match and match.group(4) != "note":
            path, line_number, column, severity, message, check = match.groups()
            current = Diagnostic(relative(path, root), int(line_number), int(column), severity, message,
                                 check or "clang-diagnostic")
            diagnostics.append(current)
        elif current is not None and not _SUMMARY.match(line):
            current.details.append(line)
    return diagnostics


def report(diagnostics: list[Diagnostic], changes: Changes) -> tuple[list[Diagnostic], int]:
    """The findings to report, without duplicates (a header analyzed twice), and how many were left out."""
    lines = {_same_case(path.as_posix()): numbers for path, numbers in changes.items()}
    kept: dict[tuple[str, int, int, str], Diagnostic] = {}
    ignored = 0
    for diagnostic in diagnostics:
        path = _same_case(diagnostic.path)
        on_change = path in lines and (lines[path] is None or diagnostic.line in lines[path])
        if diagnostic.severity == "error" or on_change:
            kept.setdefault(diagnostic.key(), diagnostic)
        else:
            ignored += 1
    return sorted(kept.values(), key=lambda diagnostic: (diagnostic.path, diagnostic.line, diagnostic.column)), ignored


def find_clang_tidy(explicit: str | None) -> str:
    if explicit:
        return explicit
    found = shutil.which("clang-tidy")
    if found:
        return found
    # Visual Studio's "C++ Clang tools for Windows" component, outside a developer prompt.
    vc_dir = os.environ.get("VCINSTALLDIR")
    if vc_dir:
        candidate = Path(vc_dir) / "Tools" / "Llvm" / "x64" / "bin" / "clang-tidy.exe"
        if candidate.is_file():
            return str(candidate)
    sys.exit("check_tidy: clang-tidy not found; install LLVM or pass --clang-tidy <path>")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", "--build-dir", required=True, type=Path,
                        help="build directory with compile_commands.json")
    parser.add_argument("--base", default="origin/main", help="git revision to compare with (default: origin/main)")
    parser.add_argument("--clang-tidy", metavar="PATH", help="clang-tidy executable to use")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1, help="parallel clang-tidy runs")
    parser.add_argument("files", nargs="*", type=Path, help="check these files whole instead of the changes")
    args = parser.parse_args()

    clang_tidy = find_clang_tidy(args.clang_tidy)
    version = subprocess.run([clang_tidy, "--version"], capture_output=True, text=True, check=True)
    print(f"check_tidy: {' '.join(version.stdout.split())}")

    if args.files:
        named = (Path(relative(str(path.resolve()), REPO_ROOT)) for path in args.files)
        changes: Changes = {path: None for path in named if checked(path)}
    else:
        changes = changed_lines(REPO_ROOT, args.base)
    if not changes:
        print("check_tidy: no changed C++ files")
        return 0

    build_dir = args.build_dir if args.build_dir.is_absolute() else REPO_ROOT / args.build_dir
    selected, uncovered = select_units(REPO_ROOT, list(changes), load_database(build_dir))
    for path in uncovered:
        print(f"check_tidy: not checked, no translation unit in {build_dir.name} covers {path.as_posix()}")
    print(f"check_tidy: {len(changes)} changed files, {len(selected)} translation units")

    # -Wno-error: a CI build's warnings-as-errors would turn a warning only this clang-tidy's Clang knows into a
    # compiler error; the build itself already enforces the warnings.
    def run(unit: Path) -> str:
        command = [clang_tidy, "-p", str(build_dir), "--quiet", "--extra-arg=-Wno-error", str(unit)]
        result = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, text=True, encoding="utf-8",
                                errors="replace")
        return result.stdout + result.stderr

    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        outputs = list(pool.map(run, selected))
    findings, ignored = report([found for output in outputs for found in parse_output(output, REPO_ROOT)], changes)

    for finding in findings:
        print(finding)
    if ignored:
        print(f"check_tidy: {ignored} findings outside the changed lines left out")
    if findings:
        print(f"check_tidy: {len(findings)} findings. Fix them, or suppress a justified one with "
              "`// NOLINT(<check>): <reason>` (docs/guides/coding-standards.md#static-analysis).")
        return 1
    print(f"check_tidy: {len(changes) - len(uncovered)} changed files are clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
