# `main.c` Function Inventory → Module Map

Record of where every extracted function domain landed during the NiOn 2.0.0
modularization (Phases 0–7). Names are grouped by the module that owns them
today. Functions that remain in `main.c` are the coordinator core.

## `main.c` (coordinator core — kept)

`main`, `on_activate`, `nion_install_actions`, `action_new_tab`-adjacent
GActionEntry table, webview-creation hub (`nion_new_tab_internal`), content
filter preparation / onion-location glue (`on_content_filter_saved`,
`nion_prepare_content_filter`, `nion_set_onion_location`, …), fail-closed
policy routing, and the callback-registration block.

## `types.h`

All shared types/enums/constants: `NionApp`, `NionTab`, `NionDownload`,
`NionBookmark`, `NionClearSiteRequest`, `NionClosedTab`,
`NionPermissionPrompt`, `NionSecurityLevel`, site rule formats, URL/port
ranges, `NION_*` session/download bounds, private-close modes/requests.

## `util.c`

`nion_profile_file_within_limit`, `nion_quarantine_profile_file`,
`nion_write_key_file_atomic`, `nion_format_bytes`, `nion_base64_state_looks_valid`,
`nion_validate_cookie_store`, IPv6 helpers.

## `navigation.c`

`nion_uri_is_http_clearnet`/`https_clearnet`, `nion_uri_is_onion`,
`nion_host_is_onion`, `nion_looks_like_host_port`, `nion_validate_uri`,
`nion_string_has_scheme`, `nion_scheme_is_internal_only`,
`nion_external_protocol_scheme`, `nion_uri_is_http_or_https`, mixed-content /
HTTPS-first predicates, local-network lock-down predicates.

## `per-site.c`

Per-site zoom (`nion_site_zoom_key_for_uri`, load/save/apply),
per-site JavaScript rules, content-blocking exceptions, autoplay exceptions,
`nion_clear_site_rules_for_host`, migration helpers.

## `permission.c`

`nion_permission_is_temporarily_allowed`,
`nion_allow_permission_mask_temporarily`,
`nion_clear_temporary_permissions_for_origin`,
`nion_security_default_javascript_enabled`, all-denied baseline helpers.

## `privacy.c`

`nion_apply_cookie_policy`, `nion_apply_privacy_settings`, ITP/third-party
cookie policy application.

## `content-filter.c`

`nion_content_filter_for_app`, `nion_content_filter_is_ready`,
`nion_content_filter_has_failed`, filter store lifecycle.

## `tor-core.c`

`nion_start_tor`, `nion_cleanup_stale_tor`, Tor child I/O,
`nion_store_tor_log`, `nion_read_tor_line`, Tor-state transitions,
dead-SOCKS replacement (`socks://127.0.0.1:9`), callback notifications
through `NionTorCallbacks`.

## `network.c`

`nion_prepare_network`, `nion_apply_network_proxy`, WebKit network-session
ephemeral/persistent split, `download-started` signal wiring.

## `session.c`

`nion_save_session`, `nion_load_session`, crash-recovery dialog flow,
`nion_reload_crashed_tab` support, private-session audit, session-file bounds
& quarantine, debounced save timer.

## `tabs.c`

Tab model ops (`nion_new_tab`, close/pin/reorder/duplicate), tab bar UI,
closed-tab stack & reopen, tab context menu, tab audio/mute indicators,
session integration (`nion_set_tab_pinned`, …).

## `webview.c`

All `on_webview_*` WebKit signal handlers (load-changed, decide-policy,
create, context-menu, permission-request, download, fullscreen, …) plus their
private helpers (`nion_http_origin_key`, `nion_web_process_recovery_html`,
`nion_permission_status_text`, …).

## `bookmarks.c`

`nion_bookmark_free`, bookmark model (title normalize, uri-exists, index),
`nion_save_bookmarks`, `nion_load_bookmarks`, bookmark window UI & row
actions, live search fold/match, `nion_update_bookmark_button`,
`nion_toggle_current_bookmark`, `nion_add_current_bookmark`,
`action_bookmark_page`, `action_bookmarks`, `on_bookmark_toolbar_clicked`.

## `downloads.c`

`nion_safe_download_filename`, `nion_unique_download_path`,
`nion_download_time_text`, download window builder & row factory, history
load/save, `on_download_*` signal handlers (started, decide-destination,
progress, received-data, failed, finished), action handlers (open/folder/
copy-link/retry/clear), `nion_cancel_active_downloads`,
`nion_private_cleanup_partial_downloads`, `action_downloads`,
`on_download_started` (WebKit network-session hook).

## `settings.c`

`nion_security_level_id/label/parse`, `nion_load_preferences`,
`nion_save_preferences`, `nion_apply_security_level_to_window`,
Preferences dialog handlers, `action_preferences`,
`on_preferences_cancel_clicked` (shared dialog closer),
`on_preferences_save_clicked`.

## `site-data.c`

`nion_tab_has_mixed_content`, `nion_site_info_row`,
`nion_permission_status_text`, Site Information window & update flows,
`nion_web_origin_key_for_uri`, clear-site-data request plumbing,
`nion_website_data_matches_host`, `nion_tab_matches_site_key`,
`nion_stop_capture_for_origin`, `nion_forget_site_local_state`,
`nion_clear_data_request_free`, `nion_clear_all_temporary_permissions`,
`nion_clear_selected_local_data`, browsing-data manager, `action_clear_*`,
`action_forget_site`, `on_site_*` handlers.

## `app.c`

`nion_prepare_dirs`, `nion_prepare_appimage_webkit_sandbox`,
`nion_set_tor_ready/progress/error`, window close/shutdown flow
(`on_window_close_request`, `nion_request_close`, `nion_finish_close`,
confirm dialog handlers), `nion_open_private_window`,
`nion_private_sync_from_owner`, `nion_sync_private_windows_tor`,
`nion_free_private_app_idle`, `on_shutdown`, `action_exit`,
`action_private_window`.

## `ui.c`

`nion_apply_css`, `nion_build_ui`, chrome/widget builders, address bar &
navigation handlers (`nion_go_to_address`, `on_address_activate`, back/
forward/reload/home), status/title/progress/control synchronizers,
`nion_update_onion_button`, tab action handlers (new-tab/close/reopen/
next/prev/focus-location/reload/hard-reload), zoom (`nion_set_zoom`,
`action_zoom_*`), find bar (`nion_find_*`, `on_find_*`), print/PDF,
fullscreen, Home/New-Tab/error pages (`nion_home_html`,
`nion_error_html`), `nion_show_error_page`, HTTP-warning & external-protocol
prompts (`nion_show_http_warning`, `nion_show_external_protocol_prompt`,
permission prompt), About dialog, Privacy Audit (`nion_audit_row`,
`action_privacy_audit`), `nion_load_home`, `nion_refresh_home_pages`,
`nion_prepare_normal_navigation`, `nion_reload_crashed_tab`,
`nion_stop_all_web_activity`, `nion_clear_retry`,
`on_permission_request` (WebKit).
