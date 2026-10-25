/**
 * Translated-shader cache (URinth patch).
 *
 * Every GLSL shader the game hands to LTW goes through a heavy translation (glsl_optimizer plus
 * the shader converter) before the phone's driver sees it. The result only depends on the shader
 * text, its type and the target GLSL version, so it can be remembered: in memory for shaders that
 * are compiled again during the same run (resource reloads, shader packs), and on disk so the next
 * launch does not translate the same shaders again.
 *
 * Set LTW_SHADER_CACHE_DIR to a writable directory to turn on the disk part.
 * Set LTW_SHADER_CACHE=0 to turn the whole cache off.
 *
 * Everything here is independent of OpenGL headers, so it can be tested on a normal computer.
 * For use under LGPL-3.0
 */
#ifndef LTW_SHADER_CACHE_H
#define LTW_SHADER_CACHE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Look up a translated shader.
 * @return a malloc'ed, NUL-terminated copy the caller must free, or NULL on a miss.
 */
char* ltw_shader_cache_lookup(unsigned int shader_type, int target_version, const char* source, size_t source_length);

/** Remember the translation of a shader. Both strings are copied. */
void ltw_shader_cache_store(unsigned int shader_type, int target_version, const char* source, size_t source_length, const char* translated);

/** Add the time spent on one real translation (milliseconds) to the statistics. */
void ltw_shader_cache_note_translation(double milliseconds);

/** Print one line with the cache statistics to stdout every `every` lookups. */
void ltw_shader_cache_report(unsigned int every);

/** Test hook: forget the in-memory table and re-read the environment. */
void ltw_shader_cache_reset_for_tests(void);

#ifdef __cplusplus
}
#endif

#endif // LTW_SHADER_CACHE_H
