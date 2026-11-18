/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_TYPES_H
#define NION_TYPES_H

#include <gtk/gtk.h>
#include <webkit/webkit.h>

/* Shared type definitions extracted from src/main.c during Phase 0
 * modularization (NiOn 2.0.0). Structs are intentionally NOT yet
 * split into sub-structs. */

#define NION_APP_ID "io.github.jeannesbryan.Nion"
#define NION_TOR_HOST "127.0.0.1"
#define NION_TOR_PORT_FIRST 19050
#define NION_TOR_PORT_LAST 19069
#define NION_REPOSITORY_URL "https://github.com/jeannesbryan/nion"
#define NION_TOR_STARTUP_TIMEOUT_SECONDS 120
#define NION_TOR_GRACEFUL_SHUTDOWN_MS 3000
#define NION_ONION_RETRY_DELAY_MS 350
#define NION_SESSION_SAVE_DELAY_MS 750
#define NION_TAB_DISCARD_AFTER_MS  300000   /* 5 min idle in background */
#define NION_TAB_DISCARD_POLL_MS    15000   /* sweep interval */
#define NION_SESSION_FORMAT 1
#define NION_MAX_SESSION_TABS 256
#define NION_MAX_SESSION_TABS_INPUT 1024
#define NION_MAX_SESSION_FILE_BYTES (16 * 1024 * 1024)
#define NION_MAX_TAB_STATE_BASE64_BYTES (8 * 1024 * 1024)
#define NION_MAX_SAVED_URI_BYTES (16 * 1024)
#define NION_MAX_PREFERENCES_FILE_BYTES (1024 * 1024)
#define NION_MAX_DOWNLOADS_FILE_BYTES (4 * 1024 * 1024)
#define NION_MAX_DOWNLOAD_HISTORY 500
#define NION_MAX_DOWNLOAD_HISTORY_INPUT 5000
#define NION_MAX_BOOKMARKS_FILE_BYTES (2 * 1024 * 1024)
#define NION_MAX_BOOKMARKS 1000
#define NION_MAX_BOOKMARKS_INPUT 5000
#define NION_MAX_BOOKMARK_TITLE_CHARS 240
/* Local browsing history (v2.3.0, roadmap T1-1). The store is a plain local
 * file: no sync, no network, and Private Windows never get a path to it. */
#define NION_MAX_HISTORY_FILE_BYTES (4 * 1024 * 1024)
#define NION_MAX_HISTORY 2000
#define NION_MAX_HISTORY_INPUT 20000
#define NION_MAX_HISTORY_TITLE_CHARS 240
#define NION_HISTORY_RETENTION_DAYS 90
#define NION_HISTORY_FORMAT 1
/* Address bar autocomplete (v2.3.1, roadmap T1-2). Suggestions are built only
 * from data already on this device (history, bookmarks, open tabs); no
 * keystroke ever leaves the machine to be completed somewhere else. */
#define NION_AUTOCOMPLETE_MAX_RESULTS 8
/* Reading list, saved for offline (v2.3.3, roadmap T1-5).
 *
 * A page saved for later is stored as TEXT, not as a copy of the page. Keeping
 * the HTML would keep its external images, stylesheets and frames too, so
 * "reading it offline" would quietly try to fetch them and the feature would be
 * offline in name only. Text is the part that can honestly be read with no
 * network at all, and it is rendered by GTK rather than by WebKit, so opening a
 * saved page cannot issue a request even by accident.
 *
 * The limit is per entry and generous for an article but small enough that the
 * whole file stays rewritable in one go. */
#define NION_MAX_READING_LIST 500
#define NION_MAX_READING_TEXT_BYTES (256 * 1024)
#define NION_MAX_READING_FILE_BYTES (16 * 1024 * 1024)
#define NION_MAX_READING_TITLE_CHARS 240
#define NION_READING_FORMAT 1
/* A page that yields less text than this is not worth calling reading material
 * (an error page, a login wall, a page still loading). Refuse rather than save
 * an entry that would disappoint when opened. */
