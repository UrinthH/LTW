/**
 * Translated-shader cache (URinth patch). See shader_cache.h for what it is for.
 *
 * Layout on disk: one small file per shader, named after a 128-bit hash of everything the
 * translation depends on (translator build, shader type, target GLSL version, source text).
 * File content: "LTWC2\n", the payload length in decimal and a newline, then the payload.
 * A file that is missing, cut short or does not start with the header is simply a miss.
 * Files are written under a temporary name and renamed, so a game killed halfway through a
 * write never leaves a half-written shader behind.
 *
 * For use under LGPL-3.0
 */
#include "shader_cache.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef LTW_CACHE_SALT
/* Every build of the library gets its own cache, so a changed translator never reuses old output. */
#define LTW_CACHE_SALT __DATE__ " " __TIME__
#endif

#define CACHE_MAGIC "LTWC2\n"
#define CACHE_MAGIC_LEN 6
#define MAX_PAYLOAD_BYTES (4u * 1024u * 1024u)
#define MAX_DISK_ENTRIES 6000
#define FILE_SUFFIX ".ltwsh"

typedef struct {
    uint64_t a, b;       /* key; a == 0 && b == 0 marks an empty slot */
    char* text;
    size_t length;
} entry_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_ready;
static bool g_enabled = true;
static char g_dir[1024];
static bool g_dir_usable;
static long g_disk_entries = -1;

static entry_t* g_table;
static size_t g_capacity;
static size_t g_count;

static unsigned long g_lookups, g_memory_hits, g_disk_hits, g_stores;
static unsigned long g_translations;
static double g_translation_ms;
static double g_saved_ms_estimate;

/* ---- hashing ---------------------------------------------------------------------------- */

static uint64_t mix64(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static void hash_bytes(uint64_t* h1, uint64_t* h2, const void* data, size_t length) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t a = *h1, b = *h2;
    for (size_t i = 0; i < length; i++) {
        a ^= p[i]; a *= 0x100000001b3ULL;
        b = (b ^ p[i]) * 0x9e3779b97f4a7c15ULL; b ^= b >> 29;
    }
    *h1 = a; *h2 = b;
}

static void make_key(unsigned int type, int version, const char* source, size_t length, uint64_t* ka, uint64_t* kb) {
    uint64_t a = 0xcbf29ce484222325ULL, b = 0x84222325cbf29ce4ULL;
    const char salt[] = LTW_CACHE_SALT;
    uint32_t t = type;
    int32_t v = version;
    uint64_t len = length;
    hash_bytes(&a, &b, salt, sizeof(salt));
    hash_bytes(&a, &b, &t, sizeof(t));
    hash_bytes(&a, &b, &v, sizeof(v));
    hash_bytes(&a, &b, &len, sizeof(len));
    hash_bytes(&a, &b, source, length);
    a = mix64(a); b = mix64(b ^ a);
    if (a == 0 && b == 0) a = 1;
    *ka = a; *kb = b;
}

/* ---- in-memory table (caller holds the lock) --------------------------------------------- */

static entry_t* table_find(uint64_t a, uint64_t b) {
    if (g_capacity == 0) return NULL;
    size_t mask = g_capacity - 1;
    size_t i = (size_t)a & mask;
    while (g_table[i].a != 0 || g_table[i].b != 0) {
        if (g_table[i].a == a && g_table[i].b == b) return &g_table[i];
        i = (i + 1) & mask;
    }
    return NULL;
}

static bool table_grow(void) {
    size_t new_capacity = g_capacity == 0 ? 256 : g_capacity * 2;
    entry_t* fresh = calloc(new_capacity, sizeof(entry_t));
    if (!fresh) return false;
    for (size_t i = 0; i < g_capacity; i++) {
        if (g_table[i].a == 0 && g_table[i].b == 0) continue;
        size_t j = (size_t)g_table[i].a & (new_capacity - 1);
        while (fresh[j].a != 0 || fresh[j].b != 0) j = (j + 1) & (new_capacity - 1);
        fresh[j] = g_table[i];
    }
    free(g_table);
    g_table = fresh;
    g_capacity = new_capacity;
    return true;
}

