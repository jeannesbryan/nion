#ifndef NION_FIND_LABEL_H
#define NION_FIND_LABEL_H

#include <glib.h>

/* Formats the find bar counter, for example "3 of 17".
 *
 * Kept free of GTK on purpose. WebKit hands over a total match count and
 * nothing else, so the position has to be tracked by the caller, which means
 * the clamping and the wraparound rules are the parts most likely to drift.
 * Being plain glib, they can be exercised by a harness that links this file
 * directly instead of by reading the code and hoping.
 *
 * index is the caller's idea of the current position and may be stale or out
 * of range; it is clamped here rather than at every call site so that no caller
 * can render "7 of 3".
 *
 * Returns a newly allocated string, or an empty string when total is 0.
 * Caller owns the result.
 */
gchar *nion_find_format_label(guint index, guint total);

/* Steps a position by one, wrapping around. direction > 0 moves forward.
 *
 * Wraps by hand rather than letting WebKit's WRAP_AROUND decide, because the
 * label has to agree with what is on screen: going past the last match lands
 * on the first, so the counter goes back to 1 instead of printing total + 1.
 *
 * Returns the new position. When total is 0 the position stays 0.
 */
guint nion_find_step_position(guint index, guint total, int direction);

#endif
