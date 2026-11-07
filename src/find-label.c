#include "find-label.h"

#include <stdint.h>

/* WebKit reports G_MAXUINT when it stops counting at its own ceiling. Printing
 * that number would claim a precision the browser does not have, so the total
 * becomes "1000+" and the position drops to a bare count, since "3 of 1000+"
 * would imply a thousandth match exists to jump to. */
gchar *nion_find_format_label(guint index, guint total)
{
    if (total == 0)
        return g_strdup("");

    if (index < 1)
        index = 1;
    if (index > total)
        index = total;

    if (total == G_MAXUINT)
        return g_strdup_printf("%u of 1000+", index);
    if (total == 1)
        return g_strdup("1 of 1");
    return g_strdup_printf("%u of %u", index, total);
}

guint nion_find_step_position(guint index, guint total, int direction)
{
    if (total == 0)
        return 0;

    if (index < 1)
        index = 1;
    if (index > total)
        index = total;

    if (direction > 0) {
        if (index >= total)
            return 1;
        return index + 1;
    }

    if (index <= 1)
        return total;
    return index - 1;
}
