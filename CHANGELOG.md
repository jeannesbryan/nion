# Changelog

## 2.3.7 — Development

A data saver that blocks requests instead of hiding things.

The obvious way to save data is a stylesheet that hides images. It saves nothing:
by the time CSS hides an image the bytes have already been downloaded, so the
page costs exactly as much and merely looks cheaper. NiOn blocks the requests
themselves, through WebKit's resource-type filter, so the bytes never travel.
That is the only thing worth calling a data saver on a slow connection.

Three resource types are blocked: image, media and font. Scripts and stylesheets
are deliberately left alone, because blocking those changes what a page does
rather than what it costs, and a broken page is not data saved: it is a page the
user has to reload without the feature.

The choice is per tab, because "this page is heavy" is a property of a page, not
of a session. Turning it on reloads the tab, since a filter only affects requests
made after it is applied, and the choice is re-applied on every navigation for
the same reason. A tab tracks two flags, not one: what the user asked for and
what the WebView actually has. They differ when the rules failed to compile or
the tab is showing an internal page, and that difference is what stops the UI
from reporting a saving that is not happening.

When the rules cannot compile, the toggle refuses and says so in the status line
rather than flipping a switch that does nothing.

### Added

- **`src/data-saver.c`**: the rules as WebKit content-blocker JSON, the per-tab
  toggle, and the apply/remove logic, using the same filter store, the same
  remove-then-add pattern and the same private-window borrowing as the existing
  content blocker.
- **Menu entry and Ctrl+Shift+D**, offered in every window including a private
  one, with the label following the current tab's state and saying "unavailable"
  when the rules failed.
- `scripts/test-data-saver-stage1.sh`: rule assertions, wiring assertions, two
  mutations, an internal-page refusal check in the real window, and the part that
  matters, described next.

### Measured, not assumed

"Compiling a filter" only proves the JSON parsed. The test therefore loads a real
page that references an image, a web font and a video, and counts what a real
server received:

- control run, filter off: the document, the image and the font all arrive.
- filtered run: the document arrives, the image and the font never do.

The control is what makes the result mean anything. Without it, "nothing reached
the server" could equally mean the harness never loaded a page, and an earlier
attempt at this measurement did in fact produce exactly that false comfort when
one of the two test servers had not come up; the failure was invisible because
nothing checked the server was listening. The script now waits for each port to
answer before using it.

The mutations are judged by the same measurement rather than by the source text:
weakening the rules until the image arrives, or narrowing them to images only
until the font arrives, must both bring a request back.

Not measured, and stated as such: the video (media) case is blocked by the same
rule as the image and the font but is not separately asserted, because the
harness page has no autoplay and the request timing is unreliable.

## 2.3.6 — Development

The start page now reports what the browser is actually doing about privacy.

The governing rule is that every figure on it comes from something NiOn really
knows. A privacy page showing a plausible "1,482 trackers blocked" that was never
counted is worse than no page at all: it teaches the reader to trust a number
that means nothing. So where NiOn does not measure something, the dashboard says
so in words instead of inventing a value. "Requests blocked: Not counted" is a
worse-looking line than a big number and a more honest one, and it is what makes
the other tiles worth believing.

Tiles are built from state the browser already keeps: Tor state and bootstrap
percentage, the local SOCKS address actually in use, third-party cookie policy,
whether the content filter loaded, the security level, per-site overrides,
temporary permission grants, how much history and reading list exist on disk, and
how many tabs are open or suspended.

Two things it deliberately does not soften:

- A failed Tor reports "Unavailable" and says in the same sentence that a failed
  connection is never replaced by a direct one. A failure that reads as fine
  would undo the whole point of a fail-closed browser.
- A filter list that failed to load reports "Not active", not "Active". Claiming
  a protection is running when it is not is the one lie this page cannot afford.

A private window is handled by absence rather than by zero: it reports that no
site overrides are kept and that nothing from this window is written, instead of
"0 history entries", which would read as a measurement of a store that does not
exist there.

### Added

- **`src/privacy-dashboard.c`**: a GTK-free module that turns a facts struct into
  tiles with a value, an explanation, and an honesty marker. Every tile is either
  KNOWN, UNKNOWN, or UNAVAILABLE, and the summary counts only the tiles that
  carry a value.
- **Dashboard on the start page**, replacing the three hardcoded tiles, with
  attention states drawn distinctly. Tile text is escaped before it reaches the
  HTML.
- `scripts/test-privacy-dashboard-stage1.sh`: 50 behavioural checks linked
  against the production module, the same run under AddressSanitizer, wiring
  assertions (page renders the module, text escaped, module freed, private window
  keeps no counts), six mutations, and a real-window pass that renders the start
  page and reads the tile count the page actually logged.

### Notes

- Two mistakes of mine, both caught by the harness rather than by review: the
  window pass first never launched the app at all (a rewrite dropped the call, so
  the log file did not exist), and the "private window claims disk writes"
  mutation survived because the assertion only looked for the word "Nothing" and
  accepted "Nothing unusual". Both are fixed; the assertion now requires the
  specific claim.
- The window pass verifies the rendered page through a debug line the renderer
  emits, because the page itself is not written to disk. A tile built correctly
  and then dropped by a format-string mistake still leaves the user without a
  dashboard, and nothing else in the suite would notice.

## 2.3.5 — Development

Address bar homograph protection, computed on this machine.

Cyrillic "а" (U+0430) and Latin "a" are different characters that look the
same, so "аpple.com" with a Cyrillic first letter is a different host from
"apple.com" while reading identically. The address bar now says so.

Everything is decided from the address itself: the ASCII form of a name is well
defined (IDNA/punycode), and characters belong to scripts. Nothing is fetched, no
reputation service is asked, and no part of the address leaves the browser. That
is the only kind of protection this browser can honestly offer, and it is the
kind that keeps working with no network.

Two signals are used. First, Latin beside Cyrillic or Greek inside a single label
is the classic construction, because those scripts look alike and are not written
together. Second, a label written entirely in another script where every
character has a Latin lookalike reads as Latin while resolving elsewhere, which
the first signal alone would miss. Every label is examined, not only the first,
because an imitation can sit under a plain-looking subdomain.

The other direction matters just as much. A legitimate international name must
not be accused, or the warning becomes noise people learn to skip. Japanese
writes Han beside Hiragana and Katakana, Korean writes Han beside Hangul, and
Russian is Cyrillic throughout: all of those are a notice, not an accusation.
Only a script mix that should not occur, or a label built from lookalikes, is
called suspicious. A plain ASCII host produces nothing at all.

Two things the window test found that reading the source would not have:

- **The address bar holds the punycode form.** WebKit normalizes a homograph URL,
  so the bar reads "xn--pple-43d.com" and the Cyrillic letter is nowhere in the
  text. Analyzing only the ASCII form reported a harmless "encoded name" for an
  active imitation. The decoded name is now analyzed with the same rules, which
  is what actually catches it in the product.
- **Only the loaded page was inspected.** On a failed or blocked load the page
  never arrives, and that is exactly when an imitation matters: an attacker does
  not need their page to render for the name to be believed. The notice now
  follows the address bar as it is typed.

### Added

- **`src/idn-guard.c`**: a GTK-free module that extracts the host from an address
  and returns a verdict (plain, encoded, or international; safe, notice, or
  suspicious) with the ASCII form, the decoded form, the scripts involved and one
  sentence of explanation. The ASCII form comes from GLib's punycode
  implementation, and the tests check it against published vectors rather than
  against whatever the code produces.
- **Address bar notice**: an international address is outlined, a suspected
  homograph is outlined harder, and the tooltip carries the sentence and the
  ASCII form. Colour is never the only signal.
- **Site information row** ("Address name"), hidden for ordinary hosts so a
  normal page shows the same panel as before.
- `scripts/test-idn-guard-stage1.sh`: 91 behavioural checks, wiring assertions
  (notice reaches the address bar, panel row hidden for plain hosts, every label
  examined), six mutations including the two that matter most (ASCII letters not
  counted as Latin, and every IDN treated as suspicious), and a real-window pass
  that opens the app on a plain address and then on a Cyrillic one and checks
  both directions in the log.

### Fixed

- **ASCII letters were not recorded as Latin script.** The first version only set
  a flag for them and left the script mask alone, so "Cyrillic letter plus Latin
  letters" looked like a single-script label and the classic attack passed
  unnoticed. The unit test caught it before any UI existed; a mutation now guards
  it permanently.

## 2.3.4 — Development

The tab list can be filtered, so thirty open tabs stop being a wall.

The overflow menu names every open tab, which is enough for three and useless
for thirty: finding one of them meant reading the whole list. It now has a field
at the top. Typing narrows the list live, the heading says how many of how many
survived, and a query that matches nothing says so instead of showing an empty
box that looks broken.

Every word of the query must match, and each word may match either the tab title
or its address. Requiring all of them is what makes typing narrow: with "any
word" the list would sometimes grow as you typed, and a filter that moves in both
directions is a filter nobody trusts. Matching is case-folded with
g_utf8_casefold, so a title in another script behaves the way a reader expects
rather than only working for ASCII.

The field sits above the scroll area and outside the container that is rebuilt on
every keystroke, so a rebuild can never destroy the widget being typed into. Each
time the menu opens it starts from the full list: keeping a stale filter would
silently hide tabs from someone who opened the menu to find one, which is the
exact problem the menu exists to solve.

Escape in the field empties it, and on an already empty field it closes the menu.
GtkSearchEntry only clears, so without that the key everyone reaches for left the
menu looking stuck open.

### Added

- **`src/tab-filter.c`**: a GTK-free matching module (active-query test,
  case-folded per-term matching across title and address).
- **Filter field** in the overflow menu, with a live heading
  ("3 of 14 tabs match") and an explained empty result.
- `scripts/test-tab-filter-stage1.sh`: 40 behavioural checks linked against the
  production `src/tab-filter.c`, wiring assertions (field outside the rebuilt
  container, reset on open, keyboard reachable from the field), five mutations,
  and a real-window pass that types into the actual GTK menu with xdotool and
  watches the row count narrow.

### Notes

- The window pass found two things a source-level test could not: a filtered row
  still activates and switches tabs (tab 0 to tab 1), and reopening the menu
  really does restore the full list after the filter held text when it closed.