#define NION_MIN_READABLE_CHARS 200
#define NION_MAX_CLOSED_TABS 10
#define NION_MAX_SITE_ZOOM_FILE_BYTES (1024 * 1024)
#define NION_MAX_SITE_ZOOM_ENTRIES 2048
#define NION_SITE_ZOOM_FORMAT 1
#define NION_MAX_SITE_JAVASCRIPT_FILE_BYTES (1024 * 1024)
#define NION_MAX_SITE_JAVASCRIPT_ENTRIES 2048
#define NION_SITE_JAVASCRIPT_FORMAT 2
#define NION_MAX_CONTENT_BLOCKING_FILE_BYTES (1024 * 1024)
#define NION_MAX_CONTENT_BLOCKING_EXCEPTIONS 2048
#define NION_CONTENT_BLOCKING_FORMAT 1
#define NION_MAX_AUTOPLAY_FILE_BYTES (1024 * 1024)
#define NION_MAX_AUTOPLAY_EXCEPTIONS 2048
#define NION_AUTOPLAY_FORMAT 1
#define NION_MAX_PREFERRED_ONION_FILE_BYTES (1024 * 1024)
#define NION_MAX_PREFERRED_ONION_ENTRIES 2048
#define NION_PREFERRED_ONION_FORMAT 1
#define NION_MAX_BRIDGE_FILE_BYTES (256 * 1024)
#define NION_MAX_BRIDGES 64
#define NION_MAX_BRIDGE_LINE_CHARS 512
#define NION_BRIDGE_FORMAT 1
#define NION_CONTENT_FILTER_ID "nion-lightweight-v1"
#define NION_ZOOM_MIN_PERCENT 50
#define NION_ZOOM_MAX_PERCENT 200
#define NION_ZOOM_DEFAULT_PERCENT 100

typedef struct _NionApp NionApp;
typedef struct _NionTab NionTab;
typedef struct _NionDownload NionDownload;
typedef struct _NionBookmark NionBookmark;
typedef struct _NionHistoryEntry NionHistoryEntry;
typedef struct _NionReadingEntry NionReadingEntry;
typedef struct _NionClearSiteRequest NionClearSiteRequest;
typedef struct _NionClosedTab NionClosedTab;
typedef struct _NionPermissionPrompt NionPermissionPrompt;
typedef struct _NionExternalProtocolPrompt NionExternalProtocolPrompt;
typedef struct _NionClearDataRequest NionClearDataRequest;

typedef enum {
    NION_PERMISSION_CAMERA = 1u << 0,
    NION_PERMISSION_MICROPHONE = 1u << 1,
    NION_PERMISSION_GEOLOCATION = 1u << 2,
    NION_PERMISSION_NOTIFICATIONS = 1u << 3,
} NionPermissionMask;

typedef enum {
    NION_SECURITY_STANDARD = 0,
    NION_SECURITY_SAFER,
    NION_SECURITY_SAFEST,
} NionSecurityLevel;

struct _NionTab {
    NionApp *app;
    GtkWidget *page;
    WebKitWebView *web_view;
    GtkWidget *title_label;
    GtkWidget *favicon_picture;
    GtkWidget *pin_indicator;
    GtkWidget *audio_button;
    GtkWidget *tab_label_box;
    GtkWidget *tab_menu_popover;
    GtkWidget *tab_menu_mute_button;
    GtkWidget *tab_menu_pin_button;
    GtkWidget *tab_menu_close_others_button;
    GtkWidget *tab_menu_close_right_button;
    GtkWidget *tab_close_button;

    gboolean pinned;
    gboolean home_page;
    gboolean error_page;
    gboolean load_failed;
    gboolean connection_committed;
    gboolean mixed_content_displayed;
    gboolean mixed_content_run;
    gboolean mixed_content_other;
    gboolean content_filter_applied;
    /* Reader mode (2.2.4): a user style sheet is attached to this tab. */
    gboolean reader_mode;

    guint onion_cancel_retries;
    guint retry_source_id;
    gchar *retry_uri;
    gchar *display_uri_override;
    gchar *onion_location;

    GtkWidget *http_warning_window;
    gchar *http_warning_uri;
    WebKitPolicyDecision *http_warning_decision;
    gchar *http_allowed_origin;
    /* Strict HTTPS (v2.1 #5): while a clearnet http:// request is being
     * auto-upgraded to its https:// twin, remember the original clearnet URI
     * so that if the https attempt fails (site has no HTTPS) NiOn can refuse
     * with the explicit plain-HTTP warning instead of silently falling back.
     * https_upgrade_refused suppresses the second failure callback (TLS then
     * load-failed) after the plain-HTTP warning is already shown. */
    gchar *https_upgrade_from;
    gboolean https_upgrade_refused;

