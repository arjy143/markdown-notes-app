#pragma once

#include <QString>
#include <QStringList>

#include <optional>

// Smart list editing in the source editor, on plain lines of text: indenting and
// outdenting items, renumbering ordered lists, toggling checkboxes, and putting
// a timestamp at the start of a line. The editor applies the changed lines as one
// undoable edit.
namespace mn::lists {

struct ListItem {
    QString indent;   // leading whitespace
    QString bullet;   // "-", "*", "+" or the number with its delimiter, e.g. "3."
    QString space;    // the spaces after the bullet
    QString checkbox; // "[ ] ", "[x] " (with its trailing space) or empty
    bool ordered = false;
    long long number = 0;
    QChar delimiter; // '.' or ')' for ordered items
    qsizetype contentStart = 0;
};

std::optional<ListItem> parseItem(QStringView line);

// One level of indentation, as this note uses it: a tab, unless its indented list
// lines use spaces (then the smallest such indent).
QString indentUnit(const QStringList& lines);

// Indents (or outdents) lines [first, last] by one unit; blank lines are left alone.
// An ordered item that becomes the first of a nested list is numbered 1.
// Returns false if nothing changed.
bool indentLines(QStringList& lines, int first, int last, const QString& unit, bool outdent);

// Renumbers the ordered lists that lines [first, last] belong to (and the lists of
// the items just above them), keeping each list's first number. Returns false if
// nothing changed.
bool renumber(QStringList& lines, int first, int last);

// "- [ ] x" <-> "- [x] x"; a plain item gains "[ ] ", a plain line becomes "- [ ] line".
QString toggleCheckbox(QStringView line);

// Puts "`time` - " at the start of the line's text (after any list marker and
// checkbox), replacing a timestamp already there. `shift` is how far text after
// the insertion point moved.
QString timestampLine(QStringView line, const QString& time, qsizetype* insertAt = nullptr,
                      qsizetype* shift = nullptr);

} // namespace mn::lists
