#!/usr/bin/env python3
"""Check that engine modules only include headers of modules they depend on.

    python tools/scripts/check_layers.py            # report violations, exit 1 if any
    python tools/scripts/check_layers.py --root DIR # check another tree (used by the script's tests)

Modules are read from `teserra_add_module(<name> LAYER <n> DEPENDS ...)` in engine/**/CMakeLists.txt; a
module owns the files under its directory (a nested module, like a backend, owns its own subtree). A file
may include `<teserra/<x>/...>` when x is its own module or a direct DEPENDS entry; transitive dependencies
do not count, so every module declares what it uses. Files under a module's tests/ may also include the
test support headers `<teserra/test/...>`. CMake enforces the layer order of DEPENDS at configure time;
this script closes the gap that PUBLIC linking leaves (docs/architecture/overview.md#how-the-layering-is-enforced).
Used by the `layer-check` / `check` CMake targets and by the CI `checks` job.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from teserra_sources import CPP_EXTENSIONS, LineIndex, Violation, lex_cpp, list_files, read_text

REPO_ROOT = Path(__file__).resolve().parents[2]
RULE = "layer-include"
TEST_SUPPORT = "test"
_MODULE_CALL = re.compile(r"\bteserra_add_module\s*\(([^)]*)\)", re.IGNORECASE)
_MODULE_KEYWORDS = frozenset({"LAYER", "DEPENDS", "OPTIONAL_DEPENDS", "PUBLIC_HEADERS", "SOURCES"})
# Optional dependencies may be absent from a build, but where they are used, they are dependencies.
_DEPENDS_KEYWORDS = frozenset({"DEPENDS", "OPTIONAL_DEPENDS"})
_TESERRA_HEADER = re.compile(r"^teserra/([^/]+)/")


@dataclass(frozen=True)
class Module:
    name: str
    directory: PurePosixPath  # relative to the root
    depends: frozenset[str]
    cmake_file: str


def _strip_cmake_comments(text: str) -> str:
    text = re.sub(r"#\[(=*)\[.*?\]\1\]", " ", text, flags=re.DOTALL)  # bracket comments
    lines = []
    for line in text.splitlines():
        in_quotes = False
        for index, char in enumerate(line):
            if char == '"' and (index == 0 or line[index - 1] != "\\"):
                in_quotes = not in_quotes
            elif char == "#" and not in_quotes:
                line = line[:index]
                break
        lines.append(line)
    return "\n".join(lines)


def find_modules(root: Path) -> dict[str, Module]:
    modules: dict[str, Module] = {}
    for cmake_file in list_files(root, ("engine",)):
        if cmake_file.name != "CMakeLists.txt":
            continue
        text = _strip_cmake_comments(read_text(root / cmake_file))
        for call in _MODULE_CALL.finditer(text):
            tokens = [token.strip('"') for token in call.group(1).split()]
            if not tokens:
                continue
            depends: set[str] = set()
            keyword = None
            for token in tokens[1:]:
                if token in _MODULE_KEYWORDS:
                    keyword = token
                elif keyword in _DEPENDS_KEYWORDS:
                    depends.add(token)
            directory = PurePosixPath(cmake_file.parent.as_posix())
            modules[tokens[0]] = Module(tokens[0], directory, frozenset(depends), cmake_file.as_posix())
    return modules


def owning_module(path: PurePosixPath, modules: dict[str, Module]) -> Module | None:
    """The module with the deepest directory that contains `path`."""
    owners = [module for module in modules.values() if module.directory in path.parents]
    return max(owners, key=lambda module: len(module.directory.parts), default=None)


def check_file(root: Path, path: PurePosixPath, module: Module, modules: dict[str, Module]) -> list[Violation]:
    text = read_text(root / path)
    lexed = lex_cpp(text)
    lines = LineIndex(text)
    in_tests = (module.directory / "tests") in path.parents
    violations = []
    for include in lexed.includes:
        match = _TESERRA_HEADER.match(include.header)
        if not match:
            continue
        target = match.group(1)
        if target == module.name or target in module.depends:
            continue
        if target == TEST_SUPPORT and in_tests:
            continue
        if target == TEST_SUPPORT:
            message = (f"<{include.header}> is test support; only {module.directory}/tests/ may include it, "
                       f"not module '{module.name}' itself")
        elif target not in modules:
            message = f"<{include.header}> belongs to no module: no teserra_add_module({target} ...) exists"
        else:
            message = (f"module '{module.name}' includes <{include.header}> but does not depend on "
                       f"'{target}'; add it to DEPENDS in {module.cmake_file}")
        line, column = lines.position(include.offset)
        violations.append(Violation(path.as_posix(), line, column, RULE, message))
    return violations


def check(root: Path) -> tuple[list[Violation], int, int]:
    """Returns the violations, the number of files checked and the number of modules."""
    modules = find_modules(root)
    violations = []
    checked = 0
    for path in list_files(root, ("engine",)):
        if path.suffix not in CPP_EXTENSIONS:
            continue
        posix = PurePosixPath(path.as_posix())
        module = owning_module(posix, modules)
        if module is None:
            continue
        checked += 1
        violations.extend(check_file(root, posix, module, modules))
    return violations, checked, len(modules)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="tree to check (default: this repository)")
    args = parser.parse_args(argv)

    start = time.perf_counter()
    violations, checked, module_count = check(args.root.resolve())
    for violation in violations:
        print(violation)
    elapsed = time.perf_counter() - start
    if violations:
        print(f"check_layers: {len(violations)} include(s) of undeclared modules in {checked} files "
              f"of {module_count} modules ({elapsed:.2f} s)")
        return 1
    print(f"check_layers: {checked} files of {module_count} modules include only their dependencies "
          f"({elapsed:.2f} s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
