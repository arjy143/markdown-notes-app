#include "FrontmatterMerge.h"

#include "core/parse/Frontmatter.h"
#include "core/parse/MarkdownDoc.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace mn {

namespace {

struct Line {
    QString text; // without the line ending
    QString ending;
};

std::vector<Line> splitLines(QStringView s)
{
    std::vector<Line> out;
    qsizetype pos = 0;
    while (pos < s.size()) {
        const qsizetype nl = s.indexOf(u'\n', pos);
        if (nl < 0) {
            out.push_back({s.mid(pos).toString(), QString()});
            break;
        }
        QStringView l = s.mid(pos, nl - pos);
        QString ending = u"\n"_s;
        if (l.endsWith(u'\r')) {
            l.chop(1);
            ending = u"\r\n"_s;
        }
        out.push_back({l.toString(), ending});
        pos = nl + 1;
    }
    return out;
}

int indentOf(const QString& s)
{
    int n = 0;
    while (n < s.size() && (s[n] == u' ' || s[n] == u'\t'))
        ++n;
    return n;
}

// "key" of a top-level "key: value" line, or empty.
QString keyOf(const QString& l)
{
    if (l.isEmpty() || l[0].isSpace() || l.startsWith(u'-') || l.startsWith(u'#')) return {};
    if (l.startsWith(u'"') || l.startsWith(u'\'')) {
        const qsizetype end = l.indexOf(l[0], 1);
        return end > 0 ? l.mid(1, end - 1) : QString();
    }
    for (qsizetype c = 0; c < l.size(); ++c) {
        if (l[c] == u':' && (c + 1 == l.size() || l[c + 1] == u' ' || l[c + 1] == u'\t'))
            return l.left(c).trimmed();
    }
    return {};
}

QString valueOf(const QString& l)
{
    const qsizetype c = l.indexOf(u':');
    return c < 0 ? QString() : l.mid(c + 1).trimmed();
}

} // namespace

QString cleanTag(QStringView tag)
{
    QString t = tag.trimmed().toString();
    while (t.startsWith(u'#'))
        t.remove(0, 1);
    QString out;
    bool digitsOnly = true; // Obsidian needs at least one character that isn't a digit
    for (QChar c : std::as_const(t)) {
        if (c.isSpace()) {
            if (!out.isEmpty() && !out.endsWith(u'-')) out += u'-';
            continue;
        }
        if (c.isSurrogate() || !md::isTagCodePoint(c.unicode())) continue;
        out += c;
        if (!c.isDigit() && c != u'/') digitsOnly = false;
    }
    while (out.endsWith(u'-') || out.endsWith(u'/'))
        out.chop(1);
    while (out.startsWith(u'/'))
        out.remove(0, 1);
    if (out.isEmpty() || digitsOnly) return {};
    return out;
}

QString yamlScalar(const QString& value)
{
    static const QRegularExpression plain(uR"(^[\p{L}\p{N}][\p{L}\p{N} _.,()/'-]*$)"_s);
    QString v = value.simplified();
    if (plain.match(v).hasMatch() && !v.endsWith(u' ')) {
        const QString lower = v.toLower();
        // Words YAML would read as booleans or null stay strings.
        static const QStringList special{u"true"_s, u"false"_s, u"yes"_s, u"no"_s, u"on"_s,
                                         u"off"_s,  u"null"_s,  u"~"_s,   u"y"_s,  u"n"_s};
        bool number = false;
        v.toDouble(&number);
        if (!special.contains(lower) && !number) return v;
    }
    QString q = v;
    q.replace(u'\\', u"\\\\"_s);
    q.replace(u'"', u"\\\""_s);
    return u'"' + q + u'"';
}

