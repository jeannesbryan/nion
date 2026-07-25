# NiOn 1.2.1 Maintenance Testing

Before runtime tests:

```bash
./scripts/test-manifest-1.2.1.sh
./scripts/release-preflight.sh
```

Expected: the manifest test and release preflight pass. Confirm `About NiOn`, `release/manifest/NION_VERSION`, the generated AppImage filename, AppImage `BUILD-INFO`, and bundled Tor `MANIFEST.ini` all agree. Then run the 1.2.0 Stage 1–6 regression scenarios below.

Stage 6 keeps the Stage 1–5 behavior and adds local, in-memory search to the existing Bookmarks window. Search must not create network traffic or change the bookmark persistence format.

## Stage 6 quick checks

1. Add several bookmarks with clearly different titles and hosts, then open **Menu → Bookmarks**. Verify the search field and total bookmark count are visible.
2. Search using part of a bookmark title with different letter case. Verify matching is case-insensitive and unrelated rows disappear.
3. Search using part of the URL/hostname instead of the title. Verify the correct bookmark remains visible.
4. Enter two terms where one matches the title and one matches the URL of the same bookmark. Verify that bookmark remains; change one term so it matches nothing and verify the no-results message appears.
5. Clear the search using the search field's clear control and verify the complete bookmark list and normal total count return.
6. With the Bookmarks window active, start typing without first clicking the search field. Verify GTK key capture routes normal search text into the field.
7. While a filtered result is visible, test **Open**, **Rename**, and **Delete**. Verify the operation targets the correct bookmark and the filtered results/count refresh immediately.
8. Close and reopen the Bookmarks window. Verify the temporary query was cleared and all bookmarks are shown.
9. Restart NiOn and verify bookmarks themselves persist exactly as before; Stage 6 adds no new bookmark database/index file.
10. Stop/crash bundled Tor. Verify local bookmark search still works, but opening a bookmark remains blocked by the existing Tor-ready guard.
11. Re-test Stage 1–5: tab recovery/context menu, page actions/PDF, per-site zoom, site information/mixed content, and download actions/retry.

Static Stage 6 check:

```bash
./scripts/test-bookmark-search-stage6.sh
```

---

## Stage 5 quick checks

1. Complete a normal download, open `Ctrl+J`, click its **⋮** menu, and verify **Open File**, **Open Containing Folder**, and **Copy Download Link** are available.
2. Click **Open File** and verify the downloaded local file is handed to the desktop's registered application. Delete/move the file and verify the action no longer remains available after the row actions refresh.
3. Click **Open Containing Folder** and verify the local Downloads directory opens.
4. Click **Copy Download Link**, paste into a text editor, and verify it matches the source request URI.
5. Restart NiOn and verify a new Stage 5 history entry still offers source-dependent actions; an older history entry created before Stage 5 should load safely even without a `source-uri` key.
6. Produce a genuine failed download if practical. Verify **Retry Failed Download** appears, creates a new download row, and uses normal collision handling for its destination.
7. Cancel a download manually. Verify it is recorded as **Cancelled** but does **not** offer Retry Failed Download.
8. With a failed entry available, stop/crash bundled Tor and click Retry. Verify NiOn reports **DOWNLOAD RETRY BLOCKED** and does not start the retry.
9. Recover/restart NiOn with Tor working, retry the same failed URL, and run the normal network audit if practical to confirm WebKit traffic remains on NiOn's loopback Tor SOCKS route.
10. Re-test Stage 1–4: tab recovery/context actions, Print/Save PDF, per-site zoom persistence, site information, and mixed-content state.

Static Stage 5 check:

```bash
./scripts/test-downloads-stage5.sh
```

---

## Stage 4 quick checks

1. Open an HTTPS clearnet page and click the information button beside the Onion-Location area. Verify **Host**, **Connection**, **Route**, **Mixed content**, and **Address** are populated.
2. Verify a normally validated HTTPS page reports **HTTPS — TLS verified** after navigation commits.
3. Open a valid plain-HTTP Tor v3 `.onion` page. Verify the connection is identified as an Onion Service and is not falsely labeled HTTPS.
4. Open an HTTPS mixed-content test page that causes WebKit to report insecure displayed or active content. Verify the information icon becomes a warning icon and the active-tab status includes **MIXED CONTENT DETECTED**.
5. Navigate that same tab to a clean HTTPS page. Verify the previous mixed-content state is cleared when the new load starts.
6. Trigger mixed content in one tab, then switch to a clean tab and back. Verify each tab keeps its own connection/mixed-content state.
7. Open Home/New Tab or an internal NiOn error page. Verify the site-information button is disabled there.
8. Stop/crash bundled Tor and verify new browsing remains fail-closed; the information UI must not create a bypass path.
9. Re-test `Ctrl++`, `Ctrl+-`, `Ctrl+0`, restart NiOn, and verify Stage 3 per-site zoom memory still works.
10. Re-test Stage 1–2 tab recovery/context actions and Print/Save as PDF.

