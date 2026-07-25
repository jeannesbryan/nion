# NiOn 1.3.0 Testing

This document is the final runtime checklist for the 1.3.0 release. Static scripts are useful guards, but they do not replace live testing on the Linux system used to build/release the AppImage.

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
5. Separately test **Clear Browsing Data…** for the global operation.

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

## 15. AppImage validation

Build first:

```bash
./scripts/build-appimage.sh
```

Then:

```bash
./scripts/test-appimage.sh dist/NiOn-1.3.0-x86_64.AppImage
cd dist
sha256sum -c NiOn-1.3.0-x86_64.AppImage.sha256
```

Run the artifact:

```bash
./NiOn-1.3.0-x86_64.AppImage
```

or without FUSE:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./NiOn-1.3.0-x86_64.AppImage
```

Repeat the critical smoke tests against the AppImage itself: Tor bootstrap, clearnet, `.onion`, target-blank/new-tab, context menu, persistent normal session, Private Window ephemerality, and normal/private downloads.

## Release gate

Do not publish 1.3.0 if any of these remain reproducibly broken:

- direct-network fallback or fail-closed regression;
- Tor bootstrap/runtime packaging;
- context-menu crash;
- `target="_blank"` / `window.open()` crash;
- cross-tab WebView corruption;
- normal session/profile persistence;
- private session/history persistence leak;
- AppImage startup or missing runtime dependency;
- release manifest/version/AppImage naming mismatch.