- NiOn already logs two GTK criticals about `GtkImage` sizing during the first
  layout of a window. Measured against an unmodified build of the previous
  commit, so they are pre-existing and unrelated to this change; the new test
  asserts its own run reports none, which is the property that matters here.

## 2.3.3 — Development

Pages can be saved for later and read with no network at all.

The obvious way to build this is to keep a copy of the page. That copy would
keep its images, stylesheets and frames too, so opening it later would quietly
try to fetch them, and "saved for offline reading" would be a label rather than a
fact. A browser whose entire premise is that nothing leaves without Tor should
not ship an offline feature that needs the network to look right.

So a saved page is text. Choosing Save for Later takes the title, the address and
the page text, with the scripts, styles, frames and navigation chrome stripped
out, and stores that. Reading it back draws the text in a GtkTextView rather than
in a WebView, so opening a saved page cannot issue a request even by accident.
The reading window says plainly that images are not kept, because leaving that
unsaid would make the first read feel broken.

Saving the same address twice refreshes the entry instead of adding a second
copy: the newer extraction may hold the full article where the first save caught
a page that had only half loaded. Newest saves come first. The list is capped,
each entry is capped, and the file is rewritten atomically so a crash mid-save
cannot corrupt a list that was fine.

A Private Window gets no reading list at all, and no file path to one. What
someone saves while browsing privately is precisely the trail the window exists
to avoid, so the refusal is explicit as well as structural, and the menu does not
offer the actions in that window.

A page that yields almost no text is refused rather than saved: a login wall or
an error page is not reading material, and an entry that disappoints when opened
is worse than a clear "nothing worth saving here".

### Added

- **`src/reading-list.c`**: the store (GKeyFile written atomically), the same
  URI gate the history store uses, whitespace normalization with UTF-8-safe
  truncation, a word count, a worth-saving threshold, and a byte-exact parser for
  the payload the page returns.
- **Save for Later** (Ctrl+Shift+S) and **Reading List** (Ctrl+Shift+B), with menu
  entries shown only where they can work.
- **The offline reader**: a transient window with the saved text, its address,
  when it was saved, and how many words.
- `scripts/test-reading-list-stage4.sh`: 80 behavioural checks linked against the
  production `src/reading-list.c`, the same run under AddressSanitizer, wiring
  assertions (text-only reader, private window has no store, gate applied to save
  and load, no accelerator collisions), and ten mutations, each required to fail.

### Fixed

- **A leak in the text normalizer**, found by the sanitizer build of the new
  test: hitting the entry size cap broke out of the loop before the current line
  was freed. The mutation check for the payload parser had to be judged under
  AddressSanitizer too, because dropping its length check reads outside the
  buffer rather than returning a different value: a wrong return value can be
  asserted on, undefined behaviour cannot.

## 2.3.2 — Development

Tor is slow by nature, and NiOn used to hide that behind a single percentage.
Now it says what Tor is doing.

The bootstrap log line carries both facts:

```
Bootstrapped 45% (requesting_descriptors): Asking for relay descriptors
```

NiOn keeps only the number before this change, so a bootstrap that sat at 45%
for half a minute looked exactly like a browser that had stopped working. The
wording in the status line, on the start page, and in the tooltip of every
waiting tab is now Tor's own sentence, taken from the line it just printed.
That is deliberate: a private table of phase names in NiOn would go stale the
first time Tor renamed a phase, while the text that Tor sends cannot describe
Tor incorrectly.

A tab opened while Tor is still bootstrapping now shows a small spinner and a
tooltip explaining that it is waiting, instead of a blank page that could not be
told apart from a hang. It disappears on its own when Tor connects.

A failed or timed-out bootstrap used to leave no way forward: browsing stayed
blocked, which is correct, but the only escape was quitting the browser. The
start page and the Tor error page now offer "Try Tor again". It stops the failed
daemon before starting a new one, so a retry can never leave two Tor processes
racing for the same SOCKS port, and it refuses to run while Tor is healthy or
still working. Like every other action that changes the machine's state, it
needs a click: a page cannot trigger it.

### Added

- **`src/tor-status.c`**: a GTK-free module that parses Tor's bootstrap lines
  (percentage, tag, description), classifies the phase, and phrases the status
  line, the tab label and the blocked-navigation explanation. Unknown phases
  deliberately say nothing rather than guessing.
- **`nion_retry_tor()`** in `src/tor-core.c`: a deliberate restart after a
  failure, reusing the port-conflict and corrupt-state recovery path.
- **Waiting-tab indicator**: a spinner in the tab strip plus a tooltip that
  names the current phase, refreshed on every Tor state change and whenever a
  tab is created.
- `scripts/test-tor-status-stage3.sh`: 108 behavioural checks whose fixtures are
  log lines captured from the bundled Tor binary (0.4.9.16), wiring assertions
  (phase reaches the UI, retry cannot stack daemons, retry needs a gesture,
  no bypass phrasing), and seven mutations, each required to fail.

## 2.3.1 — Development

The address bar now completes what you type without asking anyone else about it.

Every other browser that suggests as you type sends the partial query somewhere
before you press Enter, which tells a third party what you were considering
reading even if you then change your mind. NiOn does not. The candidates are the
local history store, the bookmark list, and the tabs that are already open in
this window, and there is no code path from a keystroke to the network. A miss
returns nothing at all, which is the honest answer: NiOn does not know the page
you are thinking of, and it will not go and ask.

Ordering is decided by how the address was typed. An address whose beginning
matches comes first, then an address with the query somewhere inside it, then a
title where every word matches. Inside one tier an open tab outranks a page you
visited, which outranks a bookmark, and a more recent visit outranks an older
one. Ties fall back to the order the candidates were collected in, so the list
does not reshuffle between keystrokes and a row does not move out from under the
pointer. One row per address: the same page is often a tab, a history entry and
a bookmark at once, and three copies of it would only push the other matches off
a list capped at eight rows.

Only `http` and `https` addresses are ever offered, through the same gate the
history store already uses. A suggestion therefore cannot hand the address bar a
scheme NiOn refuses to browse, and the same rule is applied to bookmarks and to
open tabs rather than to history alone.

The list is drawn under the toolbar, not floated over the page. An overlay
popover above an accelerated WebKitGTK view flickers badly on lightweight X11
window managers, which is why the site information panel is a separate window;
reusing that lesson cost a strip of vertical space while the list is open and
saved a class of rendering bugs this machine cannot even photograph.

Filling the address bar from code is separated from typing. Switching tabs or a
page finishing a load writes the bar through one setter, and that setter
suppresses the list, so the browser never appears to be completing an address
nobody typed.

### Fixed

- **A use-after-free in the suggestion model, found before the model was wired
  to anything.** `nion_autocomplete_suggest()` ranked a candidate collection and
  then released it, but the ranked array only borrowed the items it returned, so
  the caller was handed pointers into freed memory. The collection's free
  function destroyed every item on unref. `suggest()` now returns an array that
  owns its items, and the ownership contract is checked under AddressSanitizer
  rather than by reading the code.

### Added

- **Address bar autocomplete** (`src/autocomplete.c`): a GTK-free ranking model
  (prefix, substring, title terms; source priority; recency; per-URI dedupe;
  result cap) plus a collector that reads history, bookmarks and open tabs
  through the history URI gate.
- **Suggestion list** in `src/ui.c`: a `GtkRevealer` and `GtkListBox` under the
  toolbar, opened by typing, navigated with the arrow keys, activated by click
  or Enter, dismissed by Escape, by an empty result, or by clicking the page.
  Each row shows the title, the address, and where the suggestion came from.
- `nion_set_address_text()`: the single entry point for filling the address bar
  from code, so a programmatic fill can never open the list.
- `scripts/test-autocomplete-stage2.sh`: 49 behavioural checks linked against
  the production `src/autocomplete.c`, the same run repeated under
  AddressSanitizer, wiring assertions that forbid writing the address bar
  outside the setter, and nine mutations (duplicate rows, flattened prefix tier,
  single-term title match, history gate dropped, bookmark gate dropped, open tab
  gate dropped, reversed recency, reversed source priority, borrowed result),
  each required to fail.

## 2.3.0 — Development

Local browsing history, and four modules that had been wired to nothing since
the 2.0.0 modularization.

History is a local file and nothing else. It is not synced, never leaves the
device, records only `http` and `https` pages, and refuses every other scheme
instead of trying to be clever about it. Entries are deduplicated by address, so
revisiting a page moves its row to the front and increments a counter rather
than appending another copy; the store is capped at 2000 entries and anything
older than 90 days is dropped, both on load and on every write, so the file
cannot grow without bound. A page that reports its title after the load finished
gets its placeholder replaced, but a title that was already good is never
overwritten by a later worse one.

Three rules decide where the history is *not* written. A Private Window never
receives the history path at all, so even a mistaken call inside the module
cannot record private browsing; the module also refuses on `is_private`
explicitly, because the guard should not depend on the missing path staying
missing. New Identity deletes the store, since a "new identity" that keeps the
old URL trail only renames the old one. "Forget This Site" drops that host's
entries along with the per-site rules and permissions, because a forget that
leaves the address behind is not a forget.

The window lists what was visited with the time and visit count, searches over
title and address, opens a row in a new tab through the normal Tor-gated path,
and can delete one entry or clear everything behind a confirmation. `Ctrl+H`
opens it. A Private Window has no menu entry, because it has no history to show.

### Fixed

- **Four modules had callback hooks that nothing ever filled in.** Bookmarks,
  downloads, settings and site-data each kept a `cb` struct and a
  `nion_<module>_set_callbacks()` hook, but `main.c` never called the hook, so
  every pointer stayed NULL and the thunks silently returned a NULL tab or
  dropped the status text. Measured on the 2.2.6 binary:
  `objdump -d build/nion | grep -c 'call.*nion_bookmarks_set_callbacks'` was 0.
  The user-visible consequences: the bookmark star was permanently insensitive
  and Ctrl+D, "Bookmark Current Page" and "Open" in the bookmarks window did
  nothing; download "Retry" always answered "no active tab"; the site switches
  read a NULL tab and showed the wrong state, and clearing a site's data never
  reloaded it; preference changes never refreshed the controls. The new
  `scripts/test-module-callbacks-stage1.sh` requires every declared hook to be
  called from `main.c` and every registered struct to be fully initialised, and
  it carries a built-in mutation check so a detector that stops detecting fails
  loudly. Run against the 2.2.6 tree it reports exactly those four modules.