Static Stage 4 check:

```bash
./scripts/test-site-info-stage4.sh
```

---

## Stage 1 quick checks

1. Open three real web pages, close the newest tab, press `Ctrl+Shift+T`, and verify the URL reopens.
2. Close a New Tab/blank tab and verify `Ctrl+Shift+T` does not treat it as a recoverable website tab.
3. Close more than 10 website tabs and verify only the most recent 10 are recoverable.
4. Right-click a tab and test Reload, Duplicate Tab, Mute/Unmute, Close Tab, Close Other Tabs, and Close Tabs to the Right.
5. Close the last remaining tab and verify NiOn stays open with a fresh New Tab.
6. Verify clearnet, `.onion`, persistent login, bookmarks, downloads, HTTPS-First, and Clear Data for This Site remain functional.

Static Stage 1 check:

```bash
./scripts/test-tabs-stage1.sh
```

---

## Stage 1 — HTTPS-First for clearnet

Run the development build with:

```bash
rm -rf build
./scripts/run-dev.sh
```

Validate at minimum:

1. Enter a normal clearnet host without a scheme, for example `example.com`. The address must resolve to `https://example.com`.
2. Enter an explicit plain-HTTP clearnet URL. NiOn must show **Unencrypted clearnet connection** before the navigation is issued.
3. Choose **Cancel**. The HTTP target must not replace the current page.
4. Repeat and choose **Continue**. The original navigation must continue without being converted into a new GET request.
5. Reload or follow another plain-HTTP link on the same origin. NiOn should not prompt again while that HTTP origin remains active in the tab.
6. Navigate the tab to HTTPS, then return to the plain-HTTP origin. NiOn must warn again; the exception is not persisted.
7. Open a valid Tor v3 URL using `http://...onion`. The clearnet HTTP warning must not appear.
8. Verify HTTPS clearnet, `.onion`, Back/Forward, downloads, Onion-Location, persistent login, and session restore still work.
9. With Tor unavailable, plain HTTP and HTTPS navigation must remain fail-closed; the HTTPS-First warning must never bypass the Tor-ready guard.

---

# NiOn 1.1.0 Stable Test Matrix

1.1.0 is the stable release line built on NiOn 1.0.0 plus the four completed Everyday Privacy & Browsing stages. A test only passes when the observed result matches the expected behavior below. Keep the terminal log from `./scripts/run-dev.sh` when reporting a failure.

## 0. Preflight

```bash
rm -rf build
./scripts/run-dev.sh
```

Close NiOn normally after confirming it starts, then run:

```bash
./scripts/release-preflight.sh
```

Expected: `RELEASE PREFLIGHT: PASS`, except optional AppImage/container checks that have not been built/run yet may be reported as warnings.

## 1. Cold start / Tor bootstrap

Start NiOn with no existing NiOn process.

Expected:

- one NiOn window;
- bundled Tor starts;
- status progresses from `CONNECTING TO TOR` to `TOR CONNECTED`;
- browsing controls remain blocked until bootstrap reaches 100%;
- no system Tor configuration is required.

## 2. Tor bootstrap failure

After building, run the native binary directly with a deliberately failing Tor override:

```bash
NION_TOR_BINARY=/bin/false ./build/nion
```

Expected:

- NiOn stays fail-closed;
- status becomes `TOR ERROR` instead of connecting forever;
- entering a website does not create a direct connection;
- close NiOn and restart normally afterward.

## 3. Clearnet browsing

Open several HTTPS sites, including a JavaScript-heavy site.

Expected:

- pages load through Tor;
- Back / Forward / Reload / hard reload work;
- title/favicon update;
- normal login/session features still work.

## 4. `.onion` browsing

Open a known-good Tor v3 onion service repeatedly and from a fresh tab.

Expected:

