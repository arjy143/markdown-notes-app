#include "Frontmatter.h"

#include <QHash>

using namespace Qt::StringLiterals;

namespace mn {

namespace {

struct Line {
    QString text;   // without line ending
    int number = 0; // 0-based line in the file
};

bool isBlank(QStringView s)
{
    return s.trimmed().isEmpty();
}

bool isComment(QStringView s)
{
    return s.trimmed().startsWith(u'#');
}

int indentOf(QStringView s)
{
    int n = 0;
    while (n < s.size() && (s[n] == u' ' || s[n] == u'\t'))
        ++n;
    return n;
}

// Strips a trailing " # comment" from a plain scalar.
QString stripComment(QStringView s)
{
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s[i] == u'#' && (i == 0 || s[i - 1] == u' ' || s[i - 1] == u'\t'))
            return s.left(i).trimmed().toString();
    }
    return s.trimmed().toString();
}

// Parses one scalar. Returns false on an unterminated quote.
bool parseScalar(QStringView raw, QString& out)
{
    const QStringView s = raw.trimmed();
    if (s.startsWith(u'"')) {
        QString r;
        for (qsizetype i = 1; i < s.size(); ++i) {
            const QChar c = s[i];
            if (c == u'\\' && i + 1 < s.size()) {
                const QChar n = s[++i];
                if (n == u'n')
                    r += u'\n';
                else if (n == u't')
                    r += u'\t';
                else
                    r += n;
            } else if (c == u'"') {
                const QStringView rest = s.mid(i + 1).trimmed();
                if (!rest.isEmpty() && !rest.startsWith(u'#')) return false;
                out = std::move(r);
                return true;
            } else {
                r += c;
            }
        }
        return false;
    }
    if (s.startsWith(u'\'')) {
        QString r;
        for (qsizetype i = 1; i < s.size(); ++i) {
            if (s[i] == u'\'') {
                if (i + 1 < s.size() && s[i + 1] == u'\'') {
                    r += u'\'';
                    ++i;
                    continue;
                }
                const QStringView rest = s.mid(i + 1).trimmed();
                if (!rest.isEmpty() && !rest.startsWith(u'#')) return false;
                out = std::move(r);
                return true;
            }
            r += s[i];
        }
        return false;
    }
    out = stripComment(s);
    return true;
}

// Parses "[a, 'b', c]". Returns false if malformed.
bool parseFlowList(QStringView s, QStringList& out)
{
    QStringView t = s.trimmed();
    if (!t.startsWith(u'[')) return false;
    const qsizetype close = t.lastIndexOf(u']');
    if (close < 0) return false;
    const QStringView after = t.mid(close + 1).trimmed();
    if (!after.isEmpty() && !after.startsWith(u'#')) return false;
    t = t.mid(1, close - 1);
    QString cur;
    QChar quote;
    bool any = false;
    auto flush = [&](bool force) -> bool {
        QString v;
        if (!parseScalar(cur, v)) return false;
        if (!v.isEmpty() || force) out.append(v);
        cur.clear();
        return true;
    };
    for (qsizetype i = 0; i < t.size(); ++i) {
        const QChar c = t[i];
        if (!quote.isNull()) {
            cur += c;
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == u'"' || c == u'\'') {
            quote = c;
            cur += c;
        } else if (c == u',') {
            if (!flush(false)) return false;
            any = true;
        } else if (c == u'[' || c == u'{') {
            return false; // nested collections aren't part of the subset
        } else {
            cur += c;
        }
    }
    if (!quote.isNull()) return false;
    if (!cur.trimmed().isEmpty() || any) return flush(false);
    return true;
}

} // namespace

const FrontmatterField* Frontmatter::field(QStringView key) const
{
    for (const FrontmatterField& f : fields) {
        if (f.key == key) return &f;
    }
    return nullptr;
}

QStringList splitTagString(QStringView s)
{
    QStringList out;
    QString cur;
    auto flush = [&]() {
        QString t = cur.trimmed();
        while (t.startsWith(u'#'))
            t.remove(0, 1);
        if (!t.isEmpty()) out.append(t);
        cur.clear();
    };
    for (QChar c : s) {
        if (c == u',' || c.isSpace())
            flush();
        else
            cur += c;
    }
    flush();
    return out;
}

