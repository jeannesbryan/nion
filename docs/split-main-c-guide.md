# NiOn `main.c` Refactoring Guide (for Jeanne)

> Written by the IDES agent (Rush) on behalf of @kaluluosi, 2026-09-07.
> This document explains **how** we refactored the `main.c` "god file", **what** is left for you to refactor, and **how** you can use IDES + multiple agents to do the same.

## TL;DR

We extracted **3 modules** out of `src/main.c`, reducing it from **9003 → 8110 lines**:

| Module | What it contains | Result |
|---|---|---|
| `session.c` (122 lines) | Session save/restore (pure logic) | ✅ compiles |
| `preferences.c` (141 lines) | Preferences load/save (pure logic) | ✅ compiles |
| `tor.c` (788 lines) | Tor / network / process runtime (22 functions) | ✅ compiles |

**What's left** in `main.c` is almost entirely the **UI domain** — that's your practice ground.

---

## 1. What we did

`src/main.c` was a **god file**: 9000+ lines mixing UI (address bar, download panel, bookmarks, tabs, menus), business logic (Tor startup, session, preferences), and data persistence — all in one file.

We **moved** (not rewrote) modules out. The guiding principle was:

> **Only move code. Never change semantics.** No bug fixes, no feature changes, no "improvements" while moving.

This is crucial for a browser whose core is **fail-closed and Tor-only** — you must not accidentally break security behavior while reorganizing.

### The 3 modules we extracted

1. **`session.c`** — pure data logic (save/restore session state). Zero UI dependencies.
2. **`preferences.c`** — pure data logic (load/save preferences). Zero UI dependencies.
3. **`tor.c`** — Tor runtime management (find binary, check process, choose port, write runtime state, parse Tor logs, apply proxy, prepare network). **Zero UI dependencies** — it's pure network/process logic. It does depend on a few UI *status update* functions (`nion_set_status`, `nion_set_tor_error`, `nion_set_tor_progress`) which stay in `main.c` and are called via header declaration.

`main.c` now contains mostly **UI callbacks** (download/bookmark/tab/site-info/address bar/on_* handlers).

---

## 2. The method: how to split a god file safely

This is the reusable methodology. We used it across all 3 modules, and it's what you can copy.

### 2a. Find the real boundaries by **dependency closure**

The mistake most people make (and we initially made too) is splitting **by function name** (e.g., "download functions"). That fails because a "download data" function often touches UI widgets.

Instead, split by **dependency closure**:
- Pick a domain (e.g., Tor).
- Find *every* function in that domain.
- Check what they call *and* who calls them.
- If a function touches UI widgets (`gtk_*`, `app->window`), it's entangled with UI — either keep it in main.c, or split the **whole domain** (UI callbacks + data functions) as one unit.

> **Rule of thumb:** `gtk_` calls = UI-entangled. Zero `gtk_` calls in a cluster = clean pure-logic candidate.

We used **ast-grep** (a code-structure search tool) to scan `main.c` and get precise function boundaries + verify the dependency closure for each domain. This is far more reliable than line-number guesswork, because line numbers shift as you move code.

### 2b. **Static / exported boundary** (critical!)

When moving a function into a new `.c` file, decide:
- **Exported** (non-`static`, declared in the `.h`): functions that `main.c` (or another module) **calls**. These must stay `non-static` and be declared in the header.
- **Internal** (`static`): functions only used *inside* the new module.

Getting this wrong causes a compile error:
```
error: static declaration of 'foo' follows non-static declaration
```
because the header declares `foo` as `non-static` (for other files) but the `.c` defines it as `static`.

**We hit this twice.** So: be explicit with your agent about exactly which functions are exported vs. internal before it starts moving code.

### 2c. **Build synchronization**

Each new `.c` module must be added to `meson.build`'s `executable('nion', ...)` source list. Missing this = undefined reference at link time. This is the #1 mechanical gotcha. If using sub-agents, have them **not touch `meson.build`** — the main agent updates it after merge.

### 2d. Compile-verify after **each** module

Split one module → compile it → verify it links. Don't batch multiple splits before compiling (hard to localize errors).

If your environment can't compile locally (GTK/WebKit on non-Linux), use a **remote Linux machine + Docker** with the required deps (GTK 4.14+, webkitgtk-6.0, libsoup-3.0). Look for:
```
[14/14] Linking target nion
```
that line means **all sources compiled & linked**. That's your success signal.

---

## 3. How to do this with IDES + multiple agents

This is the part that made our refactor fast. A single human/agent grinding through a 9000-line file is slow and error-prone. We turned it into a **parallel, agent-augmented workflow**.

### Why multiple agents help

When you (or a single agent) edit `main.c`, you need a **global view** — move a function, and everything that calls it must be updated. This is why single-agent refactoring is slow (that's why you moved 10.8K → 9.1K in a day; we moved 9003 → 8110 in an evening).

With multiple agents, each **isolated module** can be refactored **in parallel** with zero conflict.

### The workflow we used (copy it)

1. **Create a git worktree per module** (an isolated working copy on its own branch).
   ```
   git worktree add ../nion-wt-tor -b refactor/extract-tor
   ```
   Each module lives in its own worktree/branch — agents work in parallel without clashing.

2. **Assign a sub-agent per module** with a **precise task**:
   - Exact function list + line range to move.
   - **Static/exported boundary** (which functions go in the `.h`, which stay `static`).
   - The dependencies the new module relies on.
   - **"Only move, don't change logic."**

3. **The sub-agent does the move** in its worktree and commits.

4. **The main agent reviews + merges** the worktree into the main branch, then **updates `meson.build`**.

5. **Compile-verify** the merged result (e.g., on the remote Linux box).

6. Clean up the worktree. Repeat for the next module.

### The key insight

> **The value isn't that we finished the split. It's that we *proved* the parallel, multi-agent method works.** You can now use this exact method to split the remaining UI domain — far faster than single-agent grinding.

---

## 4. What's left for you (the UI domain)

`main.c` (now 8110 lines) is mostly **UI**. This is your practice ground, and it's a good one because each UI sub-domain is fairly self-contained.

Candidates to split next (each as a **whole domain** — UI callbacks + related data functions together):

| Domain | Representative functions | Notes |
|---|---|---|
| **download** | `nion_show_downloads`, `nion_load/save_download_history`, `on_download_*` | Data + UI coupled; split whole domain |
| **bookmark** | `nion_load/save_bookmarks`, `nion_show_bookmarks`, `nion_toggle_current_bookmark` | Same — split whole domain |
| **tab** | `nion_close_tab`, `nion_reopen_closed_tab`, `nion_update_tab_*`, `nion_tab_context_*` | Entangled with notebook UI |
| **site-info / address bar** | `nion_update_site_info`, `nion_go_to_address`, `nion_update_onion_button` | Address-bar coupled |
| **find** | `nion_find_open`, `nion_find_run` | Relatively independent |
| **zoom** | `nion_set_zoom` | Lightweight |

> **Note on "whole-domain" splitting:** UI domains are more entangled than pure logic (download callbacks touch `NionApp` state, which is shared with tab/menu). You'll rely on `types.h` structs + header interfaces to decouple. The dependency rule: **UI depends on business, business does not depend on UI.**

---

## 5. Reference

- Methodology skill (internal to IDES): "C project split compile-verify" — covers the remote-build verification and the `.dockerignore` / static-boundary gotchas.
- `docs/split-main-c-plan.md` in this branch records the full execution log (Chinese): which module, which commit, which compile result.

---

*This guide is meant to be a starting point. The real lesson is the parallel multi-agent method — apply it to the UI domain and you'll be surprised how fast it goes.*
