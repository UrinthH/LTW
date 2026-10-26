/**
 * Shader source dump (URinth patch). See shader_dump.h.
 * For use under LGPL-3.0
 */
#include "shader_dump.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DUMPED 4096
#define MAX_BYTES (8u * 1024u * 1024u)

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bool initialised;
static char* path;                 /* NULL: dumping is off */
static FILE* file;
static uint64_t seen[MAX_DUMPED];
static unsigned seen_count;
static size_t bytes_written;

static uint64_t fnv1a(const char* type_name, const char* data, size_t length) {
    uint64_t hash = 1469598103934665603ULL;
    for (const char* p = type_name; *p; p++) { hash ^= (unsigned char)*p; hash *= 1099511628211ULL; }
    hash ^= 0xFF; hash *= 1099511628211ULL;
    for (size_t i = 0; i < length; i++) { hash ^= (unsigned char)data[i]; hash *= 1099511628211ULL; }
    return hash;
}

static void init_locked(void) {
    initialised = true;
    const char* value = getenv("LTW_SHADER_DUMP_FILE");
    if (value == NULL || value[0] == '\0') return;
    path = strdup(value);
}

void ltw_shader_dump(const char* type_name, const char* source, size_t length) {
    if (source == NULL) return;
    pthread_mutex_lock(&lock);
    if (!initialised) init_locked();
    if (path != NULL && seen_count < MAX_DUMPED && bytes_written < MAX_BYTES) {
        uint64_t hash = fnv1a(type_name, source, length);
        bool known = false;
        for (unsigned i = 0; i < seen_count; i++) {
            if (seen[i] == hash) { known = true; break; }
        }
        if (!known) {
            if (file == NULL) file = fopen(path, "w"); /* emptied once per run */
            if (file == NULL) {
                free(path);
                path = NULL; /* cannot write there: stop trying */
            } else {
                seen[seen_count++] = hash;
                int header = fprintf(file, "//// LTW shader type=%s hash=%016" PRIx64 " length=%zu\n", type_name, hash, length);
                size_t written = fwrite(source, 1, length, file);
                fputs("\n\n", file);
                fflush(file);
                if (header > 0) bytes_written += (size_t)header;
                bytes_written += written + 2;
            }
        }
    }
    pthread_mutex_unlock(&lock);
}

void ltw_shader_dump_reset_for_tests(void) {
    pthread_mutex_lock(&lock);
    if (file != NULL) { fclose(file); file = NULL; }
    free(path);
    path = NULL;
    seen_count = 0;
    bytes_written = 0;
    initialised = false;
    pthread_mutex_unlock(&lock);
}
