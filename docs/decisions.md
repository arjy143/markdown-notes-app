# Decisions log

One entry per significant decision: date, decision, reason. Newest last.

## 2026-11-22 — Math renderer: MicroTeX

**Decision.** Use MicroTeX (MIT, vendored at commit `0e3707f`, 2024-08-06) for `$…$` / `$$…$$`, not JKQTMathText.

**M0 spike** (12 representative formulas: fractions, roots, integrals, sums, matrices, `cases`, `\mathbb`, accents, `\underbrace`):

| | MicroTeX | JKQTMathText 5.0.0-beta2 |
|---|---|---|
| Quality | TeX layout with Computer Modern fonts; all 12 parsed | Upright letters and loose spacing with system fonts; `\underbrace{…}_{n}` failed to parse |
| Speed, cold / warm | 84 ms / 4 ms | 223 ms / 24 ms |
| Size | ~2.3 MB code + 2 MB fonts (embedded as a Qt resource) | ~1.2 MB code + 1.5 MB XITS fonts |
| Licence | MIT | LGPL-2.1 (would need dynamic linking and relinking notices) |
| Maintenance | No commits since 2024 (risk, mitigated by vendoring and pinning) | Active |

**Reason.** Much better output, about 6× faster, and MIT-licensed. MicroTeX needs `tinyxml2` to read its Cyrillic/Greek alphabet descriptions. `tinyxml2` is not on the approved dependency list, so instead of adding it, `third_party/microtex/shim/` implements the small read-only subset MicroTeX uses (about 200 lines) on `QXmlStreamReader` from QtCore. No new third-party dependency was added. MicroTeX exceptions are caught in `ui/MathRenderer`, so none crosses a Qt boundary.

## 2026-11-22 — md4c v0.6.0 with one local patch

**Decision.** Vendor md4c v0.6.0 (2026-09-17). Patch the hard-coded 100-character wikilink destination limit to a compile-time `MD4C_WIKILINK_MAX_DEST` (set to 4096). The patch is in `third_party/md4c/wikilink-limit.patch`.

**Reason.** Obsidian has no such limit, and long folder/title paths are common. The patch is three lines; re-apply it when updating md4c.

## 2026-11-22 — Obsidian embeds are recognised outside md4c

**Decision.** `![[target|alias]]` is recognised by our layer in `parse/MarkdownDoc.cpp`, not by md4c.

**Reason.** md4c reads `![` as the start of an image, so `![[x]]` never reaches its wikilink extension and arrives as plain text. The same layer also adds tags, `%%` comments and `^block-ids` (compat spec §2). Inside table cells, md4c returns `[[a\|b]]` with a constructed target; we re-anchor it in the source so renames can rewrite it.

## 2026-11-22 — Footnotes are rendered (deviation from compat spec §2)

**Decision.** Enable `MD_FLAG_FOOTNOTES` and render footnotes as superscript references with a list at the end.

**Reason.** The spec said "not supported by md4c, show as plain text in v1". md4c 0.6 does support footnotes. With the flag off, `[^1]: text` is parsed as a CommonMark link reference definition, so `[^1]` would become a bogus internal link to `text`. Links inside footnote definitions are indexed. Listed as an open question so the user can confirm.

## 2026-11-22 — SQLite 3.54.1 amalgamation, schema v1

**Decision.** SQLite amalgamation 3540100 with FTS5 (`unicode61 remove_diacritics 2`), used through the C API. The index stores the cache in `QStandardPaths::CacheLocation/<vault-id>/index.sqlite`, uses WAL and a 1 MB page cache, and sets `PRAGMA user_version = 1`. A mismatched version means the file is deleted and rebuilt from the vault. Each note's SHA-1 content hash is stored in `files.hash`, which is the only thing the later semantic-search phase needs.

## 2026-11-22 — Search snippets built in C++

**Decision.** Rank with FTS5 `bm25` (`LIMIT 200`), then build snippets in C++ from the stored body for the top 30 results only. The last word is prefix-matched only if it has at least 2 characters.

