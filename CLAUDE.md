# Markdown Notes

A native, lightweight desktop app for reading and writing a folder of Markdown notes (an "Obsidian vault"). It replaces Obsidian for one user, on Linux (Wayland) and Windows. It must open an existing Obsidian vault as-is and leave every file in a state Obsidian could still open.

Three goals, in priority order:

1. **Never damage the user's notes.** Data safety beats every other goal.
2. **Full compatibility with the Obsidian vault format.** Defined in `docs/obsidian-compat.md`, which is the source of truth for parsing, link resolution and file-writing behaviour.
3. **As lightweight and fast as possible.** Measured against the budgets below, not judged by feel.

## Hard rules

- **No web views, ever.** No QtWebEngine, QtWebView, QML or Qt Quick, and no embedded JavaScript engine. UI is Qt Widgets only.
- **Never write a file the user did not change.** Opening, indexing, previewing or searching a note must not modify it, including its line endings, BOM, trailing newline or frontmatter formatting. Save only when the buffer differs from what was loaded.
- **The only bulk write is renaming with link updates.** It must show the list of affected files and get confirmation first. No other feature edits notes the user hasn't opened, with one exception: **quick capture** may append to its configured target note (the inbox or today's daily note). It only appends at the end, keeps every existing byte, writes atomically, and refuses notes that aren't valid UTF-8 text (`core/capture/`). Likewise the **Tasks view** may tick off or delete one task in its note on an explicit click: the edit is planned against the note's current bytes after checking the task is still there, changes only that checkbox or that item's lines, and deletion asks first (`core/tasks/`).
- **Never write inside `.obsidian/`.** Read it once, when a vault is first opened, to import settings (see compat spec §8).
- **Writes are atomic.** Use `QSaveFile`. On a write failure, keep the buffer dirty and show an error. Never lose typed text.
- **Tests never touch a real vault.** Fixtures are copied to a temp directory (`QTemporaryDir`) before every test that writes.
- **No new third-party dependency without asking the user first.** The allowed list is below.
- **Don't guess Obsidian's behaviour.** If the compat spec doesn't cover a case, add it to "Open questions" in `docs/obsidian-compat.md`. The user prefers that such cases are decided in the way that changes the app least, recorded in the spec with a fixture test, rather than being asked. Don't invent behaviour silently.

## Tech stack

- C++20, CMake (with `CMakePresets.json`), Ninja.
- Compilers: GCC or Clang on Linux, MSVC on Windows. Warnings on (`-Wall -Wextra -Wpedantic` / `/W4`), treated as errors in CI.
- Qt 6 (current LTS or newer). Modules: **Core, Gui, Widgets** and **Test**, nothing else without asking. QtNetwork is reserved for the later semantic-search phase, behind a build option.
- Vendored in `third_party/`, pinned to a version:
  - **md4c** (MIT). The Markdown parser, with Obsidian extensions layered on top (compat spec §2).
  - **SQLite amalgamation** (public domain), built with FTS5. Used through the C API, not the QtSql module.
  - **LaTeX math renderer**: choose between MicroTeX and JKQTMathText in milestone M0 (see Milestones) and record the choice in `docs/decisions.md`.
- Licence: MIT. Ship Qt's LGPL notices with every release. Qt is linked dynamically.

## Architecture

```
src/core/    QtCore only (no QtGui/QtWidgets). All vault logic, unit-testable headless.
  vault/       scanning, file-type detection, file watching, safe reading and writing
  parse/       md4c wrapper + Obsidian extensions → NoteMetadata (links, embeds, tags, headings, block ids, frontmatter)
  links/       link resolution, rename and link-update planning
  index/       SQLite index (metadata + FTS5), on-disk cache, background indexer
  templates/   template insertion, Moment.js date-format subset
  capture/     quick capture: append to the inbox or today's daily note
  tasks/       ticking off and deleting a task from the Tasks view
  settings/    per-vault settings, import from .obsidian/
src/ui/      Qt Widgets: main window, file tree, editor + highlighter, preview, quick switcher, search, backlinks, tags
src/app/     main.cpp, application wiring
tests/       Qt Test. tests/fixtures/ holds small hand-written vaults covering the compat spec's edge cases
bench/       benchmark executables + tools/gen_vault.py (synthetic vault generator)
packaging/   AppImage (linuxdeploy + Qt plugin) and Windows portable build (windeployqt)
docs/        obsidian-compat.md (spec), decisions.md (decisions log)
```

- `core` must never depend on `ui`. `ui` talks to `core` through plain value types and signals.
- **The vault on disk is the source of truth.** The SQLite index is a disposable cache stored in `QStandardPaths::CacheLocation/<vault-id>/index.sqlite`, never inside the vault. Deleting it must lose nothing. Bump a schema version on change and rebuild when it doesn't match.
- Link resolution and autocomplete use in-memory lookup tables, built from the index on startup. Full-text search uses FTS5.
- Per-vault settings live in `<vault>/.markdown-notes/settings.json`. App-wide settings (recent vaults, window geometry) use `QSettings`.
- **Threading:** the UI thread never scans directories or runs full-vault work. Indexing, scanning and search run on a worker thread and post results back through queued signals. Reading or saving the single open note on the UI thread is fine.
- **Editor:** `QPlainTextEdit` + `QSyntaxHighlighter`. Highlighting is incremental per block, using block state for multi-line constructs (code fences, frontmatter, `$$` math, `%%` comments). Never re-highlight the whole document on a keystroke.
- **Reading view:** md4c → Qt's rich-text HTML subset → `QTextBrowser`. Math is rendered to images, cached by (formula, font size, DPI), and added as document resources. Embedded notes are rendered inline with cycle detection and a depth limit of 3.
- **File watching:** `QFileSystemWatcher` on directories. On a change event, rescan that directory by comparing mtime and size. If the open note changes on disk: reload silently when the buffer is clean, otherwise ask the user.

