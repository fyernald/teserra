from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

from script_test_support import TreeTestCase

import check_tidy

NAMING = "[readability-identifier-naming]"


class ParseDiffTest(unittest.TestCase):
    def test_collects_added_and_modified_lines(self) -> None:
        diff = (
            "diff --git a/engine/a.cpp b/engine/a.cpp\n"
            "--- a/engine/a.cpp\n"
            "+++ b/engine/a.cpp\n"
            "@@ -3 +3,2 @@ int f()\n"
            "-old\n"
            "+new\n"
            "+added\n"
            "@@ -10,2 +11 @@\n"
            "@@ -20,3 +20,0 @@\n"
            "diff --git a/engine/new.hpp b/engine/new.hpp\n"
            "--- /dev/null\n"
            "+++ b/engine/new.hpp\n"
            "@@ -0,0 +1,2 @@\n"
        )
        self.assertEqual(check_tidy.parse_diff(diff), {
            Path("engine/a.cpp"): frozenset({3, 4, 11}),
            Path("engine/new.hpp"): frozenset({1, 2}),
        })


class ChangedLinesTest(TreeTestCase):
    def git(self, *args: str) -> None:
        subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args], cwd=self.root,
                       capture_output=True, check=True)

    def test_changes_since_the_merge_base_including_untracked_files(self) -> None:
        self.git("init", "-q", "-b", "main")
        self.write("engine/a.cpp", "int a;\nint b;\n")
        self.write("engine/gone.cpp", "int c;\n")
        self.write("engine/core/tests/compile_fail/bad.cpp", "int d;\n")
        self.git("add", ".")
        self.git("commit", "-q", "-m", "base")
        self.git("checkout", "-q", "-b", "topic")
        self.write("engine/a.cpp", "int a;\nint B;\nint e;\n")
        (self.root / "engine/gone.cpp").unlink()
        self.write("engine/core/tests/compile_fail/bad.cpp", "int D;\n")
        self.write("engine/untracked.hpp", "#pragma once\n")
        self.write("docs/notes.md", "text\n")
        self.assertEqual(check_tidy.changed_lines(self.root, "main"), {
            Path("engine/a.cpp"): frozenset({2, 3}),
            Path("engine/untracked.hpp"): None,
        })


class SelectUnitsTest(TreeTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.write("engine/ecs/include/teserra/ecs/world.hpp", "#pragma once\n")
        self.write("engine/ecs/src/detail.hpp", "#pragma once\n")
        self.write("engine/ecs/src/indirect.hpp", "#pragma once\n")
        self.write("engine/ecs/src/world.cpp", '#include <teserra/ecs/world.hpp>\n#include "detail.hpp"\n')
        self.write("engine/ecs/src/query.cpp", "#include <teserra/ecs/world.hpp>\n")
        self.write("engine/scene/src/scene.cpp", "#include <teserra/ecs/world.hpp>\n")
        self.write("engine/platform/src/window_windows.cpp", "int x;\n")
        built = ["engine/ecs/src/world.cpp", "engine/ecs/src/query.cpp", "engine/scene/src/scene.cpp"]
        self.units = {check_tidy._same_case(str(self.root / path)): self.root / path for path in built}

    def select(self, *changed: str) -> tuple[list[str], list[str]]:
        units, uncovered = check_tidy.select_units(self.root, [Path(path) for path in changed], self.units)
        return ([unit.relative_to(self.root).as_posix() for unit in units],
                [path.as_posix() for path in uncovered])

    def test_sources_are_their_own_units(self) -> None:
        self.assertEqual(self.select("engine/ecs/src/query.cpp"), (["engine/ecs/src/query.cpp"], []))

    def test_a_header_prefers_the_unit_of_its_module_with_its_name(self) -> None:
        self.assertEqual(self.select("engine/ecs/include/teserra/ecs/world.hpp"), (["engine/ecs/src/world.cpp"], []))

    def test_a_header_reuses_a_selected_unit_that_includes_it(self) -> None:
        self.assertEqual(self.select("engine/scene/src/scene.cpp", "engine/ecs/include/teserra/ecs/world.hpp"),
                         (["engine/scene/src/scene.cpp"], []))

    def test_quoted_includes_resolve_next_to_the_unit(self) -> None:
        self.assertEqual(self.select("engine/ecs/src/detail.hpp"), (["engine/ecs/src/world.cpp"], []))

    def test_files_no_unit_covers_are_reported(self) -> None:
        self.assertEqual(self.select("engine/platform/src/window_windows.cpp", "engine/ecs/src/indirect.hpp"),
                         ([], ["engine/platform/src/window_windows.cpp", "engine/ecs/src/indirect.hpp"]))


class ReportTest(TreeTestCase):
    def output(self, *lines: str) -> str:
        return "\n".join(lines).replace("ROOT", str(self.root)) + "\n"

    def test_parses_diagnostics_with_their_details(self) -> None:
        text = self.output(
            f"ROOT/engine/a.cpp:2:5: warning: invalid case style for variable 'B' {NAMING}",
            "    2 | int B;",
            "      |     ^",
            "ROOT/engine/a.hpp:7:1: note: declared here",
            "1 warning generated.",
        )
        [diagnostic] = check_tidy.parse_output(text, self.root)
        self.assertEqual(str(diagnostic), "\n".join([
            f"engine/a.cpp:2:5: warning: invalid case style for variable 'B' {NAMING}",
            "    2 | int B;",
            "      |     ^",
            f"{self.root}/engine/a.hpp:7:1: note: declared here",
        ]))

    def test_reports_findings_on_changed_lines_and_compiler_errors_only(self) -> None:
        text = self.output(
            f"ROOT/engine/a.cpp:2:5: warning: changed line {NAMING}",
            f"ROOT/engine/a.cpp:9:5: warning: unchanged line {NAMING}",
            f"ROOT/engine/b.hpp:4:1: warning: whole file {NAMING}",
            f"ROOT/engine/b.hpp:4:1: warning: whole file {NAMING}",
            f"ROOT/engine/c.hpp:1:1: warning: unchanged file {NAMING}",
            "/usr/include/x.h:1:10: error: 'y.h' file not found [clang-diagnostic-error]",
        )
        changes = {Path("engine/a.cpp"): frozenset({2}), Path("engine/b.hpp"): None}
        findings, ignored = check_tidy.report(check_tidy.parse_output(text, self.root), changes)
        self.assertEqual([finding.message for finding in findings],
                         ["'y.h' file not found", "changed line", "whole file"])
        self.assertEqual(ignored, 2)


if __name__ == "__main__":
    unittest.main()
