/* Host-side test for the shader source dump. Build and run:
 *   gcc -Wall -Wextra -I ltw/src/main/tinywrapper ltw/src/test/native/shader_dump_test.c \
 *       ltw/src/main/tinywrapper/shader_dump.c -lpthread -o /tmp/shader_dump_test && /tmp/shader_dump_test */
#include "shader_dump.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static char* read_all(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = malloc((size_t)n + 1);
    size_t got = fread(data, 1, (size_t)n, f);
    data[got] = 0;
    fclose(f);
    if (size) *size = got;
    return data;
}

static int count_of(const char* haystack, const char* needle) {
    int n = 0;
    for (const char* p = haystack; (p = strstr(p, needle)) != NULL; p += strlen(needle)) n++;
    return n;
}

int main(void) {
    char path[] = "/tmp/ltw_shader_dump_test_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    const char* vs = "#version 330\nvoid main(){ gl_Position = vec4(1.0); }\n";
    const char* fs = "#version 330\nout vec4 c;\nvoid main(){ c = vec4(1.0); }\n";

    /* Off by default: nothing is written and the file is left alone. */
    unsetenv("LTW_SHADER_DUMP_FILE");
    ltw_shader_dump_reset_for_tests();
    FILE* f = fopen(path, "w"); fputs("keep", f); fclose(f);
    ltw_shader_dump("vertex", vs, strlen(vs));
    char* kept = read_all(path, NULL);
    CHECK(kept && strcmp(kept, "keep") == 0);
    free(kept);

    /* On: each different source once, same text twice is one entry, type matters. */
    setenv("LTW_SHADER_DUMP_FILE", path, 1);
    ltw_shader_dump_reset_for_tests();
    ltw_shader_dump("vertex", vs, strlen(vs));
    ltw_shader_dump("vertex", vs, strlen(vs));
    ltw_shader_dump("fragment", fs, strlen(fs));
    ltw_shader_dump("fragment", vs, strlen(vs)); /* same text, other type: separate entry */
    size_t size = 0;
    char* text = read_all(path, &size);
    CHECK(text != NULL);
    CHECK(count_of(text, "//// LTW shader type=") == 3);
    CHECK(count_of(text, "type=vertex") == 1);
    CHECK(count_of(text, "type=fragment") == 2);
    CHECK(strstr(text, "gl_Position = vec4(1.0);") != NULL);
    free(text);

    /* A new run empties the file first. */
    ltw_shader_dump_reset_for_tests();
    ltw_shader_dump("vertex", fs, strlen(fs));
    text = read_all(path, NULL);
    CHECK(text && count_of(text, "//// LTW shader type=") == 1);
    free(text);

    /* An unwritable path just turns dumping off. */
    setenv("LTW_SHADER_DUMP_FILE", "/nonexistent-dir/x/y.txt", 1);
    ltw_shader_dump_reset_for_tests();
    ltw_shader_dump("vertex", vs, strlen(vs));
    ltw_shader_dump("vertex", fs, strlen(fs));

    ltw_shader_dump("vertex", NULL, 0); /* must not crash */
    ltw_shader_dump_reset_for_tests();
    unlink(path);
    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("shader_dump: all checks passed\n");
    return 0;
}
