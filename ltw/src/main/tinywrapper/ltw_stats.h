/**
 * Count-only statistics (URinth patch).
 *
 * LTW counts how many draw calls, buffer binds, program changes and texture uploads the game makes,
 * and prints one summary line every few seconds, so a lag report can say where the CPU time goes
 * before anything is optimised blindly. Counting never changes what is drawn: each counter is a
 * plain add to a per-thread struct, and the clock is only read once every 1024 polls.
 *
 * Set LTW_STATS=0 to turn it off, LTW_STATS_SECONDS=<n> to change the 5 second interval.
 * Everything here is independent of OpenGL headers, so it can be tested on a normal computer.
 * For use under LGPL-3.0
 */
#ifndef LTW_STATS_H
#define LTW_STATS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t draw_calls;            /* glDrawElementsBaseVertex */
    uint64_t multi_draw_calls;      /* glMultiDraw* entries */
    uint64_t multi_draw_parts;      /* draws contained in those entries */
    uint64_t buffer_binds;          /* glBindBuffer */
    uint64_t buffer_binds_same;     /* ...of the buffer that was already bound */
    uint64_t program_uses;          /* glUseProgram */
    uint64_t program_uses_skipped;  /* ...already selected, not sent to the driver */
    uint64_t tex_uploads;           /* glTexImage2D */
    uint64_t tex_sub_uploads;       /* glTexSubImage2D */
} ltw_stats_t;

extern bool ltw_stats_on;
extern __thread ltw_stats_t ltw_stats_local;

#define LTW_STAT(field, amount) do { if (ltw_stats_on) ltw_stats_local.field += (uint64_t)(amount); } while (0)

/** Call from busy functions. Prints a summary line when the interval has passed. */
void ltw_stats_poll(void);

/** Write one summary line (no newline) for `seconds` of counting. Returns the length written. */
int ltw_stats_format(const ltw_stats_t* stats, double seconds, long thread_id, char* buffer, size_t size);

/** Test hooks. */
void ltw_stats_set_interval_for_tests(double milliseconds, unsigned poll_mask);
void ltw_stats_reset_for_tests(void);

#ifdef __cplusplus
}
#endif

#endif // LTW_STATS_H