### Added

- **History store** (`src/history.c`): atomic `history.ini` in the profile,
  2000 entry cap, 90 day retention, dedupe on revisit, http/https only,
  corrupt-file quarantine, and one gate (`nion_history_uri_is_recordable`) that
  every stored address passes through.
- **History window**: search, open, delete one, clear all with confirmation,
  `Ctrl+H`, and a menu entry that is absent in Private Windows.
- `scripts/test-history-stage1.sh`: 60 behavioural checks linked against the
  production `src/history.c`, plus wiring assertions and four mutations
  (private recording, no expiry, clear leaving the file, non-http accepted),
  each required to fail.

### Changed

- `NION_MAX_HISTORY`, `NION_MAX_HISTORY_FILE_BYTES`, `NION_HISTORY_RETENTION_DAYS`
  and `NION_HISTORY_FORMAT` live in `src/types.h` next to the other profile limits.

## 2.2.6 — Stable

A session that came back with the right shape and the wrong addresses, a tab
strip that could not name its own tabs, the find bar position counter, and three
Onion-Location gaps found by comparing NiOn against what Brave and Tor Browser do
with the same header.

The session bug was the one worth the release. A restored tab is a shell: its
address waits in the tab until Tor finishes bootstrapping, and the startup save
runs inside that window. The save path resolved addresses by hand and never asked
the tab for its pending one, so every tab was written without an address. Closing
or crashing before bootstrap finished replaced the whole session with blank tabs,
while the tab count, the active index and the pinned and mute flags all came back
correctly. A four-tab session seeded with three URLs was measured coming back as
four tabs with zero addresses. Both recording paths now go through one helper that
owns all four address sources, so a caller cannot forget one of them again.

The tab strip had the opposite problem: nothing was lost, but nothing could be
found. Past the point where tabs stop fitting, GTK 4 scrolls the strip and gives
the off-screen tabs no way to be identified, so tab 12 of 14 meant clicking an
arrow and reading until it appeared. The strip now has a menu that lists every
open tab, marks the current one and the ones the strip cannot show, and jumps to
whichever row is activated.

### Changed

- **The find bar reports the position, not just the total.** It used to say
  "17 matches" and leave you guessing where you were; it now says "3 of 17".
  WebKit exposes only a total through `found-text`, never which match is
  selected, so the position is tracked in `NionApp`. Two details matter:
  `found-text` also fires for next/previous, so a flag distinguishes a fresh
  search from a step (without it the counter would snap back to 1 on every
  click), and the wraparound is done by hand to match WebKit's own
  `WRAP_AROUND`, so stepping past the last match shows "1 of 17" instead of
  "18 of 17". When WebKit stops counting at `G_MAXUINT` the label reads
  "3 of 1000+" rather than pretending to a precision the browser does not have.

  One behaviour was found only by running the browser, and would have shipped
  as a visible bug: on WebKitGTK 2.52.6, `found-text` reports `match_count` as
  **1** for a next/previous step, no matter how many matches the document has.
  Measured on the start page: a fresh search for "Tor" reported 5, then the
  first step reported 1. Assigning that straight to the total made the counter
  flip to "1 of 1" on the very first click, destroying the feature the moment
  it was used. The total is now accepted only from a search that started fresh.
  The wiring test asserts that rule, and reintroducing the bug fails it.

### Added

- **`src/find-label.c` plus `scripts/test-find-label-stage8.sh`, 22 checks.**
  The formatting and stepping rules were pulled out into a glib-only file so a
  harness can compile the production source and exercise it, instead of the
  rules being buried in GTK callbacks that only a human clicking a window can
  reach. The script also asserts the wiring that a harness cannot see: the
  fresh-search flag is set and consumed, both step handlers go through the
  shared helper, closing the bar clears the state, and the old "N matches"
  wording is gone. Verified red first: dropping the clamp, dropping the
  forward wraparound, removing the fresh-search flag, and restoring the old
  label format each make the script fail.

### Fixed

- **The advert is only honoured on a 2xx document response.** NiOn read the
  `Onion-Location` header from whatever the main resource returned, including a
  404 or 500. Error handlers routinely reuse the site template, so a missing
  page could still advertise its `.onion` twin and send a reader from a page
  that does not exist to one that may not either. `webkit_uri_response_get_status_code()`
  is now checked before the header is read, and the HTML meta fallback sits
  behind the same gate so it cannot slip through on an error page.

### Changed

- **The onion badge carries a drawn onion mark instead of a bare text label.**
  The mark is drawn with cairo rather than shipped as an asset, because an asset
  would need a meson install rule plus an icon-theme lookup that has to survive
  the AppImage mount. Rendered at the real 14 px and magnified to check, three
  thin concentric arcs antialias into one muddy band and the gaps made it look
  like a bucket; a single inner arc landed as a smudge. A solid silhouette with
  a pointed top is what stays crisp, so that is what shipped. The word "Onion"
  stays next to it, which is what makes the badge readable.

### Added

- **`scripts/test-onion-location-stage7.sh`, 6 checks.** Guards the status gate
  (including the ordering rule that the status is read before the header and
  that the meta fallback sits after the gate), the drawn mark, and the safety
  rules that must survive: https clearnet only, valid `.onion` candidate, and
  preferred-onion persistence only from fresh detection. Verified red first:
  removing the 2xx gate, removing the `page_ok` gate, and restoring the bare
  text button each make the script fail.

### Fixed

- **A restored session keeps its addresses before Tor bootstraps.** A tab brought
  back at startup is a shell: its address waits in `tab->restore_uri` until Tor is
  ready and navigation starts. `src/session.c` resolved the address itself and
  never consulted that field, while `src/tabs.c` already did, so the save that runs
  immediately after restore wrote every tab without a `uri=` line. Measured before
  the fix: four tabs seeded with three URLs returned as `tab-count=4` with zero
  addresses. Closing or crashing inside the bootstrap window (5 to 20 seconds on
  this host) then replaced all restored tabs with blank ones, and the tab count,
  active index, pinned and mute flags all survived, which is what made the damage
  easy to miss. Both recording paths now resolve addresses through
  `nion_tab_recorded_uri()`, which owns the four sources (display override, live
  WebView, pending restore, suspended tab). Verified against a real Tor bootstrap
  as well: two public addresses stayed in `session.ini` after the pages loaded,
  WebKit history blobs included. A session file pointing at `127.0.0.1` is still
  refused by the Tor-only policy and becomes a blank tab, which is the correct
  outcome and was confirmed by tracing the file rather than assumed.

- **`scripts/test-session-restore.sh`.** Drives the real binary instead of grepping:
  seeds a four-tab session (one pinned, one muted, one blank), runs NiOn with
  `NION_TOR_BINARY=/bin/false` so Tor never becomes ready, waits for the startup
  save to appear, then asserts every address and flag survived. Verified red first:
  removing the pending-restore branch, letting `about:blank` through, and preferring
  the live address over the pending one each make it fail.

### Added

- **The tab strip reports every tab, not just the visible ones.** An overflow
  control at the end of the strip opens a menu listing all open tabs, with the
  current tab and the tabs the strip cannot show both marked, pinned and suspended
  markers preserved, and `Ctrl+Shift+A` as a shortcut that does not depend on
  seeing the button. A restored tab that never finished loading has no title, so
  those rows show the host of the address they were restored from instead of
  "New Tab".

  Which tabs are hidden is measured, not estimated. Measured on GTK 4 with the real
  layout at 1100 px: a tab that does not fit is not allocated at all and reports
  width 0, while visible tabs report real bounds. The first implementation compared
  each label's right edge against the notebook width minus a guessed arrow-width
  constant; the guess was unnecessary and wrong, reporting 0 hidden while only 6 of
  14 tabs were on screen. Hidden now means "no allocated area".

  Two bugs surfaced only by running it. `gtk_notebook_set_action_widget()` owns a
  single slot per pack type, so adding the overflow button and then the new-tab
  button meant the second call destroyed the first: the menu still built its rows,
  but popping it up hit a freed object and only a `Gtk-CRITICAL` showed for it. And
  a popover attached only with `gtk_widget_set_parent()` had no surface to open
  against (the parent lookup returned NULL), so it is a `GtkMenuButton`, the same
  pattern the download rows already use.

- **`scripts/test-tab-overflow.sh`.** Asserts the structure, then drives the real
  window: three tabs (nothing hidden, one row each), fourteen tabs (hidden >= 1, one
  row each, and activating a row must land on a tab the strip cannot show), and no
  `Gtk-CRITICAL` during the run. Verified red first: forcing the hidden count to 0,
  restoring the second action-widget call, dropping the tab switch, and leaving the
  popover un-rebuilt on open each make it fail. The toolbar button itself is not
  driven end to end, because synthetic clicks do not reach this widget tree here
  (mouse events are dropped even for the new-tab button); its show handler is held
  by a static assertion, and that gap is recorded in the test rather than left
  silent.

## 2.2.5 — Stable

A false error page, and a Tor bump that brings the daemon to 0.4.9.16.

Following a link into `https://jeannesbryan.my.id/nostr` showed "Website
unavailable through Tor / Frame load interrupted", and only a manual reload
opened the page. The site is not at fault. Its directory URL is answered with a
301 to the trailing-slash form, and that redirect downgrades the scheme to
`http://`. NiOn's HTTPS-first rule then ignores the `http://` policy decision
and starts the `https://` twin, which is exactly what it should do.

What went wrong is the bookkeeping. WebKitGTK 2.52 reports the frame it just
dropped as `WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE`, and
the load-failed handler only knew `WEBKIT_NETWORK_ERROR_CANCELLED` as a
non-failure. So a healthy `https://` load, already in flight, was replaced by an
error page built from an error that described NiOn's own upgrade.

Both codes are now classified in one place, `src/load-error.c`, which the test
harness compiles from the production source. The classification had to be moved
before any error page is built, so the ordering is asserted too: calling the
helper with anything other than the real error object compiles cleanly and
silently restores the bug.