    gboolean restore_pending;
    gchar *restore_uri;

    /* NiOn Home is synthetic HTML loaded as about:blank and is not guaranteed
     * to become a normal WebKit back/forward item. Keep a strong reference to
     * the page that was active before Home so Back behaves like a browser Home
     * button without turning the internal page into a network navigation. */
    WebKitBackForwardListItem *home_return_item;

    /* A WebKit WebProcess failure must not take down the browser UI. Keep the
     * last real page URI so the tab can show a local recovery page and later
     * recreate the navigation through the same Tor-gated WebView. */
    gboolean web_process_terminated;
    gchar *web_process_uri;

    /* Background Tab Discard (v2.1 #1). When a background tab has been idle
     * for NION_TAB_DISCARD_AFTER_MS the tab's WebKitWebView is destroyed to
     * release its page from the shared web process; the lightweight shell
     * (URI/title/pinned/mute/history slot) survives as a suspended tab that
     * is re-materialized on demand through the main.c webview builder. */
    gboolean discarded;
    gchar *discard_uri;
    gchar *discard_title;
    gboolean discard_muted;
    GtkWidget *suspend_indicator;
    /* v2.3.2 (T1-4): a small spinner shown in a tab that is waiting for Tor.
     * A tab opened while Tor is still bootstrapping otherwise looks like an
     * empty page with no explanation for as long as the bootstrap takes. */
    GtkWidget *tor_wait_indicator;   /* "💤" chip in the tab strip */
    gint64 last_active_msec;        /* monotonic ms of last user/load activity */
};

struct _NionBookmark {
    gchar *title;
    gchar *uri;
};

/* One visited page. Deduplicated by URI: revisiting a page moves the existing
 * entry to the front and bumps its counter instead of appending a new row. */
struct _NionHistoryEntry {
    gchar *title;
    gchar *uri;
    gint64 last_visit;   /* seconds since the epoch */
    guint visits;
};

/* One page saved for offline reading. `text` is plain text extracted at save
 * time; nothing in this struct refers to the network. */
struct _NionReadingEntry {
    gchar *uri;
    gchar *title;
    gchar *text;
    gint64 saved_at;   /* seconds since the epoch */
    guint words;
};

struct _NionClosedTab {
    gchar *uri;
    gboolean muted;
    gboolean pinned;
};

struct _NionClearSiteRequest {
    NionApp *app;
    gchar *host;
    gchar *uri;
    WebKitWebView *web_view;
    GList *website_data;
    gboolean forget_site;
};

struct _NionPermissionPrompt {
    WebKitPermissionRequest *request;
    GtkWidget *page;
    gchar *origin;
    guint permission_mask;
};

struct _NionExternalProtocolPrompt {
    GtkWidget *page;
    gchar *uri;
    gchar *scheme;
};

struct _NionClearDataRequest {
    NionApp *app;
    WebKitWebsiteDataTypes types;
    gboolean clear_zoom;
    gboolean clear_javascript;
    gboolean clear_content_blocking;
    gboolean clear_autoplay;
    gboolean clear_permissions;
};

