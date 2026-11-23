# Obsidian vault compatibility spec

This document is the source of truth for how Markdown Notes reads, interprets and writes a vault. Where Obsidian's exact behaviour wasn't checked, the user asked for the current behaviour to be kept; those choices are listed under "Decided without checking in Obsidian", each with a fixture test. New uncertain cases go under "Open questions".

## 1. The vault

- A vault is a folder. Every path is stored relative to the vault root, using `/` separators, keeping the case it has on disk.
- **Hidden:** any file or folder whose name starts with `.` (for example `.obsidian/`, `.trash/`, `.git/`, `.markdown-notes/`). Hidden items are never listed, indexed or linked to.
- **Notes** are `*.md` files.
- **Attachments** (shown in the file tree, can be linked and embedded):
  - images: `png jpg jpeg gif bmp svg webp avif`
  - documents: `pdf`
  - audio/video: `mp3 wav m4a ogg flac webm mp4 mkv mov`

  Other file types are hidden unless the user turns on "show all files".
- Names Obsidian doesn't allow in new files: `* " \ / < > : | ?`. Characters that break links: `# ^ [ ] |`. When creating or renaming a file, refuse these characters and say why. Existing files with these names must still open.
- Folders can be nested arbitrarily deep. Empty folders still appear in the file tree.

## 2. Markdown dialect

The base is CommonMark plus these GFM features, via md4c: tables, strikethrough `~~x~~`, task lists `- [ ]` / `- [x]`, and permissive autolinks. Obsidian extensions are layered on top. In every case below, text inside code spans and fenced or indented code blocks is never interpreted.

| Syntax | Meaning | Notes |
|---|---|---|
| `[[target]]`, `[[target\|alias]]` | Wikilink | See §3 for the target grammar |
| `![[target]]` | Embed | See §4 |
| `[text](target)`, `[text](<target with spaces>)` | Markdown link | Target is URL-decoded (`%20` → space). It's an external link if it has a scheme (`https:`, `mailto:` etc.) |
| `![alt](target)`, `![alt\|300](target)` | Markdown image/embed | A number after `\|` in the alt text sets the width |
| `#tag` | Tag | See §5 |
| `==text==` | Highlight | |
| `%%text%%` | Comment | Can span several lines or blocks. Hidden in the reading view, and skipped when indexing links and tags |
| `$x$` / `$$x$$` | Inline / display math | `$$` blocks can span several lines. A `$` followed by a space doesn't open inline math, and a `$` preceded by a space doesn't close it |
| `> [!type] Title` | Callout | Type is case-insensitive. `[!type]-` starts folded and `[!type]+` starts unfolded. The title is optional (default: the type, capitalised). Unknown types show as `note` |
| `^block-id` at the end of a block | Block ID | Letters, digits and `-` only. It can also sit on its own paragraph directly after a list or table, and then applies to that list or table |
| `[^1]` / `[^1]: text` | Footnote | Rendered (md4c 0.6 supports footnotes, and with them off `[^1]: text` would become a link reference definition). See `decisions.md` and open question 17 |
| `---` frontmatter | Properties | See §6 |

HTML inside notes is passed to the reading view only as far as Qt's rich-text subset supports it. Anything else is shown as plain text, never executed.

## 3. Links and resolution

**Target grammar.** `linkpath[#subpath][|alias]`, where `subpath` is either:

- `#Heading` (nested headings are written `#H1#H2`), or
- `#^blockid`.

An empty linkpath (`[[#Heading]]`) refers to the current note. A `.md` extension in the linkpath is optional for notes. Attachments must include their extension.

**Resolving a linkpath.** Used for both wikilinks and Markdown links. Mirrors Obsidian's `getFirstLinkpathDest`:

1. If the linkpath doesn't end in a recognised note or attachment extension, try it with `.md` added, then as written.
2. **Exact path from the vault root.** Skipped for paths starting with `./` or `../`.
3. **Path relative to the source note's folder.** Handles `./` and `../`.
4. **Name lookup.** Compare case-insensitively against every file whose vault path ends with `/linkpath` (or equals it). If exactly one matches, use it. Skipped for `./` and `../` paths.
5. If several match, prefer: (a) a file in the same folder as the source note, otherwise (b) the shortest path, otherwise (c) alphabetical order.
6. If nothing matches, the link is **unresolved**. It's shown in a distinct style, and following it creates the note in the configured new-note location (§8).

