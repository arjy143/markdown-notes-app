#include "ListEdit.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mn::lists {

namespace {

// Width of leading whitespace, a tab counting as 4 columns.
int columns(QStringView indent)
{
    int n = 0;
    for (QChar c : indent)
        n += c == u'\t' ? 4 - n % 4 : 1;
    return n;
}

QString leading(QStringView line)
{
    qsizetype n = 0;
    while (n < line.size() && (line[n] == u' ' || line[n] == u'\t'))
        ++n;
    return line.left(n).toString();
}

bool blank(QStringView line)
{
    return line.trimmed().isEmpty();
}

QString withNumber(QStringView line, const ListItem& item, long long number)
{
    const QString bullet = QString::number(number) + item.delimiter;
    return item.indent + bullet + line.mid(item.indent.size() + item.bullet.size()).toString();
}

// The ordered list run containing ordered item `at`: indexes of its items (same indent).
QList<int> runOf(const QStringList& lines, int at)
{
    const std::optional<ListItem> me = parseItem(lines[at]);
    QList<int> items;
    if (!me || !me->ordered) return items;
    const int col = columns(me->indent);
    auto belongs = [&](int i, bool* sibling) {
        *sibling = false;
        if (blank(lines[i])) return true;
        const int c = columns(leading(lines[i]));
        if (c > col) return true; // a nested item or continuation text
        if (c < col) return false;
        const std::optional<ListItem> it = parseItem(lines[i]);
        if (!it || !it->ordered || it->delimiter != me->delimiter) return false;
        *sibling = true;
        return true;
    };
    int first = at;
    for (int i = at - 1, blanks = 0; i >= 0; --i) {
        bool sibling = false;
        if (!belongs(i, &sibling)) break;
        blanks = blank(lines[i]) ? blanks + 1 : 0;
        if (blanks > 1) break;
        if (sibling) first = i;
    }
    for (int i = first, blanks = 0; i < lines.size(); ++i) {
        bool sibling = false;
        if (i != at && !belongs(i, &sibling)) break;
        blanks = blank(lines[i]) ? blanks + 1 : 0;
        if (blanks > 1) break;
        if (sibling || i == at) items.append(i);
    }
    return items;
}

// The nearest list item above `at` at exactly this indent width, or -1 (stops at
// less-indented text).
int previousSibling(const QStringList& lines, int at, int col)
{
    for (int i = at - 1; i >= 0; --i) {
        if (blank(lines[i])) continue;
        const int c = columns(leading(lines[i]));
        if (c < col) return -1;
        if (c == col) return parseItem(lines[i]) ? i : -1;
    }
    return -1;
}

} // namespace

std::optional<ListItem> parseItem(QStringView line)
{
    static const QRegularExpression item(uR"(^([ \t]*)([-*+]|(\d{1,9})([.)]))([ \t]+|$)(\[[ xX]\] )?)"_s);
    const QRegularExpressionMatch m = item.matchView(line);
    if (!m.hasMatch()) return std::nullopt;
    ListItem it;
    it.indent = m.captured(1);
    it.bullet = m.captured(2);
    it.space = m.captured(5);
    it.checkbox = m.captured(6);
    it.ordered = m.capturedLength(3) > 0;
    if (it.ordered) {
        it.number = m.captured(3).toLongLong();
        it.delimiter = m.captured(4).front();
    }
    it.contentStart = m.capturedEnd(0);
    return it;
}

QString indentUnit(const QStringList& lines)
{
    int smallest = 0;
    for (const QString& l : lines) {
        const QString lead = leading(l);
        if (lead.isEmpty() || !parseItem(l)) continue;
        if (lead.contains(u'\t')) return u"\t"_s;
        const int n = static_cast<int>(lead.size());
        if (smallest == 0 || n < smallest) smallest = n;
    }
    return smallest >= 2 ? QString(smallest, u' ') : u"\t"_s;
}