struct _NionApp {
    GtkApplication *application;
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *address;
    GtkWidget *back_button;
    GtkWidget *forward_button;
    GtkWidget *reload_button;
    GtkWidget *home_button;
    GtkWidget *onion_button;
    GtkWidget *site_info_button;
    GtkWidget *site_info_icon;
    GtkWidget *site_info_window;
    GtkWidget *site_info_title_label;
    GtkWidget *site_info_host_label;
    /* v2.3.5 (T2-7): what an international address really resolves to. Hidden
     * for ordinary ASCII hosts, because a warning on every page is a warning
     * nobody reads. */
    GtkWidget *site_info_idn_label;
    GtkWidget *site_info_idn_row;
    GtkWidget *site_info_connection_label;
    GtkWidget *site_info_route_label;
    GtkWidget *site_info_mixed_label;
    GtkWidget *site_info_tracking_label;
    GtkWidget *site_info_security_label;
    GtkWidget *site_info_uri_label;
    GtkWidget *site_info_javascript_switch;
    GtkWidget *site_info_javascript_status_label;
    GtkWidget *site_info_content_blocking_switch;
    GtkWidget *site_info_content_blocking_status_label;
    GtkWidget *site_info_autoplay_switch;
    GtkWidget *site_info_autoplay_status_label;
    GtkWidget *site_info_camera_label;
    GtkWidget *site_info_microphone_label;
    GtkWidget *site_info_geolocation_label;
    GtkWidget *site_info_notifications_label;
    GtkWidget *site_info_permissions_reset_button;
    GtkWidget *site_info_forget_button;
    gboolean updating_site_controls;
    GtkWidget *bookmark_button;
    GtkWidget *new_tab_button;
    /* Tab strip overflow (Priority+). The notebook scrolls its own strip, but a
     * scrolled-out tab cannot be identified or reached by name, so the strip
     * gets an explicit overflow menu that lists every open tab. */
    GtkWidget *tab_overflow_button;
    GtkWidget *tab_overflow_popover;
    GtkWidget *tab_overflow_box;
    /* v2.3.4 (T2-6): the overflow menu lists every open tab, which stops being
     * useful around thirty tabs. The field narrows that list. It lives outside
     * the rebuilt container, so typing does not destroy the entry mid-word. */
    GtkWidget *tab_filter_entry;
    /* Set while the field is cleared from code (each time the menu opens), so a
     * programmatic clear does not look like the user typing. */
    gboolean tab_filter_updating;
    GtkWidget *progress_bar;
    GtkWidget *status_label;
    GtkWidget *downloads_panel;
    GtkWidget *downloads_list;
    GtkWidget *downloads_window;
    GtkWidget *downloads_empty_label;
    GtkWidget *bookmarks_window;
    GtkWidget *bookmarks_list;
    GtkWidget *bookmarks_empty_label;
    GtkWidget *bookmarks_search_entry;
    GtkWidget *bookmarks_result_label;
    GtkWidget *menu_button;
    GtkWidget *preferences_window;
    GtkWidget *toolbar;
    GtkWidget *find_bar;
    GtkWidget *find_entry;
    /* WebKit reports only the total match count, never which match is
     * currently selected, so the position is tracked here. find_new_search
     * exists because found-text fires for next/previous too: without a flag
     * to tell a fresh search apart from a step, every step would reset the
     * counter back to 1. */
    guint find_total;
    guint find_index;
    gboolean find_new_search;
    GtkWidget *find_match_label;
    GtkWidget *find_prev_button;
    GtkWidget *find_next_button;
    GtkWidget *find_close_button;

    /* Address bar autocomplete (v2.3.1, roadmap T1-2). A GtkRevealer with a
     * GtkListBox sits under the toolbar instead of a GtkPopover: an overlay
     * popover above an accelerated WebKitGTK view flickers badly on lightweight
     * X11 window managers, the same reason the Site Information panel is a
     * transient window. The list takes a little vertical space while it is open
     * and gives it back when it closes. */
    GtkWidget *autocomplete_revealer;
    GtkWidget *autocomplete_scroller;
    GtkWidget *autocomplete_list;
    /* Set while the address bar is filled from code (tab switch, load
     * finished). Without this flag the "changed" handler would open the
     * suggestion list on every tab switch, as if the browser were typing. */
    gboolean address_updating;

    WebKitNetworkSession *network_session;

    gboolean is_private;
    NionApp *owner;
    GPtrArray *private_windows;

    GSubprocess *tor_process;
    GDataInputStream *tor_output;
    guint16 tor_socks_port;
    gchar *tor_proxy_uri;
    gchar *tor_binary_path;
    gchar *tor_runtime_file;
    guint tor_startup_timeout_id;
    /* v2.3.2 (roadmap T1-4): what Tor says it is doing, copied from its own
     * bootstrap log line ("Loading relay descriptors"). Shown in the status
     * line, in the start page, and in the tooltip of every tab that is waiting,
     * so a slow bootstrap looks like work in progress rather than a hang. */
    gchar *tor_bootstrap_summary;
    gboolean tor_recovery_attempted;
    guint tor_port_retry_count;
    gboolean tor_saw_port_conflict;
    gboolean tor_saw_corruption;
    gboolean tor_ready;
    gboolean tor_failed;
    /* Set while a deliberate New Identity circuit rotation is in flight, so
     * the old Tor child's exit callback does not trigger the error path. */
    gboolean tor_switching_identity;
    gint tor_bootstrap_percent;
    gboolean shutting_down;
    gboolean close_confirm_open;
    gboolean close_confirmed;
    gboolean private_tab_close_confirm_open;
    GtkWidget *private_tab_close_confirm_window;
    gboolean external_protocol_prompt_open;
    gchar *tor_last_log;

