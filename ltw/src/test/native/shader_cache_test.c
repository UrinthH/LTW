/* Host-side test for the translated-shader cache. Build and run:
 *   gcc -Wall -Wextra -I ltw/src/main/tinywrapper ltw/src/test/native/shader_cache_test.c \
 *       ltw/src/main/tinywrapper/shader_cache.c -lpthread -o /tmp/shader_cache_test && /tmp/shader_cache_test */
#include "shader_cache.h"
#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const char* SRC = "#version 330\nvoid main(){ gl_Position = vec4(1.0); }\n";
static const char* OUT = "#version 300 es\nvoid main(){ gl_Position = vec4(1.0); }\n";

static void clean_env(void) { unsetenv("LTW_SHADER_CACHE_DIR"); unsetenv("LTW_SHADER_CACHE"); ltw_shader_cache_reset_for_tests(); }

static void test_memory_only(void) {
    clean_env();
    CHECK(ltw_shader_cache_lookup(0x8B31, 300, SRC, strlen(SRC)) == NULL);
    ltw_shader_cache_store(0x8B31, 300, SRC, strlen(SRC), OUT);
    char* hit = ltw_shader_cache_lookup(0x8B31, 300, SRC, strlen(SRC));
    CHECK(hit && strcmp(hit, OUT) == 0);
    free(hit);
}

static void test_key_parts_matter(void) {
    clean_env();
    ltw_shader_cache_store(0x8B31, 300, SRC, strlen(SRC), OUT);
    CHECK(ltw_shader_cache_lookup(0x8B30, 300, SRC, strlen(SRC)) == NULL);   /* other shader type */
    CHECK(ltw_shader_cache_lookup(0x8B31, 310, SRC, strlen(SRC)) == NULL);   /* other target version */
    CHECK(ltw_shader_cache_lookup(0x8B31, 300, SRC, strlen(SRC) - 1) == NULL); /* other text */
    char other[256]; snprintf(other, sizeof other, "%s ", SRC);
    CHECK(ltw_shader_cache_lookup(0x8B31, 300, other, strlen(other)) == NULL);
}

static void test_returned_copy_is_independent(void) {
    clean_env();
    ltw_shader_cache_store(0x8B31, 300, SRC, strlen(SRC), OUT);
    char* one = ltw_shader_cache_lookup(0x8B31, 300, SRC, strlen(SRC));
    one[0] = 'X';
    char* two = ltw_shader_cache_lookup(0x8B31, 300, SRC, strlen(SRC));
    CHECK(two && two[0] == '#');
    free(one); free(two);
}

static void test_disk_roundtrip(void) {
    char dir[] = "/tmp/ltwcache_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    clean_env();
    setenv("LTW_SHADER_CACHE_DIR", dir, 1);
    ltw_shader_cache_store(0x8B30, 300, SRC, strlen(SRC), OUT);
    /* simulate the next launch: memory forgotten, disk kept */
    ltw_shader_cache_reset_for_tests();
    setenv("LTW_SHADER_CACHE_DIR", dir, 1);
    char* hit = ltw_shader_cache_lookup(0x8B30, 300, SRC, strlen(SRC));
    CHECK(hit && strcmp(hit, OUT) == 0);
    free(hit);
    /* and the disk hit is remembered in memory */
    char cmd[256]; snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    ltw_shader_cache_reset_for_tests();
    setenv("LTW_SHADER_CACHE_DIR", dir, 1);
    hit = ltw_shader_cache_lookup(0x8B30, 300, SRC, strlen(SRC));
    CHECK(hit != NULL);
    free(hit);
    clean_env();
    CHECK(system(cmd) == 0);
}