**Reason.** Measured on the 5k-note vault: FTS5 `snippet()` re-runs the match for each row, and on its own pushed p95 to 87 ms (one-letter prefixes like `a*` cost 94 ms). With these changes the median is 8 ms and p95 11 ms (budget: 30 / 100 ms).

## 2026-11-22 — Line endings, encodings and when a note opens read-only

**Decision.**
- Notes are edited in "buffer form" (`\n` line breaks, no BOM). On save, the BOM and the dominant line-ending style are put back.
- For files that **mix** LF and CRLF, lines in the unchanged leading and trailing parts keep their own ending. If the line count didn't change, every line keeps its ending by position. Only changed lines get the dominant style.
- A note opens **read-only**, with an explanation banner, when it is not valid UTF-8, contains NUL bytes, contains a lone CR (old Mac line ending), or doesn't round-trip exactly through the editor. After loading, the editor's text is compared with the file's decoded text; any difference makes the note read-only.
- The buffer is read from `QTextBlock::text()`, not `toPlainText()`, which would turn non-breaking spaces into spaces. Shift+Enter inserts `\n`, not U+2028.

**Reason.** Data safety: no feature may change bytes the user didn't edit.

## 2026-11-22 — Rename keeps every link pointing at the same file

**Decision.** Rename/move plans with one invariant: every link that resolved before resolves to the same file afterwards, with moved files at their new paths. This covers links to the moved file and the moved note's own relative links. It also covers "collateral" links whose target would change because the new name collides with an existing file, for example when the same-folder tie-break would pick the new file. All of them are shown in the confirmation list.

Further details:
- Shortest-form links to a name that becomes ambiguous get the full vault path.
- Markdown link paths written by the app are percent-encoded for space, `%`, `<`, `>`, `(`, `)`, `[` and `]`, unless the original used `<…>`.
- Before each write, the file's bytes are compared with the bytes used for planning. A file that changed in the meantime is skipped and reported.
- If the move itself fails, no file is edited. Moves already made in the same plan are rolled back.

**Reason.** Compat spec §7 only says "every resolved link that points at it". Without the invariant, a move can silently break or redirect other links. The cases the spec doesn't cover are listed in its open questions.

## 2026-11-22 — Wayland/X11: skip OpenGL integration by default

**Decision.** At startup, if they're unset, set `QT_WAYLAND_CLIENT_BUFFER_INTEGRATION=none` and `QT_XCB_GL_INTEGRATION=none`. Qt Wayland prints a two-line warning on stderr when this is on.

**Reason.** The UI is raster-painted Qt Widgets and never uses OpenGL. The EGL/GLX integration loads Mesa and LLVM, which measured as about 55 MB of RSS. Widgets render through `wl_shm` either way. Users can override either variable.

## 2026-11-22 — M0 baseline memory and the idle-memory budget (needs the user's decision)

Measured on the development machine: Debian 13, KDE Plasma on Wayland, system Qt 6.8.2, 1280×820 window, RSS read from `/proc`.

| Process | RSS |
|---|---|
| Bare `QMainWindow`, offscreen platform | 27 MB |
| Bare `QMainWindow`, Wayland, defaults | **125 MB** (42 MB is `libLLVM` via Mesa/EGL; the KDE platform theme also loads QtQuick/QML) |
| Bare `QMainWindow`, Wayland, GL integration off | 70 MB |
| Markdown Notes, Wayland, empty vault, one note open | 98 MB |
| Markdown Notes, Wayland, 5k-note vault, one note open | **~108 MB** (range 107–118 over runs) |

Of the ~38 MB the app adds over the bare window: ~10 MB is vault data (file table, tree, index cache), ~6 MB extra Wayland shm buffers, ~4 MB fontconfig cache, ~4 MB binary, plus fonts, harfbuzz and ICU.

**Update, 2026-11-22: the user accepted the current memory use** (about 105–111 MB on KDE/Wayland with a vault open). See the entry "Idle-memory budget accepted" below.