Path comparison is case-insensitive on every platform, but the file's real case is always kept.

**Heading subpaths** match a heading's text with formatting stripped, case-insensitively, after collapsing whitespace. If nothing matches, punctuation is ignored too (`[[Note#Setup step 1]]` finds `## Setup: step 1`). Duplicate headings resolve to the first. For nested subpaths (`#H1#H2`) the last segment decides. If the heading isn't found, open the note at the top.

**Aliases** (from frontmatter) are suggested in autocomplete, which inserts `[[Real Name|alias]]`. Obsidian does not resolve `[[alias]]` to the note, so neither do we.

**Backlinks:** every resolved link or embed pointing at a note. **Unlinked mentions:** whole-word, case-insensitive matches of the note's name or aliases in other notes that aren't already links.

## 4. Embeds

- `![[image.png]]`, `![[image.png|300]]` (width), `![[image.png|300x200]]` (width × height). Images are scaled down to fit the view width, keeping aspect ratio.
- `![[Note]]` shows the whole note inline, excluding its frontmatter.
- `![[Note#Heading]]` shows from that heading up to the next heading of the same or higher level.
- `![[Note#^id]]` shows the block that has that ID.
- Recursion is limited to depth 3, and cycles are detected. A cycle or missing target shows a placeholder line, not an error dialog.
- `![[file.pdf]]` and audio/video files show a clickable card that opens the file in the system's default app. No inline players in v1.

## 5. Tags

