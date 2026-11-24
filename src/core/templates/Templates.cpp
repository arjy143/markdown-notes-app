#include "Templates.h"

#include "core/parse/Frontmatter.h"

#include <QLocale>
#include <QSet>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace mn {

namespace {

QString pad(int v, int width)
{
    return QString::number(v).rightJustified(width, u'0');
}

QString ordinal(int n)
{
    const int mod100 = n % 100;
    const char* suffix = "th";
    if (mod100 < 11 || mod100 > 13) {
        switch (n % 10) {
        case 1:
            suffix = "st";
            break;
        case 2:
            suffix = "nd";
            break;
        case 3:
            suffix = "rd";
            break;
        default:
            break;
        }
    }
    return QString::number(n) + QLatin1StringView(suffix);
}

struct FmSplit {
    bool present = false;
    QString block; // "---\n...\n---\n" including the closing line ending
    QString inner; // lines between the delimiters (with '\n')
    QString rest;  // text after the block
};

FmSplit splitFrontmatter(QStringView text)
{
    FmSplit s;
    if (!text.startsWith(u"---\n"_s) && text != u"---") return s;
    qsizetype pos = 4;
    while (pos <= text.size()) {
        qsizetype nl = text.indexOf(u'\n', pos);
        const qsizetype end = nl < 0 ? text.size() : nl;
        if (text.mid(pos, end - pos) == u"---") {
            const qsizetype after = nl < 0 ? text.size() : nl + 1;
            s.present = true;
            s.block = text.left(after).toString();
            s.inner = text.mid(4, pos - 4).toString();
            s.rest = text.mid(after).toString();
            return s;
        }
        if (nl < 0) break;
        pos = nl + 1;
    }
    return s;
}

// Splits YAML lines into top-level entries (key line + its continuation lines).
QList<QPair<QString, QString>> topLevelEntries(const QString& inner)
{
    QList<QPair<QString, QString>> out;
    const QStringList lines = inner.split(u'\n');
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString& l = lines[i];
        if (i == lines.size() - 1 && l.isEmpty()) break;
        const bool topLevel = !l.isEmpty() && !l[0].isSpace() && !l.startsWith(u'-') && !l.startsWith(u'#');
        if (topLevel) {
            const qsizetype colon = l.indexOf(u':');
            QString key = (colon > 0 ? l.left(colon) : l).trimmed();
            if (key.size() >= 2 && (key.startsWith(u'"') || key.startsWith(u'\'')))
                key = key.mid(1, key.size() - 2);
            out.append({key, l + u'\n'});
        } else if (!out.isEmpty()) {
            out.last().second += l + u'\n';
        }
    }
    return out;
}

} // namespace

QString formatMoment(const QDateTime& dt, QStringView format)
{
    const QLocale en(QLocale::English, QLocale::UnitedStates);
    const QDate d = dt.date();
    const QTime t = dt.time();
    QString out;
    qsizetype i = 0;
    auto at = [&](QStringView tok) { return format.mid(i).startsWith(tok); };
    while (i < format.size()) {
        if (format[i] == u'[') {
            const qsizetype close = format.indexOf(u']', i + 1);
            if (close > 0) {
                out += format.mid(i + 1, close - i - 1);
                i = close + 1;
                continue;
            }
        }
        struct Tok {
            QStringView tok;
            int kind;
        };
        static const Tok toks[] = {
            {u"YYYY", 0}, {u"YY", 1},  {u"MMMM", 2}, {u"MMM", 3},  {u"MM", 4},  {u"M", 5},  {u"Do", 6},
            {u"DD", 7},   {u"D", 8},   {u"dddd", 9}, {u"ddd", 10}, {u"HH", 11}, {u"H", 12}, {u"hh", 13},
            {u"h", 14},   {u"mm", 15}, {u"m", 16},   {u"ss", 17},  {u"s", 18},  {u"A", 19}, {u"a", 20},
        };
        bool matched = false;
        for (const Tok& tk : toks) {
            if (!at(tk.tok)) continue;
            const int h12 = t.hour() % 12 == 0 ? 12 : t.hour() % 12;
            switch (tk.kind) {
            case 0:
                out += pad(d.year(), 4);
                break;
            case 1:
                out += pad(d.year() % 100, 2);
                break;
            case 2:
                out += en.monthName(d.month(), QLocale::LongFormat);
                break;
            case 3:
                out += en.monthName(d.month(), QLocale::ShortFormat);
                break;
            case 4:
                out += pad(d.month(), 2);
                break;
            case 5:
                out += QString::number(d.month());
                break;
            case 6:
                out += ordinal(d.day());
                break;
            case 7:
                out += pad(d.day(), 2);
                break;
            case 8:
                out += QString::number(d.day());
                break;
            case 9:
                out += en.dayName(d.dayOfWeek(), QLocale::LongFormat);
                break;
            case 10:
                out += en.dayName(d.dayOfWeek(), QLocale::ShortFormat);
                break;
            case 11:
                out += pad(t.hour(), 2);
                break;
            case 12:
                out += QString::number(t.hour());
                break;
            case 13:
                out += pad(h12, 2);
                break;
            case 14:
                out += QString::number(h12);
                break;
            case 15:
                out += pad(t.minute(), 2);
                break;
            case 16:
                out += QString::number(t.minute());
                break;
            case 17:
                out += pad(t.second(), 2);
                break;
            case 18:
                out += QString::number(t.second());
                break;
            case 19:
                out += t.hour() < 12 ? u"AM"_s : u"PM"_s;
                break;
            case 20:
                out += t.hour() < 12 ? u"am"_s : u"pm"_s;
                break;
            default:
                break;
            }
            i += tk.tok.size();
            matched = true;
            break;
        }
        if (!matched) {
            out += format[i];
            ++i;
        }
    }
    return out;
}

