/* Copyright (C) 2026 Jeannes Bryan */

/* reader (NiOn 2.2.4). */

#include "reader.h"

#include <webkit/webkit.h>

#include "types.h"

#define NION_READER_CSS_PATH "/io/github/jeannesbryan/Nion/reader.css"

const gchar *nion_reader_css(void)
{
    static gchar *cached = NULL;
    static gboolean tried = FALSE;

    if (tried)
        return cached;
    tried = TRUE;

    GError *error = NULL;
    GBytes *bytes = g_resources_lookup_data(NION_READER_CSS_PATH,
                                           G_RESOURCE_LOOKUP_FLAGS_NONE,
                                           &error);
    if (!bytes) {
        g_warning("reader: stylesheet resource missing: %s",
                  error ? error->message : "unknown");
        g_clear_error(&error);
        return NULL;
    }

    gsize size = 0;
    const gchar *data = g_bytes_get_data(bytes, &size);
    cached = g_strndup(data, size);
    g_bytes_unref(bytes);
    return cached;
}

void nion_reader_set_enabled(NionTab *tab, gboolean enabled)
{
    if (!tab || !tab->web_view)
        return;

    WebKitUserContentManager *manager =
        webkit_web_view_get_user_content_manager(tab->web_view);
    if (!manager)
        return;

    /* Always clear first. Adding twice would stack the sheet, and WebKit has no
     * "is this sheet attached" query, so remove_all is the only reliable reset. */
    webkit_user_content_manager_remove_all_style_sheets(manager);

    if (!enabled) {
        tab->reader_mode = FALSE;
        return;
    }

    const gchar *css = nion_reader_css();
    if (!css) {
        tab->reader_mode = FALSE;
        return;
    }

    WebKitUserStyleSheet *sheet = webkit_user_style_sheet_new(
        css,
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_STYLE_LEVEL_USER,
        NULL,
        NULL);
    if (!sheet) {
        tab->reader_mode = FALSE;
        return;
    }

    webkit_user_content_manager_add_style_sheet(manager, sheet);
    webkit_user_style_sheet_unref(sheet);
    tab->reader_mode = TRUE;
}

gboolean nion_reader_toggle(NionTab *tab)
{
    if (!tab)
        return FALSE;
    nion_reader_set_enabled(tab, !tab->reader_mode);
    return tab->reader_mode;
}
