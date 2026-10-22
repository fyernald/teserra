"""Shared helpers of the source checks in tools/scripts (docs/guides/build-and-ci.md#layer-and-leak-checks).

- list_files(): the files to check, from git (tracked and untracked-but-not-ignored) when the root is a
  repository, else from the directory tree; third-party and generated code is skipped.
- lex_cpp(): a C++ source with comments and string/character literals blanked out, so identifiers can be
  matched in code only, plus the comments and the #include / __has_include header names.
- Violation: one finding, printed as `path:line:col: error: message [rule]` for the problem matcher.
"""

from __future__ import annotations

import bisect
import os
import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

SOURCE_DIRS = ("engine", "examples", "tests", "tools", "benchmarks", "editor")
CPP_EXTENSIONS = frozenset({".cpp", ".hpp", ".h", ".inl"})
EXCLUDED_PARTS = frozenset({"third_party", "generated"})


@dataclass(frozen=True)
class Violation:
    path: str  # relative to the checked root, with forward slashes
    line: int
    column: int
    rule: str
    message: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}:{self.column}: error: {self.message} [{self.rule}]"


def list_files(root: Path, dirs: tuple[str, ...] = SOURCE_DIRS) -> list[Path]:
    """Files under `dirs` of `root`, relative to `root` and sorted, without third-party/generated code."""
    if (root / ".git").exists():
        result = subprocess.run(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", *dirs],
            cwd=root,
            capture_output=True,
            text=True,
            check=True,
        )
        candidates = {Path(line) for line in result.stdout.splitlines()}
    else:
        candidates = set()
        for directory in dirs:
            for dirpath, _, filenames in os.walk(root / directory):
                candidates.update(Path(dirpath, name).relative_to(root) for name in filenames)
    return sorted(
        path
        for path in candidates
        if not EXCLUDED_PARTS.intersection(path.parts) and (root / path).is_file()  # skip deleted files
    )


def read_text(path: Path) -> str:
    # Universal newlines: columns do not depend on the line endings of the checkout.
    return path.read_text(encoding="utf-8", errors="replace")


class LineIndex:
    """Turns offsets into 1-based (line, column) pairs."""

    def __init__(self, text: str) -> None:
        self._starts = [0] + [match.end() for match in re.finditer("\n", text)]

    def position(self, offset: int) -> tuple[int, int]:
        line = bisect.bisect_right(self._starts, offset)
        return line, offset - self._starts[line - 1] + 1


@dataclass(frozen=True)
class Comment:
    offset: int
    text: str


@dataclass(frozen=True)
class Include:
    offset: int  # of the header name
    header: str


@dataclass
class LexedSource:
    code: str  # the source with comments and literals replaced by spaces (newlines kept)
    comments: list[Comment] = field(default_factory=list)
    includes: list[Include] = field(default_factory=list)


_RAW_STRING_PREFIXES = frozenset({"R", "LR", "uR", "UR", "u8R"})
_INCLUDE_DIRECTIVE = re.compile(r"#\s*include\s*([<\"])")
_HAS_INCLUDE = re.compile(r"\s*\(\s*([<\"])")


def _is_identifier_char(char: str) -> bool:
    return char.isalnum() or char == "_"


def lex_cpp(text: str) -> LexedSource:
    """Separates code from comments and literals. Not a full C++ lexer, but exact for these cases:
    // and /* */ comments, string and character literals with escapes and prefixes, raw strings, digit
    separators (16'666'667), #include directives and __has_include()."""
    code = list(text)
    result = LexedSource(code="")
    length = len(text)

    def blank(start: int, end: int) -> None:
        for index in range(start, end):
            if code[index] != "\n":
                code[index] = " "

    def header_name(open_quote: int) -> int:
        # Records the header name that starts at `open_quote` and returns the offset after it.
        close = ">" if text[open_quote] == "<" else '"'
        end = text.find(close, open_quote + 1)
        line_end = text.find("\n", open_quote)
        if end == -1 or (line_end != -1 and end > line_end):
            return open_quote + 1
        result.includes.append(Include(open_quote + 1, text[open_quote + 1 : end]))
        blank(open_quote, end + 1)
        return end + 1

    at_line_start = True  # only whitespace since the last newline: a '#' starts a directive
    index = 0
    while index < length:
        char = text[index]
        if char == "\n":
            at_line_start = True
            index += 1
            continue
        if char in " \t\r\f\v":
            index += 1
            continue
        if text.startswith("//", index):
            end = text.find("\n", index)
            end = length if end == -1 else end
            result.comments.append(Comment(index, text[index:end]))
            blank(index, end)
            index = end
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = length if end == -1 else end + 2
            result.comments.append(Comment(index, text[index:end]))
            blank(index, end)
            index = end
            continue

        line_start, at_line_start = at_line_start, False
        if char == "#" and line_start:
            directive = _INCLUDE_DIRECTIVE.match(text, index)
            if directive:
                index = header_name(directive.start(1))
            else:
                index += 1
            continue
        if char.isdigit() or (char == "." and index + 1 < length and text[index + 1].isdigit()):
            # A preprocessing number, so that digit separators are not taken for character literals.
            index += 1
            while index < length:
                if text[index] in "eEpP" and index + 1 < length and text[index + 1] in "+-":
                    index += 2
                elif _is_identifier_char(text[index]) or text[index] == ".":
                    index += 1
                elif text[index] == "'" and index + 1 < length and _is_identifier_char(text[index + 1]):
                    index += 2
                else:
                    break
            continue
        if _is_identifier_char(char):
            start = index
            while index < length and _is_identifier_char(text[index]):
                index += 1
            identifier = text[start:index]
            if identifier in _RAW_STRING_PREFIXES and index < length and text[index] == '"':
                index = _skip_raw_string(text, index, blank)
            elif identifier == "__has_include":
                has_include = _HAS_INCLUDE.match(text, index)
                if has_include:
                    index = header_name(has_include.start(1))
            # A u8/u/U/L prefix is followed by the literal, which the next iteration handles.
            continue
        if char in "\"'":
            index = _skip_quoted(text, index, blank)
            continue
        index += 1

    result.code = "".join(code)
    return result


def _skip_quoted(text: str, start: int, blank) -> int:
    quote = text[start]
    index = start + 1
    while index < len(text) and text[index] != quote and text[index] != "\n":
        index += 2 if text[index] == "\\" else 1
    end = min(index + 1, len(text))
    blank(start, end)
    return end


def _skip_raw_string(text: str, quote: int, blank) -> int:
    # R"delimiter( ... )delimiter"
    paren = text.find("(", quote + 1)
    if paren == -1:
        return quote + 1
    terminator = ")" + text[quote + 1 : paren] + '"'
    end = text.find(terminator, paren + 1)
    end = len(text) if end == -1 else end + len(terminator)
    blank(quote, end)
    return end