**Status at the time.** The idle-memory target (≤ 60 MB) and hard limit (100 MB) were **not met** on this platform. The bare toolkit window alone is 70 MB once GL is disabled. Reductions already made: GL integration skipped, one shared immutable file table, no file-tree icons (they pulled in the icon theme and image plugins), lazily built completion and switcher lists, `malloc_trim` after indexing, and a 1 MB SQLite cache. The budget is unchanged: CLAUDE.md requires agreeing any change with the user.

## 2026-11-22 — Heading font sizes in `QPlainTextEdit` (M0 spike)

**Decision.** Keep `QPlainTextEdit`. Headings get larger bold fonts from the highlighter; scales for h1–h6 are 1.6, 1.4, 1.25, 1.12, 1.05 and 1.0.

**Reason.** `QPlainTextDocumentLayout` lays out each block with its own line height, so the heading block's height grows with the font (verified by `tst_mainwindow::headingFontSizesInPlainTextEdit`). The 5,000-line keystroke benchmark stays within budget.

## 2026-11-22 — Catching external edits

**Decision.** Directory watches (`QFileSystemWatcher`) catch files being created, deleted and renamed, including atomic saves by other apps. The open note is also watched as a file. When the app regains focus, the worker rescans the vault by mtime and size.

**Reason.** Linux inotify directory watches don't report in-place content writes. A focus-time rescan costs about 270 ms of background work on 6k files (15 ms on the user's vault), so it never blocks the UI.

## 2026-11-22 — Keystrokes repaint only the edited line

**Decision.** `NoteEditor` uses a `QPlainTextDocumentLayout` subclass (`BlockLocalLayout`, public Qt API). When a reported change lies within one block, it passes the change on narrowed to that block. Monospace text is sized so its ascent and descent fit within the body font's.

**Reason.** `QSyntaxHighlighter` always resets a highlighted block's formats and reports the block as changed with `(start, length, length)`. `QPlainTextDocumentLayout` adds the two lengths, decides the change spills into the next block, and repaints the whole viewport. A taller monospace font has the same effect, because changing a block's height also forces a full repaint. Measured on the 5,000-line note:

- Before: every keystroke repainted the full 984×798 viewport, 6–12 ms of painting (p95 about 24 ms on Wayland).
- After: only the line (984×19) is repainted; keystroke to repaint is about 3 ms median and 5 ms p95.

Block-count changes still take Qt's full path. The test `typingRepaintsOnlyTheEditedLine` guards this.

## 2026-11-22 — Indexing: prepared statements and bulk mode

**Decision.** `IndexStore` prepares each statement once per connection. It skips the delete step for files that are new to the index. Batches of more than 200 files run with a 64 MB page cache and `synchronous=OFF`, after which the 1 MB cache is restored and `PRAGMA shrink_memory` runs. Setting `MN_INDEX_PROFILE=1` prints phase timings.

**Reason.** Measured on the 5k-note vault, database writes dominated the first index. They went from 1,048 ms to about 785 ms, and the first index from about 1.7 s to about 1.4 s. Running without fsync is safe because the index is a disposable cache: a crash mid-batch only costs a rebuild.

## 2026-11-22 — Smaller choices

