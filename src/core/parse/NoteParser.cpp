#include "MarkdownDoc.h"
#include "NoteMetadata.h"

#include <QRegularExpression>
#include <QSet>

#include <md4c.h>

using namespace Qt::StringLiterals;

namespace mn {

namespace {

QString decodeEntity(QStringView e)
{
    if (e == u"&amp;") return u"&"_s;
    if (e == u"&lt;") return u"<"_s;
    if (e == u"&gt;") return u">"_s;
    if (e == u"&quot;") return u"\""_s;
    if (e == u"&apos;") return u"'"_s;
    if (e == u"&nbsp;") return QString(QChar(0xA0));
    if (e.startsWith(u"&#") && e.endsWith(u';')) {
        bool ok = false;
        const bool hex = e.size() > 3 && (e[2] == u'x' || e[2] == u'X');
        const uint cp = e.mid(hex ? 3 : 2, e.size() - (hex ? 4 : 3)).toUInt(&ok, hex ? 16 : 10);
        if (ok && cp > 0 && cp <= 0x10FFFF) {
            const char32_t c = cp;
            return QString::fromUcs4(&c, 1);
        }
    }
    return e.toString();
}

struct OpenBlock {
    MD_BLOCKTYPE type;
    int first = -1;
    int last = -1;
};

struct PendingBlockId {
    QString id;
    std::size_t stackIndex;
};

struct OpenLink {
    LinkRef link;
    bool external = false;
    bool hasAlias = false;
    bool skip = false;
    QString text;
};

} // namespace

void splitLinkTarget(QStringView target, QString& linkpath, QString& subpath)
{
    const qsizetype hash = target.indexOf(u'#');
    if (hash < 0) {
        linkpath = target.toString();
        subpath.clear();
    } else {
        linkpath = target.left(hash).toString();
        subpath = target.mid(hash + 1).toString();
    }
}

bool hasUrlScheme(QStringView s)
{
    qsizetype i = 0;
    while (i < s.size()) {
        const QChar c = s[i];
        const bool ok = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') ||
                        (i > 0 && ((c >= u'0' && c <= u'9') || c == u'+' || c == u'.' || c == u'-'));
        if (!ok) break;
        ++i;
    }
    return i >= 2 && i < s.size() && s[i] == u':';
}

QString percentDecode(QStringView s)
{
    if (!s.contains(u'%')) return s.toString();
    const QByteArray decoded = QByteArray::fromPercentEncoding(s.toUtf8());
    QStringDecoder dec(QStringDecoder::Utf8);
    QString out = dec.decode(decoded);
    if (dec.hasError()) return s.toString();
    return out;
}

void addPropertyTags(NoteMetadata& meta, const QStringList& keys)
{
    if (keys.isEmpty() || !meta.frontmatter.valid) return;
    QSet<QString> have;
    for (const TagRef& t : meta.tags)
        have.insert(t.tag.toCaseFolded());
    for (const FrontmatterField& f : std::as_const(meta.frontmatter.fields)) {
        if (!keys.contains(f.key) || f.complex) continue;
        QStringList values;
        if (f.isList) {
            for (const QString& item : f.list)
                values += splitTagString(item);
        } else {
            values = splitTagString(f.value);
        }
        for (const QString& v : std::as_const(values)) {
            if (have.contains(v.toCaseFolded())) continue;
            have.insert(v.toCaseFolded());
            meta.tags.push_back({v, -1});
        }
    }
}

QString parseDueDate(QStringView text)
{
    static const QRegularExpression re(uR"((?:\x{1F4C5}\s*|(?<![\w])due::?\s*)(\d{4}-\d{2}-\d{2}))"_s,
                                       QRegularExpression::UseUnicodePropertiesOption |
                                           QRegularExpression::CaseInsensitiveOption);
    const auto m = re.matchView(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString headingKey(QStringView text)
{
    return text.toString().simplified().toCaseFolded();
}

namespace {
QString looseHeadingKey(QStringView text)
{
    QString out;
    out.reserve(text.size());
    for (QChar c : text)
        out += (c.isLetterOrNumber() || c.isMark()) ? c : QChar(u' ');
    return out.simplified().toCaseFolded();
}
} // namespace

int findHeading(const QStringList& headingTexts, QStringView subpath)
{
    const qsizetype hash = subpath.lastIndexOf(u'#');
    const QStringView last = hash >= 0 ? subpath.mid(hash + 1) : subpath;
    const QString exact = headingKey(last);
    for (qsizetype i = 0; i < headingTexts.size(); ++i) {
        if (headingKey(headingTexts[i]) == exact) return static_cast<int>(i);
    }
    const QString loose = looseHeadingKey(last);
    if (loose.isEmpty()) return -1;
    for (qsizetype i = 0; i < headingTexts.size(); ++i) {
        if (looseHeadingKey(headingTexts[i]) == loose) return static_cast<int>(i);
    }
    return -1;
}

NoteMetadata parseNote(QByteArrayView data)
{
    NoteMetadata m;
    m.frontmatter = parseFrontmatter(data);
    for (const QString& t : std::as_const(m.frontmatter.tags))
        m.tags.push_back({t, -1});

    const md::Document doc = md::parse(data, m.frontmatter.present ? m.frontmatter.bodyOffset : 0);
    const char* src = data.data();

    bool inComment = false;
    std::vector<OpenBlock> stack;
    std::vector<std::pair<int, int>> lastClosed; // per depth: range of the most recently closed block
    std::vector<PendingBlockId> pendingIds;
    std::vector<OpenLink> links;
    QString headingText;
    int headingLevel = 0;
    bool inHeading = false;
    bool headingSkip = false;

    auto touchLine = [&](int line) {
        for (OpenBlock& b : stack) {
            if (b.first < 0) b.first = line;
            if (line > b.last) b.last = line;
        }
    };
    // Task items: the LI's stack index and its index in m.tasks. Text goes to a task
    // only while its LI is the innermost open list item (nested items are their own).
    struct OpenTask {
        std::size_t stackIndex;
        std::size_t task;
    };
    std::vector<OpenTask> tasks;
    std::vector<std::size_t> liStack;
    auto taskActive = [&]() {
        return !tasks.empty() && !liStack.empty() && tasks.back().stackIndex == liStack.back();
    };
    auto wantsText = [&]() { return !inComment && (inHeading || !links.empty() || taskActive()); };
    auto appendText = [&](const QString& s) {
        if (inComment) return;
        if (inHeading) headingText += s;
        if (!links.empty()) links.back().text += s;
        if (taskActive()) m.tasks[tasks.back().task].text += s;
    };

    for (const md::Event& e : doc.events) {
        if (e.offset >= 0) touchLine(doc.lineOf(e.offset));
        switch (e.type) {
        case md::Ev::EnterBlock: {
            const auto type = static_cast<MD_BLOCKTYPE>(e.kind);
            stack.push_back({type});
            if (lastClosed.size() < stack.size() + 1) lastClosed.resize(stack.size() + 1, {-1, -1});
            lastClosed[stack.size()] = {-1, -1};
            if (type == MD_BLOCK_LI) {
                liStack.push_back(stack.size() - 1);
                const md::Detail& d = doc.detailOf(e);
                if (d.flag != 0 && !inComment) {
                    TaskItem t;
                    t.done = d.mark != ' ';
                    t.line = d.a.offset >= 0 ? doc.lineOf(d.a.offset) : 0;
                    t.markOffset = d.a.offset;
                    m.tasks.push_back(t);
                    tasks.push_back({stack.size() - 1, m.tasks.size() - 1});
                }
            }
            if (type == MD_BLOCK_H) {
                inHeading = true;
                headingSkip = inComment;
                headingLevel = doc.detailOf(e).level;
                headingText.clear();
            }
            break;
        }
        case md::Ev::LeaveBlock: {
            if (stack.empty()) break;
            const OpenBlock closed = stack.back();
            const std::size_t index = stack.size() - 1;
            for (auto it = pendingIds.begin(); it != pendingIds.end();) {
                if (it->stackIndex == index) {
                    m.blocks.push_back({it->id, closed.first, closed.last});
                    it = pendingIds.erase(it);
                } else {
                    ++it;
                }
            }
            if (closed.type == MD_BLOCK_LI) {
                if (!tasks.empty() && tasks.back().stackIndex == index) {
                    TaskItem& t = m.tasks[tasks.back().task];
                    t.text = t.text.simplified();
                    t.endLine = std::max(t.line, closed.last);
                    t.due = parseDueDate(t.text);
                    tasks.pop_back();
                }
                if (!liStack.empty()) liStack.pop_back();
            }
            stack.pop_back();
            lastClosed[stack.size()] = {closed.first, closed.last};
            if (closed.type == MD_BLOCK_H) {
                inHeading = false;
                const QString text = headingText.simplified();
                if (!headingSkip && !text.isEmpty() && closed.first >= 0)
                    m.headings.push_back({headingLevel, text, closed.first});
            }
            break;
        }
        case md::Ev::EnterSpan: {
            const auto type = static_cast<MD_SPANTYPE>(e.kind);
            if (type != MD_SPAN_WIKILINK && type != MD_SPAN_A && type != MD_SPAN_IMG) break;
            const md::Detail& d = doc.detailOf(e);
            OpenLink ol;
            ol.skip = inComment;
            QString raw = QString::fromUtf8(d.a.text);
            if (type == MD_SPAN_WIKILINK) {
                ol.link.kind = LinkRef::Kind::Wiki;
                ol.link.embed = d.embed;
                qsizetype rawEnd = d.a.offset >= 0 ? d.a.offset + d.a.rawLength : -1;
                if (raw.endsWith(u'\\')) { // [[a\|b]] inside a table
                    raw.chop(1);
                    if (rawEnd > 0) --rawEnd;
                }
                if (rawEnd >= 0) {
                    const qsizetype after = rawEnd;
                    ol.hasAlias = after < data.size() &&
                                  (src[after] == '|' ||
                                   (src[after] == '\\' && after + 1 < data.size() && src[after + 1] == '|'));
                }
                splitLinkTarget(raw, ol.link.linkpath, ol.link.subpath);
                if (d.a.offset >= 0) {
                    ol.link.targetStart = d.a.offset;
                    ol.link.targetEnd = d.a.offset + ol.link.linkpath.toUtf8().size();
                }
                ol.link.line = doc.lineOf(std::max<qsizetype>(d.a.offset, 0));
            } else {
                ol.link.kind = LinkRef::Kind::Markdown;
                ol.link.embed = type == MD_SPAN_IMG;
                const bool autolink = type == MD_SPAN_A && d.flag != 0;
                if (raw.isEmpty() || autolink || hasUrlScheme(raw)) {
                    ol.external = true;
                    if (!raw.isEmpty() && !inComment) {
                        const int line = d.a.offset >= 0
                                             ? doc.lineOf(d.a.offset)
                                             : (stack.empty() ? 0 : std::max(0, stack.back().last));
                        m.externalLinks.push_back({raw, line});
                    }
                } else {
                    QString rawPath;
                    QString rawSub;
                    splitLinkTarget(raw, rawPath, rawSub);
                    ol.link.linkpath = percentDecode(rawPath);
                    ol.link.subpath = percentDecode(rawSub);
                    ol.link.percentEncoded = rawPath.contains(u'%');
                    if (d.a.offset >= 0) {
                        ol.link.targetStart = d.a.offset;
                        ol.link.targetEnd = d.a.offset + rawPath.toUtf8().size();
                        ol.link.angleBrackets = d.a.offset > 0 && src[d.a.offset - 1] == '<';
                        ol.link.line = doc.lineOf(d.a.offset);
                    }
                }
            }
            links.push_back(std::move(ol));
            break;
        }
        case md::Ev::LeaveSpan: {
            const auto type = static_cast<MD_SPANTYPE>(e.kind);
            if (type != MD_SPAN_WIKILINK && type != MD_SPAN_A && type != MD_SPAN_IMG) break;
            if (links.empty()) break;
            OpenLink ol = std::move(links.back());
            links.pop_back();
            const QString text = ol.text.simplified();
            if (!links.empty()) links.back().text += ol.text;
            if (ol.skip) break;
            if (ol.external) break;
            if (ol.link.targetStart < 0 && !stack.empty()) ol.link.line = std::max(0, stack.back().last);
            ol.link.display = text;
            if (ol.hasAlias) ol.link.alias = text;
            m.links.push_back(std::move(ol.link));
            break;
        }
        case md::Ev::Text: {
            if (!wantsText()) break;
            const auto type = static_cast<MD_TEXTTYPE>(e.kind);
            switch (type) {
            case MD_TEXT_NORMAL:
            case MD_TEXT_CODE:
            case MD_TEXT_LATEXMATH:
                appendText(doc.stringOf(e));
                break;
            case MD_TEXT_ENTITY:
                appendText(decodeEntity(doc.stringOf(e)));
                break;
            case MD_TEXT_SOFTBR:
            case MD_TEXT_BR:
                appendText(u" "_s);
                break;
            default:
                break;
            }
            break;
        }
        case md::Ev::Tag:
            if (!inComment) {
                const QString t = doc.stringOf(e);
                m.tags.push_back({t.mid(1), doc.lineOf(e.offset)});
                appendText(t);
            }
            break;
        case md::Ev::Comment:
            inComment = !inComment;
            break;
        case md::Ev::BlockId: {
            if (inComment || stack.empty()) break;
            const QString id = doc.stringOf(e);
            const md::Detail& d = doc.detailOf(e);
            const std::size_t top = stack.size() - 1;
            if (d.standalone) {
                const auto prev = lastClosed.size() > top ? lastClosed[top] : std::pair<int, int>{-1, -1};
                if (prev.first >= 0) {
                    m.blocks.push_back({id, prev.first, std::max(prev.second, doc.lineOf(e.offset))});
                    break;
                }
            }
            std::size_t target = top;
            const MD_BLOCKTYPE t = stack[top].type;
            if (t == MD_BLOCK_TD || t == MD_BLOCK_TH) {
                for (std::size_t i = top + 1; i-- > 0;) {
                    if (stack[i].type == MD_BLOCK_TABLE) {
                        target = i;
                        break;
                    }
                }
            } else if (t == MD_BLOCK_P && top > 0 &&
                       (stack[top - 1].type == MD_BLOCK_LI || stack[top - 1].type == MD_BLOCK_QUOTE)) {
                target = top - 1;
            }
            pendingIds.push_back({id, target});
            break;
        }
        }
    }
    return m;
}

} // namespace mn
