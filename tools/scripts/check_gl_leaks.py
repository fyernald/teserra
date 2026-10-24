#!/usr/bin/env python3
"""Check that backend, platform and third-party names stay where they belong, and that TODOs name an issue.

    python tools/scripts/check_gl_leaks.py            # report violations, exit 1 if any
    python tools/scripts/check_gl_leaks.py --root DIR # check another tree (used by the script's tests)

Each rule in RULES lists identifiers (matched in code, not in comments or string literals) and headers
(matched in #include and __has_include) and the only place they may appear. TODO comments must have the
form `TODO(#123)`. Third-party and generated code is skipped. See
docs/guides/build-and-ci.md#layer-and-leak-checks and docs/architecture/rhi.md#what-must-not-leak-out-of-the-rhi.
Used by the `leak-check` / `check` CMake targets and by the CI `checks` job.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from teserra_sources import CPP_EXTENSIONS, LineIndex, Violation, lex_cpp, list_files, read_text

REPO_ROOT = Path(__file__).resolve().parents[2]


def _under(prefix: str) -> Callable[[PurePosixPath], bool]:
    return lambda path: path.as_posix().startswith(prefix)


def _windows_only_platform_file(path: PurePosixPath) -> bool:
    # OS-specific platform code lives in a windows/ directory or a *_windows file, which CMake compiles
    # only on Windows (docs/architecture/platform-abstraction.md).
    in_platform = _under("engine/platform/src/")(path) or _under("engine/platform/tests/")(path)
    return in_platform and ("windows" in path.parent.parts or path.stem.endswith("_windows"))


# flatBinding() turns (set, binding) into OpenGL's single binding index. Only the code that emits GLSL with
# it and the backend that binds at it may use it, so no other code depends on GL's numbering
# (docs/architecture/rhi.md#binding-model).
_FLAT_BINDING_FILES = frozenset({
    "engine/rhi/include/teserra/rhi/shader_blob.hpp",
    "engine/rhi/src/shader_blob.cpp",
    "engine/rhi/tests/shader_blob_test.cpp",
})


def _flat_binding_allowed(path: PurePosixPath) -> bool:
    return (path.as_posix() in _FLAT_BINDING_FILES or _under("engine/rhi/backends/opengl/")(path)
            or _under("tools/shaderc/")(path))


@dataclass(frozen=True)
class Rule:
    name: str
    what: str  # for messages: "OpenGL", "GLFW", ...
    allowed_in: str  # for messages
    is_allowed: Callable[[PurePosixPath], bool]
    identifiers: re.Pattern[str] | None
    headers: re.Pattern[str] | None


RULES = (
    Rule(
        "gl-leak",
        "OpenGL",
        "engine/rhi/backends/opengl/",
        _under("engine/rhi/backends/opengl/"),
        # gl[A-Z][a-z] leaves glTF, glm and glfw alone; GL[a-z] covers GLuint, GLenum, GLsizei, ...
        re.compile(r"\b(?:GL_\w+|GLAD_\w+|gl[A-Z][a-z]\w*|GL[a-z]\w*)\b"),
        re.compile(r"^(?:glad|GL|GLES\d*|EGL|KHR|OpenGL)/"),
    ),
    Rule(
        "vulkan-leak",
        "Vulkan",
        "engine/rhi/backends/vulkan/",
        _under("engine/rhi/backends/vulkan/"),
        # Not VK_*: Win32 virtual-key codes (VK_ESCAPE) share the prefix. Vulkan code cannot avoid the
        # Vk types and vk functions anyway.
        re.compile(r"\b(?:Vk[A-Z]\w*|vk[A-Z]\w*|PFN_vk\w*)\b"),
        re.compile(r"^(?:vulkan/|volk\.h$|vk_mem_alloc\.h$)"),
    ),
    Rule(
        "glfw-leak",
        "GLFW",
        "engine/platform/src/glfw/",
        _under("engine/platform/src/glfw/"),
        re.compile(r"\b(?:GLFW\w*|glfw[A-Z]\w*)\b"),
        re.compile(r"^GLFW/"),
    ),
    Rule(
        "win32-leak",
        "Win32",
        "Windows-only platform files (a windows/ directory or a *_windows file under engine/platform/)",
        _windows_only_platform_file,
        None,  # Win32 names (HWND, DWORD, ...) are too short and common to match reliably; headers suffice
        re.compile(
            r"^(?:windows|windef|winbase|winuser|wingdi|winnt|minwindef|winerror|dwmapi|shellscalingapi|"
            r"shellapi|shlobj|objbase|combaseapi|commctrl|commdlg|dbghelp|psapi|timeapi|mmsystem|imm|"
            r"xinput|winsock2|ws2tcpip)\.h$",
            re.IGNORECASE,
        ),
    ),
    Rule(
        "flat-binding-leak",
        "Flat GL binding",
        "<teserra/rhi/shader_blob.hpp>, engine/rhi/backends/opengl/ and tools/shaderc/",
        _flat_binding_allowed,
        re.compile(r"\bflatBinding\b"),
        None,
    ),
    Rule(
        "glm-leak",
        "glm",
        "engine/core/ (use the aliases of <teserra/core/math.hpp>, ADR-0008)",
        _under("engine/core/"),
        re.compile(r"\b(?:glm\s*::|GLM_\w+)"),
        re.compile(r"^glm/"),
    ),
)

# Reviewed exceptions: (path, rule) -> reason. Keep this list short; each entry is also noted in the file.
EXCEPTIONS = {
    ("engine/core/src/assert.cpp", "win32-leak"):
        "TESERRA_FATAL breaks into an attached debugger, and core is below platform (ADR-0015 review note)",
}

TODO_RULE = "todo-issue"
_BARE_TODO = re.compile(r"\bTODO\b(?!\(#\d+\))")
CMAKE_NAMES = frozenset({"CMakeLists.txt"})
CMAKE_EXTENSIONS = frozenset({".cmake"})


def check_cpp(root: Path, path: PurePosixPath) -> list[Violation]:
    text = read_text(root / path)
    lexed = lex_cpp(text)
    lines = LineIndex(text)
    violations = []

    def report(offset: int, rule: str, message: str) -> None:
        line, column = lines.position(offset)
        violations.append(Violation(path.as_posix(), line, column, rule, message))

    for rule in RULES:
        if rule.is_allowed(path) or (path.as_posix(), rule.name) in EXCEPTIONS:
            continue
        if rule.identifiers:
            for match in rule.identifiers.finditer(lexed.code):
                report(match.start(), rule.name,
                       f"{rule.what} name '{match.group(0)}' outside {rule.allowed_in}")
        if rule.headers:
            for include in lexed.includes:
                if rule.headers.search(include.header):
                    report(include.offset, rule.name,
                           f"{rule.what} header <{include.header}> outside {rule.allowed_in}")

    for comment in lexed.comments:
        for match in _BARE_TODO.finditer(comment.text):
            report(comment.offset + match.start(), TODO_RULE, "TODO without an issue: write TODO(#123)")
    violations.sort(key=lambda violation: (violation.line, violation.column))
    return violations


def check_cmake(root: Path, path: PurePosixPath) -> list[Violation]:
    violations = []
    for number, line in enumerate(read_text(root / path).splitlines(), start=1):
        for match in _BARE_TODO.finditer(line):
            violations.append(Violation(path.as_posix(), number, match.start() + 1, TODO_RULE,
                                        "TODO without an issue: write TODO(#123)"))
    return violations


def check(root: Path) -> tuple[list[Violation], int]:
    """Returns the violations and the number of files checked."""
    violations = []
    checked = 0
    for path in list_files(root):
        posix = PurePosixPath(path.as_posix())
        if path.suffix in CPP_EXTENSIONS:
            violations.extend(check_cpp(root, posix))
        elif path.name in CMAKE_NAMES or path.suffix in CMAKE_EXTENSIONS:
            violations.extend(check_cmake(root, posix))
        else:
            continue
        checked += 1
    return violations, checked


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="tree to check (default: this repository)")
    args = parser.parse_args(argv)

    start = time.perf_counter()
    violations, checked = check(args.root.resolve())
    for violation in violations:
        print(violation)
    elapsed = time.perf_counter() - start
    if violations:
        print(f"check_gl_leaks: {len(violations)} violation(s) in {checked} files ({elapsed:.2f} s)")
        return 1
    print(f"check_gl_leaks: {checked} files, no leaks ({elapsed:.2f} s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