FrontmatterEdit planFrontmatterEdit(QStringView noteText, const FrontmatterPatch& patch)
{
    FrontmatterEdit e;
    QStringList tags;
    for (const QString& t : patch.addTags) {
        const QString c = cleanTag(t);
        bool dup = false;
        for (const QString& have : std::as_const(tags))
            dup = dup || have.compare(c, Qt::CaseInsensitive) == 0;
        if (!c.isEmpty() && !dup) tags.append(c);
    }
    QList<QPair<QString, QString>> props;
    for (const auto& [k, v] : patch.addIfMissing) {
        if (!k.trimmed().isEmpty() && !v.trimmed().isEmpty()) props.append({k.trimmed(), v.trimmed()});
    }

    const QByteArray utf8 = noteText.toUtf8();
    const Frontmatter fm = parseFrontmatter(utf8);
    const std::vector<Line> lines = splitLines(noteText);
    if (!fm.present) {
        // A new block at the top, in the note's line-ending style.
        const QString nl = (!lines.empty() && lines.front().ending == u"\r\n") ? u"\r\n"_s : u"\n"_s;
        if (props.isEmpty() && tags.isEmpty()) {
            e.ok = true;
            return e;
        }
        QString block = u"---"_s + nl;
        for (const auto& [k, v] : std::as_const(props))
            block += k + u": "_s + yamlScalar(v) + nl;
        if (!tags.isEmpty()) {
            block += u"tags:"_s + nl;
            for (const QString& t : std::as_const(tags))
                block += u"  - "_s + t + nl;
        }
        block += u"---"_s + nl;
        e.ok = e.changed = true;
        e.replaceLines = 0;
        e.text = std::move(block);
        return e;
    }
    if (!fm.valid) {
        e.error = u"The note's frontmatter can't be read (%1), so it was left unchanged."_s.arg(fm.error);
        return e;
    }

    const int closing = fm.closingLine;
    const QString nl = lines.front().ending.isEmpty() ? u"\n"_s : lines.front().ending;
    std::vector<Line> out; // frontmatter lines 1..closing-1; new lines get `nl`
    for (int i = 1; i < closing; ++i)
        out.push_back(lines[static_cast<std::size_t>(i)]);

    // Properties: only keys that aren't there yet.
    QList<QPair<QString, QString>> newProps;
    for (const auto& [k, v] : std::as_const(props)) {
        if (fm.field(k) == nullptr) newProps.append({k, v});
    }

    // Tags: append to the existing tags property in its own style.
    const FrontmatterField* tagField = nullptr;
    QString tagKey;
    for (const QString& k : {u"tags"_s, u"tag"_s, u"Tags"_s}) {
        if (tagField == nullptr && fm.field(k) != nullptr) {
            tagField = fm.field(k);
            tagKey = k;
        }
    }
    QStringList addTags;
    for (const QString& t : std::as_const(tags)) {
        bool have = false;
        for (const QString& h : fm.tags)
            have = have || h.compare(t, Qt::CaseInsensitive) == 0;
        if (!have) addTags.append(t);
    }
    bool newTagsBlock = false;
    if (!addTags.isEmpty() && tagField == nullptr) {
        newTagsBlock = true;
    } else if (!addTags.isEmpty() && tagField != nullptr) {
        // Find the key's line and the lines that belong to it.
        std::size_t keyLine = out.size();
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (keyOf(out[i].text) == tagKey) keyLine = i;
        }
        std::size_t last = keyLine; // last line belonging to the property
        std::size_t lastItem = keyLine;
        for (std::size_t j = keyLine + 1; j < out.size(); ++j) {
            const QString& t = out[j].text;
            if (t.trimmed().isEmpty()) continue;
            if (indentOf(t) == 0 && !t.startsWith(u'-')) break;
            last = j;
            if (t.trimmed().startsWith(u'-')) lastItem = j;
        }
        const QString value = keyLine < out.size() ? valueOf(out[keyLine].text) : QString();
        if (keyLine >= out.size() || tagField->complex) {
            e.skipped.append(u"tags (the existing value is too complex to extend)"_s);
        } else if (tagField->isList && value.isEmpty() && lastItem > keyLine) {
            // Block list: new items after the last one, indented the same way.
            const QString& item = out[lastItem].text;
            const QString prefix = item.left(indentOf(item)) + u"- "_s;
            std::vector<Line> extra;
            for (const QString& t : std::as_const(addTags))
                extra.push_back({prefix + t, nl});
            out.insert(out.begin() + static_cast<std::ptrdiff_t>(lastItem) + 1, extra.begin(), extra.end());
        } else if (tagField->isList && value.startsWith(u'[') && last == keyLine && value.endsWith(u']')) {
            // One-line flow list: "tags: [a, b]".
            QString& l = out[keyLine].text;
            const qsizetype close = l.lastIndexOf(u']');
            const QString inner = l.mid(l.indexOf(u'[') + 1, close - l.indexOf(u'[') - 1).trimmed();
            l.insert(close, (inner.isEmpty() ? QString() : u", "_s) + addTags.join(u", "_s));
        } else if (!tagField->isList && value.isEmpty() && last == keyLine) {
            // "tags:" with nothing after it: becomes a block list.
            std::vector<Line> extra;
            for (const QString& t : std::as_const(addTags))
                extra.push_back({u"  - "_s + t, nl});
            out.insert(out.begin() + static_cast<std::ptrdiff_t>(keyLine) + 1, extra.begin(), extra.end());
        } else if (!tagField->isList && !value.isEmpty() && last == keyLine && !value.startsWith(u'"') &&
                   !value.startsWith(u'\'') && !value.contains(u'#')) {
            // "tags: a, b" or "tags: a b".
            out[keyLine].text += (value.contains(u',') ? u", "_s : u" "_s) +
                                 addTags.join(value.contains(u',') ? u", "_s : u" "_s);
        } else {
            e.skipped.append(
                u"tags (the existing value is written in a form that can't be extended safely)"_s);
        }
    }

    // New properties (and a new tags list) go just before the closing "---".
    for (const auto& [k, v] : std::as_const(newProps))
        out.push_back({k + u": "_s + yamlScalar(v), nl});
    if (newTagsBlock) {
        out.push_back({u"tags:"_s, nl});
        for (const QString& t : std::as_const(addTags))
            out.push_back({u"  - "_s + t, nl});
    }

    // Every line keeps its own line ending.
    QString block = lines[0].text + lines[0].ending;
    for (const Line& l : out)
        block += l.text + l.ending;
    block += lines[static_cast<std::size_t>(closing)].text + lines[static_cast<std::size_t>(closing)].ending;
    QString original;
    for (int i = 0; i <= closing; ++i)
        original += lines[static_cast<std::size_t>(i)].text + lines[static_cast<std::size_t>(i)].ending;
    e.ok = true;
    e.replaceLines = closing + 1;
    e.changed = block != original;
    e.text = std::move(block);
    // A new block that doesn't parse back the same way would be a bug: refuse it.
    if (e.changed) {
        const QByteArray checkText = e.text.toUtf8() + QByteArray("\n");
        const Frontmatter check = parseFrontmatter(checkText);
        if (!check.present || !check.valid) {
            e.ok = e.changed = false;
            e.error = u"The change would make the frontmatter unreadable, so it was not made."_s;
        }
    }
    return e;
}

QString applyFrontmatterEdit(QStringView noteText, const FrontmatterEdit& edit)
{
    if (!edit.ok || !edit.changed) return noteText.toString();
    qsizetype pos = 0;
    for (int i = 0; i < edit.replaceLines; ++i) {
        const qsizetype nl = noteText.indexOf(u'\n', pos);
        pos = nl < 0 ? noteText.size() : nl + 1;
    }
    return edit.text + noteText.mid(pos);
}

} // namespace mn