QString substituteTemplate(QStringView text, const TemplateContext& ctx)
{
    QString out;
    out.reserve(text.size());
    qsizetype i = 0;
    while (i < text.size()) {
        const qsizetype open = text.indexOf(u"{{"_s, i);
        if (open < 0) {
            out += text.mid(i);
            break;
        }
        const qsizetype close = text.indexOf(u"}}"_s, open + 2);
        if (close < 0) {
            out += text.mid(i);
            break;
        }
        out += text.mid(i, open - i);
        const QStringView inner = text.mid(open + 2, close - open - 2);
        const QStringView name = inner.trimmed();
        QString replacement;
        bool known = true;
        if (name.compare(u"title", Qt::CaseInsensitive) == 0) {
            replacement = ctx.title;
        } else if (name.compare(u"date", Qt::CaseInsensitive) == 0) {
            replacement = formatMoment(ctx.now, ctx.dateFormat);
        } else if (name.compare(u"time", Qt::CaseInsensitive) == 0) {
            replacement = formatMoment(ctx.now, ctx.timeFormat);
        } else if (name.startsWith(u"date:", Qt::CaseInsensitive) ||
                   name.startsWith(u"time:", Qt::CaseInsensitive)) {
            replacement = formatMoment(ctx.now, name.mid(5).trimmed());
        } else {
            known = false;
        }
        out += known ? replacement : text.mid(open, close + 2 - open).toString();
        i = close + 2;
    }
    return out;
}

TemplateInsertion planTemplateInsertion(QStringView noteText, QStringView templateText)
{
    TemplateInsertion r;
    const FmSplit tpl = splitFrontmatter(templateText);
    if (!tpl.present) {
        r.body = templateText.toString();
        return r;
    }
    r.body = tpl.rest;
    const FmSplit note = splitFrontmatter(noteText);
    if (!note.present) {
        r.frontmatterChanged = true;
        r.replaceLength = 0;
        r.frontmatter = tpl.block;
        if (!r.frontmatter.endsWith(u'\n')) r.frontmatter += u'\n';
        return r;
    }
    const Frontmatter parsed = parseFrontmatter(note.block.toUtf8());
    if (!parsed.valid) {
        r.warning = u"Template properties were not merged: the note's frontmatter is invalid."_s;
        return r;
    }
    QSet<QString> existing;
    for (const auto& e : topLevelEntries(note.inner))
        existing.insert(e.first);
    QString additions;
    for (const auto& e : topLevelEntries(tpl.inner)) {
        if (!existing.contains(e.first)) additions += e.second;
    }
    if (additions.isEmpty()) return r;
    QString inner = note.inner;
    if (!inner.isEmpty() && !inner.endsWith(u'\n')) inner += u'\n';
    r.frontmatterChanged = true;
    r.replaceLength = static_cast<int>(note.block.size());
    const bool closingHadNewline = note.block.endsWith(u'\n');
    r.frontmatter = u"---\n"_s + inner + additions + u"---"_s + (closingHadNewline ? u"\n"_s : QString());
    return r;
}

} // namespace mn