- valid v3 address loads;
- invalid/legacy onion address is rejected locally;
- transient provisional cancellation may retry once automatically;
- no raw `Operation was cancelled` page should remain visible.

## 5. Multiple tabs / repeated creation and destruction

Create 30–50 tabs using `Ctrl+T`, navigate a subset, then close them in varying order.

Expected:

- no crash or obvious runaway UI lag;
- active-tab controls track the selected tab;
- closing the final tab creates a fresh New Tab instead of closing NiOn.

## 6. Login persistence / normal restart

Log into a test account on a website, close NiOn normally, then reopen it.

Expected:

- cookies/site data remain persistent;
- the site normally remains logged in unless the site itself invalidates the session;
- restored tabs and back/forward state remain usable.

## 7. Abnormal shutdown recovery

Open several tabs, then from another terminal identify the NiOn process and force-kill only NiOn:

```bash
pgrep -af '/nion($| )'
kill -9 PID
```

Restart NiOn.

Expected:

- the most recent dirty session snapshot is restored;
- stale NiOn Tor is conservatively cleaned up if necessary;
- an unrelated process is never killed from stale PID metadata.

## 8. Downloads

Test one successful download, one cancelled download, and if practical one interrupted download.

Expected:

- all downloads use the WebKit Tor-proxied network session;
- successful file lands in the Downloads directory;
- collision gets a numbered filename rather than overwrite;
- cancelled/failed partial destination is removed;
- `Ctrl+J` history records the outcome;
- completed rows expose Open File / Open Containing Folder / Copy Download Link as applicable;
- genuine failed rows expose Retry Failed Download, while cancelled rows do not;
- retry is blocked while Tor is unavailable and otherwise creates a fresh WebKit download;
- only the newest 500 completed/failed/cancelled history entries are retained.

## 9. Network interruption

While a page is loading, disconnect the machine from the network for a short period and reconnect it.

Expected:

- NiOn does not switch to a direct/default proxy;
- page may fail while Tor cannot reach the network;
- after connectivity/Tor recovers, reloading can work again;
- no crash.

## 10. Tor process crash / fail-closed

Start NiOn and wait for `TOR CONNECTED`, then run:

```bash
./scripts/test-fail-closed.sh 10
```

Expected:

- Tor child is stopped;
- NiOn changes to `TOR ERROR`;
- active page loads/downloads are stopped/cancelled;
- the test observes no established NiOn/WebKit TCP connection or concrete UDP peer after Tor shutdown;
- new HTTP/HTTPS navigation is blocked by policy;
- restart NiOn after the test.

## 11. Runtime leak sample

With NiOn connected, actively load one clearnet site and one onion site while running:

```bash
./scripts/audit-network.sh 30
```

Expected:

- observed NiOn/WebKit TCP peers point only to NiOn's selected local Tor SOCKS port;
- no concrete outbound UDP peer is observed for NiOn/WebKit;
- result is `PASS for this sample`.

A PASS is a sample, not a proof that every possible WebKit path is leak-free.

## 12. AppImage build / replacement

Build:

```bash
./scripts/build-appimage.sh
./scripts/test-appimage.sh
```

Expected:

- `dist/NiOn-1.1.0-x86_64.AppImage` is created;
- WebKit subprocess diagnostics pass;
- bundled Tor reports the pinned expected version;
- the profile replacement sentinel confirms old/new AppImage filenames resolve to the same external profile path.

Then manually replace the AppImage with another copy/name and verify existing login/session state survives.

## 13. Additional distro loader smoke test

With Docker or Podman:

```bash
./scripts/test-appimage-containers.sh
```

Expected: packaged extraction/loader diagnostics pass on the configured Debian, Ubuntu, and Fedora images. This is not a substitute for a real GUI desktop test.

## 14. Profile migration

Start 0.11.0 once with a normal profile, close it, then start 1.0.0 using the same account/home directory.

Expected:

- no versioned profile directory is created;
- cookies, website data, session, download history and preferences remain available;
- 1.0.0 accepts the existing session format.

## 15. Corrupted profile handling

After building and closing the normal NiOn instance:

```bash
./scripts/test-profile-recovery.sh
```

The script uses temporary isolated XDG directories and does not modify the real NiOn profile.

Expected: malformed session/preferences/download-history/cookie files are renamed with `.corrupt-...` suffixes and NiOn does not repeatedly crash on them.

