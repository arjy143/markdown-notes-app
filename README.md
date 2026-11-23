# Markdown Notes

A fast, native desktop editor for Obsidian vaults, for Linux (Wayland) and Windows. Qt Widgets, no web views. It has been designed to be pretty much fully compatible with existing Obsidian vaults.

![alt text](image.png)

## Build

Needs CMake ≥ 3.25, Ninja, a C++20 compiler and Qt ≥ 6.5. Everything else is in `third_party/`.

```sh
cmake --preset release && cmake --build --preset release
./build/release/src/markdown-notes [vault-folder]
ctest --preset debug    # tests (after building the debug preset)
```

To install it for your user on Linux (launcher entry, icon, and a Meta+Shift+N quick-capture shortcut on KDE), run `./install.sh`. Use `--appimage FILE` to install an AppImage instead, `--desktop-icon` to add a desktop launcher, and `--uninstall` to remove it.

Packaging: `packaging/linux/build-appimage.sh` (AppImage) and `packaging/windows/build-zip.ps1` (portable zip).

## Beyond Obsidian

- **Vault health** (Tools): broken links, orphan and empty notes, unused attachments, look-alike tags, near-duplicate notes, and notes with too few category tags.
- **Related notes** in the right panel, based on shared words, links and tags.
- **Tasks** tab: every `- [ ]` in the vault, with due dates (`2025-01-31` or `due:2025-01-31`). Tick to complete; Delete removes a task.
- **Tags as structure:** clicking a tag shows its notes as a tree grouped by their other tags. Right-click a tag to pin it to the top of the list. Vault settings can count properties like `primary` / `secondary` as tags and define category tags. Search accepts `#tag` filters.
- **Link suggestions:** mentions of note names are underlined as you type; Alt+Enter links one.
- **Quick capture:** `markdown-notes --capture` appends a line to `Inbox.md` (or today's daily note). Bind it to a global shortcut in your desktop settings.
- **Appearance** (View): theme, accent colour, fonts, readable line length.

## Keys

| Key | Action |
|---|---|
| Ctrl+O | Quick switcher (Shift+Enter creates a note) |
| Ctrl+Shift+F | Search |
| Ctrl+E | Reading view |
| Ctrl+N / F2 | New note / rename (updates links) |
| Ctrl+F / Ctrl+H | Find / replace |
| Ctrl+Shift+C | Quick capture |
| Ctrl+Shift+B / R | Toggle left / right panel |
| Alt+Left / Right | Back / forward |
| Ctrl+click | Follow link (creates missing notes) |

## Licence

MIT. Third-party licences are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); Qt is LGPL, linked dynamically.
