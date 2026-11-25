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

To install for the current user on Linux (launcher entry, icon and a Meta+Shift+N quick-capture shortcut on KDE), run `./install.sh`. Options: `--appimage FILE`, `--desktop-icon`, `--uninstall`.

Packaging: `packaging/linux/build-appimage.sh` (AppImage) and `packaging/windows/build-zip.ps1` (portable zip).

## Beyond Obsidian

- **Vault health** (Tools): broken links, orphan and empty notes, unused attachments, similar tags, near-duplicate notes, and notes missing category tags.
- **Passage search:** Search › Passages returns matching sections rather than whole notes, sortable by date. `#tag` filters apply per passage.
- **Related notes** and **related passages** for the current paragraph, in the right panel.
- **Tasks** tab: all `- [ ]` items in the vault, with due dates (`2025-01-31` or `due:2025-01-31`). Tasks can be completed or deleted from the list.
- **Properties as a form, not YAML:** Ctrl+N (new note) and Ctrl+T (Edit › Properties) show the note's properties as a form: category dropdowns, a fuzzy tag picker that lists near-duplicates together, status choices, summary, aliases and sources (one per line), and created (today on new notes). A link on the clipboard becomes a new note's source. Only changed values are written, other properties are left alone, and blank fields can be written empty so every note has the same keys. With the assistant, Suggest fills the empty fields. The property list is set in Vault settings › Properties and tags. Typing in a frontmatter `tags:` list autocompletes too.
- **Tags:** filter, sort and pin tags; selecting a tag shows its notes grouped by their other tags. Frontmatter properties such as `primary` / `secondary` can count as tags, and with category tags configured, new notes (Ctrl+N) offer them as dropdowns.
- **Link suggestions:** mentions of note names are underlined while typing; Alt+Enter converts one to a link.
- **Today's note** (Alt+D, or `markdown-notes --today`): opens today's daily note, creating it first with the daily-note tags (`journal` by default, set in Vault settings).
- **Quick capture:** `markdown-notes --capture` appends a line to `Inbox.md` or today's daily note. Paste images or draw (Ctrl+D) in the box, or double-click an image to draw on it; they are saved to the attachment folder and embedded.
- **Appearance** (View): theme, accent colour, fonts and line length.

## AI assistant (optional)

Configure an OpenAI-compatible server (Ollama, LM Studio, llama.cpp, vLLM) in **Tools › AI assistant settings**. Note content is sent only to that server, and notes are only changed when you apply a suggestion.

- **Suggest properties** (Ctrl+Shift+M, or Suggest in the Properties form): fills the form's empty fields with categories, tags and a summary for you to review; existing values are kept.
- **Ask** (Ctrl+Shift+A): answers questions from your notes, with cited sources.
- **Semantic search:** with an embedding model, Search › Meaning matches passages by meaning as well as keywords. Passages are indexed in the background.

For llama.cpp, which serves one model per process, set "Embedding server" separately. Build with `-DMN_AI=OFF` to exclude the assistant and Qt Network.

## Keys

| Key | Action |
|---|---|
| Ctrl+O | Quick switcher (Shift+Enter creates a note) |
| Ctrl+Shift+F | Search |
| Ctrl+E | Reading view |
| Ctrl+N / F2 | New note (name and properties) / rename (updates links) |
| Ctrl+T | The note's properties |
| Ctrl+F / Ctrl+H | Find / replace |
| Alt+D | Today's note |
| Ctrl+Shift+C | Quick capture |
| Ctrl+Shift+M / A | Suggest properties / ask your notes |
| Ctrl+Shift+B / R | Toggle left / right panel |
| Alt+Left / Right | Back / forward |
| Ctrl+click | Follow link (creates missing notes) |

## Licence

MIT. Third-party licences are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); Qt is LGPL, linked dynamically.