/** Takes ownership of text. */
static void table_put(uint64_t a, uint64_t b, char* text, size_t length) {
    entry_t* existing = table_find(a, b);
    if (existing) {
        free(existing->text);
        existing->text = text;
        existing->length = length;
        return;
    }
    if ((g_count + 1) * 2 > g_capacity && !table_grow()) {
        free(text);
        return;
    }
    size_t mask = g_capacity - 1;
    size_t i = (size_t)a & mask;
    while (g_table[i].a != 0 || g_table[i].b != 0) i = (i + 1) & mask;
    g_table[i].a = a; g_table[i].b = b; g_table[i].text = text; g_table[i].length = length;
    g_count++;
}

static void table_clear(void) {
    for (size_t i = 0; i < g_capacity; i++) free(g_table[i].text);
    free(g_table);
    g_table = NULL;
    g_capacity = g_count = 0;
}

/* ---- disk -------------------------------------------------------------------------------- */

static void mkdir_p(const char* path) {
    char tmp[sizeof(g_dir)];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(tmp)) return;
    memcpy(tmp, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (tmp[i] != '/') continue;
        tmp[i] = 0;
        mkdir(tmp, 0755);
        tmp[i] = '/';
    }
    mkdir(tmp, 0755);
}

static void init_locked(void) {
    if (g_ready) return;
    g_ready = true;
    const char* on = getenv("LTW_SHADER_CACHE");
    g_enabled = !(on != NULL && on[0] == '0');
    const char* dir = getenv("LTW_SHADER_CACHE_DIR");
    g_dir_usable = false;
    g_dir[0] = 0;
    if (g_enabled && dir != NULL && dir[0] != 0 && strlen(dir) < sizeof(g_dir) - 64) {
        strcpy(g_dir, dir);
        size_t n = strlen(g_dir);
        while (n > 1 && g_dir[n - 1] == '/') g_dir[--n] = 0;
        mkdir_p(g_dir);
        g_dir_usable = access(g_dir, W_OK) == 0;
    }
}

static void file_name(char* out, size_t out_size, uint64_t a, uint64_t b) {
    snprintf(out, out_size, "%s/%016llx%016llx" FILE_SUFFIX, g_dir,
             (unsigned long long)a, (unsigned long long)b);
}

static char* read_entry_file(const char* path, size_t* length_out) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    char header[CACHE_MAGIC_LEN + 24];
    size_t got = fread(header, 1, sizeof(header) - 1, f);
    header[got] = 0;
    if (got < CACHE_MAGIC_LEN + 2 || memcmp(header, CACHE_MAGIC, CACHE_MAGIC_LEN) != 0) {
        fclose(f);
        return NULL;
    }
    char* end = NULL;
    unsigned long long payload = strtoull(header + CACHE_MAGIC_LEN, &end, 10);
    if (end == header + CACHE_MAGIC_LEN || *end != '\n' || payload == 0 || payload > MAX_PAYLOAD_BYTES) {
        fclose(f);
        return NULL;
    }
    long data_start = (long)((end + 1) - header);
    if (fseek(f, data_start, SEEK_SET) != 0) { fclose(f); return NULL; }
    char* text = malloc((size_t)payload + 1);
    if (!text) { fclose(f); return NULL; }
    size_t read = fread(text, 1, (size_t)payload, f);
    /* A complete file ends exactly here. */
    int extra = fgetc(f);
    fclose(f);
    if (read != (size_t)payload || extra != EOF) {
        free(text);
        return NULL;
    }
    text[payload] = 0;
    *length_out = (size_t)payload;
    return text;
}

static long count_disk_entries(void) {
    DIR* d = opendir(g_dir);
    if (!d) return 0;
    long n = 0;
    struct dirent* e;
    size_t suffix = strlen(FILE_SUFFIX);
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (len > suffix && strcmp(e->d_name + len - suffix, FILE_SUFFIX) == 0) n++;
    }
    closedir(d);
    return n;
}

static void write_entry_file(uint64_t a, uint64_t b, const char* text, size_t length) {
    static unsigned long counter;
    char final_path[sizeof(g_dir) + 64];
    char temp_path[sizeof(g_dir) + 96];
    file_name(final_path, sizeof(final_path), a, b);
    snprintf(temp_path, sizeof(temp_path), "%s.%ld.%lu.tmp", final_path, (long)getpid(), counter++);
    FILE* f = fopen(temp_path, "wb");
    if (!f) return;
    bool ok = fprintf(f, "%s%zu\n", CACHE_MAGIC, length) > 0 && fwrite(text, 1, length, f) == length;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(temp_path, final_path) != 0) unlink(temp_path);
}