- **Link resolution step 1:** a link path "has an extension" when it ends in a recognised note or attachment extension. So `Meeting 2024.01.05` still tries `.md` first. Paths starting with `./` or `../` skip the root-exact and name-lookup steps.
- **Reading view:** single line breaks are shown as line breaks (Obsidian's default, with "strict line breaks" off). Remote images aren't fetched, because there's no network module in v1; they show as links. SVG shows as a file card unless Qt's SVG image plugin is present. Raw HTML is sanitised tag by tag to a small formatting allowlist and is never executed.
- **Tag pane:** counts are notes per tag. A parent shows the sum over itself and its nested tags.
- **File tree:** no icons. Attachments are dimmed.
- **Unlinked mentions:** FTS5 phrase query for candidates, then a whole-word, case-insensitive regex that skips text inside link syntax. They're computed only when the section is expanded.
- **Development toolchain:** Ninja 1.12.1, clang-format 23 and clang-tidy 23 weren't installed and there was no sudo. Standalone binaries were installed to `~/.local/bin` (outside the repo). On a fresh machine, use `apt install ninja-build clang-format clang-tidy`.

## 2026-11-22 — Benchmark results (Release, Wayland, KDE Plasma, 4-core laptop)

Run: `bench_index`, `bench_ui` and `bench_app` on `tools/gen_vault.py --notes 5000 --attachments 1000` and on a copy of the user's vault (324 notes, 1.3 MB of Markdown).

| Metric | Target / limit | 5k synthetic vault | User's vault (copy) |
|---|---|---|---|
| Idle memory (RSS), one note open | 60 / 100 MB | **106 MB, over the limit** (see the M0 baseline entry) | **111 MB, over the limit** |
| Launch to usable window (cache present) | 300 ms / 1 s | 237 ms | 241 ms |
| First-ever index (background) | 2 s / 5 s | 0.9–1.4 s | 95 ms |
| Keystroke to repaint, median / p95 | 8 / 16 ms | 3.7 / 6.2 ms (5,000-line note) | 2.3 / 3.2 ms |
| Quick-switcher filter per keystroke, max | 5 / 16 ms | 0.9 ms | 0.1 ms |
| Full-text search, median / p95 | 30 / 100 ms | 6.5 / 8.0 ms | 1.2 / 4.4 ms |
| Reading-view render (math cached) | 50 / 150 ms | 24 ms | 15 ms |

Notes on the numbers:
- Timings vary by ±30% with background load on this machine (VS Code's C++ indexer, builds). These were taken at a load average around 1.2.
- First render with math is slower once per session, while MicroTeX loads its fonts: 79 ms here, up to about 380 ms on a formula-heavy note.
- The user's vault has 0 unresolved links by our count. Comparing that with Obsidian's count (compat spec open question 8) needs the user.

## 2026-11-22 — Idle-memory budget accepted

**Decision.** The user accepted the measured idle memory (about 105–111 MB on KDE Plasma/Wayland with the 5k vault or their own vault open) instead of the 60 / 100 MB budget. Most of it is the toolkit's own baseline: a bare Qt Widgets window is 70 MB on this desktop. The budget table in `CLAUDE.md` is annotated. Changes should still not make memory grow noticeably; re-measure with `bench_app`.

## 2026-11-22 — Obsidian open questions closed

**Decision.** The user asked for the open questions to be settled without checking Obsidian, keeping the app's current behaviour. The decisions are listed in the compat spec under "Decided without checking in Obsidian". Only four small things changed:
- Heading links fall back to ignoring punctuation.
- A `$` preceded by a space doesn't close inline math.
- `strictLineBreaks` is imported from `app.json` and respected by the reading view.
- The unresolved-link baseline check against Obsidian is dropped.

## 2026-11-23 — Appearance settings

**Decision.** View > Appearance… sets the theme (System, Light or Dark), an accent colour, fonts (interface, editor, code, reading view) and an optional readable line length. Changes preview live and are stored app-wide in `QSettings` (group `appearance`), not per vault.

How it works:
- **Light and Dark** use the Fusion style with our own palette.
- **System** restores the desktop's style and palette, which are captured before the first change.
- **Defaults** (System theme, no accent, default fonts) don't touch the application style, palette or font at all. So the desktop look, launch time and memory are unchanged unless you change something.
- **Accent colour** goes through `Theme`, so the editor's links, the reading view and the selection colour follow it.
- **Code font** is still capped so it never makes an editor line taller (see the keystroke entry above).
- **Readable line length** centres a column of about N characters using viewport margins, which are painted in the page colour.

## 2026-11-23 — Vault health, tasks, quick capture, link suggestions, related notes

**Vault health** (Tools > Vault health…) is a read-only report on the worker thread. It covers:
- broken links;
- orphan notes (no links from other notes; the templates folder is skipped);
- empty notes;
- unused attachments (nothing links to or embeds them);
- tags that look alike (equal after ignoring case, `-`, `_` and a trailing plural `s`);
- near-duplicate notes.

**Near-duplicate notes, with no model or vector database:** MinHash over 3-word shingles of each note's prose (frontmatter, URLs and link targets removed). It uses 63 hash functions in 21 bands of 3, which finds about 94% of pairs at 50% overlap. Candidate pairs from the bands are confirmed with exact Jaccard similarity of the shingle sets, threshold 0.5. On the user's vault it finds exactly the five Syncthing `sync-conflict` copies.

**Related notes** (sidebar, under linked mentions) also use no model:
- Each note is a TF-IDF vector over words, with sublinear term frequency. Document frequencies come from an FTS5 vocabulary table (`fts5vocab`). Stop words, numbers, 1–2 letter words, and terms in more than a third of the notes are ignored.
- Candidates are the notes that FTS5 ranks highest for the note's 20 most distinctive terms, plus its link neighbours.
- The score is cosine similarity, plus 0.1 for a direct link and 0.03 per shared tag. A tag only counts if fewer than a quarter of notes have it.
- The UI shows the reason: linked, shared tags, and the top shared terms.
- Document frequencies and tag counts are loaded once per index change. On the user's vault this takes about 30 ms per note.

**Tasks** (left-hand Tasks tab) lists every `- [ ]` / `- [x]` item from the index, which is now at schema v2 with a `tasks` table. Due dates come from `📅 YYYY-MM-DD`, `due:YYYY-MM-DD` or `[due:: YYYY-MM-DD]`. The list can be filtered by words, `#tag` (in the task, or on its note) and `folder/`, and shown as open, done or all. It's read-only for now: clicking a task opens it in its note.

**Quick capture** writes to a note the user hasn't opened, which is an amendment to the `CLAUDE.md` hard rule agreed when the user asked for this feature.
- `markdown-notes --capture` opens only a small box (about 230 ms on KDE/Wayland) and appends `- {{time}} {{text}}` to `Inbox.md`, or to today's daily note.
- Appends never change existing bytes, use the file's own line-ending style, are atomic, and refuse binary or invalid UTF-8 files. Hidden or `..` targets are refused too.
- Wayland doesn't let an app register a global hotkey, so the user binds a desktop shortcut to the command. File > Quick capture (Ctrl+Shift+C) does the same inside the app; when the target note is open, the text goes into the editor as an undoable edit instead.
- Daily-note settings are imported from `.obsidian/daily-notes.json`.

**Link suggestions:**
- After a 350 ms pause in typing, the current paragraph is matched against a table of note names and aliases (case- and accent-insensitive phrases, longest match first).
- Text inside links, code, URLs, tags, comments and math is skipped. Single-word names need at least 4 letters and must not be stop words or numbers. Names shared by two notes are skipped.
- Matches get a dotted accent underline and a status-bar hint. Alt+Enter or the context menu links one, as `[[Name]]` or `[[Name|as written]]` (or a Markdown link if that's the vault's setting). Esc dismisses them for the open note.
- It can be turned off under Edit > Suggest links while typing.

## 2026-11-23 — Tasks can be ticked off and deleted; tag sorting; pane toggles

**Tasks.** The user asked for this, so the Tasks view now edits notes: it's the second exception to "never edit a note that isn't open", and `CLAUDE.md` is amended to match.
- **Ticking** (checkbox, Space, or the context menu) changes the one byte between the brackets.
- **Delete** (context menu or the Delete key, after a confirmation) removes the item's lines, including any nested items under it.
- **Safety check:** the edit is planned against the note's current bytes, after finding the task at its indexed line with the same text. If the note shifted since indexing, the task is found by its text, but only if that text is unique. Otherwise nothing is written and the list refreshes.
- **Open notes:** if the note is open, the edit goes through the editor (undoable) and is saved. Otherwise the file is rewritten atomically, keeping its BOM and line endings.

**Tags pane.** Sort by name, most used first, or least used first. Counts sort numerically, ties stay alphabetical, and the choice is remembered. Nested tags sort within each level by their summed counts.

**Panes.** View > Left sidebar (Ctrl+Shift+B), Right panel (Ctrl+Shift+R) and Reset layout. They work through the splitter sizes, so a pane collapsed by dragging can always be brought back, and the menu checkmarks follow the splitter. Ctrl+Alt+L was avoided because KDE uses it to lock the screen.

## 2026-11-23 — Tags for a category-based vault

The user's vault isn't organised in folders: its seven category tags (tech, projects, research, science, personal, humanities, meta) carry the structure. Every note names two of them in `primary:` / `secondary:` properties and normally repeats them in `tags:`. In the user's vault, 8 of the 324 notes repeat neither, so they were missing from the tag pane under their categories.

- **Properties that count as tags** (vault setting, opt-in, default empty). The values of these frontmatter properties are indexed as tags. This deviates from Obsidian, whose tag pane only reads `tags:`, so it's off unless the user sets it. Notes are never edited. Changing the setting re-indexes every note; the index records the setting in a new `meta` table (schema v3), so a change made while the app was closed is picked up too.
- **Category tags and a minimum per note** (vault setting, default 2). Category tags are listed first and in bold in the tag pane, under every sort order. Vault health reports notes with fewer categories than the minimum; templates are skipped.
- **Detect from vault** (Vault settings) suggests both settings. A frontmatter property counts as holding a category if at least half the notes set it to a short one-word value, it uses 2–15 distinct values, and each value is shared by two or more notes on average (so summaries and titles don't qualify). On the user's vault it finds exactly `primary, secondary` and the seven categories. With those settings, only `Home.md` (no frontmatter) and `pininfo.md` have fewer than two categories.
- **Tag filters in search:** words starting with `#` filter by tag (nested tags included, and all must match), alongside the full-text words. For example: `#tech #python pandas`. A query of only tags lists those notes.
- **Drill-down tree** (Tags tab, "Group by other tags"). Clicking a tag shows its notes as a folder-like tree. Each note is filed exactly once, at the path formed by its other tags: category tags first (in category order), then the remaining tags from most to least used, up to 4 levels deep. Branches with a single note are folded into their parent. "Plain list" shows the flat list instead.
- **Fixed:** the editor theme was computed while the editor was still disabled (before a note opens), so headings got the disabled text colour. `Theme` now always reads the Active colour group.


## 2026-11-23 — Pinned tags

- **Pinned tags** (right-click a tag in the Tags tab: Pin to top, Unpin, Move up or down). They are listed first, in pin order, whatever the sort setting. A pinned nested tag (`tech/ai`) gets its own top-level entry under its full name and also stays in place under its parent. The pins are saved in `.markdown-notes/settings.json` as a new optional `pinnedTags` list. Older settings files without it still load, so the format version stays 1. A pin is kept even while no note has that tag, so it comes back when the tag does.
- The app remembers which left tab (Files, Search, Tags or Tasks) was open, so a vault browsed by tags opens on the Tags tab.

## 2026-11-24 — Passages: search and related passages by topic, not by note

- **Problem:** daily notes mix several topics, so ranking whole notes buries a topic that is one paragraph of a long page.
- **Chunker** (`core/index/Chunker`, heuristic, no models): splits each note into passages of roughly one topic.
  - Units are paragraphs, top-level list items and code/math/comment blocks (never split). A paragraph over 250 words written as separate lines is split into its lines.
  - Within a heading section, neighbouring units merge while the chunk is under 15 words, or while they share vocabulary (cosine ≥ 0.1 of their word counts) and stay under 250 words. A block joins the text before it.
  - A section under a heading of up to 120 words stays whole. Headings mark topics, and short bullet lists share few words. Text before the first heading, like a daily note's, is split by topic.
  - A heading directly above a subheading isn't a passage of its own; the subsections carry it as context ("Jenkins › Job types › Freestyle").
- **Index schema v4:** a `chunks` table holds line ranges and the heading path, and `chunk_fts` is a contentless FTS5 table (`contentless_delete=1`) over the passage text plus note name and headings. Passage text is sliced from `fts.body` when shown, so note text isn't stored twice.
- **Passage search** (Search › Passages): BM25 over passages. Sorted by best match, newest or oldest. The date comes from a `YYYY-MM-DD` in the file name, else the modification time. A `#tag` filter keeps passages that contain the tag inline, or whose note has it in frontmatter. Notes mode is unchanged and stays the default.
- **Related to this paragraph** (right panel), 0.8 s after the cursor rests:
  - The open buffer is chunked on the worker thread, and the passage at the cursor is matched against other notes' passages (FTS5 candidates on its 12 most distinctive words, then TF-IDF cosine).
  - Plurals are folded ("jobs" = "job"), and at least two distinctive shared words are required: single shared words gave false matches such as Jenkins "jobs" for job applications.
  - At most two passages per note, five in all.
- **Cost:** about 6 passages per note on the 5k benchmark vault and 10 on the user's. The chunker was rewritten to hash words into sorted integer vectors (no allocation per word): 1.3 s down to 0.43 s single-threaded for 5k notes, run on the parallel parse threads. The extra FTS inserts add about 0.5 s (about a third) to a first-time index of the 5k vault. Passage search is about 2–3× the cost of note search, still inside the search budget. Nothing new is held in memory at idle. An FTS5 `hashsize` increase was tried and made no measurable difference.

## 2026-11-24 — A local-model assistant (MN_AI)

- **Scope, kept small on purpose:** suggest frontmatter and tags for the open note; ask questions answered from the notes; semantic (meaning) search over passages, also used by "Related to this paragraph". No autonomous actions and no bulk edits.
- **Safety:** the assistant never writes a note. A suggestion opens a review dialog where every value can be edited or cleared, and a preview shows the exact frontmatter result.
  - Apply goes into the editor as one undoable edit and is saved like typing. It only adds missing keys and tags, never changes existing values, and leaves unreadable frontmatter alone.
  - The edit is planned against the buffer at apply time, in case the note changed while the model was thinking.
  - New tags are added in the note's existing style (block list, flow list or string), and every line keeps its own line ending.
- **Protocol:** OpenAI-compatible `/v1/chat/completions` and `/v1/embeddings`, non-streaming. This covers Ollama, LM Studio, llama.cpp and vLLM. `<think>` blocks are stripped, and JSON is pulled out of prose or code fences. An optional separate embedding server covers llama.cpp's one-model-per-server setup.
- **Settings** are app-wide `QSettings`, because the server belongs to the machine, not the vault. The API key is stored there in plain text; it is optional and usually unused for local servers.
- **Build:** QtNetwork lives in `mn_ai`, behind the `MN_AI` option (on by default), as CLAUDE.md reserved it. `QNetworkAccessManager` is created only on first use.
- **Vectors:**
  - Stored unit-length in `embeddings.sqlite` next to the index, keyed by (passage hash, model). They survive index rebuilds, renames and moves. Index schema v5 adds `chunks.hash`.
  - Each vector is stored as a float scale plus one int8 per dimension, in a rowid table. This replaced float32 in a WITHOUT ROWID table: 3 KB rows spilled onto overflow pages.
  - On the 5k benchmark vault (20,528 passages, 768 dimensions), the file went from 96 MB to 18 MB, and hybrid search from 91 ms to 41 ms median (51 ms max). About 20 ms of that is the keyword half.
  - The query is quantised too, so the scan is integer dot products with four independent sums. int8 is plenty for ranking.
  - Embedding runs in batches of 16 after each index update. A server error pauses it with a status-bar message.
  - Searches scan the vectors from disk, so idle memory is unchanged: about 107 MB with or without `MN_AI`, measured back to back.
- **Hybrid ranking:** FTS5 passage hits and vector hits are merged by reciprocal rank fusion (k = 60).
  - Questions use any-word keyword matching on their rarest words; searches use all words.
  - `#tag` queries stay keyword-only.
  - In related passages, semantic hits must be within 0.15 of the best match, and at least 0.5, since similarity scales differ between models.
- **Frontmatter prompt:** includes the vault's categories (tag properties and category tags), the 150 most used tags, and the frontmatter of up to 3 similar notes as style examples. Replies are checked: categories must be known and are used once each, tags are cleaned and given the vault's existing spelling.
- **Tested** against a fake OpenAI-compatible server (`tests/FakeAiServer.h`) whose embeddings put synonyms on the same dimension. This shows semantic search finding the sleep passages for "insomnia", a word in no note, and checks that the vault's bytes never change.

## 2026-11-24 — Tag filter; categories when creating a note

- **Tag filter:** "Find a tag…" above the tag list shows tags containing the text, case-insensitive and ignoring a leading `#`. Parents of matching nested tags stay visible and open. Enter selects the first match, Down moves into the list, Esc clears the filter, and the filter survives index refreshes.
- **New note with categories:** when the vault settings define both category properties (tag properties such as `primary` and `secondary`) and category tags, Ctrl+N and the file tree's "New note" show a dialog first.
  - It asks for a name and offers one dropdown per property, listing the categories plus "(none)". Picking a category already used by an earlier property clears the later one.
  - The chosen values become the new file's frontmatter (`primary: tech`, and so on). Unset properties are left out, and Cancel creates nothing.
  - An existing name is refused rather than opened.
  - Vaults without categories keep the old behaviour: an "Untitled" note is created straight away.
  - Notes created from links and the quick switcher are unchanged, because they already have a name and are meant to be quick.

## 2026-11-24 — Images and drawings in quick capture

- **Images:** pasting an image (a screenshot) or an image file into the capture box lists it as a thumbnail under the text. Ctrl+D or Draw opens a blank drawing, and double-clicking a thumbnail draws over that image. Delete removes one. Esc asks before discarding a capture that has images.
- **Saving:** nothing is written until Enter. The images are then saved as new files in the attachment folder for the target note (inbox or today's daily note), named as Obsidian names pastes (`Pasted image YYYYMMDDHHmmss.png`, `Drawing YYYYMMDDHHmmss.png`). A dropped file keeps its name, and keeps its bytes unless it was drawn on. An embed per image follows the captured text, indented inside the list item, so `- 09:05 text` gains a `  ![[Pasted image ….png]]` line.
  - This extends the quick-capture exception in `CLAUDE.md`: capture also creates new attachment files. It never changes an existing file other than the append.
  - The target note is checked before any image is saved, so a binary target or a hidden attachment folder writes nothing. If the note can't be written, the images just saved are removed.
- **Links** use the vault's link settings, through `newFileLink` (`core/links/NewLink`), which the editor's paste now shares. A shortest-form link uses the full path when another file already has the name. In-app the index answers that. The standalone `--capture` process has no index and treats the name as free, since the names carry the time to the second.
  - The editor previously checked this for Markdown links only once two other files had the name. It now checks for any, like wikilinks.
- **Drawings** are plain PNGs, so Obsidian shows them without plugins. The canvas has a pen, highlighter (semi-transparent, drawn as one path so crossings don't darken) and rectangle, five colours, three widths, undo, redo and clear.
  - Widths are in screen pixels, converted to image pixels at the current zoom, and strokes are drawn onto the image at full size.
  - Strokes stay editable until the capture is saved: reopening a drawing can undo them one at a time.
  - Blank drawings are 1600×1000 on white, readable in light and dark themes. The background and finished strokes are cached at view size, so a stroke in progress doesn't rescale a large screenshot on each mouse move.
  - Not included: pressure from pen tablets (Qt turns tablet input into mouse input), cropping, text, and annotating images already in notes.