Frontmatter parseFrontmatter(QByteArrayView data)
{
    Frontmatter fm;
    // Walk lines (\n, \r\n or lone \r) until the closing "---".
    std::vector<QByteArrayView> raw;
    qsizetype pos = 0;
    std::size_t closing = 0;
    while (true) {
        qsizetype i = pos;
        while (i < data.size() && data[i] != '\n' && data[i] != '\r')
            ++i;
        const QByteArrayView line = data.sliced(pos, i - pos);
        qsizetype next = data.size();
        if (i < data.size())
            next = (data[i] == '\r' && i + 1 < data.size() && data[i + 1] == '\n') ? i + 2 : i + 1;
        if (raw.empty() && line != "---") return fm;
        if (!raw.empty() && line == "---") {
            closing = raw.size();
            fm.bodyOffset = next;
            raw.push_back(line);
            break;
        }
        raw.push_back(line);
        if (i >= data.size()) return fm; // no closing line: not frontmatter
        pos = next;
    }

    fm.present = true;
    fm.closingLine = static_cast<int>(closing);

    std::vector<Line> lines;
    for (std::size_t i = 1; i < closing; ++i)
        lines.push_back({QString::fromUtf8(raw[i]), static_cast<int>(i)});

    auto fail = [&](const Line& l, const QString& why) {
        fm.valid = false;
        fm.error = why;
        fm.errorLine = l.number;
    };

    QHash<QString, int> seen;
    for (std::size_t i = 0; i < lines.size() && fm.valid; ++i) {
        const Line& l = lines[i];
        if (isBlank(l.text) || isComment(l.text)) continue;
        if (indentOf(l.text) > 0 || l.text.startsWith(u'-')) {
            fail(l, u"Unexpected indented line or list item outside a property."_s);
            break;
        }
        // key: value
        qsizetype colon = -1;
        QString key;
        if (l.text.startsWith(u'"') || l.text.startsWith(u'\'')) {
            const QChar q = l.text[0];
            const qsizetype end = l.text.indexOf(q, 1);
            if (end < 0 || end + 1 >= l.text.size() || l.text[end + 1] != u':') {
                fail(l, u"Malformed quoted key."_s);
                break;
            }
            key = l.text.mid(1, end - 1);
            colon = end + 1;
        } else {
            for (qsizetype c = 0; c < l.text.size(); ++c) {
                if (l.text[c] == u':' &&
                    (c + 1 == l.text.size() || l.text[c + 1] == u' ' || l.text[c + 1] == u'\t')) {
                    colon = c;
                    break;
                }
            }
            if (colon <= 0) {
                fail(l, u"Expected \"key: value\"."_s);
                break;
            }
            key = l.text.left(colon).trimmed();
        }
        if (seen.contains(key)) {
            fail(l, u"Duplicate property \"%1\"."_s.arg(key));
            break;
        }
        seen.insert(key, 1);

        FrontmatterField f;
        f.key = key;
        const QString rest = l.text.mid(colon + 1).trimmed();
        if (rest.isEmpty() || rest.startsWith(u'#')) {
            // Block list, nested map, or empty value.
            std::size_t j = i + 1;
            bool sawList = false;
            bool sawOther = false;
            for (; j < lines.size(); ++j) {
                const QString& t = lines[j].text;
                if (isBlank(t)) continue;
                const int ind = indentOf(t);
                const QStringView body = QStringView(t).mid(ind);
                if (body.startsWith(u'#')) continue;
                if (body.startsWith(u"- "_s) || body == u"-") {
                    if (ind == 0 && sawOther) break;
                    sawList = true;
                    QString v;
                    if (!parseScalar(body.mid(1), v)) {
                        fail(lines[j], u"Unterminated quoted string."_s);
                        break;
                    }
                    if (!v.isEmpty()) f.list.append(v);
                    continue;
                }
                if (ind == 0) break;
                sawOther = true; // nested mapping content
            }
            if (!fm.valid) break;
            f.isList = sawList;
            f.complex = sawOther;
            f.value = f.list.join(u", "_s);
            i = j - 1;
        } else if (rest.startsWith(u'[')) {
            // A flow list may continue over several lines.
            QString joined = rest;
            std::size_t j = i;
            while (!joined.contains(u']') && j + 1 < lines.size())
                joined += u' ' + lines[++j].text.trimmed();
            if (!parseFlowList(joined, f.list)) {
                fail(l, u"Malformed list."_s);
                break;
            }
            f.isList = true;
            f.value = f.list.join(u", "_s);
            i = j;
        } else if (rest.startsWith(u'{')) {
            f.complex = true;
            f.value = rest;
        } else if (rest.startsWith(u'|') || rest.startsWith(u'>')) {
            f.complex = true;
            std::size_t j = i + 1;
            QStringList block;
            for (; j < lines.size(); ++j) {
                if (!isBlank(lines[j].text) && indentOf(lines[j].text) == 0) break;
                block.append(lines[j].text.trimmed());
            }
            f.value = block.join(rest.startsWith(u'|') ? u"\n"_s : u" "_s).trimmed();
            i = j - 1;
        } else {
            if (!parseScalar(rest, f.value)) {
                fail(l, u"Unterminated quoted string."_s);
                break;
            }
        }
        fm.fields.append(f);
    }

    if (!fm.valid) {
        fm.fields.clear();
        return fm;
    }

    for (const FrontmatterField& f : std::as_const(fm.fields)) {
        if (f.key == u"tags" || f.key == u"tag" || f.key == u"Tags") {
            if (f.isList) {
                for (const QString& item : f.list)
                    fm.tags += splitTagString(item);
            } else if (!f.complex) {
                fm.tags += splitTagString(f.value);
            }
        } else if (f.key == u"aliases" || f.key == u"alias") {
            if (f.isList) {
                fm.aliases += f.list;
            } else if (!f.complex && !f.value.isEmpty()) {
                fm.aliases.append(f.value);
            }
        } else if (f.key == u"cssclasses") {
            if (f.isList)
                fm.cssclasses += f.list;
            else if (!f.complex && !f.value.isEmpty())
                fm.cssclasses.append(f.value);
        }
    }
    return fm;
}

} // namespace mn
