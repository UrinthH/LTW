/**
 * Count-only statistics (URinth patch). See ltw_stats.h.
 * For use under LGPL-3.0
 */
#define _GNU_SOURCE
#include "ltw_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

bool ltw_stats_on = true;
__thread ltw_stats_t ltw_stats_local;

static double interval_ms = 5000.0;
static unsigned poll_mask = 1023;

static __thread unsigned polls;
static __thread double window_started_ms;

static double clock_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

__attribute__((constructor)) static void ltw_stats_init(void) {
    const char* on = getenv("LTW_STATS");
    if (on != NULL && (strcmp(on, "0") == 0 || strcmp(on, "false") == 0)) ltw_stats_on = false;
    const char* seconds = getenv("LTW_STATS_SECONDS");
    if (seconds != NULL) {
        double value = atof(seconds);
        if (value >= 1.0 && value <= 600.0) interval_ms = value * 1000.0;
    }
}

static double per_second(uint64_t count, double seconds) {
    return seconds > 0.0 ? (double)count / seconds : 0.0;
}

static double percent(uint64_t part, uint64_t whole) {
    return whole > 0 ? 100.0 * (double)part / (double)whole : 0.0;
}

int ltw_stats_format(const ltw_stats_t* s, double seconds, long thread_id, char* buffer, size_t size) {
    double parts_per_call = s->multi_draw_calls > 0 ? (double)s->multi_draw_parts / (double)s->multi_draw_calls : 0.0;
    return snprintf(buffer, size,
        "LTW stats (%.1fs, thread %ld): draws/s=%.0f multidraw/s=%.0f (%.1f draws each) "
        "buffer binds/s=%.0f (%.0f%% repeat) program uses/s=%.0f (%.0f%% skipped) "
        "texture uploads/s=%.0f sub-uploads/s=%.0f",
        seconds, thread_id,
        per_second(s->draw_calls, seconds), per_second(s->multi_draw_calls, seconds), parts_per_call,
        per_second(s->buffer_binds, seconds), percent(s->buffer_binds_same, s->buffer_binds),
        per_second(s->program_uses, seconds), percent(s->program_uses_skipped, s->program_uses),
        per_second(s->tex_uploads, seconds), per_second(s->tex_sub_uploads, seconds));
}

void ltw_stats_poll(void) {
    if (!ltw_stats_on) return;
    if ((++polls & poll_mask) != 0) return;
    double now = clock_ms();
    if (window_started_ms == 0.0) {
        window_started_ms = now;
        memset(&ltw_stats_local, 0, sizeof(ltw_stats_local));
        return;
    }
    double elapsed = now - window_started_ms;
    if (elapsed < interval_ms) return;
    ltw_stats_t* s = &ltw_stats_local;
    if (s->draw_calls + s->multi_draw_calls + s->buffer_binds > 0) {
        char line[512];
        ltw_stats_format(s, elapsed / 1000.0, (long)syscall(SYS_gettid), line, sizeof(line));
        printf("%s\n", line);
        fflush(stdout);
    }
    memset(s, 0, sizeof(*s));
    window_started_ms = now;
}

void ltw_stats_set_interval_for_tests(double milliseconds, unsigned mask) {
    interval_ms = milliseconds;
    poll_mask = mask;
}

void ltw_stats_reset_for_tests(void) {
    memset(&ltw_stats_local, 0, sizeof(ltw_stats_local));
    polls = 0;
    window_started_ms = 0.0;
    ltw_stats_on = true;
}
