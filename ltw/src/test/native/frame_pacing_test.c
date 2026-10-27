/* Host-side test for the frame pacing override. Build and run:
 *   gcc -Wall -Wextra -I ltw/src/main/tinywrapper ltw/src/test/native/frame_pacing_test.c \
 *       ltw/src/main/tinywrapper/frame_pacing.c -o /tmp/frame_pacing_test && /tmp/frame_pacing_test */
#include "frame_pacing.h"
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void with_env(const char* value) {
    if (value) setenv("LTW_FORCE_SWAP_INTERVAL", value, 1);
    else unsetenv("LTW_FORCE_SWAP_INTERVAL");
    ltw_frame_pacing_reset_for_tests();
}

static void test_unset_passes_through(void) {
    with_env(NULL);
    CHECK(!ltw_swap_interval_forced());
    CHECK(ltw_effective_swap_interval(0) == 0);
    CHECK(ltw_effective_swap_interval(1) == 1);
    CHECK(ltw_effective_swap_interval(2) == 2);
}

static void test_forced_zero(void) {
    with_env("0");
    CHECK(ltw_swap_interval_forced());
    CHECK(ltw_effective_swap_interval(1) == 0);
    CHECK(ltw_effective_swap_interval(0) == 0);
    CHECK(ltw_effective_swap_interval(3) == 0);
}

static void test_forced_other(void) {
    with_env("2");
    CHECK(ltw_swap_interval_forced());
    CHECK(ltw_effective_swap_interval(1) == 2);
}

static void test_bad_values_ignored(void) {
    const char* bad[] = {"", "-1", "5", "9", "01", "a", "1 ", " 1", "00", NULL};
    for (int i = 0; bad[i]; i++) {
        with_env(bad[i]);
        CHECK(!ltw_swap_interval_forced());
        CHECK(ltw_effective_swap_interval(1) == 1);
    }
}

static void test_read_once(void) {
    with_env("0");
    CHECK(ltw_effective_swap_interval(1) == 0);
    setenv("LTW_FORCE_SWAP_INTERVAL", "3", 1); /* changed later: the first reading stays */
    CHECK(ltw_effective_swap_interval(1) == 0);
    ltw_frame_pacing_reset_for_tests();
    CHECK(ltw_effective_swap_interval(1) == 3);
}

int main(void) {
    test_unset_passes_through();
    test_forced_zero();
    test_forced_other();
    test_bad_values_ignored();
    test_read_once();
    unsetenv("LTW_FORCE_SWAP_INTERVAL");
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("frame_pacing: all tests passed\n");
    return 0;
}
