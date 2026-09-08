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
#define NION_CONTENT_FILTER_ID "nion-lightweight-v1"
#define NION_ZOOM_MIN_PERCENT 50
#define NION_ZOOM_MAX_PERCENT 200
#define NION_ZOOM_DEFAULT_PERCENT 100

typedef struct _NionApp NionApp;
typedef struct _NionTab NionTab;
typedef struct _NionDownload NionDownload;
typedef struct _NionBookmark NionBookmark;
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

    guint onion_cancel_retries;
    guint retry_source_id;
    gchar *retry_uri;
    gchar *display_uri_override;
    gchar *onion_location;

    GtkWidget *http_warning_window;
    gchar *http_warning_uri;
    WebKitPolicyDecision *http_warning_decision;
    gchar *http_allowed_origin;

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
};

struct _NionBookmark {
    gchar *title;
    gchar *uri;
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
    GtkWidget *find_match_label;
    GtkWidget *find_prev_button;
    GtkWidget *find_next_button;
    GtkWidget *find_close_button;

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
    gchar *site_zoom_file;
    gchar *site_javascript_file;
    gchar *content_blocking_file;
    gchar *autoplay_file;
    gchar *content_filter_store_dir;
    GPtrArray *bookmarks;
    GHashTable *site_zoom;
    GHashTable *site_javascript_disabled;
    GHashTable *site_javascript_enabled;
    GHashTable *content_blocking_disabled;
    GHashTable *autoplay_allowed_sites;
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