static void test_damaged_files_are_misses(void) {
    char dir[] = "/tmp/ltwcache_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    clean_env();
    setenv("LTW_SHADER_CACHE_DIR", dir, 1);
    ltw_shader_cache_store(0x8B30, 300, SRC, strlen(SRC), OUT);
    DIR* d = opendir(dir);
    struct dirent* e; char path[512] = "";
    while ((e = readdir(d))) if (strstr(e->d_name, ".ltwsh")) snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    closedir(d);
    CHECK(path[0] != 0);

    /* truncated */
    FILE* f = fopen(path, "rb"); char buf[512]; size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
    f = fopen(path, "wb"); fwrite(buf, 1, n - 5, f); fclose(f);
    ltw_shader_cache_reset_for_tests(); setenv("LTW_SHADER_CACHE_DIR", dir, 1);
    CHECK(ltw_shader_cache_lookup(0x8B30, 300, SRC, strlen(SRC)) == NULL);

    /* trailing garbage */
    f = fopen(path, "wb"); fwrite(buf, 1, n, f); fputc('!', f); fclose(f);
    ltw_shader_cache_reset_for_tests(); setenv("LTW_SHADER_CACHE_DIR", dir, 1);
    CHECK(ltw_shader_cache_lookup(0x8B30, 300, SRC, strlen(SRC)) == NULL);

    /* wrong header */
    f = fopen(path, "wb"); fputs("not a cache file", f); fclose(f);
    ltw_shader_cache_reset_for_tests(); setenv("LTW_SHADER_CACHE_DIR", dir, 1);
    CHECK(ltw_shader_cache_lookup(0x8B30, 300, SRC, strlen(SRC)) == NULL);

    /* intact again */
    f = fopen(path, "wb"); fwrite(buf, 1, n, f); fclose(f);
    ltw_shader_cache_reset_for_tests(); setenv("LTW_SHADER_CACHE_DIR", dir, 1);
    char* hit = ltw_shader_cache_lookup(0x8B30, 300, SRC, strlen(SRC));
    CHECK(hit && strcmp(hit, OUT) == 0);
    free(hit);
    char cmd[256]; snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    clean_env();
    CHECK(system(cmd) == 0);
}

static void test_disabled_and_bad_dir(void) {
    clean_env();
    setenv("LTW_SHADER_CACHE", "0", 1);
    ltw_shader_cache_store(0x8B31, 300, SRC, strlen(SRC), OUT);
    CHECK(ltw_shader_cache_lookup(0x8B31, 300, SRC, strlen(SRC)) == NULL);
    clean_env();
    setenv("LTW_SHADER_CACHE_DIR", "/proc/definitely/not/writable", 1);
    ltw_shader_cache_store(0x8B31, 300, SRC, strlen(SRC), OUT);   /* must not crash, still cached in memory */
    char* hit = ltw_shader_cache_lookup(0x8B31, 300, SRC, strlen(SRC));
    CHECK(hit != NULL);
    free(hit);
    clean_env();
}

static void test_odd_inputs(void) {
    clean_env();
    CHECK(ltw_shader_cache_lookup(1, 300, NULL, 0) == NULL);
    CHECK(ltw_shader_cache_lookup(1, 300, "", 0) == NULL);
    ltw_shader_cache_store(1, 300, SRC, strlen(SRC), NULL);   /* ignored */
    ltw_shader_cache_store(1, 300, SRC, strlen(SRC), "");     /* ignored */
    CHECK(ltw_shader_cache_lookup(1, 300, SRC, strlen(SRC)) == NULL);
    ltw_shader_cache_note_translation(2.5);
    ltw_shader_cache_report(0);
    ltw_shader_cache_report(1);
}

static void* worker(void* arg) {
    long id = (long)arg;
    for (int i = 0; i < 400; i++) {
        char src[64], out[64];
        int k = (i * 7 + (int)id) % 150;
        snprintf(src, sizeof src, "shader %d", k);
        snprintf(out, sizeof out, "translated %d", k);
        char* hit = ltw_shader_cache_lookup(1, 300, src, strlen(src));
        if (hit) { if (strcmp(hit, out) != 0) failures++; free(hit); }
        else ltw_shader_cache_store(1, 300, src, strlen(src), out);
    }
    return NULL;
}

static void test_many_entries_and_threads(void) {
    clean_env();
    pthread_t t[4];
    for (long i = 0; i < 4; i++) pthread_create(&t[i], NULL, worker, (void*)i);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    for (int k = 0; k < 150; k++) {
        char src[64], out[64];
        snprintf(src, sizeof src, "shader %d", k);
        snprintf(out, sizeof out, "translated %d", k);
        char* hit = ltw_shader_cache_lookup(1, 300, src, strlen(src));
        CHECK(hit && strcmp(hit, out) == 0);
        free(hit);
    }
    clean_env();
}

int main(void) {
    test_memory_only();
    test_key_parts_matter();
    test_returned_copy_is_independent();
    test_disk_roundtrip();
    test_damaged_files_are_misses();
    test_disabled_and_bad_dir();
    test_odd_inputs();
    test_many_entries_and_threads();
    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("shader cache: all tests passed\n");
    return 0;
}