/* ---- public API -------------------------------------------------------------------------- */

char* ltw_shader_cache_lookup(unsigned int shader_type, int target_version, const char* source, size_t source_length) {
    if (!source || source_length == 0) return NULL;
    uint64_t a, b;
    make_key(shader_type, target_version, source, source_length, &a, &b);

    pthread_mutex_lock(&g_lock);
    init_locked();
    if (!g_enabled) {
        pthread_mutex_unlock(&g_lock);
        return NULL;
    }
    g_lookups++;
    entry_t* hit = table_find(a, b);
    if (hit) {
        char* copy = malloc(hit->length + 1);
        if (copy) memcpy(copy, hit->text, hit->length + 1);
        if (copy) g_memory_hits++;
        pthread_mutex_unlock(&g_lock);
        return copy;
    }
    bool use_disk = g_dir_usable;
    char path[sizeof(g_dir) + 64];
    if (use_disk) file_name(path, sizeof(path), a, b);
    pthread_mutex_unlock(&g_lock);

    if (!use_disk) return NULL;
    size_t length = 0;
    char* text = read_entry_file(path, &length);
    if (!text) return NULL;

    char* remembered = malloc(length + 1);
    if (remembered) memcpy(remembered, text, length + 1);
    pthread_mutex_lock(&g_lock);
    g_disk_hits++;
    if (remembered) table_put(a, b, remembered, length);
    pthread_mutex_unlock(&g_lock);
    return text;
}

void ltw_shader_cache_store(unsigned int shader_type, int target_version, const char* source, size_t source_length, const char* translated) {
    if (!source || source_length == 0 || !translated) return;
    size_t length = strlen(translated);
    if (length == 0 || length > MAX_PAYLOAD_BYTES) return;
    uint64_t a, b;
    make_key(shader_type, target_version, source, source_length, &a, &b);

    char* copy = malloc(length + 1);
    if (!copy) return;
    memcpy(copy, translated, length + 1);

    pthread_mutex_lock(&g_lock);
    init_locked();
    if (!g_enabled) {
        pthread_mutex_unlock(&g_lock);
        free(copy);
        return;
    }
    table_put(a, b, copy, length);
    g_stores++;
    bool write_disk = g_dir_usable;
    if (write_disk) {
        if (g_disk_entries < 0) g_disk_entries = count_disk_entries();
        if (g_disk_entries >= MAX_DISK_ENTRIES) write_disk = false;
        else g_disk_entries++;
    }
    pthread_mutex_unlock(&g_lock);

    if (write_disk) write_entry_file(a, b, translated, length);
}

void ltw_shader_cache_note_translation(double milliseconds) {
    pthread_mutex_lock(&g_lock);
    g_translations++;
    g_translation_ms += milliseconds;
    pthread_mutex_unlock(&g_lock);
}

void ltw_shader_cache_report(unsigned int every) {
    if (every == 0) return;
    pthread_mutex_lock(&g_lock);
    if (!g_enabled || g_lookups == 0 || g_lookups % every != 0) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    unsigned long lookups = g_lookups, mem = g_memory_hits, disk = g_disk_hits, trans = g_translations;
    double total_ms = g_translation_ms;
    double average = trans ? total_ms / (double)trans : 0.0;
    double saved = average * (double)(mem + disk);
    g_saved_ms_estimate = saved;
    pthread_mutex_unlock(&g_lock);
    printf("LTW shader cache: %lu lookups, %lu from memory, %lu from disk, %lu translated (avg %.1f ms, %.0f ms total); "
           "about %.0f ms of translation avoided\n", lookups, mem, disk, trans, average, total_ms, saved);
}

void ltw_shader_cache_reset_for_tests(void) {
    pthread_mutex_lock(&g_lock);
    table_clear();
    g_ready = false;
    g_enabled = true;
    g_dir_usable = false;
    g_dir[0] = 0;
    g_disk_entries = -1;
    g_lookups = g_memory_hits = g_disk_hits = g_stores = g_translations = 0;
    g_translation_ms = g_saved_ms_estimate = 0;
    pthread_mutex_unlock(&g_lock);
}