    gboolean restore_session;
    gboolean block_third_party_cookies;
    NionSecurityLevel security_level;
    gchar *search_engine;
    gboolean previous_shutdown_clean;
    gboolean restored_previous_session;
    gboolean crash_recovery_decision_pending;
    GtkWidget *crash_recovery_window;
    guint session_save_source_id;
    guint discard_sweep_source_id;
    gboolean normalizing_tab_order;

    gchar *data_dir;
    gchar *cache_dir;
    gchar *tor_dir;
    gchar *cookie_file;
    gchar *download_dir;
    gchar *config_dir;
    gchar *preferences_file;
    gchar *session_file;
    gchar *downloads_file;
    gchar *bookmarks_file;
    gchar *history_file;
    gchar *site_zoom_file;
    gchar *site_javascript_file;
    gchar *content_blocking_file;
    gchar *autoplay_file;
    gchar *preferred_onion_file;
    gchar *bridge_file;
    gchar *content_filter_store_dir;
    GPtrArray *bookmarks;
    /* NULL in a Private Window: private browsing must not write a URL trail,
     * so the private app never receives a history path at all. */
    GPtrArray *history;
    /* v2.3.3 (T1-5): pages saved for offline reading. NULL in a Private Window,
     * for the same reason history is: what someone reads in private is not a
     * trail to keep. */
    gchar *reading_list_file;
    GPtrArray *reading_list;
    GtkWidget *reading_window;
    GtkWidget *reading_list_box;
    GtkWidget *reading_empty_label;
    GtkWidget *reading_count_label;
    GtkWidget *history_window;
    GtkWidget *history_list;
    GtkWidget *history_search_entry;
    GtkWidget *history_result_label;
    GtkWidget *history_empty_label;
    GHashTable *site_zoom;
    GHashTable *site_javascript_disabled;
    GHashTable *site_javascript_enabled;
    GHashTable *content_blocking_disabled;
    GHashTable *autoplay_allowed_sites;
    GHashTable *preferred_onion;   /* site key -> .onion URI string */
    /* Censorship circumvention (v2.2). Bridge mode is opt-in and fail-closed:
     * when it is enabled the bundled Tor must start with UseBridges 1 and the
     * bundled pluggable transports, or NiOn refuses to start Tor at all rather
     * than silently connecting without the bridges the user asked for. */
    gboolean bridges_enabled;
    GPtrArray *bridges;            /* validated bridge lines, file order */
    GtkWidget *bridges_window;
    GtkWidget *bridges_enable_check;
    GtkWidget *bridges_text_view;
    GtkWidget *bridges_transport_label;
    GtkWidget *bridges_error_label;
    GHashTable *temporary_permissions;
    WebKitUserContentFilterStore *content_filter_store;
    WebKitUserContentFilter *content_filter;
    gboolean content_filter_ready;
    gboolean content_filter_failed;
    GQueue *closed_tabs;
};

struct _NionDownload {
    NionApp *app;
    WebKitDownload *download;
    GtkWidget *row;
    GtkWidget *name_label;
    GtkWidget *detail_label;
    GtkWidget *progress_bar;
    GtkWidget *action_button;
    GtkWidget *more_button;
    GtkWidget *open_button;
    GtkWidget *folder_button;
    GtkWidget *copy_link_button;
    GtkWidget *retry_button;

    gchar *destination;
    gchar *filename;
    gchar *source_uri;
    gboolean failed;
    gboolean finished;
    gboolean cancel_requested;
    gchar *history_id;
    gchar *history_status;
    gchar *history_detail;
    gint64 history_time;
};

typedef enum {
    NION_PRIVATE_CLOSE_SINGLE,
    NION_PRIVATE_CLOSE_OTHERS,
    NION_PRIVATE_CLOSE_RIGHT
} NionPrivateCloseMode;

typedef struct {
    NionApp *app;
    NionTab *anchor;
    NionPrivateCloseMode mode;
} NionPrivateCloseRequest;

#endif /* NION_TYPES_H */