- Inline: `#` followed by letters (any script), digits, `_`, `-` or `/`. It must contain at least one non-digit character (`#2024` isn't a tag). The `#` must be at the start of a line or follow whitespace or an opening bracket. It ends at the first character not in that set (so `#tag.` is `tag`, and an emoji ends a tag). `word#tag` isn't a tag. Tags inside link text aren't counted.
- Nested tags: `#a/b/c` also counts towards `#a` and `#a/b` in the tag pane.
- Frontmatter: `tags:` as a YAML list or a single string, comma- or space-separated, with or without `#`. The legacy keys `tag` and `Tags` are accepted too. An `aliases:` string is one alias (it isn't split on commas).
- Matching and grouping are case-insensitive. The tag pane shows the most common spelling.

## 6. Frontmatter

- Frontmatter is a YAML block that starts with `---` on the **first line** of the file and ends at the next line that is exactly `---`. Anything else is treated as body text.
- Parse only what we need: `tags`, `tag`, `aliases`, `alias`, `cssclasses`. Use a small hand-written YAML subset parser: scalars, quoted strings, block lists (`- x`) and flow lists (`[a, b]`).
- If the YAML is invalid, ignore the properties, keep the text untouched and show a small warning in the editor. Never "fix" it.
- The reading view shows properties as a compact key/value table. The editor shows the raw YAML, with highlighting.

## 7. Writing files

- Encoding is UTF-8. The BOM and line-ending style (LF or CRLF) are preserved per file, and so is the trailing newline or its absence. A new file uses LF and ends with a newline.
- A save writes the editor buffer exactly as it is, with no automatic formatting.
- **Rename/move** a note or attachment:
  1. Work out every resolved link that points at it.
  2. Show the list of affected files and ask for confirmation.
  3. Rewrite each link in the same style it was written in (wikilink or Markdown link; shortest, relative or absolute path), keeping its alias and subpath. A shortest-form link only gets a path added if the new name would be ambiguous.
  4. Rewrite each affected file as one atomic write.

  If rewriting a file fails, report exactly which files were updated and which weren't.
- **Pasted or dropped images** are saved to the attachment folder (§8) as `Pasted image YYYYMMDDHHmmss.png`, the same as Obsidian, adding ` 1`, ` 2`, … if the name already exists. An embed is inserted in the configured link format.
- **New notes** from an unresolved link get the link's text as their filename, in the configured location. "Untitled" notes are named `Untitled`, `Untitled 1`, ….

## 8. Settings imported from `.obsidian/`

When a vault is opened for the first time and `.markdown-notes/settings.json` doesn't exist yet, read these as defaults. `.obsidian/` is never written to.

| Source | Key | Values | Default if missing |
|---|---|---|---|
| `app.json` | `attachmentFolderPath` | `/` (vault root), `./` (same folder as note), `./sub` (subfolder of the note's folder), `folder/path` | `/` |
| `app.json` | `newLinkFormat` | `shortest`, `relative`, `absolute` | `shortest` |
| `app.json` | `useMarkdownLinks` | bool | `false` |
| `app.json` | `newFileLocation` + `newFileFolderPath` | `root`, `current`, `folder` | `root` |
| `app.json` | `strictLineBreaks` | bool (reading view joins single line breaks) | `false` |
| `app.json` | `alwaysUpdateLinks` | bool (if false, rename asks every time; if true, it still shows the list) | `false` |
| `templates.json` | `folder`, `dateFormat`, `timeFormat` | | none, `YYYY-MM-DD`, `HH:mm` |

## 9. Templates (core Templates plugin behaviour)

- "Insert template" lists every `.md` file in the template folder and inserts the chosen one at the cursor, after substituting:
  - `{{title}}` → the current note's filename without extension
  - `{{date}}` and `{{time}}` → the configured formats
  - `{{date:FORMAT}}` and `{{time:FORMAT}}` → an explicit format
- Date formats use a Moment.js subset: `YYYY YY MMMM MMM MM M DD D dddd ddd HH H hh h mm m ss s A a Do`, plus text in `[...]` copied literally. Unknown tokens are copied unchanged.
- If the template has frontmatter and the note already has some, merge the keys (the note's values win; template keys the note lacks are appended). Otherwise insert it at the top.

## Decided without checking in Obsidian

The user chose to keep the app's current behaviour rather than verify these in Obsidian. Each has a fixture test.

1. **Name tie-break** (§3 step 5): same folder, then shortest path, then alphabetical.
2. **Comments**: links, tags, headings and block IDs inside `%%…%%` are ignored.
3. **Aliases**: `[[alias]]` doesn't resolve. Aliases are only offered by autocomplete and the quick switcher.
4. **Heading links**: case-insensitive with whitespace collapsed, then with punctuation ignored. Duplicates go to the first heading.
5. **Tag boundaries**: `(#tag)` and `[#tag]` count; `word#tag` doesn't; `#tag.` is `tag`; an emoji ends a tag; tags inside link text don't count.
6. **Block ID on its own paragraph** after a list or table applies to that list or table. At the end of a list item it applies to the item.
7. **Template frontmatter**: keys are merged, the note's values win, and template keys the note lacks are appended.
8. **Unresolved-link baseline** against Obsidian: dropped. Tools > Unresolved links shows our count.
9. **Single line breaks** in the reading view are line breaks unless `strictLineBreaks` is on (imported from `app.json`, editable in Vault settings).
10. **`aliases: a, b` as a string** is one alias.
11. **"Has an extension"** (§3 step 1) means a recognised note or attachment extension, so `[[Meeting 2024.01.05]]` tries `.md` first.
12. **Moving a note** rewrites its own relative links, and any other link whose target would change, so every link keeps pointing at the same file. All of them are listed in the confirmation.
13. **A shortest link whose name becomes ambiguous** after a rename gets the full vault path.
14. **Tag pane counts** are notes per tag. A nested parent shows the sum of its own count and its children's.
15. **Explicit relative links** (`./x`, `../x`) never fall back to the vault root or name lookup.
16. **Inline math**: no space after the opening `$` or before the closing `$`. Money amounts like `$5 and $6` stay text.
17. **Footnotes** are rendered (superscript references and a list at the end).
18. **Tags inside link text** (`[[a #b]]`, `[#b](x)`) aren't counted.

## Open questions

None at the moment.
