# NiOn 1.4.0 Testing

This document is the runtime checklist for the NiOn 1.4.0 Stable release. Static scripts are useful guards, but they do not replace live testing on the Linux system used to build/release the AppImage.

## 1. Static regression suite

Run:

```bash
./scripts/release-preflight.sh
```

The preflight verifies the centralized manifest, shell/XML syntax, fail-closed invariants, profile resilience, historical feature regressions, pinned tabs, Private Window isolation, private downloads, context-menu ownership/signature, and related-view new-window handling.

The dedicated feature checks can also be run individually:

```bash
./scripts/test-https-first-stage1.sh
./scripts/test-bookmarks-stage2.sh
./scripts/test-site-data-stage3.sh
./scripts/test-audio-stage4.sh
./scripts/test-tabs-stage1.sh
./scripts/test-page-actions-stage2.sh
./scripts/test-zoom-stage3.sh
./scripts/test-site-info-stage4.sh
./scripts/test-downloads-stage5.sh
./scripts/test-bookmark-search-stage6.sh
./scripts/test-pinned-tabs-stage1.sh
./scripts/test-private-window-stage2.sh
./scripts/test-private-downloads-stage3.sh
./scripts/test-private-session-audit-stage4.sh
./scripts/test-new-window-related-view.sh
./scripts/test-context-menu-ownership.sh
./scripts/test-context-menu-gtk4-signature.sh
./scripts/test-docs-stage5.sh
./scripts/test-site-controls-stage1.sh
./scripts/test-recovery-data-stage2.sh
./scripts/test-hardening-stage3.sh
./scripts/test-content-blocking-stage1.sh
```

## 2. Native build smoke test

```bash
rm -rf build
./scripts/run-dev.sh
```

Confirm:

- build completes without an error;
- NiOn opens;
- bundled Tor bootstraps;
- status reaches Tor connected;
- a clearnet HTTPS site loads;
- a Tor v3 `.onion` site loads.

Compiler deprecation warnings from GTK/WebKit headers are not equivalent to a build failure, but any new NiOn compiler error must be fixed before release.

## 3. Tor fail-closed test

While NiOn is running:

1. Load a working clearnet page.
2. Confirm a working `.onion` page.
3. Terminate the NiOn-owned Tor child process.
4. Attempt a new HTTP(S) navigation.
5. Confirm NiOn blocks the navigation rather than loading directly.
6. Confirm active tracked downloads do not continue through an alternate direct path.
7. Restart NiOn and confirm Tor can bootstrap again.

Run the static fail-closed helper as well:

```bash
./scripts/test-fail-closed.sh
```

## 4. Tab and new-window regression

Test normal tabs:

- `Ctrl+T` creates a tab;
- `Ctrl+W` closes the active tab;
- `Ctrl+Shift+T` restores the last closed tab;
- Duplicate Tab works;
- Close Other Tabs works;
- Close Tabs to the Right works;
- switching/reordering tabs does not display another tab's WebView.

Test web-created tabs:

- click a link using `target="_blank"`;
- exercise a page using `window.open()`;
- right-click a link and choose Open Link in New Tab;
- right-click plain text, a link, and an image.

None of these actions may crash NiOn.

## Home / Back navigation regression

1. Open a new tab.
2. Navigate to `https://google.com` (or another reachable test site).
3. Click the NiOn Home button.
4. Confirm the internal Home page appears and **Back is enabled**.
5. Click Back or press `Alt+Left`.
6. Confirm the tab returns to the page that was active before Home.
7. Repeat from a page that already has back-history; the first Back from Home must return to the immediately previous active page, then normal WebKit history should continue.

## 5. Pinned tabs

1. Open several tabs.
2. Pin at least two non-leftmost tabs.
3. Confirm pinned tabs move into the left block.
4. Confirm each pinned tab shows a `📌` marker.
5. Drag pinned tabs inside the pinned group.
6. Drag a normal tab toward the pinned group and confirm the pinned/normal boundary is restored.
7. Use Close Other Tabs and Close Tabs to the Right; pinned tabs must remain protected.
8. Explicitly close a pinned tab and restore it with `Ctrl+Shift+T`; it should return pinned.
9. Restart NiOn with session restore enabled; pinned state/order should return.

## 6. Private Window

Open a Private Window with `Ctrl+Shift+P`.

Verify:

- the window opens only after the private WebKit session passes the ephemeral-session/credential checks;
- clearnet and `.onion` browsing still requires Tor;
- normal bookmarks are visible;
- the selected search engine is shared;
- normal tabs/session state are not mixed with private tabs;
- private tabs are not restored after closing/restarting NiOn;
- private per-site zoom changes do not appear in `site-zoom.ini`;
- closing an individual private tab asks for No/Yes confirmation;
- bulk private close actions use one confirmation rather than one dialog per tab.

### Private cookie/site-data test

1. Sign into a test site in Private Window or create identifiable site state.
2. Close every Private Window.
3. Open a fresh Private Window.
4. Confirm the previous private login/site state is gone.
5. Confirm the same action did not erase the normal-window site's persistent state.

