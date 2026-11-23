#pragma once

#include "Frontmatter.h"

#include <QString>

#include <vector>

namespace mn {

struct LinkRef {
    enum class Kind : unsigned char { Wiki, Markdown };
    Kind kind = Kind::Wiki;
    bool embed = false;
    QString linkpath; // target path as written (URL-decoded for Markdown links), may be empty
    QString subpath;  // text after the first '#', without it ("H1#H2" or "^blockid"), or empty
    QString display;  // alias / link text (plain)
    QString alias;    // explicit alias after '|' (wikilinks) or empty
    int line = 0;     // 0-based source line
    // Byte range of the target text in the source (BOM-stripped bytes), for rewriting.
    // targetStart < 0 means the link can't be rewritten automatically.
    qsizetype targetStart = -1;
    qsizetype targetEnd = -1;
    bool angleBrackets = false;  // Markdown destination written as <...>
    bool percentEncoded = false; // Markdown destination contained %xx escapes
    bool hasExplicitSubpathOnly() const { return linkpath.isEmpty() && !subpath.isEmpty(); }
};

struct ExternalLink {
    QString url;
    int line = 0;
};

struct TagRef {
    QString tag;  // without '#', original case
    int line = 0; // -1 for frontmatter tags
};

struct Heading {
    int level = 1;
    QString text; // formatting stripped
    int line = 0;
};

struct BlockRef {
    QString id;
    int startLine = 0;
    int endLine = 0;
};

struct TaskItem {
    QString text; // the item's own text (formatting stripped, nested items excluded)
    bool done = false;
    int line = 0;
    QString due; // "YYYY-MM-DD" from "📅 YYYY-MM-DD", "due:YYYY-MM-DD" or "[due:: YYYY-MM-DD]", or empty
    qsizetype markOffset = -1; // byte offset of the ' '/'x' between the brackets (BOM-stripped source)
    int endLine = 0;           // last line of the item, nested items included
};

// Finds a due date in task text (see TaskItem::due).
QString parseDueDate(QStringView taskText);

struct NoteMetadata {
    Frontmatter frontmatter;
    std::vector<LinkRef> links; // internal links and embeds, in source order
    std::vector<ExternalLink> externalLinks;
    std::vector<TagRef> tags; // inline and frontmatter
    std::vector<Heading> headings;
    std::vector<BlockRef> blocks;
    std::vector<TaskItem> tasks; // "- [ ]" / "- [x]" list items
};

// Adds the values of the given frontmatter properties as tags (frontmatter tags,
// line -1), skipping ones the note already has. Keys match exactly.
void addPropertyTags(NoteMetadata& meta, const QStringList& propertyKeys);

// Parses a note's bytes (BOM stripped, any line endings).
NoteMetadata parseNote(QByteArrayView data);

// Splits "path#sub" into its parts (first '#').
void splitLinkTarget(QStringView target, QString& linkpath, QString& subpath);

// True if a Markdown link destination has a URL scheme (https:, mailto:, ...).
bool hasUrlScheme(QStringView s);

// Percent-decodes a Markdown link destination.
QString percentDecode(QStringView s);

// Normalises heading text for subpath matching: case-folded, whitespace collapsed.
QString headingKey(QStringView text);

// Finds the heading a subpath ("H1#H2" or "H2") points at: the last segment is
// matched case-insensitively with whitespace collapsed; if nothing matches, again
// ignoring punctuation (Obsidian drops characters like ':' or '#' from heading
// links). Duplicates resolve to the first match. Returns the index or -1.
int findHeading(const QStringList& headingTexts, QStringView subpath);

} // namespace mn
