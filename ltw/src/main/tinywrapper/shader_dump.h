/**
 * Shader source dump (URinth patch).
 *
 * When LTW_SHADER_DUMP_FILE is set, every different shader source the game hands to LTW is written
 * once to that file (the file is emptied at the first write of each run), with its type and a hash
 * of its text. This is for reading what the game's shaders really look like, so that renderer
 * features can be written against the real text instead of guesses. It only copies text; the shader
 * itself is not touched. Without the variable nothing is written.
 *
 * Independent of OpenGL headers, so it can be tested on a normal computer.
 * For use under LGPL-3.0
 */
#ifndef LTW_SHADER_DUMP_H
#define LTW_SHADER_DUMP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Write the source once. `type_name` is a short label such as "vertex" or "fragment". */
void ltw_shader_dump(const char* type_name, const char* source, size_t length);

/** Test hook: forget what was written and re-read the environment. */
void ltw_shader_dump_reset_for_tests(void);

#ifdef __cplusplus
}
#endif

#endif // LTW_SHADER_DUMP_H