### Private closed-tab recovery

1. Close a private tab and approve the confirmation.
2. Press `Ctrl+Shift+T`; the tab may return while that Private Window remains alive.
3. Close the Private Window.
4. Open a new Private Window.
5. Confirm the old private closed-tab queue cannot be recovered.

## 7. Downloads

### Normal downloads

Verify:

- a download completes through NiOn;
- `Ctrl+J` shows it;
- Open File works;
- Open Containing Folder works;
- Copy Download Link works;
- a genuine failed download can be retried;
- a cancelled download is not offered as a failed retry;
- history remains after restart.

### Private downloads

1. Start a download in Private Window.
2. Confirm it appears only in Private Downloads.
3. Confirm normal `downloads.ini` does not receive that private entry.
4. Confirm Open File / Open Folder / Copy Link work during the live private session where applicable.
5. Start a large download and close Private Window before completion; it should be cancelled and its tracked partial file cleaned where possible.
6. Complete a private download and close Private Window; NiOn history disappears but the completed file remains on disk.
7. Confirm NiOn does not emit its normal desktop completion notification for private downloads.

## 8. Bookmarks and search

1. Bookmark a clearnet page with `Ctrl+D` or `☆`.
2. Bookmark an `.onion` page.
3. Confirm the toolbar changes to `★` for a saved URL.
4. Open Bookmarks and search by title.
5. Search by URL fragment.
6. Use multiple terms and confirm all terms must match the title/URL combination.
7. Rename and delete bookmarks while a search is active.
8. Restart NiOn and confirm bookmarks persist.
9. Confirm adding a bookmark from Private Window intentionally persists globally.

## 9. Page actions and zoom

Test:

- Find in Page (`Ctrl+F`);
- Reload and hard reload;
- Print / Save as PDF (`Ctrl+P`);
- Open Link/Image in New Tab from the page context menu;
- Save Image where WebKit offers it;
- zoom in/out/reset;
- restart and confirm normal per-site zoom persistence;
- `Ctrl+0` removes the site's stored override;
- HTTP/HTTPS on the same hostname share zoom;
- non-default ports stay distinct;
- Private Window does not persist/read normal per-site zoom state.

## 10. Site information and mixed content

Test an ordinary HTTPS page and confirm Site Information reports the expected host/address/Tor status.

Test an HTTP Tor v3 `.onion` URL and confirm it is identified as an Onion Service rather than falsely labeled HTTPS.

If a reproducible mixed-content test page is available, confirm the warning state appears for insecure displayed/active content and resets on a clean navigation.

## 11. Audio/mute

On a page that plays audio:

- confirm the tab audio indicator appears;
- mute/unmute that tab;
- switch tabs and confirm state stays per tab;
- restart a normal saved session and confirm the intended mute state is restored.

## 12. Site-data clearing

On a test site with cookies/storage:

1. Use **Clear Data for This Site…**.
2. Approve the confirmation.
3. Confirm site state is removed/reloaded as expected.
4. Confirm unrelated sites are not deliberately selected for clearing.
5. Separately test **Browsing Data…** for the global operation.

## 13. Profile recovery

Run:

```bash
./scripts/test-profile-recovery.sh
```

Also verify a normal upgrade/replacement preserves healthy profile data:

- cookies/login state;
- session tabs;
- bookmarks;
- downloads history;
- preferences;
- site zoom;
- Tor client state.

## 14. Network audit

While loading both clearnet and `.onion` pages:

```bash
./scripts/audit-network.sh 30
```

Review the observed sockets/processes. The expected browser-network path is through the local NiOn Tor SOCKS endpoint; no unexplained direct remote WebKit/browser connection should be accepted as normal.

## Final 1.4.0 hardening check

```bash
./scripts/test-hardening-stage3.sh
./scripts/release-preflight.sh
```

Both must pass before producing/uploading the release AppImage. Static PASS does not replace the live Tor, Private Window, permission, recovery, data-clear, Home/Back, and network-audit scenarios in this document.

## 15. AppImage validation

Build first:

```bash
./scripts/build-appimage.sh
```

Then:

```bash
./scripts/test-appimage.sh dist/NiOn-1.5.0-x86_64.AppImage
cd dist
sha256sum -c NiOn-1.5.0-x86_64.AppImage.sha256
```

Run the artifact:

```bash
./NiOn-1.5.0-x86_64.AppImage
```

