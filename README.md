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
- **Passage search:** switch Search from Notes to Passages to get just the parts of notes that match, not whole notes. Sorted newest or oldest first, a topic spread over months of daily notes reads as one timeline. `#tag` filters keep passages that contain the tag.
- **Related notes** in the right panel, based on shared words, links and tags, and **related to this paragraph**: passages in other notes about the paragraph you're in.
- **Tasks** tab: every `- [ ]` in the vault, with due dates (`2025-01-31` or `due:2025-01-31`). Tick to complete; Delete removes a task.
- **Tags as structure:** clicking a tag shows its notes as a tree grouped by their other tags. Right-click a tag to pin it to the top of the list. Vault settings can count properties like `primary` / `secondary` as tags and define category tags. Search accepts `#tag` filters.
- **Link suggestions:** mentions of note names are underlined as you type; Alt+Enter links one.
- **Quick capture:** `markdown-notes --capture` appends a line to `Inbox.md` (or today's daily note). Bind it to a global shortcut in your desktop settings.
- **Appearance** (View): theme, accent colour, fonts, readable line length.

## AI assistant (optional, local)

Connect your own model server in **Tools › AI assistant settings**: any server with the OpenAI API (Ollama, LM Studio, llama.cpp's `llama-server`, vLLM). Set a chat model, an embedding model, or both; "Test connection" checks them. Notes' text goes only to that server, and the assistant never changes a note by itself.

- **Suggest frontmatter and tags** (Ctrl+Shift+M): proposes category properties (from your vault settings), tags (preferring ones you already use) and a summary, in the style of similar notes. You review and edit them; Apply adds only what's missing, as one undoable edit.
- **Ask** tab (Ctrl+Shift+A): questions answered from your notes, citing the passages used; click a citation to open it.
- **Semantic search:** with an embedding model, passages are embedded once in the background (progress in the status bar), and Search gets a **Meaning** mode that finds passages by meaning as well as words. "Related to this paragraph" uses it too.

With llama.cpp, which serves one model per server, put the embedding server's address in "Embedding server". Build with `-DMN_AI=OFF` to leave the assistant (and Qt Network) out.

## Keys

| Key | Action |
|---|---|
| Ctrl+O | Quick switcher (Shift+Enter creates a note) |
| Ctrl+Shift+F | Search |
| Ctrl+E | Reading view |
| Ctrl+N / F2 | New note / rename (updates links) |
| Ctrl+F / Ctrl+H | Find / replace |
| Ctrl+Shift+C | Quick capture |
| Ctrl+Shift+M / A | Suggest frontmatter / ask your notes |
| Ctrl+Shift+B / R | Toggle left / right panel |
| Alt+Left / Right | Back / forward |
| Ctrl+click | Follow link (creates missing notes) |

## Licence

MIT. Third-party licences are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); Qt is LGPL, linked dynamically.