bool indentLines(QStringList& lines, int first, int last, const QString& unit, bool outdent)
{
    bool changed = false;
    first = std::max(first, 0);
    last = std::min(last, static_cast<int>(lines.size()) - 1);
    for (int i = first; i <= last; ++i) {
        QString& l = lines[i];
        if (blank(l)) continue;
        if (!outdent) {
            l.prepend(unit);
            changed = true;
            continue;
        }
        if (l.startsWith(u'\t')) {
            l.remove(0, 1);
            changed = true;
        } else {
            qsizetype n = 0;
            while (n < unit.size() && n < l.size() && l[n] == u' ')
                ++n;
            if (n == 0 && unit == u"\t" && l.startsWith(u' ')) {
                while (n < 4 && n < l.size() && l[n] == u' ')
                    ++n;
            }
            if (n > 0) {
                l.remove(0, n);
                changed = true;
            }
        }
    }
    if (!changed) return false;
    // An ordered item that now starts a nested list counts from 1; one that joined
    // a list continues it.
    for (int i = first; i <= last; ++i) {
        const std::optional<ListItem> it = parseItem(lines[i]);
        if (!it || !it->ordered) continue;
        const int prev = previousSibling(lines, i, columns(it->indent));
        const std::optional<ListItem> p = prev >= 0 ? parseItem(lines[prev]) : std::nullopt;
        if (p && p->ordered && p->delimiter == it->delimiter)
            lines[i] = withNumber(lines[i], *it, p->number + 1);
        else if (!outdent && i == first)
            lines[i] = withNumber(lines[i], *it, 1);
    }
    return true;
}

bool renumber(QStringList& lines, int first, int last)
{
    if (lines.isEmpty()) return false;
    first = std::clamp(first, 0, static_cast<int>(lines.size()) - 1);
    last = std::clamp(last, first, static_cast<int>(lines.size()) - 1);
    // Start points: ordered items in the range, and the ordered items just above it
    // at each indent (the lists a removed or moved line belonged to).
    QList<int> starts;
    for (int i = first; i <= last; ++i) {
        if (const auto it = parseItem(lines[i]); it && it->ordered) starts.append(i);
    }
    QList<int> seenCols;
    for (int i = first - 1; i >= 0 && i >= first - 200; --i) {
        const auto it = parseItem(lines[i]);
        if (!it) {
            if (!blank(lines[i]) && columns(leading(lines[i])) == 0) break;
            continue;
        }
        const int c = columns(it->indent);
        if (seenCols.contains(c)) continue;
        seenCols.append(c);
        if (it->ordered) starts.append(i);
        if (c == 0) break;
    }
    bool changed = false;
    QList<int> done;
    for (int s : std::as_const(starts)) {
        if (done.contains(s)) continue;
        const QList<int> run = runOf(lines, s);
        if (run.isEmpty()) continue;
        done += run;
        long long n = parseItem(lines[run.front()])->number;
        for (int i : run) {
            const std::optional<ListItem> it = parseItem(lines[i]);
            if (it->number != n) {
                lines[i] = withNumber(lines[i], *it, n);
                changed = true;
            }
            ++n;
        }
    }
    return changed;
}

QString toggleCheckbox(QStringView line)
{
    const std::optional<ListItem> it = parseItem(line);
    if (!it) {
        const QString lead = leading(line);
        return lead + u"- [ ] "_s + line.mid(lead.size()).toString();
    }
    const qsizetype boxAt = it->contentStart - it->checkbox.size();
    if (it->checkbox.isEmpty()) {
        QString out = line.toString();
        if (it->space.isEmpty()) return out + u" [ ] "_s; // "-" alone at the end
        out.insert(it->contentStart, u"[ ] "_s);
        return out;
    }
    QString out = line.toString();
    out.replace(boxAt + 1, 1, it->checkbox[1] == u' ' ? u"x"_s : u" "_s);
    return out;
}

QString timestampLine(QStringView line, const QString& time, qsizetype* insertAt, qsizetype* shift)
{
    static const QRegularExpression stamp(uR"(^\d{1,2}:\d{2}(:\d{2})?(\s?[AaPp][Mm])? - )"_s);
    const std::optional<ListItem> it = parseItem(line);
    const qsizetype at = it ? it->contentStart : leading(line).size();
    const QString text = time + u" - "_s;
    const QRegularExpressionMatch m = stamp.matchView(line.mid(at));
    const qsizetype old = m.hasMatch() ? m.capturedLength(0) : 0;
    if (insertAt) *insertAt = at;
    if (shift) *shift = text.size() - old;
    return line.left(at).toString() + text + line.mid(at + old).toString();
}

} // namespace mn::lists
