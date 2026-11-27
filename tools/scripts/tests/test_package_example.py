from __future__ import annotations

import contextlib
import io
import os
import subprocess
import tarfile
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
        self.write("THIRD_PARTY_NOTICES.md", "| [GLFW](https://www.glfw.org/) |\n| [spdlog](x) |\n")
        self.write("tools/packaging/README.txt.in", """
            $title
            $underline
            $description
            Run $executable ($example $version).
            Feedback: $feedback_url
            """)
        self.write("tools/packaging/README-examples.txt.in", """
            $title
            $underline
            $version
            $examples
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

    def test_the_assets_folder_of_an_example_is_packaged(self) -> None:
        self.write("build/examples/rhi_triangle/assets/orientation_f.png", "png")
        self.write("build/examples/rhi_triangle/assets/fonts/mono.ttf", "font")

        made = self.package()

        self.assertEqual(made.files[3:5], ("assets/fonts/mono.ttf", "assets/orientation_f.png"))
        with zipfile.ZipFile(made.path) as archive:
            self.assertEqual(
                archive.read("teserra-0.1.0-alpha.5-windows-x64-rhi_triangle/assets/orientation_f.png"), b"png")

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

    def test_a_dll_without_a_notice_is_an_error(self) -> None:
        self.write("build/examples/rhi_triangle/newlib.dll", "new")
        with self.assertRaisesRegex(package_example.PackageError, "newlib.dll would ship without a notice"):
            self.package()

        (self.root / "build/examples/rhi_triangle/newlib.dll").unlink()
        self.write("build/examples/rhi_triangle/fmt.dll", "fmt")  # known, but not in the fixture's notices
        with self.assertRaisesRegex(package_example.PackageError, "fmt.dll would ship without a notice"):
            self.package()

    def package_examples(self, **overrides: object) -> package_example.Package:
        self.write("build/examples/rhi_clear/teserra_rhi_clear.exe", "exe")
        self.write("build/examples/rhi_clear/glfw3.dll", "glfw")
        self.write("build/examples/rhi_clear/assets/clear.png", "png")
        arguments: dict[str, object] = {
            "root": self.root,
            "build_dir": self.root / "build",
            "version": "0.1.0-rc.1",
            "out_dir": self.root / "dist",
            "feedback_url": "https://example.com/feedback",
            "vc_runtime": self.root / "redist/x64/Microsoft.VC145.CRT",
            "examples": ("rhi_triangle", "rhi_clear"),
        }
        arguments.update(overrides)
        return package_example.package_examples(**arguments)  # type: ignore[arg-type]

    def test_the_examples_zip_holds_a_folder_per_example_and_the_notices_once(self) -> None:
        made = self.package_examples()

        self.assertEqual(made.path, self.root / "dist/teserra-0.1.0-rc.1-windows-x64-examples.zip")
        runtime = list(package_example.VC_RUNTIME_DLLS)
        self.assertEqual(list(made.files), [
            "README.txt", "LICENSE", "THIRD_PARTY_NOTICES.md",
            *[f"rhi_triangle/{name}" for name in ["teserra_rhi_triangle.exe", "glfw3.dll", "spdlog.dll", *runtime]],
            *[f"rhi_clear/{name}" for name in ["teserra_rhi_clear.exe", "glfw3.dll", "assets/clear.png", *runtime]],
        ])
        with zipfile.ZipFile(made.path) as archive:
            self.assertEqual(archive.namelist()[0], "teserra-0.1.0-rc.1-windows-x64-examples/README.txt")
            readme = archive.read("teserra-0.1.0-rc.1-windows-x64-examples/README.txt").decode("utf-8")
        title = "Teserra 0.1.0-rc.1 - examples"
        self.assertEqual(readme.splitlines()[:3], [title, "=" * len(title), "0.1.0-rc.1"])
        self.assertIn("rhi_triangle\\teserra_rhi_triangle.exe\r\n", readme)
        self.assertIn("a triangle in the middle", readme)
        self.assertIn("rhi_clear\\teserra_rhi_clear.exe\r\n", readme)
        self.assertIn("Feedback: https://example.com/feedback", readme)

    def test_the_examples_zip_needs_every_example(self) -> None:
        with self.assertRaisesRegex(package_example.PackageError, "teserra_sprite_stress.exe does not exist"):
            self.package_examples(examples=("rhi_triangle", "sprite_stress"))
        self.write("build/CMakeCache.txt", "CMAKE_BUILD_TYPE:STRING=Debug\n")
        with self.assertRaisesRegex(package_example.PackageError, "'Debug' build"):
            self.package_examples()

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

    def test_main_makes_the_examples_zip(self) -> None:
        arguments = ["--root", str(self.root), "--build-dir", str(self.root / "build"), "--all-examples",
                     "--version", "0.1.0", "--out", str(self.root / "dist"),
                     "--vc-runtime", str(self.root / "redist/x64/Microsoft.VC145.CRT")]
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            self.assertEqual(package_example.main(arguments), 1)
        # The fixture builds only rhi_triangle, not the first packaged example.
        self.assertIn(f"teserra_{package_example.PACKAGED_EXAMPLES[0]}.exe does not exist", errors.getvalue())

        for name in package_example.PACKAGED_EXAMPLES:
            self.write(f"build/examples/{name}/teserra_{name}.exe", "exe")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(package_example.main(arguments), 0)
        self.assertIn("teserra-0.1.0-windows-x64-examples.zip", output.getvalue())

        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            package_example.main(arguments + ["--example", "rhi_triangle"])  # one or the other


class LinuxPackageTest(TreeTestCase):
    """--platform linux-x64 (#317): a .tar.gz with mode 755 executables, no libraries, and the ldd check."""

    SYSTEM = ["linux-vdso.so.1", "libstdc++.so.6", "libm.so.6", "libgcc_s.so.1", "libc.so.6",
              "ld-linux-x86-64.so.2"]

    def setUp(self) -> None:
        super().setUp()
        self.write("LICENSE", "MIT License\n")
        self.write("THIRD_PARTY_NOTICES.md", "| [GLFW](https://www.glfw.org/) |\n")
        self.write("tools/packaging/README-linux.txt.in", """
            $title
            $underline
            $description
            Run ./$executable ($example $version).
            Feedback: $feedback_url
            """)
        self.write("tools/packaging/README-examples-linux.txt.in", """
            $title
            $underline
            $examples
            Feedback: $feedback_url
            """)
        self.write("build/CMakeCache.txt", "CMAKE_BUILD_TYPE:STRING=Release\n")
        for example in ("rhi_triangle", "rhi_clear"):
            self.write(f"build/examples/{example}/teserra_{example}", "elf")
            self.write(f"build/examples/{example}/teserra.log", "a smoke run's log")
        self.write("build/examples/rhi_clear/assets/clear.png", "png")

    def arguments(self, **overrides: object) -> dict[str, object]:
        arguments: dict[str, object] = {
            "root": self.root,
            "build_dir": self.root / "build",
            "version": "0.2.0",
            "out_dir": self.root / "dist",
            "feedback_url": "https://example.com/feedback",
            "platform": package_example.LINUX,
            "ldd": lambda executable: self.SYSTEM,
        }
        arguments.update(overrides)
        return arguments

    def test_the_examples_archive_keeps_the_executable_bit_and_needs_no_runtime(self) -> None:
        made = package_example.package_examples(
            **self.arguments(examples=("rhi_triangle", "rhi_clear")))  # type: ignore[arg-type]

        self.assertEqual(made.path, self.root / "dist/teserra-0.2.0-linux-x64-examples.tar.gz")
        prefix = "teserra-0.2.0-linux-x64-examples"
        with tarfile.open(made.path) as archive:
            members = {member.name: member for member in archive.getmembers()}
            self.assertEqual(list(members), [f"{prefix}/{name}" for name in [
                "README.txt", "LICENSE", "THIRD_PARTY_NOTICES.md", "rhi_triangle/teserra_rhi_triangle",
                "rhi_clear/teserra_rhi_clear", "rhi_clear/assets/clear.png",
            ]])
            self.assertEqual(members[f"{prefix}/rhi_triangle/teserra_rhi_triangle"].mode, 0o755)
            self.assertEqual(members[f"{prefix}/rhi_clear/assets/clear.png"].mode, 0o644)
            self.assertEqual(members[f"{prefix}/README.txt"].mode, 0o644)
            self.assertEqual(members[f"{prefix}/LICENSE"].uid, 0)
            readme_file = archive.extractfile(f"{prefix}/README.txt")
            assert readme_file is not None
            readme = readme_file.read().decode("utf-8")
            clear = archive.extractfile(f"{prefix}/rhi_clear/assets/clear.png")
            assert clear is not None
            self.assertEqual(clear.read(), b"png")
        self.assertIn("rhi_triangle/teserra_rhi_triangle\n", readme)
        self.assertNotIn("\r", readme)

    def test_one_example_in_its_own_archive(self) -> None:
        made = package_example.package(**self.arguments(example="rhi_triangle"))  # type: ignore[arg-type]

        self.assertEqual(made.path, self.root / "dist/teserra-0.2.0-linux-x64-rhi_triangle.tar.gz")
        self.assertEqual(made.files, ("teserra_rhi_triangle", "LICENSE", "THIRD_PARTY_NOTICES.md", "README.txt"))
        with tarfile.open(made.path) as archive:
            readme_file = archive.extractfile("teserra-0.2.0-linux-x64-rhi_triangle/README.txt")
            assert readme_file is not None
            self.assertIn("Run ./teserra_rhi_triangle (rhi_triangle 0.2.0).", readme_file.read().decode("utf-8"))

    def test_a_library_outside_the_system_is_an_error(self) -> None:
        with self.assertRaisesRegex(package_example.PackageError, "links libX11.so.6, which a Linux desktop"):
            package_example.package(**self.arguments(  # type: ignore[arg-type]
                example="rhi_triangle", ldd=lambda executable: [*self.SYSTEM, "libX11.so.6"]))

    def test_a_shared_library_next_to_an_example_is_an_error(self) -> None:
        self.write("build/examples/rhi_triangle/libglfw.so.3", "so")
        with self.assertRaisesRegex(package_example.PackageError, "libglfw.so.3"):
            package_example.package(**self.arguments(example="rhi_triangle"))  # type: ignore[arg-type]

    def test_linked_libraries_reads_ldd(self) -> None:
        output = ("\tlinux-vdso.so.1 (0x00007ffd)\n"
                  "\tlibstdc++.so.6 => /lib/x86_64-linux-gnu/libstdc++.so.6 (0x00007f12)\n"
                  "\t/lib64/ld-linux-x86-64.so.2 (0x00007f34)\n")
        completed = subprocess.CompletedProcess(["ldd"], 0, stdout=output, stderr="")
        with mock.patch.object(package_example.subprocess, "run", return_value=completed):
            self.assertEqual(package_example.linked_libraries(Path("teserra_hello")),
                             ["linux-vdso.so.1", "libstdc++.so.6", "ld-linux-x86-64.so.2"])
        with mock.patch.object(package_example.subprocess, "run", side_effect=OSError("no ldd")):
            with self.assertRaisesRegex(package_example.PackageError, "ldd"):
                package_example.linked_libraries(Path("teserra_hello"))

    def test_main_packages_for_linux(self) -> None:
        stdout = io.StringIO()
        for example in package_example.PACKAGED_EXAMPLES:
            self.write(f"build/examples/{example}/teserra_{example}", "elf")
        with mock.patch.object(package_example, "linked_libraries", return_value=self.SYSTEM), \
                contextlib.redirect_stdout(stdout):
            status = package_example.main([
                "--platform", "linux-x64", "--build-dir", str(self.root / "build"), "--all-examples",
                "--version", "0.2.0", "--out", str(self.root / "dist"), "--root", str(self.root)])
        self.assertEqual(status, 0, stdout.getvalue())
        self.assertTrue((self.root / "dist/teserra-0.2.0-linux-x64-examples.tar.gz").is_file())


class PackagedExamplesTest(unittest.TestCase):
    """The lists in package_example.py against this repository."""

    root = Path(__file__).resolve().parents[3]

    def test_every_example_is_packaged_or_a_console_example(self) -> None:
        examples = {path.name for path in (self.root / "examples").iterdir() if (path / "main.cpp").is_file()}
        packaged = set(package_example.PACKAGED_EXAMPLES)
        console = set(package_example.CONSOLE_EXAMPLES)
        self.assertEqual(examples, packaged | console, "add the example to PACKAGED_EXAMPLES or CONSOLE_EXAMPLES")
        self.assertFalse(packaged & console)

    def test_every_packaged_example_has_a_description(self) -> None:
        for example in package_example.PACKAGED_EXAMPLES:
            self.assertIn(example, package_example.DESCRIPTIONS)

    def test_every_noticed_dll_is_in_the_notices(self) -> None:
        notices = (self.root / package_example.NOTICES).read_text(encoding="utf-8")
        for dll, component in package_example.NOTICED_DLLS.items():
            self.assertIn(f"[{component}]", notices, dll)


if __name__ == "__main__":
    unittest.main()
