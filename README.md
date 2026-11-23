# Markdown Notes

A native, lightweight desktop app for reading and writing an Obsidian vault (a folder of Markdown notes), for Linux (Wayland) and Windows. Qt Widgets, no web views.

- **Safe:** it only writes a note you changed, atomically, keeping its BOM, line endings and trailing newline. Files it can't round-trip exactly open read-only.
- **Compatible:** follows [docs/obsidian-compat.md](docs/obsidian-compat.md): wikilinks, embeds, tags, frontmatter, callouts, math, block IDs, and Obsidian's link resolution.
- **Fast:** see the budgets in `CLAUDE.md` and the results in [docs/decisions.md](docs/decisions.md).

## Build

Requirements: CMake ≥ 3.25, Ninja, a C++20 compiler (GCC/Clang or MSVC), and Qt ≥ 6.5 (Core, Gui, Widgets, Test). Everything else is vendored in `third_party/`.

```sh
cmake --preset debug && cmake --build --preset debug     # debug build
ctest --preset debug --output-on-failure                 # tests
cmake --preset release && cmake --build --preset release # release build
./build/release/src/markdown-notes [vault-folder] [note]
```

On Linux, `cmake --preset asan` builds with AddressSanitizer and UBSan.

## Benchmarks

```sh
python3 tools/gen_vault.py --out /tmp/vault5k --notes 5000 --attachments 1000
./build/release/bench/bench_index --vault /tmp/vault5k   # indexing, search, backlinks
./build/release/bench/bench_ui --vault /tmp/vault5k      # editor keystrokes, quick switcher, reading view
./build/release/bench/bench_app --vault /tmp/vault5k     # launch time, idle memory
```

Only point these at a generated vault or a **copy** of a real vault. They create `.markdown-notes/settings.json` in it.

## Packaging

- Linux AppImage: `packaging/linux/build-appimage.sh`
- Windows portable zip: `packaging/windows/build-zip.ps1`

## Features beyond Obsidian

- **Vault health** (Tools menu): broken links, orphan and empty notes, unused attachments, tags that look alike (`#todo`/`#to-do`), and near-duplicate notes (such as sync-conflict copies).
- **Related notes** in the sidebar, worked out from shared distinctive words, links and tags. No AI model is needed.
- **Tasks** tab: every `- [ ]` item in the vault, with due dates (`📅 2025-01-31` or `due:2025-01-31`), filtered by words, `#tag` or `folder/`. Tick a box to complete a task. Delete (or the right-click menu) removes it from its note, after confirming. Double-click opens it.
- **Tags as structure:** click a tag in the Tags tab to see its notes grouped by their other tags as a tree (categories first). Vault settings can count frontmatter properties such as `primary` / `secondary` as tags, mark your top-level **category tags** (shown first, and checked by Vault health against a minimum per note), and **Detect** both from your notes. Search accepts tag filters: `#tech #python pandas`.
- **Link suggestions:** mentions of note names are underlined while you type. Alt+Enter turns one into a link; Esc dismisses them.
- **Quick capture:** `markdown-notes --capture` opens a one-line box and appends what you type to `Inbox.md` (or today's daily note; see Vault settings). To use it from anywhere on KDE, open System Settings > Keyboard > Shortcuts > Add New > Command or Script, enter `markdown-notes --capture` (the full path to the AppImage if you use one), and pick a key. On Windows, put a shortcut to `markdown-notes.exe --capture` in the Start menu and set its "Shortcut key". Inside the app it's Ctrl+Shift+C.

## Looks

View > Appearance… changes the theme (System, Light, Dark), the accent colour, the interface, editor, code and reading-view fonts, and an optional readable line length. Per-vault behaviour (link format, attachment folder, templates, strict line breaks, …) is under File > Vault settings….

## Keys

| Key | Action |
|---|---|
| Ctrl+O | Quick switcher (Shift+Enter creates a note) |
| Ctrl+Shift+F | Search the vault |
| Ctrl+E | Toggle reading view |
| Ctrl+N | New note |
| F2 | Rename / move the open note (updates links after confirmation) |
| Ctrl+F / Ctrl+H | Find / replace in the note |
| Alt+T | Insert template |
| Alt+Enter | Link the suggested note mention at the cursor |
| Ctrl+Shift+C | Quick capture |
| Ctrl+Shift+K | Tasks |
| Ctrl+Shift+B / Ctrl+Shift+R | Show or hide the left sidebar / right panel |
| Alt+Left / Alt+Right | Back / forward |
| Ctrl+click | Follow a link (an unresolved link creates the note) |

## Licence

MIT (see [LICENSE](LICENSE)). Third-party licences are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Qt is used under the LGPL and linked dynamically.
