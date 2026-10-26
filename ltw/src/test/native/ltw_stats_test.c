/* Host-side test for the count-only statistics. Build and run:
 *   gcc -Wall -Wextra -I ltw/src/main/tinywrapper ltw/src/test/native/ltw_stats_test.c \
 *       ltw/src/main/tinywrapper/ltw_stats.c -lpthread -o /tmp/ltw_stats_test && /tmp/ltw_stats_test */
#include "ltw_stats.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_counting(void) {
    ltw_stats_reset_for_tests();
    LTW_STAT(draw_calls, 3);
    LTW_STAT(draw_calls, 2);
    LTW_STAT(multi_draw_parts, 40);
    CHECK(ltw_stats_local.draw_calls == 5);
    CHECK(ltw_stats_local.multi_draw_parts == 40);
}

static void test_off_switch(void) {
    ltw_stats_reset_for_tests();
    ltw_stats_on = false;
    LTW_STAT(draw_calls, 9);
    CHECK(ltw_stats_local.draw_calls == 0);
    ltw_stats_on = true;
}

static void test_format(void) {
    ltw_stats_t s;
    memset(&s, 0, sizeof(s));
    s.draw_calls = 1000;
    s.multi_draw_calls = 100;
    s.multi_draw_parts = 450;
    s.buffer_binds = 2000;
    s.buffer_binds_same = 500;
    s.program_uses = 400;
    s.program_uses_skipped = 300;
    s.tex_uploads = 10;
    char line[512];
    int n = ltw_stats_format(&s, 5.0, 77, line, sizeof(line));
    CHECK(n > 0 && n < (int)sizeof(line));
    CHECK(strstr(line, "draws/s=200") != NULL);
    CHECK(strstr(line, "multidraw/s=20 (4.5 draws each)") != NULL);
    CHECK(strstr(line, "buffer binds/s=400 (25% repeat)") != NULL);
    CHECK(strstr(line, "program uses/s=80 (75% skipped)") != NULL);
    CHECK(strstr(line, "thread 77") != NULL);

    /* An empty window must not divide by zero. */
    memset(&s, 0, sizeof(s));
    n = ltw_stats_format(&s, 0.0, 1, line, sizeof(line));
    CHECK(n > 0 && strstr(line, "nan") == NULL && strstr(line, "inf") == NULL);
}

static void test_poll_prints_and_resets(void) {
    ltw_stats_reset_for_tests();
    ltw_stats_set_interval_for_tests(30.0, 0); /* look at the clock on every poll */
    ltw_stats_poll();                          /* first poll only starts the window */
    LTW_STAT(draw_calls, 7);
    CHECK(ltw_stats_local.draw_calls == 7);
    usleep(60 * 1000);
    ltw_stats_poll();                          /* interval passed: line printed, counters cleared */
    CHECK(ltw_stats_local.draw_calls == 0);
    ltw_stats_set_interval_for_tests(5000.0, 1023);
}

static void test_poll_is_cheap_between_checks(void) {
    ltw_stats_reset_for_tests();
    ltw_stats_set_interval_for_tests(1.0, 1023);
    LTW_STAT(draw_calls, 1);
    for (int i = 0; i < 1000; i++) ltw_stats_poll(); /* fewer than 1024 polls: clock never read */
    CHECK(ltw_stats_local.draw_calls == 1);
    ltw_stats_set_interval_for_tests(5000.0, 1023);
}

static void* worker(void* arg) {
    ltw_stats_reset_for_tests();
    LTW_STAT(draw_calls, 11);
    *(uint64_t*)arg = ltw_stats_local.draw_calls;
    return NULL;
}

static void test_threads_count_separately(void) {
    ltw_stats_reset_for_tests();
    LTW_STAT(draw_calls, 4);
    uint64_t seen = 0;
    pthread_t t;
    pthread_create(&t, NULL, worker, &seen);
    pthread_join(t, NULL);
    CHECK(seen == 11);
    CHECK(ltw_stats_local.draw_calls == 4);
}

int main(void) {
    test_counting();
    test_off_switch();
    test_format();
    test_poll_prints_and_resets();
    test_poll_is_cheap_between_checks();
    test_threads_count_separately();
    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("ltw_stats: all checks passed\n");
    return 0;
}
