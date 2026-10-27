/**
 * Frame pacing override (URinth patch). See frame_pacing.h.
 * For use under LGPL-3.0
 */
#include "frame_pacing.h"
#include <stdlib.h>

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

void ltw_frame_pacing_reset_for_tests(void) {
    forced_interval = -1;
    read_env = false;
}