Tor runtime: Expert Bundle 16.0.1, daemon 0.4.9.15 to 0.4.9.16. The release
notes for 0.4.9.16 are mostly directory-authority work, but this bundle is the
first one that carries it, and staying on 0.4.9.15 meant missing the 0.4.9.13
and 0.4.9.14 security releases as well.

## 2.2.4 — Stable

Reader Mode, bound to Ctrl+Alt+R and available from the menu. It attaches a user
style sheet to the tab rather than rewriting the page, so it survives navigation
until it is switched off and leaves the site untouched when it is. The sheet
hides navigation, asides and footers, then gives the remaining text a serif face
at a readable measure.

Ctrl+Shift+R was the first binding, and it did nothing: hard reload already owned
that combination, and a duplicate accelerator loses silently. Found only by
running the app and pressing the key, which is why the guard test now compares
the two accelerator strings instead of just checking that a binding exists.

## 2.2.3 — Stable

Security release for the bundled Tor runtime. No source changes: every line
below is a version bump and its verification, so the 2.2.0/2.2.1/2.2.2 behaviour
guarantees are untouched.

### Updated dependencies

- **Tor runtime moved to Expert Bundle 16.0:** Tor daemon 0.4.9.14 →
  **0.4.9.15**. The 0.4.9.15 ChangeLog (2026-10-28) opens with "This is another
  security release that includes new DoS defenses against large volumes of
  directory requests received by relays", and closes its summary with "This
  release primarily affects relays so please upgrade as soon as possible."
  That framing matters for reading the risk honestly: NiOn runs Tor as a
  client (`ClientOnly 1`), never as a relay, so the new `DoSBeginDirRate*`
  defences do not add client-side protection. What does touch a client:
  - **TAP keys are gone from microdescriptors.** Directory authorities now
    require the `DESC_NO_TAP` / `MICRODESC_NO_TAP` capabilities, so **Tor
    0.4.8 and earlier no longer work on the network at all**. NiOn ships
    0.4.9.x and was already unaffected, but anyone pinning an older runtime
    would have found it unable to build a circuit.
  - **Scheduler ordering bugfix (`smartlist_pqueue_remove`, bug 41428,
    bugfix on 0.2.2.7-alpha).** Schedulers for circuits within a channel and
    for channels on the internet connection could serve items out of order,
    temporarily starving higher-priority circuits. This is a client bug, not a
    relay one.
  - Corrected handling of the middle-only flag at directory authorities
    (bug 41457, bugfix on 0.4.9.14, so a direct follow-up to what 2.2.2
    shipped), a conflux/control-port `ATTACHSTREAM` fix (bug 41454) and an
    onion-service control-port memory leak in `HSPOST` (bug 41455). Neither of
    the last two applies to NiOn: it opens no control port, by design.
- **Why "15 → 16" looks bigger than it is:** Expert Bundle 16.0 rides a major
  Tor Browser release (Firefox ESR 153, a redesigned Settings surface), but
  NiOn never takes the browser from that bundle. It takes `tor/` and
  `pluggable_transports/` only, so none of the Firefox-side changes are
  reachable from NiOn's GTK 4 + WebKitGTK UI. The bundle's own layout is
  unchanged (flat: `data/ debug/ docs/ tor/ pluggable_transports/`), so
  `TOR_RUNTIME_LAYOUT` stays at 2.
- **Deliberately not bumped:** GTK, GLib and WebKitGTK. None of them is pinned by
  this repository — the release AppImage is built inside the pinned
  `ubuntu:24.04` container and takes those libraries from there, so their
  versions are an artifact of the build image, not of a manifest value.
  `GTK_TESTED_VERSION` / `GLIB_TESTED_VERSION` / `WEBKITGTK_TESTED_VERSION` are
  records of what the current release was verified against, not requests.
  Raising WebKitGTK to 2.54 would mean moving the build image and re-checking
  both the glibc floor promise and the 4 GB memory target first.

### Verification

- Expert Bundle 16.0 fetched through `scripts/fetch-tor-runtime.sh`, which
  reported a **Good signature** from `Tor Browser Developers (signing key)`
  (primary fingerprint `EF6E286DDA85EA2A4BA7DE684E2C6E8793298290`, matching
  `TOR_SIGNING_FINGERPRINT`), sha256
  `cc950ebe9acb80188325c3a2cd8817def7dd7237d6fdcd861bb87b234a0cb2fd`, and a
  bundled daemon reporting `Tor version 0.4.9.15 (git-4d8efff53a86670b)`.
- The signature is made by subkey `022DA248432D2A0E0F54E65E316C1FACD62D07D9`,
  which is newer than the subkey that signed 15.0.26. It is a subkey of the
  pinned primary key, not a replacement for it — worth stating because a
  keyring that only carried the older subkey would report a failure that is
  not a compromise.
- The bundled 0.4.9.15 was also run directly, outside NiOn, before this
  release: it bootstrapped from an empty `DataDirectory`, `check.torproject.org`
  returned `{"IsTor":true,"IP":"204.8.96.68"}`, a clearnet page loaded with
  HTTP 200, and a Tor v3 onion service loaded with HTTP 200 in 4.5 s. No
  warnings were logged.
- `--verify-config` accepts NiOn's exact torrc in both shapes: direct
  (`ClientOnly`, `SafeSocks`, `ClientRejectInternalAddresses`,
  `ClientDNSRejectInternalAddresses`) and bridge mode
  (`UseBridges` + `ClientTransportPlugin obfs4 exec …/lyrebird`). No obsolete
  options were reported, so 16.0 does not invalidate any stored bridge set.
- Bridge transports still shipped by the bundle: `lyrebird` (obfs4,
  webtunnel, meek_lite) and `conjure-client` — all four transports remain
  available.

## 2.2.2 — Stable

Security release for the bundled Tor runtime. No source changes: every line
below is a version bump and its verification, so the 2.2.0/2.2.1 behaviour
guarantees are untouched.

### Updated dependencies

- **Tor runtime moved to Expert Bundle 15.0.26:** Tor daemon 0.4.9.13 →
  **0.4.9.14**. This is not a routine bump — the 0.4.9.14 ChangeLog opens with
  "Another week, another security release... We strongly recommend upgrading as
  soon as possible" and carries **TROVE-2026-067** (the `MiddleOnly` flag was
  ignored on non-authority relays, so a client could use a MiddleOnly relay for
  a role it was not meant to hold in onion-service circuits) and
  **TROVE-2026-062** (a router descriptor was treated as unexpired even after
  its family certificate had expired), plus a conflux/LINK bug that could
  trigger a fatal assertion on relays. Both Troves affect onion-service paths,
  which is precisely what NiOn is built to use.
- **Correction to 2.2.1's note:** that entry said 15.0.24 was the newest release
  and that there was no 15.0.25. Both statements were true when written; they
  are not true now. 15.0.25 and 15.0.26 have since been published, and
  15.0.24 has been removed from `dist.torproject.org` (HTTP 404). It remains
  available from `archive.torproject.org`, which is the host
  `fetch-tor-runtime.sh` actually uses, so a clean clone can still fetch 15.0.24
  if a checkout pins it. Do not "fix" that script by pointing it at `dist`.
- **Deliberately not bumped:** GTK, GLib and WebKitGTK. None of them is pinned by
  this repository — the release AppImage is built inside the pinned
  `ubuntu:24.04` container and takes those libraries from there, so their
  versions are an artifact of the build image, not of a manifest value.
  `GTK_TESTED_VERSION` / `GLIB_TESTED_VERSION` / `WEBKITGTK_TESTED_VERSION` are
  records of what the current release was verified against, not requests.
  Raising WebKitGTK to 2.54 would mean moving the build image and re-checking
  both the glibc floor promise and the 4 GB memory target first.

### Verification

- Expert Bundle 15.0.26 fetched through `scripts/fetch-tor-runtime.sh`, which
  reported a **Good signature** from `Tor Browser Developers (signing key)`
  (primary fingerprint `EF6E286DDA85EA2A4BA7DE684E2C6E8793298290`, matching
  `TOR_SIGNING_FINGERPRINT`), sha256
  `19cf18726eecb362e0f21d27afa9ea9efe5da928bfdfbb1ceb5fe54519e87f4a`, and a
  bundled daemon reporting `Tor version 0.4.9.14`.
- The bundled 0.4.9.14 was also run directly, outside NiOn, before this release:
  it bootstrapped to 100%, `check.torproject.org` returned
  `{"IsTor":true,"IP":"192.42.116.94"}`, a clearnet page loaded with HTTP 200,
  and a Tor v3 onion service loaded with HTTP 200. No warnings were logged.
- Bundle layout is unchanged (flat: `data/ debug/ docs/ tor/
  pluggable_transports/`), so `TOR_RUNTIME_LAYOUT` stays at 2.

### Upgrade note: stale Tor cache can trip the bootstrap timeout

Measured while testing this release's AppImage on the build machine, and worth
knowing before reporting a hang:

| Tor `DataDirectory` | time from 0% to 100% |
|---|---|
| fresh (empty) | **47 s** |
| a copy of an 18-day-old profile | **200 s** |

The old profile carried an unused `cached-microdescs` file of 22 MB dated nine
days earlier plus a 20 MB `.new` from an interrupted run. NiOn's fail-closed
bootstrap guard gives Tor **120 seconds** and then stops it, so an installation
with a cache in that state can hit the timeout even though nothing is wrong
with Tor itself. Deleting `cached-microdescs`, `cached-microdescs.new` and
`cached-microdesc-consensus` from the Tor data directory clears it — those are
download caches, not user data: cookies, logins, bookmarks and per-site rules
live elsewhere in the profile and are untouched. Tor re-fetches them on the next
start. This is a pre-existing condition, not a change in 2.2.2; the timeout
value is deliberately left alone because it is a fail-closed guard.

## 2.2.1 — Stable

Dependency refresh only. No source changes: every line below is a version bump
and its verification, so the 2.2.0 behaviour guarantees are untouched.

### Updated dependencies

