/**
 * Frame pacing override (URinth patch).
 *
 * Minecraft asks for vertical sync through glfwSwapInterval, which ends up as eglSwapInterval(1):
 * every frame then waits for the screen's next refresh, so the frame rate cannot go above the
 * refresh rate (60 or 90). When the launcher sets LTW_FORCE_SWAP_INTERVAL=0 (Performance Mode),
 * LTW passes 0 to the system instead, whatever the game asked for, so frames are handed over as
 * soon as they are drawn. The screen still shows at most one frame per refresh; what changes is
 * that the game is never held back by the wait, and always hands over its newest frame.
 *
 * Nothing in the game's own settings is changed. Independent of EGL headers, so it can be tested
 * on a normal computer.
 * For use under LGPL-3.0
 */
#ifndef LTW_FRAME_PACING_H
#define LTW_FRAME_PACING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The swap interval to really use for a requested one. Without LTW_FORCE_SWAP_INTERVAL in the
 * environment (or with a value that is not a whole number from 0 to 4) the request is returned
 * unchanged. The environment is read once.
 */
int ltw_effective_swap_interval(int requested);

/** True if a forced interval is in effect (so the caller can say so once in the log). */
bool ltw_swap_interval_forced(void);

/* ---------------------------------------------------------------------------------------------
 * Frame cap (URinth patch)
 *
 * With vsync forced off the game could draw as fast as the phone allows, which keeps the CPU and
 * GPU at their limit: the phone heats up, then throttles, and that shows as stutter and freezes.
 * The launcher can ask for a ceiling instead. It writes a number (frames per second, 0 = no
 * ceiling) to the file named by LTW_FRAME_CAP_FILE and may change it while the game runs (for
 * example when the phone gets hot). After each swap, LTW sleeps just long enough to keep to it.
 * ------------------------------------------------------------------------------------------- */

/** Parse a cap from text such as "90\n". 0..1000 is valid; anything else gives -1. */
int ltw_parse_frame_cap(const char* text);

/** Pacing state; zero it to start. */
typedef struct {
    int64_t next_ns;
} ltw_pacer_t;

/**
 * How many nanoseconds to wait now so frames come at most {@code fps} times a second. Does not
 * sleep itself. A frame that is already late is let through at once, and a long stall does not
 * cause a burst of catch-up frames afterwards. fps <= 0 means no ceiling.
 */
int64_t ltw_pacer_delay_ns(ltw_pacer_t* pacer, int64_t now_ns, int fps);

/**
 * The ceiling in force at {@code now_ns}. The file is looked at at most twice a second, and a
 * missing or unreadable file or a bad number keeps the last good value (0 before there is one).
 */
int ltw_frame_cap_current(int64_t now_ns);

/** Called by the swap wrapper after each real swap: sleep if the frame came too early. */
void ltw_frame_cap_after_swap(void);

/** Forget the cached environment, for tests. */
void ltw_frame_pacing_reset_for_tests(void);

#ifdef __cplusplus
}
#endif

#endif
