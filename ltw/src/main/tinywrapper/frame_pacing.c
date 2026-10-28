/**
 * Frame pacing override (URinth patch). See frame_pacing.h.
 * For use under LGPL-3.0
 */
#include "frame_pacing.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int forced_interval = -1;   /* -1: not forced */
static bool read_env;

static void read_environment(void) {
    if (read_env) return;
    read_env = true;
    const char* value = getenv("LTW_FORCE_SWAP_INTERVAL");
    if (value == NULL || value[0] == '\0') return;
    if (value[0] < '0' || value[0] > '4' || value[1] != '\0') return; /* only 0..4 */
    forced_interval = value[0] - '0';
}

int ltw_effective_swap_interval(int requested) {
    read_environment();
    return forced_interval >= 0 ? forced_interval : requested;
}

bool ltw_swap_interval_forced(void) {
    read_environment();
    return forced_interval >= 0;
}

int ltw_parse_frame_cap(const char* text) {
    if (text == NULL) return -1;
    while (*text == ' ' || *text == '\t') text++;
    if (*text < '0' || *text > '9') return -1;
    long value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        if (value > 1000) return -1;
        text++;
    }
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text++;
    return *text == '\0' ? (int) value : -1;
}

int64_t ltw_pacer_delay_ns(ltw_pacer_t* pacer, int64_t now_ns, int fps) {
    if (pacer == NULL) return 0;
    if (fps <= 0) {
        pacer->next_ns = 0;
        return 0;
    }
    const int64_t period = 1000000000LL / fps;
    if (pacer->next_ns == 0) {
        pacer->next_ns = now_ns + period;
        return 0;
    }
    if (now_ns >= pacer->next_ns) {
        /* Late. Keep the rhythm if it is only a little late, start afresh after a real stall. */
        pacer->next_ns = (now_ns - pacer->next_ns > period) ? now_ns + period : pacer->next_ns + period;
        return 0;
    }
    int64_t delay = pacer->next_ns - now_ns;
    pacer->next_ns += period;
    return delay;
}

#define CAP_FILE_CHECK_NS 500000000LL

static int cap_value;
static int64_t cap_checked_ns = -CAP_FILE_CHECK_NS;
static const char* cap_path;
static bool cap_path_read;
static ltw_pacer_t swap_pacer;

int ltw_frame_cap_current(int64_t now_ns) {
    if (!cap_path_read) {
        cap_path_read = true;
        cap_path = getenv("LTW_FRAME_CAP_FILE");
        if (cap_path != NULL && cap_path[0] == '\0') cap_path = NULL;
    }
    if (cap_path == NULL) return cap_value;
    if (now_ns - cap_checked_ns < CAP_FILE_CHECK_NS) return cap_value;
    cap_checked_ns = now_ns;
    FILE* file = fopen(cap_path, "r");
    if (file == NULL) return cap_value;
    char text[32];
    size_t got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    int parsed = ltw_parse_frame_cap(text);
    if (parsed >= 0) cap_value = parsed;
    return cap_value;
}

static int64_t monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void ltw_frame_cap_after_swap(void) {
    int64_t now = monotonic_ns();
    int fps = ltw_frame_cap_current(now);
    if (fps <= 0 && swap_pacer.next_ns == 0) return;
    int64_t delay = ltw_pacer_delay_ns(&swap_pacer, now, fps);
    if (delay <= 0) return;
    struct timespec wait = { (time_t) (delay / 1000000000LL), (long) (delay % 1000000000LL) };
    nanosleep(&wait, NULL);
}

void ltw_frame_pacing_reset_for_tests(void) {
    forced_interval = -1;
    read_env = false;
    cap_value = 0;
    cap_checked_ns = -CAP_FILE_CHECK_NS;
    cap_path = NULL;
    cap_path_read = false;
    swap_pacer.next_ns = 0;
}