or without FUSE:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./NiOn-1.5.0-x86_64.AppImage
```

Repeat the critical smoke tests against the AppImage itself: Tor bootstrap, clearnet, `.onion`, target-blank/new-tab, context menu, persistent normal session, Private Window ephemerality, and normal/private downloads.

## Site Controls — 1.4.0 Stage 1

Run:

```bash
./scripts/test-site-controls-stage1.sh
```

Runtime checks:

- open Site Information on two different sites and verify JavaScript state is site-specific;
- disable JavaScript for one normal site, restart NiOn, and verify the rule persists only for that site;
- in Private Window, disable JavaScript, close the Private Window, open a new one, and verify the rule is gone;
- verify camera, microphone, geolocation, and notification requests are blocked unless **Allow temporarily** is selected;
- allow a supported permission, reload/re-request it in the same window and verify the temporary grant is reused;
- close the window and verify the temporary grant is gone;
- reset temporary permissions from Site Information and confirm matching camera/microphone capture stops;
- verify screen/display capture remains blocked;
- verify `target=_blank`, right-click context menus, normal/private downloads, and Tor fail-closed behavior remain functional.


## Content Blocking — 1.5.0 Stage 1

Run:

```bash
./scripts/test-content-blocking-stage1.sh
```

Runtime checks:

1. Open a normal HTTPS site and click the Site Information button repeatedly. The transient Site Information window must open without flashing the browser window.
2. Confirm **Content blocking** is enabled by default and reports the bundled lightweight filter as ready.
3. Visit a site known to load common third-party advertising/tracking resources and compare behavior with Content blocking ON versus OFF for that site. Do not expect complete ad removal; this is a deliberately small ruleset.
4. Disable Content blocking for a normal site, restart NiOn, and confirm the exception persists only for that site.
5. Re-enable it and confirm the exception disappears.
6. In Private Window, disable Content blocking, close the Private Window, open a new Private Window, and confirm the exception is gone.
7. Use **Browsing Data…** to clear normal/private content-blocking exceptions and confirm the switches return to their default state.
8. Re-test `target=_blank`, `window.open()`, right-click link/image context menus, per-site JavaScript, normal/private downloads, and Tor fail-closed behavior.

The filter must not create a new network client or remote update path. All web requests that are not blocked must continue through NiOn's existing WebKit/Tor session.

## Release gate

Do not ship the 1.5.0 release if any of these remain reproducibly broken:

- direct-network fallback or fail-closed regression;
- Tor bootstrap/runtime packaging;
- context-menu crash;
- `target="_blank"` / `window.open()` crash;
- cross-tab WebView corruption;
- normal session/profile persistence;
- private session/history persistence leak;
- AppImage startup or missing runtime dependency;
- release manifest/version/AppImage naming mismatch.


## Recovery & Data Controls — 1.4.0 Stage 2

### Site Information window regression

1. Open a normal HTTPS page.
2. Click the information button beside Home repeatedly.
3. Confirm the popover stays open instead of flashing and immediately closing.
4. Toggle the JavaScript switch, close/reopen the popover, and confirm the state remains usable.
5. Repeat on a page with mixed-content warning if available.

### Unclean-shutdown recovery

1. Enable session restore and open several normal tabs, including at least one pinned tab.
2. Terminate NiOn uncleanly (for example, kill the NiOn process; do not use normal Exit).
3. Start NiOn again.
4. Confirm NiOn shows **Restore Tabs** / **Start Fresh** and does not navigate restored tabs automatically before a choice.
5. Choose **Restore Tabs** and confirm URLs, pinned state, and mute state recover after Tor is ready.
6. Repeat the unclean shutdown, then choose **Start Fresh** and confirm only a blank tab remains.
7. Restart normally and confirm the fresh state is now the saved session.
8. Corrupt a disposable copy of `session.ini`; confirm NiOn quarantines it and starts fresh rather than crashing.

### Browsing Data Manager

1. Open **Browsing Data…**. Confirm website data and Web cache are selected by default.
2. Clear only saved zoom levels; confirm open tabs reset to 100% and `site-zoom.ini` overrides are gone.
3. Create a per-site JavaScript disable rule, clear only JavaScript site rules, then confirm the site defaults to JavaScript enabled on the next/reloaded navigation.
4. Temporarily allow a permission, clear only Temporary site permissions, and confirm camera/microphone capture is stopped and the Site Information permission status returns to blocked-by-default.
5. Clear website data + cache and confirm logins/site storage/cache are removed while bookmarks, downloads history, and session tabs remain.
6. Repeat in Private Window and confirm no normal profile files are created or modified by private-only JS/permission clearing.


## Final 1.5.0 release audit

Before publishing 1.5.0 Stable:

1. Open Site Information on a 1366×768-class display or smaller. Confirm the window stays on-screen, is resizable, and the lower Address/permission controls are reachable with vertical scrolling.
2. Confirm Content blocking defaults ON, per-site normal exceptions persist, and Private exceptions disappear with the Private Window.
3. Confirm WebKit ITP is the default tracking mode and the strict third-party-cookie preference still switches to the explicit blanket policy.
4. Confirm audible autoplay is blocked by default, muted autoplay works, and per-site sound exceptions obey normal/private persistence rules.
5. Re-test Tor fail-closed, `target=_blank`, right-click context menus, crash recovery, Browsing Data, normal/private downloads, and Home → Back navigation.
6. Build the AppImage, run `./scripts/release-preflight.sh`, verify the SHA-256 file, and smoke-test the packaged AppImage.