## 16. Corrupted Tor state handling

This is best tested with a disposable copy of the NiOn Tor data directory or a separate test user/profile. Do not deliberately corrupt the only real Tor state you care about.

Expected when Tor itself reports a parse/corruption problem:

- NiOn attempts recovery only once per run;
- affected disposable cache/state files are moved under `tor/recovery-.../` rather than silently deleted;
- ordinary cache-related log lines alone do not trigger corruption recovery;
- if the recovery directory cannot be created, NiOn stops with an error rather than claiming recovery succeeded.

## 17. Close/crash cleanup

Close NiOn normally and inspect:

```bash
pgrep -af 'tor.*nion/tor'
```

Expected: no NiOn-owned Tor process remains after normal shutdown.

## Release decision

Do not call 1.1.0 release-ready if a reproducible failure remains in:

- fail-closed behavior;
- Tor bootstrap/runtime lifecycle;
- profile persistence/recovery;
- clearnet or onion browsing;
- WebKit AppImage subprocess startup;
- login persistence;
- downloads;
- crash recovery.

Non-critical cosmetic issues may be deferred, but 1.1.0 should contain no known network-routing regression.

## NiOn 1.1.0 Stage 2 — Simple Bookmarks

1. Open a normal HTTPS clearnet page and press `Ctrl+D`; status should report **BOOKMARK ADDED**.
2. Press `Ctrl+D` again on the exact same URL; NiOn should report **ALREADY BOOKMARKED** and not duplicate it.
3. Open the hamburger menu → **Bookmarks** and verify the saved title and URL.
4. Activate a bookmark row or click **Open**; it should open in a new NiOn tab and remain Tor-routed.
5. Click **Rename**, save a new title, close/reopen the Bookmarks window, and verify the title persists.
6. Restart NiOn and verify the bookmark remains in `~/.local/share/nion/bookmarks.ini`.
7. Add a valid Tor v3 `.onion` page; it should bookmark normally.
8. Try `Ctrl+D` on NiOn's blank New Tab/internal error page; it should not create a bookmark.
9. Click **Delete** and verify only that bookmark is removed.
10. Confirm Stage 1 HTTPS-First behavior, downloads, login persistence, session restore, and Onion-Location still work.

Static Stage 2 check:

```bash
./scripts/test-bookmarks-stage2.sh
```


## NiOn 1.1.0 Stage 3 — Bookmark Toolbar + Clear Data for This Site

### Bookmark toolbar improvement

1. Open New Tab: the bookmark button beside the address bar must be disabled.
2. Open a normal HTTPS page: the button becomes enabled and shows the unbookmarked state.
3. Click the button: status should report **BOOKMARK ADDED** and the icon should switch to its bookmarked/active appearance.
4. Reload the page, switch to another tab, then switch back: NiOn should still detect the saved exact URL and show it as bookmarked.
5. Click the active bookmark button again: the bookmark is removed and the icon returns to the unbookmarked state.
6. Add a bookmark with `Ctrl+D`: the toolbar icon must update immediately.
7. Delete the active page from the Bookmarks window: the toolbar icon must update immediately.
8. Verify New Tab, blank pages, and internal error pages cannot be bookmarked.

### Clear Data for This Site

1. Sign in to or create persistent site data on Site A.
2. Also sign in to Site B so there is unrelated persistent data to protect.
3. On Site A choose **Menu → Clear Data for This Site…**.
4. Verify the dialog names Site A/current URL and warns that the current site may be signed out.
5. Cancel once and verify nothing changes.
6. Repeat and choose **Clear Site Data**.
7. After success NiOn should reload the active Site A page without cache; cookies/storage for Site A should be gone where WebKit attributes them to the matching host/domain record.
8. Visit Site B and verify its unrelated login/site data remains.
9. Run the action on New Tab: no website data should be cleared and NiOn should report that the tab has no site data.
10. Confirm the existing **Clear Browsing Data…** global action still works independently.

Static Stage 3 check:

```bash
./scripts/test-site-data-stage3.sh
```


## NiOn 1.1.0 Stage 4 — Tab Audio Indicator & Mute

