from __future__ import annotations

import contextlib
import io
import unittest

from script_test_support import TreeTestCase

import check_layers


class CheckLayersTest(TreeTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.write("engine/core/CMakeLists.txt", "teserra_add_module(core LAYER 0)\n")
        self.write("engine/core/include/teserra/core/types.hpp", "#pragma once\n")
        self.write("engine/platform/CMakeLists.txt", """
            # teserra_add_module(platform LAYER 1 DEPENDS rhi) is only a comment
            teserra_add_module(platform LAYER 1
                DEPENDS core  # the only dependency
                PUBLIC_HEADERS include/teserra/platform/window.hpp
                SOURCES src/window.cpp)
            """)
        self.write("engine/rhi/CMakeLists.txt", "teserra_add_module(rhi LAYER 2 DEPENDS platform)\n")

    def violations(self) -> list[str]:
        found, _, _ = check_layers.check(self.root)
        return [str(violation) for violation in found]

    def test_own_headers_and_direct_dependencies_are_allowed(self) -> None:
        self.write("engine/platform/src/window.cpp", """
            #include <teserra/platform/window.hpp>
            #include "teserra/core/types.hpp"
            #include <vector>
            """)
        self.assertEqual(self.violations(), [])

    def test_transitive_dependencies_must_be_declared(self) -> None:
        self.write("engine/rhi/include/teserra/rhi/device.hpp", """
            #pragma once
            #include <teserra/platform/window.hpp>
            #include <teserra/core/types.hpp>
            """)
        self.assertEqual(self.violations(), [
            "engine/rhi/include/teserra/rhi/device.hpp:3:11: error: module 'rhi' includes "
            "<teserra/core/types.hpp> but does not depend on 'core'; add it to DEPENDS in "
            "engine/rhi/CMakeLists.txt [layer-include]",
        ])

    def test_higher_layers_are_rejected(self) -> None:
        self.write("engine/core/src/log.cpp", "#include <teserra/platform/window.hpp>\n")
        self.assertEqual(len(self.violations()), 1)
        self.assertIn("module 'core' includes <teserra/platform/window.hpp>", self.violations()[0])

    def test_unknown_modules_are_rejected(self) -> None:
        self.write("engine/core/src/log.cpp", "#include <teserra/renderer/mesh.hpp>\n")
        self.assertEqual(self.violations(), [
            "engine/core/src/log.cpp:1:11: error: <teserra/renderer/mesh.hpp> belongs to no module: "
            "no teserra_add_module(renderer ...) exists [layer-include]",
        ])

    def test_test_support_only_from_tests(self) -> None:
        self.write("engine/core/tests/log_test.cpp", "#include <teserra/test/log_capture.hpp>\n")
        self.assertEqual(self.violations(), [])
        self.write("engine/core/src/log.cpp", "#include <teserra/test/log_capture.hpp>\n")
        self.assertEqual(len(self.violations()), 1)
        self.assertIn("is test support; only engine/core/tests/ may include it", self.violations()[0])

    def test_tests_follow_the_rules_of_their_module(self) -> None:
        self.write("engine/platform/tests/window_test.cpp", "#include <teserra/rhi/device.hpp>\n")
        self.assertEqual(len(self.violations()), 1)

    def test_a_nested_module_owns_its_directory(self) -> None:
        self.write("engine/rhi/backends/opengl/CMakeLists.txt",
                   'teserra_add_module(rhi_opengl LAYER 3 DEPENDS rhi "platform")\n')
        self.write("engine/rhi/backends/opengl/src/gl_device.cpp", """
            #include <teserra/rhi/device.hpp>
            #include <teserra/platform/window.hpp>
            #include <teserra/core/types.hpp>
            """)
        self.assertEqual([violation.split(": error: ")[1] for violation in self.violations()], [
            "module 'rhi_opengl' includes <teserra/core/types.hpp> but does not depend on 'core'; "
            "add it to DEPENDS in engine/rhi/backends/opengl/CMakeLists.txt [layer-include]",
        ])

    def test_commented_out_includes_are_ignored(self) -> None:
        self.write("engine/core/src/log.cpp", """
            // #include <teserra/platform/window.hpp>
            /* #include <teserra/rhi/device.hpp> */
            """)
        self.assertEqual(self.violations(), [])

    def test_main_reports_and_fails(self) -> None:
        self.write("engine/core/src/log.cpp", "#include <teserra/platform/window.hpp>\n")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            exit_code = check_layers.main(["--root", str(self.root)])
        self.assertEqual(exit_code, 1)
        self.assertIn("engine/core/src/log.cpp:1:11: error:", output.getvalue())

    def test_main_passes_a_clean_tree(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(check_layers.main(["--root", str(self.root)]), 0)


if __name__ == "__main__":
    unittest.main()
