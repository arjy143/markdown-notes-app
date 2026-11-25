#pragma once

#include <QString>

// Smart paste: rich text copied from a web page or a document, as Markdown.
// Qt's HTML import builds the document; this walks it (headings, bold, italic,
// strike-through, inline code, links, images, lists and checkboxes, quotes,
// tables, code blocks, rules) and writes Markdown without hard-wrapping lines.
namespace mn::ui {

// True when the HTML has formatting worth keeping (links, emphasis, headings,
// lists, tables…). Plain text wrapped in HTML (an editor's coloured spans, a
// terminal) is better pasted as its plain text.
bool htmlHasFormatting(const QString& html);

QString htmlToMarkdown(const QString& html);

} // namespace mn::ui