## Performance budgets

Measured in Release builds on two vaults: `tools/gen_vault.py --notes 5000 --attachments 1000`, and a copy of the user's real vault. Milestone M0 measures a bare Qt Widgets window first. Agree any change to these budgets with the user and record it in `docs/decisions.md`.

| Metric | Target | Hard limit |
|---|---|---|
| Idle memory (RSS), 5k-note vault open, one note in editor | ≤ 60 MB | 100 MB (user accepted ~110 MB on KDE/Wayland, 2026-11-22; see `docs/decisions.md`) |
| Launch to usable window (index cache present) | ≤ 300 ms | 1 s |
| First-ever index of 5k notes (background, UI stays responsive) | ≤ 2 s | 5 s |
| Keystroke to repaint, 5,000-line note | ≤ 8 ms | 16 ms |
| Quick-switcher filtering, per keystroke | ≤ 5 ms | 16 ms |
| Full-text search, 5k notes | ≤ 30 ms | 100 ms |
| Reading-view render, typical note (math cached) | ≤ 50 ms | 150 ms |

- Benchmarks in `bench/` report these numbers. Run them whenever a change touches `core/index`, `core/parse`, the editor or the preview, and report before/after numbers in the summary.
- Prefer simple data structures and fewer allocations. Don't add caches without a measured reason. Don't load attachments into memory except to display them.

## Commands

```sh
cmake --preset debug && cmake --build --preset debug          # debug build
ctest --preset debug --output-on-failure                      # tests
cmake --preset asan && cmake --build --preset asan && ctest --preset asan   # ASan/UBSan (Linux)
cmake --preset release && cmake --build --preset release      # release build
./build/release/bench/bench_index --vault <path>              # benchmarks
python3 tools/gen_vault.py --out /tmp/vault5k --notes 5000 --attachments 1000
```

If these presets don't exist yet, create them as part of M0.

## How to work

- Before saying a task is done: build with no warnings, all tests pass, `clang-format` applied, and `clang-tidy` clean on changed files.
- **Every behaviour from the compat spec gets a fixture test**, and so does every parser or resolver bug fix (a test that failed first). Fixture vaults are small and hand-written, with each file showing a named edge case.
- Keep changes small and focused, one milestone step at a time. Ask before large refactors or anything that changes the on-disk format of `.markdown-notes/`.
- Record significant decisions (library choices, budget changes, deviations from Obsidian) in `docs/decisions.md`: one short entry each, with date, decision and reason.
- Code style: RAII. No owning raw `new` except for Qt parent-owned widgets. No exceptions across Qt boundaries (return `std::optional` or a small result type). `QString` for text. Paths stored vault-relative with `/` separators internally on every OS.
- Text handling: UTF-8 in and out. Detect and preserve the BOM and line-ending style per file. Link and tag matching is case-insensitive, as specified in the compat spec.

## Milestones (v1)

- **M0 – Skeleton and spikes.** CMake presets, CI (Linux + Windows), an empty main window, a baseline memory measurement. Spike: heading font sizes in `QPlainTextEdit`. Spike: compare MicroTeX and JKQTMathText on quality, speed, size and maintenance, then pick one.
- **M1 – Core.** Vault scan, parser + Obsidian extensions, frontmatter subset, link resolution, SQLite index + cache, background indexer, file watcher. All compat-spec behaviour covered by fixture tests.
- **M2 – Editor.** Open a vault, file tree, styled source editor, autosave (debounced ~1 s, plus on switching notes, losing focus and quitting), find/replace in note, back/forward history, Ctrl+click to follow links, `[[` autocomplete (files, then `#` headings, then `^` blocks), `#` tag autocomplete, creating a note from an unresolved link.
- **M3 – Reading view.** Toggle with Ctrl+E. Covers images with sizes, note/heading/block embeds, math, callouts, tables, task lists, highlights, hidden comments, clickable links.
- **M4 – Navigation.** Quick switcher (Ctrl+O), full-text search (Ctrl+Shift+F), backlinks + unlinked mentions panel, tag pane with nested tags.
- **M5 – Writing helpers.** Insert template, paste or drop images into the attachment folder, rename/move with confirmed link updates, new note in the configured location.
- **M6 – Release.** AppImage (Wayland platform plugin, with xcb fallback) and a portable Windows zip (`.exe` + Qt DLLs). Budgets verified on both vaults. Open a copy of the user's vault and check that the count of unresolved links matches Obsidian's.

**Not in v1** (don't build any of these, and don't add abstractions for them): graph view, canvas, Bases, plugins, sync, mobile, live preview, tabs or split panes, spellcheck, macOS packaging.

## Later: semantic search (not in v1)

Planned shape, so v1 doesn't block it:

- Embeddings come from the user's existing local model over HTTP (OpenAI-compatible `/v1/embeddings`), behind a build option that brings in QtNetwork.
- Notes are split into chunks by heading or paragraph. Vectors are stored as float32 BLOBs in a separate SQLite file in the same cache directory, keyed by (path, chunk index, content hash, model id). Only changed chunks get re-embedded.
- Search is a brute-force cosine similarity scan over normalised vectors, with no vector-DB dependency. Results are blended with FTS5 scores for hybrid ranking.
- **v1 only needs one thing for this:** the index records a content hash per note.