- **Tor runtime moved to the current Tor Browser release:** Expert Bundle 15.0.23 → **15.0.24**, Tor daemon 0.4.9.12 → **0.4.9.13**. Checked against `dist.torproject.org` (15.0.24 is the newest release; there is no 15.0.25) and fetched with the usual GPG verification against the pinned signing key. Tor 0.4.9.14 exists as a standalone daemon release but is not what the bundle ships, and the manifest must describe the bundle — `fetch-tor-runtime.sh` refuses to proceed unless the bundled `tor --version` matches, so the pair cannot silently drift.
- **Deliberately not bumped:** GTK, GLib and WebKitGTK. None of them is pinned by this repository — the release AppImage is built inside the pinned `ubuntu:24.04` container and takes those libraries from there, so their versions are an artifact of the build image, not of a manifest value. `GTK_TESTED_VERSION` / `GLIB_TESTED_VERSION` / `WEBKITGTK_TESTED_VERSION` are records of what the current release was verified against, not requests. Raising WebKitGTK to 2.54 would mean moving the build image and re-checking both the glibc floor promise and the 4 GB memory target first.

### Verification

- `scripts/fetch-tor-runtime.sh` fetched Expert Bundle 15.0.24, verified its signature, and reported the bundled daemon as `Tor version 0.4.9.13`.

## 2.2.0 — Stable

### Censorship circumvention (bridges)

- **Bridges / pluggable transports:** a new **Bridges (Censorship Circumvention)…** dialog (menu ☰) reaches the Tor network through bridges — unlisted relays that disguise Tor traffic — for networks where Tor is blocked outright. NiOn bundles the pluggable transports already; this release adds the configuration on top.
- **Every transport the verified bundle ships:** `obfs4`, `webtunnel` and `meek_lite` (via `lyrebird`) plus `conjure` (via `conjure-client`). The dialog lists which ones the running build actually provides.
- **Fail-closed:** with bridge mode enabled, an empty list, a malformed line, or a missing transport makes NiOn refuse to start Tor altogether. It never silently falls back to a bridge-less Tor connection — the exact outcome a user in a censored network is trying to avoid.
- **Validated bridge lines:** a line must name a bundled transport and is rejected outright if it contains control characters or torrc keywords, so a stored line cannot inject an extra Tor directive into NiOn's torrc.
- **No new attack surface:** bridges are pure torrc; NiOn still opens no Tor control port.
- **Routing change, not identity change:** applying a new bridge set restarts Tor through the normal fail-closed path while keeping cookies, logins and per-site rules — unlike **New Identity**, which is deliberately still available next to it.
- Bridge settings persist in `bridges.ini` (0600, size- and entry-capped, corrupt state quarantined and never partially applied); Private Windows inherit them and never write them.

### Release engineering

- **Supported C library floor is now enforced, not just documented:** `scripts/check-glibc-floor.sh` reads the required `GLIBC_*` symbol versions out of every ELF in the AppDir and in the finished AppImage, and fails the build when the requirement exceeds `release/manifest/GLIBC_FLOOR` (2.39). This turns the 2.1.0 `GLIBC_2.43 not found` loader failure into a build-time error.
- **The AppImage explains itself instead of dumping loader errors:** `AppRun` records the required glibc in `usr/lib/nion/GLIBC-REQUIRED` and, on an older system, prints the two versions plus concrete options instead of one raw loader error per bundled library.
- **Cross-distribution smoke test is now wired in:** `scripts/test-appimage-containers.sh` extracts and starts the AppImage inside the pinned floor image (Ubuntu 24.04), Debian stable and Fedora. It runs from `release-preflight.sh` and, with `NION_REQUIRE_CONTAINER_TEST=1`, from CI where it is mandatory. The check existed since 2.1.0 but was never called — which is why the broken artifact shipped.
- **Pinned container release builds:** `scripts/build-release-container.sh` builds the AppImage inside the pinned release environment (`ubuntu:24.04`), verifies the image's glibc is at or below the floor *before* building, and re-checks the artifact afterwards. Works with rootless `podman` or `docker`.
- **CI verifies its own environment:** the release workflow refuses to run on a runner whose glibc is newer than the supported floor, since that would silently produce an unsupported AppImage.
- **Release baseline now describes reality:** the documented baseline is the pinned build environment — GTK 4.14.5, WebKitGTK 2.52.6, GLib 2.80.0, glibc 2.39 (Ubuntu 24.04 LTS / Linux Mint 22.x) — instead of version numbers that no shipped artifact was ever built against.

### Dependencies

- **Tor runtime updated to the current Tor Browser release:** Expert Bundle 15.0.19 → **15.0.23**, Tor daemon 0.4.9.11 → **0.4.9.12**. Both are downloaded from the Tor Project and verified against the pinned signing key (`EF6E286DDA85EA2A4BA7DE684E2C6E8793298290`) at build time. The bundle still ships `lyrebird` and `conjure-client`, so all four bridge transports remain available.
- The GTK/GLib/WebKitGTK/libsoup versions are unchanged by design: they come from the pinned release environment (GTK 4.14.5, WebKitGTK 2.52.6, GLib 2.80.0) so that the AppImage keeps working on systems at the documented glibc 2.39 floor. Chasing newer upstream GNOME/WebKit releases here would mean dropping the floor.

### Bug fixes and maintenance

- **The repository can be built from a clean clone again.** `.gitignore` ignored all of `/release/`, which swallowed `release/manifest/` — the files `meson.build` and every script read their version from. A fresh clone could not configure, fetch Tor, or produce a source archive. The rule now ignores only the generated archives.
- **Corrupt configuration no longer reaches Tor:** a bridge file that fails validation is quarantined and bridge mode falls back to disabled-with-no-bridges rather than starting with a partial set.
- Removed the obsolete `WarnUnsafeSocks 1` option from the generated torrc **and** from `scripts/diagnose-tor.sh`; Tor 0.4.9 reports it as obsolete and skips it, so it only added log noise. `SafeSocks` — the actual protection — remains.
- Replaced the deprecated `gtk_css_provider_load_from_data()` with `gtk_css_provider_load_from_string()` where GTK ≥ 4.12 provides it, keeping the GTK 4.10 compatibility path.
- Stopped calling the deprecated, no-op `webkit_settings_set_enable_hyperlink_auditing()`, which produced a warning on every tab in WebKitGTK ≥ 2.52; the call is kept for older WebKit where it still honours the setting.
- The About dialog now reports bridge mode, the C library floor, and the dependency baseline.
- The 2.1.0 feature regression suites no longer pin the release version, so a version bump cannot silently retire them; release-specific metadata assertions live in `test-hardening-stage3-<version>.sh`.
- Added `scripts/test-bridges-stage1.sh` and `scripts/test-hardening-stage3-2.2.0.sh`. The bridge suite drives the real validator and torrc builder through a new in-binary self-check (`NION_BRIDGE_SELFCHECK=1`) instead of grepping for them, and asserts behaviourally that the generated torrc contains no control-port or injected SOCKS directive.

# Changelog

## 2.1.0 — Stable

### Privacy & anonymity

- **Tor New Identity (circuit rotation + clean slate):** a single action (menu ☰ → New Identity, `Ctrl+Shift+U`, or the Start Page button) stops the bundled Tor, rotates its guard state aside for fresh circuits, wipes every per-site behavioral rule (zoom, JavaScript overrides, content-blocking exceptions, autoplay allowances) and all temporary permissions across the normal window and every open Private Window, asynchronously purges cookies/storage/caches through WebKit's data manager (gated so the new circuit never sees stale site data), then restarts Tor through the fail-closed path — every window rides `connecting → bootstrapping → ● TOR CONNECTED`. Deliberately adds no Tor control-port surface.
- **Strict HTTPS:** clearnet `http://` navigation is auto-upgraded to its `https://` twin before anything loads. Only when a site genuinely has no HTTPS or serves an invalid certificate does NiOn refuse the silent downgrade and surface an explicit plain-HTTP warning (per-tab, per-origin temporary allowance preserved). `.onion` services are never upgraded — plain HTTP by design, end-to-end inside Tor.
- **Onion-Location memory:** a freshly advertised Onion-Location is remembered per clearnet site (`preferred-onion.ini`, 0600, size/entry-capped, quarantine-on-corruption), so repeat visits can open the `.onion` twin in a new tab with one click even when the page stops advertising. Forget Site and New Identity clean-slate both drop the mapping.
- **Privacy Dashboard Start Page:** the internal home page now mirrors live state — Tor connection status, current Security Level, active tab count, and a live "Discarded" count — and carries a user-gesture-gated **↻ New Identity** button (`nion://new-identity`).

### Memory & performance (4 GB friendly)

