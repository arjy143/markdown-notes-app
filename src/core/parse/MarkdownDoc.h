#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <vector>

// md4c wrapper with the Obsidian extensions layered on top (compat spec §2).
//
// md4c parses CommonMark + GFM tables, strikethrough, task lists, permissive
// autolinks, wikilinks, $math$, ==highlight== and footnotes. On top of its
// event stream this layer recognises, inside normal text only (never in code):
//   - #tags                        -> Ev::Tag
//   - %% comment delimiters        -> Ev::Comment (toggles; may span blocks)
//   - ^block-id at the end of a block -> Ev::BlockId
//   - the '!' of ![[embeds]]       -> sets Detail::embed on the wikilink
//
// Every text-like event carries a byte offset into the source buffer, so
// consumers can compute line numbers and rewrite links in place.
namespace mn::md {

enum class Ev : unsigned char { EnterBlock, LeaveBlock, EnterSpan, LeaveSpan, Text, Tag, Comment, BlockId };

struct Event {
    Ev type = Ev::Text;
    unsigned char kind = 0; // MD_BLOCKTYPE, MD_SPANTYPE or MD_TEXTTYPE
    int detail = -1; // index into Document::details (blocks/spans), or extra text (Text with offset < 0)
    qsizetype offset = -1; // byte offset into the source, -1 if the text isn't a slice of it
    qsizetype length = 0;
};

struct Attr {
    QByteArray text;       // processed attribute text (escapes resolved)
    qsizetype offset = -1; // byte offset in source if the raw text is a slice of it, else -1
    qsizetype rawLength = 0;
};

struct Detail {
    int level = 0;           // heading level, OL start, table column count
    int flag = 0;            // UL/OL tight, LI is_task, A is_autolink, code fence char, TD align
    char mark = 0;           // LI task mark, list bullet, OL delimiter
    bool embed = false;      // wikilink preceded by '!'
    bool standalone = false; // BlockId on its own paragraph (applies to previous block)
    Attr a;                  // href / src / wikilink target / code info / footnote label
    Attr b;                  // title / code lang
};

class Document {
public:
    QByteArrayView source;    // whole buffer (BOM stripped)
    qsizetype bodyOffset = 0; // where Markdown parsing started (after frontmatter)
    std::vector<Event> events;
    std::vector<Detail> details;
    std::vector<QByteArray> extraText;

    int lineOf(qsizetype offset) const;
    QByteArrayView textOf(const Event& e) const;
    QString stringOf(const Event& e) const { return QString::fromUtf8(textOf(e)); }
    const Detail& detailOf(const Event& e) const { return details[static_cast<std::size_t>(e.detail)]; }

private:
    friend Document parse(QByteArrayView, qsizetype);
    std::vector<qsizetype> lineStarts_;
};

// Parses `source[bodyOffset..]`. `source` must outlive the Document.
Document parse(QByteArrayView source, qsizetype bodyOffset = 0);

// Obsidian tag character test for one Unicode code point (compat spec §5).
bool isTagCodePoint(char32_t c);

} // namespace mn::md
