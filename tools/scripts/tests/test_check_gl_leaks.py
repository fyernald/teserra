from __future__ import annotations

import contextlib
import io
import unittest

from script_test_support import TreeTestCase

import check_gl_leaks


class CheckGlLeaksTest(TreeTestCase):
    def violations(self) -> list[str]:
        found, _ = check_gl_leaks.check(self.root)
        return [str(violation) for violation in found]

    def rules(self) -> list[str]:
        found, _ = check_gl_leaks.check(self.root)
        return [violation.rule for violation in found]

    # Acceptance criteria of #22.

    def test_gl_type_in_a_renderer_header_fails(self) -> None:
        self.write("engine/renderer/include/teserra/renderer/mesh.hpp", """
            #pragma once
            struct Mesh {
                GLuint vertexBuffer;
            };
            """)
        self.assertEqual(self.violations(), [
            "engine/renderer/include/teserra/renderer/mesh.hpp:3:5: error: OpenGL name 'GLuint' outside "
            "engine/rhi/backends/opengl/ [gl-leak]",
        ])

    def test_windows_header_outside_windows_platform_files_fails(self) -> None:
        self.write("engine/platform/src/common/input.cpp", "#include <windows.h>\n")
        self.write("engine/renderer/src/renderer.cpp", "#include <Windows.h>\n")
        self.assertEqual(self.rules(), ["win32-leak", "win32-leak"])
        self.assertIn("engine/platform/src/common/input.cpp:1:11: error: Win32 header <windows.h>",
                      self.violations()[0])

    # Where each name may appear.

    def test_gl_is_allowed_in_the_opengl_backend(self) -> None:
        self.write("engine/rhi/backends/opengl/src/gl_device.cpp", """
            #include <glad/gl.h>
            void clear() { glClear(GL_COLOR_BUFFER_BIT); GLuint id = 0; }
            """)
        self.assertEqual(self.violations(), [])

    def test_gl_headers_and_names_are_found(self) -> None:
        self.write("engine/renderer/src/renderer.cpp", """
            #include <glad/gl.h>
            #include "GL/gl.h"
            void f() { glDrawArrays(0, 0, 3); auto e = GLenum{}; int x = GL_TRIANGLES; GLAD_GL_VERSION_4_5; }
            """)
        self.assertEqual(self.rules(), ["gl-leak"] * 6)

    def test_names_that_only_look_like_gl_pass(self) -> None:
        self.write("engine/assets/src/gltf_loader.cpp", """
            // Loads glTF 2.0 files with glClear-free code: "glClear" and GLuint in comments and strings.
            bool glTF = true; int glossary = 0; const char* name = "glCreateBuffers"; float glow = 1.0F;
            OpenGlContext* context = nullptr; auto api = GraphicsApi::OpenGL;
            """)
        self.assertEqual(self.violations(), [])

    def test_vulkan_names_only_in_the_vulkan_backend(self) -> None:
        self.write("engine/renderer/src/renderer.cpp", """
            #include <vulkan/vulkan.h>
            VkImage image; void f() { vkCmdDraw(cmd, 3, 1, 0, 0); }
            """)
        self.write("engine/rhi/backends/vulkan/src/vk_device.cpp", "#include <vulkan/vulkan.h>\nVkImage image;\n")
        self.assertEqual(self.rules(), ["vulkan-leak"] * 3)

    def test_win32_virtual_key_codes_are_not_vulkan(self) -> None:
        self.write("engine/platform/tests/windows/window_events_windows_test.cpp", """
            #include <windows.h>
            int keys[] = {VK_ESCAPE, VK_LEFT};
            """)
        self.assertEqual(self.violations(), [])

    def test_glfw_only_in_the_glfw_backend(self) -> None:
        self.write("engine/platform/src/glfw/glfw_window.cpp", """
            #include <GLFW/glfw3.h>
            void f(GLFWwindow* w) { glfwSwapBuffers(w); int t = GLFW_TRUE; }
            """)
        self.write("engine/platform/src/common/input.cpp", """
            #include <GLFW/glfw3.h>
            // GLFW key codes are translated in src/glfw/ only.
            int key = GLFW_KEY_A; class GlfwWindow;
            """)
        self.assertEqual(self.violations(), [
            "engine/platform/src/common/input.cpp:1:11: error: GLFW header <GLFW/glfw3.h> outside "
            "engine/platform/src/glfw/ [glfw-leak]",
            "engine/platform/src/common/input.cpp:3:11: error: GLFW name 'GLFW_KEY_A' outside "
            "engine/platform/src/glfw/ [glfw-leak]",
        ])

    def test_windows_headers_in_windows_only_platform_files(self) -> None:
        self.write("engine/platform/src/glfw/glfw_native_windows.cpp",
                   "#include <windows.h>\n#include <GLFW/glfw3native.h>\n")
        self.write("engine/platform/src/windows/win32_window.cpp", "#include <windows.h>\n")
        self.write("engine/platform/tests/windows/events_test.cpp", "#include <winuser.h>\n")
        self.write("engine/platform/src/glfw/glfw_native_linux.cpp", "#if __has_include(<windows.h>)\n#endif\n")
        self.assertEqual(self.violations(), [
            "engine/platform/src/glfw/glfw_native_linux.cpp:1:20: error: Win32 header <windows.h> outside "
            "Windows-only platform files (a windows/ directory or a *_windows file under engine/platform/) "
            "[win32-leak]",
        ])

    def test_reviewed_exception_for_core_assert(self) -> None:
        self.write("engine/core/src/assert.cpp", "#include <windows.h>\n")
        self.write("engine/core/src/log.cpp", "#include <windows.h>\n")
        self.assertEqual(len(self.violations()), 1)
        self.assertTrue(self.violations()[0].startswith("engine/core/src/log.cpp:1:11:"))

    def test_glm_only_in_core(self) -> None:
        self.write("engine/core/include/teserra/core/math.hpp", """
            #include <glm/vec3.hpp>
            using Vec3 = glm::vec3;
            #if defined(GLM_FORCE_RADIANS)
            #endif
            """)
        self.write("engine/platform/src/common/input.cpp", """
            #include <glm/vec2.hpp>
            #define GLM_FORCE_LEFT_HANDED
            auto v = glm :: vec2{};
            """)
        self.assertEqual(self.rules(), ["glm-leak"] * 3)

    def test_flat_bindings_only_in_the_shader_blob_header_the_gl_backend_and_shaderc(self) -> None:
        for allowed in ("engine/rhi/include/teserra/rhi/shader_blob.hpp",
                        "engine/rhi/backends/opengl/src/gl_device.cpp",
                        "engine/rhi/backends/opengl/tests/opengl_gpu_test.cpp",
                        "tools/shaderc/src/translator.cpp"):
            self.write(allowed, "unsigned index = rhi::flatBinding(2, 1);\n")
        self.write("engine/renderer/src/material.cpp", """
            // flatBinding() in a comment is fine
            unsigned index = rhi::flatBinding(2, 1);
            """)
        self.assertEqual(self.violations(), [
            "engine/renderer/src/material.cpp:2:23: error: Flat GL binding name 'flatBinding' outside "
            "<teserra/rhi/shader_blob.hpp>, engine/rhi/backends/opengl/ and tools/shaderc/ [flat-binding-leak]",
        ])

    def test_examples_and_tests_are_checked_too(self) -> None:
        self.write("examples/sandbox/main.cpp", "GLuint id;\n")
        self.write("tests/gpu/draw_test.cpp", "void f() { glFinish(); }\n")
        self.assertEqual(self.rules(), ["gl-leak", "gl-leak"])

    def test_generated_and_third_party_code_is_skipped(self) -> None:
        self.write("engine/renderer/generated/gl_tables.hpp", "GLuint id;\n")
        self.write("engine/renderer/third_party/glad/gl.h", "GLuint id;\n")
        self.assertEqual(self.violations(), [])

    # TODO hygiene.

    def test_todo_must_reference_an_issue(self) -> None:
        self.write("engine/core/src/log.cpp", """
            // TODO(#107): Tracy-backed implementations.
            // TODO: no issue
            /* see TODO (#12) */ int todo = 0; const char* s = "TODO";
            """)
        self.write("engine/core/CMakeLists.txt", "# TODO(#5): fine\n# TODO make this faster\n")
        self.assertEqual(self.violations(), [
            "engine/core/CMakeLists.txt:2:3: error: TODO without an issue: write TODO(#123) [todo-issue]",
            "engine/core/src/log.cpp:2:4: error: TODO without an issue: write TODO(#123) [todo-issue]",
            "engine/core/src/log.cpp:3:8: error: TODO without an issue: write TODO(#123) [todo-issue]",
        ])

    # Command line.

    def test_main_reports_and_fails(self) -> None:
        self.write("engine/renderer/src/renderer.cpp", "GLuint id;\n")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            exit_code = check_gl_leaks.main(["--root", str(self.root)])
        self.assertEqual(exit_code, 1)
        self.assertIn("engine/renderer/src/renderer.cpp:1:1: error: OpenGL name 'GLuint'", output.getvalue())

    def test_main_passes_a_clean_tree(self) -> None:
        self.write("engine/core/src/log.cpp", "int main() { return 0; }\n")
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(check_gl_leaks.main(["--root", str(self.root)]), 0)


if __name__ == "__main__":
    unittest.main()