- **Background Tab Discard (auto-suspend):** a background tab idle past 5 minutes is softly unloaded — its heavy page is released from the shared WebKit web process by swapping to a tiny internal "💤 Suspended" document while the lightweight tab shell, strip entry, title, pinned and mute state survive. Clicking the tab (or switching to it) revives it through the normal Tor-gated loader. A 💤 chip marks suspended tabs, the 15-second sweep only ever touches non-current / non-pinned / non-audio / real-URI tabs, and session restore persists the *real* URI (never the placeholder). Verified safe against WebKitGTK's shared-process model: tabs share one WebProcess, so discard unloads rather than killing (a kill would take sibling tabs down).
- **Always-on memory-pressure shedding:** native `WebKitMemoryPressureSettings` are installed before the first network session — a 1.5 GB working-set cap with 0.33/0.50 release thresholds, kill disabled, and a 2-second poll (WebKit's default is 30 s) so memory freed by a discard returns to the OS within seconds. No Preferences toggle: every NiOn user gets the optimized lightweight experience by default.

### Hardening

- HTTPS-first and escape-guard invariants preserved through all of the above; `.onion` and Tor-internal HTTP are excluded from strict-HTTPS upgrade; double failure-callbacks (TLS + load-failed) are suppressed so the plain-HTTP refusal appears exactly once.

## 2.0.0 — Stable

### Modular architecture

- Completed the decomposition of the former single-file `src/main.c` (~10.8k lines, ~86% of `src/`) into focused modules, each with a matching `.h` interface and a one-directional dependency policy.
- `src/types.h` — shared types/constants; `src/util.c` — pure helpers; `src/navigation.c` — URI/protocol predicates.
- `src/per-site.c`, `src/permission.c`, `src/privacy.c`, `src/content-filter.c` — leaf site/permission/privacy/content-filter state.
- `src/tor-core.c`, `src/network.c` — bundled-Tor subprocess lifecycle, dead-SOCKS fail-closed routing, and download wiring; decoupled from the UI through a `NionTorCallbacks` status/error/progress surface.
- `src/session.c`, `src/tabs.c`, `src/webview.c`, `src/bookmarks.c`, `src/downloads.c`, `src/settings.c`, `src/site-data.c` — session/tabs/webview/bookmarks/downloads/preferences/site-data feature domains.
- `src/app.c` — application lifecycle (dirs, sandbox, Tor-state coordination, close/shutdown, private windows); UI touchpoints injected via `NionAppLifecycleCallbacks`.
- `src/ui.c` — core chrome (toolbar/window builder, remaining actions, dialogs, find/zoom/print, status/title/progress/control synchronizers); main.c tab/status/action-table implementations injected via `NionUiCallbacks`.
- `src/main.c` is now the coordinator core (~600 lines): entry point, `on_activate` callback registration, GAction table, webview-creation hub, and security glue (content filter / onion-location / fail-closed policy).
- Kept the security invariants intact through the refactor: fail-closed Tor routing, Private Window isolation, all-permissions-denied baseline, URI protocol boundary, and local-network lock-down are unchanged and still covered by the static/runtime suites.
- Centralized static test greps on `src/*` (recursive over `.c`/`.h`) so the suite stays meaningful after extraction; every code-location guardrail is green.
- Retired the "god file"; see `docs/split-main-c-plan.md` for the full module map and extraction record.

# 1.8.0 — Stable

This release focuses on vital memory safety improvements, stability fixes, and Tor process lifecycle enhancements.

## Bug Fixes & Improvements
* **WebKit Context Memory Safety:** Fixed potential fatal crashes (Segmentation Faults) and internal WebKit status corruption by removing manual destruction on the default WebKit context during application teardown.
* **Compilation Error Fix:** Resolved build errors (`expected 'gpointer' but argument is of type 'int'`) caused by type mismatches inside `g_clear_object` macros.
* **Tor Process Lifecycle:** Refactored background Tor process cleanup to ensure `SIGTERM` signals are properly delivered and memory objects are safely unreferenced, preventing GLib dangling pointers.
* **Code Refactoring:** Streamlined `nion_free_private_app_idle` cleanup routines to strictly adhere to GObject memory management best practices.

## 1.7.0 — Stable

- Finalized **Security Levels & Escape Guards** after the Stage 1/2 runtime validation cycle.
- Kept minimum API requirements at GTK 4.10 and WebKitGTK 2.40 instead of unnecessarily raising distro compatibility requirements.
- Added centralized stable dependency-baseline metadata for GTK 4.22.4, WebKitGTK 2.52.5, and GLib 2.88.2; About and AppImage `BUILD-INFO` expose baseline/actual runtime provenance separately.
- Final release guidance prefers stable GTK/GLib branches over GTK 4.23.x / GLib 2.89.x development branches.
- Added a final 1.7.0 hardening regression guard and integrated it into release preflight.
- Completed final audit of Security Levels, external-protocol escape boundary, popup/new-window user-gesture enforcement, normal/private persistence, Tor fail-closed behavior, content/tracking protection, crash recovery, Site Information sizing, and manifest/source-archive integrity.

- Added **External Protocol Guard**: HTTP/HTTPS stay inside NiOn; selected non-web schemes require a direct user gesture plus an explicit Cancel/Open Anyway warning before the desktop handler can run.
- Kept `file:`, `javascript:`, `data:`, `blob:`, `about:`, and `nion:` out of desktop delegation.
- External-protocol requests without a user gesture are blocked at WebKit policy level, and the confirmation explicitly states that external applications are outside NiOn's Tor guarantee.
- Added popup/new-window defense in depth: `NEW_WINDOW_ACTION` and the WebView `create` callback both require `webkit_navigation_action_is_user_gesture()`, while user-clicked `target=_blank` continues to open in a related NiOn tab.
- Kept automatic JavaScript window opening disabled at every Security Level.
- Added Stage 2 static regression coverage and release-preflight integration.
- Added global **Security Levels**: Standard, Safer, and Safest.
- Standard preserves NiOn 1.6.0's existing privacy baseline; levels only tighten settings and never re-enable WebRTC, WebGL, WebAudio, JavaScript clipboard access, or automatic JavaScript popups.
- Safer disables page-controlled fullscreen and blocks all autoplay by default while keeping JavaScript and permission-gated MediaStream available.
- Safest additionally disables JavaScript and MediaStream by default.
- Expanded per-site JavaScript persistence to support explicit enable exceptions required by Safest while remaining backward-compatible with format-1 disabled-site rules.
- Security Level is persisted in Preferences, inherited by Private Windows, shown in Site Information/Privacy Audit, and applied to all open tabs when changed.
- Changing level revokes temporary permissions, stops camera/microphone capture, reloads active website tabs, and keeps Tor/fail-closed routing unchanged.

## 1.6.0 — 2026-07-27

### Reliability & Site Privacy

- Added per-tab WebKit web-process crash recovery using `WebKitWebView::web-process-terminated`. A terminated/crashed WebProcess no longer has to take the NiOn UI down with it: the affected tab shows a local recovery page, preserves the original URL, distinguishes memory-limit termination, and can recreate the page through the normal Tor-gated navigation path. Synthetic crash-page WebKit session state is never persisted; normal session recovery keeps the original URL instead.
- Added **Forget This Site…** as a deliberate superset of the existing **Clear Data for This Site…** operation. It removes targeted WebKit website data plus saved site zoom, JavaScript override, content-blocking exception, autoplay exception, temporary permission grants/capture, and matching temporary HTTP allowance. Bookmarks, download history, and downloaded files are intentionally preserved.
- Added **Forget This Site…** to both the main menu and Site Information.
- Kept the 1.5.0 Site Information transient-window width/scroll fixes and bounded long-URL wrapping.
- Added 1.6.0 regression guards for WebProcess recovery and per-site forgetting, re-ran historical feature regressions, and promoted 1.6.0 metadata to Stable.

## 1.5.0 — 2026-07-26

### Lightweight Content Blocking

- Added a deliberately small bundled JSON ruleset for common third-party advertising/tracking endpoints. Rules are compiled and enforced by WebKit's native `WebKitUserContentFilterStore` / `WebKitUserContentManager` pipeline; NiOn does not add an extension engine, daemon, or background list updater.
- Blocking is on by default for normal web pages and intentionally excludes top-level document blocking to reduce breakage.
- Added a **Content blocking** switch and status row to Site Information. Normal per-site disable exceptions persist in bounded atomic `~/.config/nion/content-blocking.ini`; Private Window exceptions remain memory-only.
- Added content-blocking exception clearing to Browsing Data for both normal and private contexts.
- Gave each WebView its own `WebKitUserContentManager` so per-tab/site filter state remains isolated while popup/new-window WebViews still use WebKitGTK 6 `related-view` and inherit the opener's Tor-backed network session.
- Embedded the ruleset in the NiOn GResource and added a dedicated Stage 1 regression test plus release-preflight coverage.

### Tracking & Media Protection

- Enabled WebKit Intelligent Tracking Prevention (ITP) by default on NiOn network sessions. The existing strict third-party-cookie option remains available as an explicit alternative and disables ITP to avoid WebKit's documented policy supersession.
- Added native WebKit autoplay protection using `WebKitWebsitePolicies`: audible autoplay is blocked by default while muted autoplay is allowed.
- Added a per-site **Allow autoplay with sound** control. Normal exceptions persist in bounded/atomic `~/.config/nion/autoplay.ini`; Private Window exceptions are memory-only.
- Applied autoplay policy at WebKit navigation-policy acceptance time with `webkit_policy_decision_use_with_policies()` and added autoplay-exception clearing to Browsing Data.

### Site Information reliability

- Replaced the Site Information `GtkMenuButton`/`GtkPopover` overlay entirely with a plain toolbar `GtkButton` that opens a transient Site Information window. This avoids popover-over-WebKit compositing, which continued to flicker on the tested lightweight X11 desktop even after earlier child/refresh fixes. The final 1.5.0 window is compact, resizable, and vertically scrollable so it remains usable on small displays.
- The transient window keeps the existing TLS/Tor/mixed-content/permission/JavaScript/content-blocking information and now also reports tracking-protection and autoplay state.

### Final hardening & release

- Added a bounded `GtkScrolledWindow` to Site Information so its growing security/privacy controls cannot force the transient window off-screen.
- Re-audited content filtering, ITP/strict-cookie mode, autoplay policy, JavaScript/permission controls, normal/private state separation, Tor fail-closed handling, popup/context-menu paths, recovery/data controls, and centralized release metadata.
- Promoted 1.5.0 release metadata to Stable. Feature development is paused after 1.5.0; subsequent work is maintenance and bug fixes unless the roadmap is explicitly reopened.

## 1.4.0 — 2026-07-26

### Site Controls

- Upgraded the existing deny-all permission handler into an explicit privacy-first gate for camera, microphone, geolocation, and notifications. Supported permissions remain blocked by default and can be allowed temporarily per origin for the lifetime of the current NiOn window; grants are never written to disk.
- Kept WebRTC peer connections disabled while allowing MediaStream permission requests to reach NiOn's camera/microphone gate. Screen/display capture and unscoped permission classes remain blocked.
- Added per-site JavaScript enable/disable controls to Site Information. Normal rules persist in bounded `~/.config/nion/site-javascript.ini`; Private Window rules are memory-only and never read the normal rule store.
- Added Site Information permission status rows and a reset action that revokes temporary grants and stops matching camera/microphone capture.
- Permission grants are revalidated against the requesting origin and Tor-ready state before approval; Tor failure revokes temporary grants and stops active capture.

### Recovery & Data Controls

- Added explicit unclean-shutdown recovery with **Restore Tabs** and **Start Fresh** choices. Unclean sessions are never auto-navigated before the user decides.
- Unclean recovery deliberately skips opaque WebKit session-state blobs and rebuilds from bounded URL/tab metadata first, reducing startup crash-loop risk while retaining malformed/oversized session quarantine.
- Upgraded the former all-or-nothing global clear into **Browsing Data…**, with selective clearing for cookies/site storage, Web cache, saved zoom, per-site JavaScript rules, and temporary permissions.
- Kept **Clear Data for This Site…** as the narrower per-site operation and provided equivalent early-clear controls for ephemeral Private Window state.
- Fixed the Site Information popover flashing closed by keeping a stable `GtkImage` child and updating only the image instead of resetting the `GtkMenuButton` child.

### Hardening & reliability

- Fixed Home navigation history: entering NiOn's synthetic Home/New Tab page now retains a strong reference to the active WebKit back/forward item, keeps Back enabled, and returns to that exact page when Back/`Alt+Left` is used.
- Re-audited Tor fail-closed navigation, private-session ephemerality, temporary permissions, per-site JavaScript isolation, crash recovery, data clearing, pinned/session state, downloads, `target=_blank`, and GTK4 context-menu invariants.
- Promoted 1.4.0 release metadata to Stable and added a final hardening/release regression guard.

## 1.3.0 — 2026-07-26

### Tabs & Private Browsing

- Added pinned tabs with a dedicated left-side block, session persistence, bulk-close protection, closed-tab recovery, and a visible `📌` marker.
- Added **New Private Window** (`Ctrl+Shift+P`) backed by a dedicated ephemeral WebKit network session while continuing to use NiOn's shared bundled Tor runtime and fail-closed routing.
- Private Window does not persist cookies/site data, WebKit credentials, tab/session restore, pinned state, per-site zoom, private download history, or private recently-closed tabs after the window closes.
- Bookmarks and the selected search engine remain intentionally global between normal and private windows.
- Added private download handling: history/source URLs stay memory-only, desktop completion notifications are suppressed, active downloads are cancelled on Private Window close/Tor failure, and tracked partial files are cleaned where possible. Completed files intentionally remain on disk.
- Closing an individual private tab requires confirmation; private bulk-close actions use a single confirmation.
- Added runtime verification that the private WebKit session is truly ephemeral and that persistent credential storage is disabled; Private Window creation fails closed if those invariants are not met.
- Expanded **Privacy & Leak Audit** to report private-session persistence boundaries and intentional user-export/global exceptions.

### Runtime fixes

- Fixed `target="_blank"`, `window.open()`, and related new-view handling for WebKitGTK 6 by constructing popup WebViews with the `related-view` property while preserving opener settings/content policies.
- Fixed page-context-menu crashes on WebKitGTK 6 / GTK4 by using the current `context-menu` signal signature without the removed `GdkEvent` parameter.
- Corrected context-menu ownership handling so NiOn does not free WebKit-owned menu lists and manages custom menu-item references safely.
- Added regression checks for related-view handling, context-menu ownership, and the GTK4/WebKitGTK 6 context-menu callback signature.

### Documentation and release cleanup

- Promoted 1.3.0 to Stable release metadata and AppStream `stable` status.
- Reworked `README.md` to focus on NiOn as a product: features, capabilities, privacy model, strengths, limitations, shortcuts, profile behavior, and concise run/build guidance.
- Consolidated AppImage, Tor-runtime, upgrade, repository, and release-process guidance into the remaining core documentation.
- Removed obsolete/duplicative Markdown files and made historical feature tests independent of `ROADMAP.md`.
- Kept the centralized `release/manifest/` as the single source of truth for NiOn/Tor/AppImage release-critical values.

## 1.2.1 — Version & Dependency Manifest

- Added `release/manifest/` as the canonical source for NiOn release version, AppImage architecture, bundled Tor pins, release status, and minimum GTK/WebKitGTK versions.
- Meson now reads the canonical version directly and generates C/About version macros from manifest values.
- Converted AppStream metadata to a build-time template so the current release version/type is manifest-driven.
- AppImage/source archive naming, Tor fetch/repair validation, runtime metadata, and development runtime checks now consume the same manifest.
- GitHub Actions release tags/assets are manifest-driven and reject tag/version mismatches.
- Added `test-manifest-1.2.1.sh` and expanded release preflight to detect release-critical version drift.
- Kept NiOn 1.2.0 Stage 1–6 browser behavior and profile formats unchanged.

## 1.2.0 — Stage 6: Bookmark Search

- Added a native GTK search field to the existing Bookmarks window without changing `bookmarks.ini` or creating a separate index/database.
- Search filters saved bookmarks live by both title and URL.
- Matching is case-insensitive, Unicode-aware for valid UTF-8 text, and supports multiple whitespace-separated terms; every non-empty term must match either the title or URL.
- Added live result-count feedback and a dedicated no-results message while preserving the existing no-bookmarks placeholder.
- Typing while the Bookmarks window is active can feed the search field through GTK search key capture; the temporary query is cleared when the window is closed.
- Open, Rename, Delete, duplicate protection, `Ctrl+D`, and the toolbar bookmark toggle continue to operate on the same bounded local bookmark store.
- No network requests, Tor configuration, session format, cookie state, download history, or site-zoom state are added or changed by bookmark search.

## 1.2.0 — Stage 5: Download Improvements

- Added a compact per-download **⋮** actions menu without widening the existing Downloads row layout.
- Completed files can be opened with the desktop's registered local file handler.
- **Open Containing Folder** launches the local parent directory without sending it through WebKit or Tor.
- **Copy Download Link** copies the original WebKit download request URI to the system clipboard.
- Failed downloads can be retried with a fresh `webkit_web_view_download_uri()` request on the active NiOn WebView, preserving NiOn's Tor-proxied WebKit network session and fail-closed behavior.
- Retry is only offered for genuine **Failed** history entries; user-cancelled downloads are not treated as failures.
- Download history now optionally stores a bounded `source-uri` field so Copy Link and Retry survive a NiOn restart. Existing history without this field remains backward-compatible.
- Source-dependent retry accepts only NiOn-valid HTTP(S) addresses and is blocked while bundled Tor is unavailable.
- No changes to cookie/session persistence, bookmark storage, site zoom, TLS/mixed-content handling, or the saved-tab session format.

## 1.2.0 — Stage 4: Site & Connection Information

- Added a compact site-information button and popover beside the Onion-Location area for normal web pages.
- The popover reports the host, website connection type, bundled-Tor route state, mixed-content state, and complete current address.
- HTTPS pages read WebKit TLS information after navigation commits; certificate verification remains strict and the network session explicitly uses `WEBKIT_TLS_ERRORS_POLICY_FAIL`.
- Tor v3 `.onion` pages are identified separately so a plain-HTTP onion is not falsely labeled as HTTPS.
- Added WebKit `insecure-content-detected` handling for insecure active/run content and insecure displayed content.
- A mixed-content event changes the site-information icon to a warning icon and surfaces a `MIXED CONTENT DETECTED` status for the active tab.
- Mixed-content flags reset on each new navigation and remain per-tab; switching tabs therefore shows the selected page's own state.
- Home/New Tab and internal error pages keep the site-information control disabled.
- No proxy-routing, cookie/session persistence, bookmark, download, or site-zoom format changes in this stage.

## 1.2.0 — Stage 3: Per-Site Zoom Memory

- Page zoom now persists per HTTP(S) hostname and Tor v3 `.onion` hostname across tabs and NiOn restarts.
- HTTP and HTTPS on the same hostname intentionally share one zoom preference; explicit non-default ports remain separate site keys.
- Existing `Ctrl++` / `Ctrl+=`, `Ctrl+-`, and `Ctrl+0` controls remain the UI for zoom.
- `Ctrl+0` returns the active page to 100% and removes its stored site-specific override instead of persisting a redundant default value.
- Stored zoom is applied when navigation commits, so redirects use the destination site's remembered value.
- New Tab/Home and internal error pages always render at 100% and never create a site-zoom record.
- Added bounded, atomic `~/.config/nion/site-zoom.ini` persistence with mode `0600`, a 1 MiB file limit, at most 2048 remembered sites, and quarantine of malformed/oversized state.
- No Tor proxy/fail-closed, cookies, bookmarks, downloads, or session-format changes in this stage.

## 1.2.0 — Stage 2: Page Actions

- Improved WebKit's page context menu without replacing its context-sensitive editing/media actions.
- Relabeled WebKit's new-window link/image actions as **Open Link in New Tab** and **Open Image in New Tab**; they continue through NiOn's existing `WebView::create` handler, so new content stays inside NiOn.
- Relabeled the WebKit image download action as **Save Image**; downloads continue through NiOn's Tor-proxied WebKit download pipeline and Downloads history.
- Kept WebKit's native Copy Link Address, Copy Selection, editing, spelling, and media context actions intact.
- Added **Print / Save as PDF…** to the page context menu and hamburger menu.
- Added `Ctrl+P` for printing the active page with `WebKitPrintOperation`. The system print dialog can use the platform's Print to File/PDF option where available.
- New Tab remains intentionally non-printable.
- No Tor, cookie, session, bookmark, or profile-format changes in this stage.

## 1.2.0 — Stage 1: Tab Recovery & Tab Context Menu

- Added `Ctrl+Shift+T` to reopen recently closed web tabs.
- Kept a bounded in-memory stack of the 10 most recently closed reopenable HTTP(S) tabs; blank/New Tab pages are not recorded.
- Reopened tabs preserve their URL and muted state without pretending to restore the full back/forward history.
- Added a right-click tab context menu with Reload, Duplicate Tab, Mute/Unmute Tab, Close Tab, Close Other Tabs, and Close Tabs to the Right.
- Closing the final tab still keeps NiOn alive by creating a fresh New Tab.
- Bulk tab-close actions feed the same bounded reopen stack, so accidental closes can be recovered in reverse order.
- No Tor/network/profile format changes in this stage.

## 1.1.0 - 2026-07-25

### Stable

- Added HTTPS-First handling for clearnet with an explicit warning before plain-HTTP clearnet navigation.
- Added simple persistent local bookmarks plus an address-bar bookmark toggle and `Ctrl+D`.
- Added per-site browsing-data clearing without intentionally clearing unrelated sites.
- Added per-tab audio playback indication and mute/unmute.
- Kept the 1.0.0 profile layout and AppImage upgrade path compatible.
- Updated AppImage/release packaging metadata and GitHub Actions for the 1.1.0 release.

## 1.1.0 — Stage 4: Tab Audio Indicator & Mute

- Added a compact per-tab audio button driven by WebKit's `is-playing-audio` property.
- Clicking the audio button toggles only that tab's `is-muted` state.
- The button icon changes between speaker and muted states and exposes matching tooltips.
- Saved per-tab mute state in `session.ini` so restored tabs keep their intended mute state without affecting other tabs.
- Removed the redundant **Bookmark This Page** item from the hamburger menu; the toolbar bookmark button and `Ctrl+D` remain available.
- Completed the four planned NiOn 1.1.0 stages.

## 1.1.0 — Stage 3: Clear Data for This Site + Bookmark Toolbar

- Added a bookmark icon button immediately beside the address bar and before the hamburger menu.
- The bookmark icon automatically follows the active tab/URL and switches between unbookmarked and bookmarked states.
- Clicking the bookmark icon toggles the current URL in `bookmarks.ini`.
- Blank/New Tab and internal error pages remain non-bookmarkable and keep the toolbar bookmark button disabled.
- Bookmark state is refreshed on tab switches, URI changes, bookmark additions, and bookmark deletion.
- Added **Clear Data for This Site…** with a confirmation dialog for the active HTTP(S) host.
- Site-specific clearing uses WebKit website-data fetch/remove APIs rather than clearing the whole profile.
- Matching records include the current host and a parent-domain record when WebKit groups a subdomain under that domain; unrelated/third-party records are not deliberately selected.
- Successful site-data clearing reloads the active page without cache so cookie/storage changes become visible immediately.
- Kept the existing global **Clear Browsing Data…** action unchanged.

## 1.1.0 — Stage 2: Simple Bookmarks

- Added `Ctrl+D` to bookmark the current clearnet or `.onion` page.
- Added **Bookmark This Page** and **Bookmarks** entries to the NiOn menu.
- Added a local Bookmarks window with Open, Rename, and Delete actions.
- Bookmarks persist in `~/.local/share/nion/bookmarks.ini`.
- Duplicate URLs are ignored instead of creating repeated entries.
- Added bounded bookmark profile loading and quarantine for oversized/malformed bookmark files.
- Kept bookmarks deliberately local and flat: no folders, tags, accounts, sync, or cloud services.

## 1.1.0 - Unreleased

### Stage 1 — HTTPS-First for clearnet

- Clearnet addresses without an explicit scheme continue to resolve to HTTPS first.
- Added an interstitial warning before top-level plain-HTTP clearnet navigation is allowed.
- The warning explains that Tor still carries traffic to an exit relay, while plain HTTP does not provide HTTPS protection between the exit relay and the clearnet website.
- `http://` Tor v3 `.onion` addresses remain allowed without the clearnet HTTP warning.
- Choosing **Continue** allows plain HTTP for that clearnet origin in the current tab until the tab commits to a non-HTTP page; this avoids repeated prompts for same-site links, reloads, and form submissions.
- Choosing **Cancel** leaves the current page in place and does not issue the blocked HTTP navigation.
- No profile-format change. Cookies, saved sessions, downloads history, and 1.0.0 profiles remain compatible.

## 1.0.0 - 2026-07-25

### Stable

- Promoted the feature-frozen 0.11.0 release-candidate code line to NiOn 1.0.0 Stable without introducing a new browser feature family.
- Finalized release metadata, desktop integration, AppStream information, icon packaging, and version strings for 1.0.0.
- Added GPL-3.0-or-later licensing for NiOn source code plus third-party runtime notices.
- Added a release-quality `.gitignore` so build output, AppImage artifacts, downloaded Tor runtime files, tools, crash files, and editor state do not pollute the repository.
- Reworked the README for the public repository and added `BUILDING.md`, `UPGRADING.md`, `SECURITY.md`, `GITHUB_RELEASE.md`, and repository metadata suggestions.
- Renamed the RC preflight/test documentation to stable release validation and retained the fail-closed/profile recovery test suite.
- AppImage builds now include NiOn license/notices in the AppDir and run metadata validation when the relevant host tools are available.
- Added a GitHub Actions release workflow for tag-driven x86_64 AppImage builds and GitHub Release asset upload.
- Documented the stable profile path and 0.11.0 → 1.0.0 upgrade behavior. No profile-format bump is introduced by 1.0.0.

## 0.11.0 - 2026-07-25

### Release Candidate

- Feature freeze: no new browser feature family was introduced.
- Strengthened fail-closed behavior after Tor failure by moving WebKit to a dead loopback SOCKS endpoint and rejecting new HTTP/HTTPS navigation in policy code while Tor is offline.
- Added bounded/corruption-aware loading for preferences, saved sessions, and download history. Malformed or oversized NiOn-owned profile files are quarantined with `.corrupt-...` suffixes instead of being retried forever.
- Added a SQLite-header sanity check for the persistent cookie database before WebKit opens it; obviously invalid cookie DB/WAL/SHM files are quarantined.
- Bounded opaque WebKit tab-session snapshots and total session-state budget while preserving URL fallback recovery.
- Capped persistent download history to the newest 500 finished/failed/cancelled records.
- Increased session-save debounce from 250 ms to 750 ms to reduce repeated session serialization during busy tab activity.
- Normalized NiOn data/config/cache/Tor directories to mode `0700` where possible.
- Refined Tor corruption detection so ordinary mentions of cached consensus/microdescriptor files no longer count as corruption without a damage/parse/read failure indicator.
- Tor state recovery now reports a hard failure when its quarantine directory cannot be created.
- AppImage packaging removes Tor Expert Bundle debug-symbol directories from the production AppDir and uses safer temporary files in diagnostics.
- Removed the desktop file's unimplemented `%u`/HTTP(S) handler advertisement; internal About links continue opening inside NiOn.
- Added `rc-preflight.sh`, `test-profile-recovery.sh`, `test-fail-closed.sh`, and `RC_TESTING.md`.

## 0.10.0 Hotfix 1 - 2026-07-25

- Fixed a C17 compile failure in Find in Page by forward-declaring the `found-text` and `failed-to-find-text` callbacks before `nion_new_tab()` connects them.
- No behavior or profile-format changes.

## 0.10.0 - 2026-07-25

### Browsing Essentials

- Removed the unreliable Tor Circuit viewer and removed its private Tor ControlPort configuration. NiOn now reserves only its SOCKS listener.
- Added Find in Page with `Ctrl+F`, live searching, match count, next/previous controls, Enter/Shift+Enter navigation and Escape to close.
- Added per-tab page zoom with `Ctrl++`, `Ctrl+-`, `Ctrl+0` and Zoom controls in the hamburger menu.
- Added hard reload / cache-bypass with `Ctrl+Shift+R`.
- Added `F11` fullscreen mode; browser chrome is hidden while the window is actually fullscreen and restored on exit.
- Added a persistent default search-engine preference with DuckDuckGo, Brave Search and Startpage choices.
- Kept the 0.9.0 AppImage packaging baseline and internal About repository-link handling.

## 0.9.0 - 2026-07-25

### AppImage Engineering

- Replaced the thin/developer AppImage builder with a final AppDir-oriented packaging pipeline.
- Bundles the NiOn executable and the verified Tor Expert Bundle runtime.
- Explicitly discovers and bundles WebKitGTK 6 WebKitWebProcess/WebKitNetworkProcess and WebKitGPUProcess when present.
- Added recursive ELF dependency deployment while deliberately leaving glibc and host graphics-driver libraries to the target system.
- Added GIO runtime module deployment for TLS/proxy backends where available.
- Added a small WebKit subprocess spawn-path shim for distro builds with compile-time absolute helper paths.
- Keeps WebKitGTK 6's mandatory WebProcess sandbox enabled and exposes the read-only AppImage mount to that sandbox before any WebProcess is created.
- Added final AppRun environment setup, root desktop/icon links, AppStream metadata and package build provenance.
- Added AppImage diagnostics, profile-path replacement checks and optional container loader smoke tests.
- Repository links in About NiOn now open inside NiOn rather than the operating system's default browser.

## 0.8.0 - 2026-07-25

### Browser UX & Tor Integration

- Added Onion-Location discovery from the standardized HTTP response header and equivalent HTML `meta http-equiv="onion-location"` declaration.
- Onion-Location is accepted only from HTTPS clearnet pages and must point to a valid HTTP/HTTPS Tor v3 `.onion` URL.
- Added an address-bar Onion badge; clicking it opens the advertised onion site in a new tab.
- Added a Tor Circuit viewer backed by NiOn's private loopback ControlPort with cookie authentication.
- Circuit snapshots show Guard / Middle / Exit roles for clearnet, and Guard / Middle / Rendezvous for onion circuits when available, plus relay nickname, IP and Tor GeoIP country code.
- Added paired dynamic ControlPort selection (`127.0.0.1:19150-19169`) alongside the SOCKS port range.
- Closing the final tab now creates a clean blank/New Tab page instead of terminating NiOn.
- Added close confirmation when one or more nonblank website tabs remain open. All-blank sessions close directly.
- Expanded About NiOn with author, repository, runtime status and GTK/WebKitGTK/libsoup/GLib versions.
- Added Exit to the hamburger menu.
- Added a dedicated persistent Downloads window with `Ctrl+J`.
- Download history records completed, failed and cancelled downloads across NiOn restarts and supports Clear Downloads.
- Added a Home button next to Reload to return the current tab to NiOn's blank/New Tab page.

## 0.7.0 - 2026-07-24

- UI polish, NiOn icon integration, cleaner toolbar/tab layout, keyboard navigation, improved new-tab page and About dialog.

## 0.6.0 - 2026-07-24

- Bundled Tor Expert Bundle runtime lifecycle.
- Added runtime repair to avoid loading debug-symbol objects as shared libraries.

### 1.3.0 Stage 1 Fix 4
- Fixed the remaining immediate crash when opening a web-page context menu on WebKitGTK 6 / GTK4.
- Corrected the `WebKitWebView::context-menu` callback signature by removing the GTK3-era `GdkEvent` parameter. On WebKitGTK 6 that extra argument shifted `hit_test_result` and `user_data`, allowing an invalid `NionTab` pointer to be dereferenced.
- Added a regression guard for the WebKitGTK 6 signal signature.
