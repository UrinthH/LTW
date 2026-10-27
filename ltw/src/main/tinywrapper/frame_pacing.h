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

/** Forget the cached environment, for tests. */
void ltw_frame_pacing_reset_for_tests(void);

#ifdef __cplusplus
}
#endif

#endif