1. Open a page that plays normal HTML5 audio/video. The tab should show a speaker icon once WebKit reports audio playback.
2. Click the speaker icon: audio should mute only in that tab and the icon should switch to the muted state.
3. Click it again: audio should resume and the icon should return to the speaker state.
4. Open two audio-playing tabs. Mute only one and verify the other remains audible.
5. Switch repeatedly between tabs and verify each tab keeps its own mute state.
6. Restart NiOn with session restore enabled and verify a muted restored tab remains muted.
7. Open a fresh tab and verify it does not inherit another tab's mute state.
8. Close an audio tab and verify no indicator/state leaks into other tabs.
9. Enter fullscreen and return; tab audio behavior should remain intact.
10. Open the hamburger menu and verify **Bookmark This Page** is gone, while the toolbar bookmark button, `Ctrl+D`, and **Bookmarks** manager still work.
11. Regression-check Stage 1 HTTPS-First, Stage 2 bookmarks, Stage 3 site-data clearing, downloads, Onion-Location, login persistence, and Tor fail-closed behavior.

Static Stage 4 check:

```bash
./scripts/test-audio-stage4.sh
```


## NiOn 1.2.0 Stage 2 — Page Actions

### Better Web Page Context Menu

1. Right-click a normal hyperlink and verify the menu says **Open Link in New Tab** rather than Open in New Window.
2. Activate it and verify the target opens as a new NiOn tab, not an external/default OS browser.
3. Verify **Copy Link Address** still works.
4. Right-click an image and verify **Open Image in New Tab** and **Save Image** are available when WebKit exposes those stock actions.
5. Activate **Save Image** and verify the file uses NiOn's normal Downloads pipeline (`Ctrl+J`) and remains Tor-routed.
6. Select text and right-click; WebKit's **Copy** action should remain available.
7. Right-click editable text/input and media elements and verify the normal WebKit editing/media controls were not removed.

### Print / Save as PDF

1. Open a normal clearnet page and press `Ctrl+P`. The WebKit/GTK print dialog should appear.
2. Cancel once; NiOn should continue browsing normally.
3. Reopen the print dialog and use the system **Print to File** / PDF option if available; verify a PDF is created.
4. Repeat from an `.onion` page; printing should use the already rendered page and must not alter Tor routing.
5. Right-click the page and verify **Print / Save as PDF…** appears at the bottom of the page context menu.
6. Verify hamburger menu → **Print / Save as PDF…** triggers the same dialog.
7. On NiOn's New Tab page, `Ctrl+P` should not start a print job.
8. Regression-check Stage 1 tab recovery/context menu, NiOn 1.1.0 HTTPS-First/bookmarks/site-data/audio features, downloads, session restore, and fail-closed behavior.

Static Stage 2 check:

```bash
./scripts/test-page-actions-stage2.sh
```


## NiOn 1.2.0 Stage 3 — Per-Site Zoom Memory

1. Open `https://example.com`, press `Ctrl++` several times, and note the zoom percentage.
2. Open another tab to the same hostname and verify the remembered zoom is applied after the page commits.
3. Restart NiOn, revisit the same hostname, and verify the zoom survives restart.
4. Visit the same hostname over explicit `http://` (where safe/available and after NiOn's HTTPS-First warning) and verify it shares the hostname zoom.
5. Visit a different hostname and verify it remains at 100% until changed.
6. Test a Tor v3 `.onion` site, change zoom, open the onion in another tab, and verify the same remembered value is applied.
7. When a test service is available on a non-default port, verify `example.test:8443` can keep a zoom value distinct from the default-port site.
8. On the remembered site press `Ctrl+0`, close/reopen the tab, and verify it returns to 100% because the override was removed.
9. Change a site's zoom, then navigate that tab to NiOn Home/New Tab; the internal page must render at 100%. Navigate back to the site and verify its stored zoom returns.
10. Trigger a NiOn internal error page from a failed navigation and verify the error UI renders at 100% without overwriting the site's stored zoom.
11. Inspect `~/.config/nion/site-zoom.ini`: it should be mode `0600`, contain only non-default remembered values, and remain outside the AppImage.
12. Corrupt a disposable copy of `site-zoom.ini` or make it exceed 1 MiB, start NiOn, and verify the invalid file is quarantined with a `.corrupt-...` suffix and browsing continues with default zoom.
13. Regression-check Stage 1 tab recovery/context menu, Stage 2 page actions/printing, NiOn 1.1.0 HTTPS-First/bookmarks/site-data/audio, downloads, session restore, and Tor fail-closed behavior.

Static Stage 3 check:

```bash
./scripts/test-zoom-stage3.sh
```
