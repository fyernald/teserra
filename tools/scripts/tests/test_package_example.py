from __future__ import annotations

import contextlib
import io
import os
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from script_test_support import TreeTestCase

import package_example


class PackageExampleTest(TreeTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.write("LICENSE", "MIT License\n")
        self.write("THIRD_PARTY_NOTICES.md", "# Third-party notices\n")
        self.write("tools/packaging/README.txt.in", """
            $title
            $underline
            $description
            Run $executable ($example $version).
            Feedback: $feedback_url
            """)
        self.write("build/CMakeCache.txt", "CMAKE_BUILD_TYPE:STRING=Release\n")
        self.write("build/examples/rhi_triangle/teserra_rhi_triangle.exe", "exe")
        self.write("build/examples/rhi_triangle/glfw3.dll", "glfw")
        self.write("build/examples/rhi_triangle/spdlog.dll", "spdlog")
        self.write("build/examples/rhi_triangle/teserra.log", "a smoke run's log")
        self.write("build/examples/rhi_triangle/teserra_rhi_triangle.pdb", "symbols")
        for dll in package_example.VC_RUNTIME_DLLS:
            self.write(f"redist/x64/Microsoft.VC145.CRT/{dll}", dll)

    def package(self, **overrides: object) -> package_example.Package:
        arguments: dict[str, object] = {
            "root": self.root,
            "build_dir": self.root / "build",
            "example": "rhi_triangle",
            "version": "0.1.0-alpha.5",
            "out_dir": self.root / "dist",
            "feedback_url": "https://example.com/feedback",
            "vc_runtime": self.root / "redist/x64/Microsoft.VC145.CRT",
        }
        arguments.update(overrides)
        return package_example.package(**arguments)  # type: ignore[arg-type]

    def test_zip_holds_the_example_its_dlls_the_runtime_and_the_notices(self) -> None:
        made = self.package()

        self.assertEqual(made.path, self.root / "dist/teserra-0.1.0-alpha.5-windows-x64-rhi_triangle.zip")
        with zipfile.ZipFile(made.path) as archive:
            self.assertEqual(archive.namelist(), [
                f"teserra-0.1.0-alpha.5-windows-x64-rhi_triangle/{name}" for name in [
                    "teserra_rhi_triangle.exe", "glfw3.dll", "spdlog.dll", "msvcp140.dll", "vcruntime140.dll",
                    "vcruntime140_1.dll", "LICENSE", "THIRD_PARTY_NOTICES.md", "README.txt",
                ]
            ])
            readme = archive.read("teserra-0.1.0-alpha.5-windows-x64-rhi_triangle/README.txt").decode("utf-8")
        title = "Teserra 0.1.0-alpha.5 - rhi_triangle"
        self.assertEqual(readme.splitlines()[:2], [title, "=" * len(title)])
        self.assertIn("a triangle in the middle", readme)
        self.assertIn("Run teserra_rhi_triangle.exe (rhi_triangle 0.1.0-alpha.5).", readme)
        self.assertIn("Feedback: https://example.com/feedback", readme)
        self.assertIn("\r\n", readme)
        self.assertNotIn("\r\r", readme)

    def test_the_runtime_is_found_through_the_developer_environment(self) -> None:
        with mock.patch.dict(os.environ, {"VCToolsRedistDir": str(self.root / "redist")}):
            made = self.package(vc_runtime=None)
        self.assertIn("vcruntime140_1.dll", made.files)

    def test_a_missing_runtime_is_an_error(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(package_example.PackageError, "pass --vc-runtime"):
                self.package(vc_runtime=None)
        with self.assertRaisesRegex(package_example.PackageError, "msvcp140.dll"):
            self.package(vc_runtime=self.root / "build")

    def test_only_release_builds_are_packaged(self) -> None:
        self.write("build/CMakeCache.txt", "CMAKE_BUILD_TYPE:STRING=Debug\n")
        with self.assertRaisesRegex(package_example.PackageError, "'Debug' build"):
            self.package()

    def test_bad_arguments_are_errors(self) -> None:
        with self.assertRaisesRegex(package_example.PackageError, "not a version"):
            self.package(version="alpha")
        with self.assertRaisesRegex(package_example.PackageError, "not an example name"):
            self.package(example="../rhi_triangle")
        with self.assertRaisesRegex(package_example.PackageError, "build the example first"):
            self.package(example="rhi_clear")
        with self.assertRaisesRegex(package_example.PackageError, "not a CMake build directory"):
            self.package(build_dir=self.root / "missing")

    def test_main_reports_errors_and_the_package(self) -> None:
        arguments = ["--root", str(self.root), "--build-dir", str(self.root / "build"), "--example", "rhi_triangle",
                     "--out", str(self.root / "dist"), "--vc-runtime", str(self.root / "redist/x64/Microsoft.VC145.CRT")]
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(package_example.main(arguments + ["--version", "0.1.0"]), 0)
        self.assertIn("teserra-0.1.0-windows-x64-rhi_triangle.zip (9 files)", output.getvalue())

        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            self.assertEqual(package_example.main(arguments + ["--version", "x"]), 1)
        self.assertIn("package_example: 'x' is not a version", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
