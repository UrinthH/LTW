/* Host-side test for the frame pacing override. Build and run:
 *   gcc -Wall -Wextra -I ltw/src/main/tinywrapper ltw/src/test/native/frame_pacing_test.c \
 *       ltw/src/main/tinywrapper/frame_pacing.c -o /tmp/frame_pacing_test && /tmp/frame_pacing_test */
#include "frame_pacing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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


static void test_parse_cap(void) {
    CHECK(ltw_parse_frame_cap("0") == 0);
    CHECK(ltw_parse_frame_cap("90\n") == 90);
    CHECK(ltw_parse_frame_cap("  120 \r\n") == 120);
    CHECK(ltw_parse_frame_cap("1000") == 1000);
    CHECK(ltw_parse_frame_cap("1001") == -1);
    CHECK(ltw_parse_frame_cap("-5") == -1);
    CHECK(ltw_parse_frame_cap("") == -1);
    CHECK(ltw_parse_frame_cap("abc") == -1);
    CHECK(ltw_parse_frame_cap("90x") == -1);
    CHECK(ltw_parse_frame_cap("9 0") == -1);
    CHECK(ltw_parse_frame_cap("99999999999999999999") == -1);
    CHECK(ltw_parse_frame_cap(NULL) == -1);
}

#define MS 1000000LL

static void test_pacer_steady(void) {
    ltw_pacer_t p = {0};
    /* 100 fps = 10 ms period. First frame starts the rhythm. */
    CHECK(ltw_pacer_delay_ns(&p, 0 + 5 * MS, 100) == 0);
    /* The next frame is ready after 2 ms: it has to wait the 8 ms that are left. */
    CHECK(ltw_pacer_delay_ns(&p, 7 * MS, 100) == 8 * MS);
    /* After waiting, the frame after that is ready at 17 ms (2 ms of work): waits 8 ms again. */
    CHECK(ltw_pacer_delay_ns(&p, 17 * MS, 100) == 8 * MS);
}

static void test_pacer_late_and_stall(void) {
    ltw_pacer_t p = {0};
    ltw_pacer_delay_ns(&p, 100 * MS, 100);          /* next due 110 ms */
    CHECK(ltw_pacer_delay_ns(&p, 115 * MS, 100) == 0); /* 5 ms late: let through, rhythm kept (next 120) */
    CHECK(ltw_pacer_delay_ns(&p, 116 * MS, 100) == 4 * MS);
    /* A stall of a whole second: no burst of catch-up frames afterwards. */
    CHECK(ltw_pacer_delay_ns(&p, 1200 * MS, 100) == 0);
    CHECK(ltw_pacer_delay_ns(&p, 1201 * MS, 100) == 9 * MS);
}

static void test_pacer_no_cap(void) {
    ltw_pacer_t p = {0};
    CHECK(ltw_pacer_delay_ns(&p, 5 * MS, 0) == 0);
    CHECK(ltw_pacer_delay_ns(&p, 6 * MS, -3) == 0);
    CHECK(ltw_pacer_delay_ns(NULL, 6 * MS, 60) == 0);
    /* turning the cap off clears the rhythm; turning it on again starts afresh */
    ltw_pacer_delay_ns(&p, 10 * MS, 100);
    ltw_pacer_delay_ns(&p, 11 * MS, 0);
    CHECK(p.next_ns == 0);
    CHECK(ltw_pacer_delay_ns(&p, 500 * MS, 100) == 0);
}

static void write_file(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) { fputs(text, f); fclose(f); }
}

static void test_cap_file(void) {
    char path[] = "/tmp/ltw_cap_test_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    setenv("LTW_FRAME_CAP_FILE", path, 1);
    ltw_frame_pacing_reset_for_tests();

    write_file(path, "90\n");
    CHECK(ltw_frame_cap_current(1000 * MS) == 90);
    write_file(path, "60\n");
    CHECK(ltw_frame_cap_current(1100 * MS) == 90);  /* looked at less than half a second ago */
    CHECK(ltw_frame_cap_current(1600 * MS) == 60);
    write_file(path, "garbage");
    CHECK(ltw_frame_cap_current(2200 * MS) == 60);  /* bad text keeps the last good value */
    write_file(path, "0");
    CHECK(ltw_frame_cap_current(2800 * MS) == 0);
    unlink(path);
    CHECK(ltw_frame_cap_current(3400 * MS) == 0);   /* missing file keeps it too */
    write_file(path, "75");
    CHECK(ltw_frame_cap_current(4000 * MS) == 75);  /* and it picks the file up again */
    unlink(path);

    unsetenv("LTW_FRAME_CAP_FILE");
    ltw_frame_pacing_reset_for_tests();
    CHECK(ltw_frame_cap_current(1 * MS) == 0);      /* no file named: no cap */
    ltw_frame_cap_after_swap();                      /* must not sleep or crash without a cap */
}

int main(void) {
    test_unset_passes_through();
    test_forced_zero();
    test_forced_other();
    test_bad_values_ignored();
    test_read_once();
    test_parse_cap();
    test_pacer_steady();
    test_pacer_late_and_stall();
    test_pacer_no_cap();
    test_cap_file();
    unsetenv("LTW_FORCE_SWAP_INTERVAL");
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("frame_pacing: all tests passed\n");
    return 0;
}
