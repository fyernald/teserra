from __future__ import annotations

import unittest

from script_test_support import TreeTestCase

from teserra_sources import LineIndex, Violation, lex_cpp, list_files


class LexCppTest(unittest.TestCase):
    def test_blanks_comments_and_keeps_their_text(self) -> None:
        lexed = lex_cpp("int a; // glClear\n/* GLuint\n x */ int b;\n")
        blanked = "int a; " + " " * len("// glClear") + "\n" + " " * len("/* GLuint") + "\n" + " " * len(" x */")
        self.assertEqual(lexed.code, blanked + " int b;\n")
        self.assertEqual([comment.text for comment in lexed.comments], ["// glClear", "/* GLuint\n x */"])

    def test_blanks_string_and_character_literals(self) -> None:
        lexed = lex_cpp('f("glClear", u8"GLFW\\"x", L\'g\', \'\\\'\');')
        self.assertNotIn("gl", lexed.code)
        self.assertNotIn("GLFW", lexed.code)
        self.assertTrue(lexed.code.startswith("f("))
        self.assertTrue(lexed.code.endswith(");"))

    def test_blanks_raw_strings(self) -> None:
        lexed = lex_cpp('auto s = R"x(glClear)" still ")x"; GLuint id;')
        self.assertNotIn("glClear", lexed.code)
        self.assertIn("GLuint id;", lexed.code)

    def test_digit_separators_are_not_character_literals(self) -> None:
        lexed = lex_cpp("auto n = 16'666'667; GLuint id; auto h = 0xFF'FF; auto e = 1.5e+3'0;")
        self.assertIn("GLuint id;", lexed.code)
        self.assertIn("16'666'667", lexed.code)

    def test_finds_include_directives_and_has_include(self) -> None:
        lexed = lex_cpp(
            '#include <teserra/core/log.hpp>\n'
            '  #  include "glad/gl.h"\n'
            '#if __has_include(<windows.h>)\n'
            '#endif\n'
            '// #include <GLFW/glfw3.h>\n'
            'auto s = "#include <vulkan/vulkan.h>";\n'
        )
        self.assertEqual([include.header for include in lexed.includes],
                         ["teserra/core/log.hpp", "glad/gl.h", "windows.h"])

    def test_include_offset_points_at_the_header_name(self) -> None:
        text = "int a;\n#include <GL/gl.h>\n"
        lexed = lex_cpp(text)
        self.assertEqual(LineIndex(text).position(lexed.includes[0].offset), (2, 11))

    def test_a_hash_inside_a_line_is_not_a_directive(self) -> None:
        self.assertEqual(lex_cpp("#define STR(x) #x\nSTR(include <GL/gl.h>)\n").includes, [])


class LineIndexTest(unittest.TestCase):
    def test_positions_are_one_based(self) -> None:
        index = LineIndex("ab\ncd\n\nx")
        self.assertEqual(index.position(0), (1, 1))
        self.assertEqual(index.position(1), (1, 2))
        self.assertEqual(index.position(3), (2, 1))
        self.assertEqual(index.position(7), (4, 1))


class ListFilesTest(TreeTestCase):
    def test_skips_third_party_and_generated_code(self) -> None:
        self.write("engine/core/src/a.cpp", "")
        self.write("engine/rhi/backends/opengl/third_party/glad/gl.c", "")
        self.write("engine/core/generated/include/version.hpp", "")
        self.write("docs/readme.md", "")
        self.assertEqual([path.as_posix() for path in list_files(self.root)], ["engine/core/src/a.cpp"])


class ViolationTest(unittest.TestCase):
    def test_formats_for_the_problem_matcher(self) -> None:
        self.assertEqual(str(Violation("a/b.cpp", 3, 7, "gl-leak", "OpenGL name")),
                         "a/b.cpp:3:7: error: OpenGL name [gl-leak]")


if __name__ == "__main__":
    unittest.main()
