#include "MarkdownDoc.h"

#include <QChar>

#include <algorithm>
#include <md4c.h>

namespace mn::md {

namespace {

constexpr unsigned kFlags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS |
                            MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_LATEXMATHSPANS | MD_FLAG_WIKILINKS |
                            MD_FLAG_HIGHLIGHT | MD_FLAG_FOOTNOTES;

// Decodes one UTF-8 code point at p (bounded by end). Returns 0 length on error.
char32_t decodeUtf8(const char* p, const char* end, int& len)
{
    const auto b0 = static_cast<unsigned char>(p[0]);
    if (b0 < 0x80U) {
        len = 1;
        return b0;
    }
    int n = 0;
    char32_t cp = 0;
    if ((b0 & 0xE0U) == 0xC0U) {
        n = 2;
        cp = b0 & 0x1FU;
    } else if ((b0 & 0xF0U) == 0xE0U) {
        n = 3;
        cp = b0 & 0x0FU;
    } else if ((b0 & 0xF8U) == 0xF0U) {
        n = 4;
        cp = b0 & 0x07U;
    } else {
        len = 0;
        return 0;
    }
    if (end - p < n) {
        len = 0;
        return 0;
    }
    for (int i = 1; i < n; ++i) {
        const auto b = static_cast<unsigned char>(p[i]);
        if ((b & 0xC0U) != 0x80U) {
            len = 0;
            return 0;
        }
        cp = (cp << 6U) | (b & 0x3FU);
    }
    len = n;
    return cp;
}

bool isBlockIdChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

struct OpenBlock {
    MD_BLOCKTYPE type;
    std::size_t enterIndex;
};

class Builder {
public:
    Builder(Document& doc) : doc_(doc) {}

    Attr attr(const MD_ATTRIBUTE& a)
    {
        Attr out;
        if (a.text == nullptr) return out;
        out.text = QByteArray(a.text, static_cast<qsizetype>(a.size));
        if (inSource(a.text)) {
            out.offset = a.text - doc_.source.data();
            out.rawLength = static_cast<qsizetype>(a.size);
        }
        return out;
    }

    bool inSource(const char* p) const
    {
        return p >= doc_.source.data() && p <= doc_.source.data() + doc_.source.size();
    }

    int addDetail(Detail d)
    {
        doc_.details.push_back(std::move(d));
        return static_cast<int>(doc_.details.size() - 1);
    }

    void push(Event e) { doc_.events.push_back(e); }

    int enterBlock(MD_BLOCKTYPE type, void* detail)
    {
        Detail d;
        bool hasDetail = true;
        switch (type) {
        case MD_BLOCK_UL: {
            auto* u = static_cast<MD_BLOCK_UL_DETAIL*>(detail);
            d.flag = u->is_tight;
            d.mark = u->mark;
            break;
        }
        case MD_BLOCK_OL: {
            auto* o = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
            d.level = static_cast<int>(o->start);
            d.flag = o->is_tight;
            d.mark = o->mark_delimiter;
            break;
        }
        case MD_BLOCK_LI: {
            auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
            d.flag = li->is_task;
            d.mark = li->task_mark;
            // md4c reports the mark relative to the parsed body; store it as a source offset.
            if (li->is_task) d.a.offset = doc_.bodyOffset + static_cast<qsizetype>(li->task_mark_offset);
            break;
        }
        case MD_BLOCK_H:
            d.level = static_cast<int>(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
            break;
        case MD_BLOCK_CODE: {
            auto* c = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
            d.a = attr(c->info);
            d.b = attr(c->lang);
            d.flag = static_cast<unsigned char>(c->fence_char);
            break;
        }
        case MD_BLOCK_TABLE:
            d.level = static_cast<int>(static_cast<MD_BLOCK_TABLE_DETAIL*>(detail)->col_count);
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            d.flag = static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align;
            break;
        case MD_BLOCK_FOOTNOTE_DEF: {
            auto* f = static_cast<MD_BLOCK_FOOTNOTE_DEF_DETAIL*>(detail);
            d.level = static_cast<int>(f->id);
            d.a = attr(f->label);
            break;
        }
        default:
            hasDetail = false;
            break;
        }
        Event e{Ev::EnterBlock, static_cast<unsigned char>(type), hasDetail ? addDetail(std::move(d)) : -1,
                -1, 0};
        stack_.push_back({type, doc_.events.size()});
        push(e);
        return 0;
    }

    int leaveBlock(MD_BLOCKTYPE type)
    {
        if (type == MD_BLOCK_P || type == MD_BLOCK_H || type == MD_BLOCK_TD || type == MD_BLOCK_TH ||
            type == MD_BLOCK_LI) {
            detectBlockId(type);
        }
        if (!stack_.empty()) stack_.pop_back();
        push({Ev::LeaveBlock, static_cast<unsigned char>(type), -1, -1, 0});
        return 0;
    }

    int enterSpan(MD_SPANTYPE type, void* detail)
    {
        Detail d;
        bool hasDetail = true;
        switch (type) {
        case MD_SPAN_A: {
            auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
            d.a = attr(a->href);
            d.b = attr(a->title);
            d.flag = a->is_autolink;
            break;
        }
        case MD_SPAN_IMG: {
            auto* i = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
            d.a = attr(i->src);
            d.b = attr(i->title);
            break;
        }
        case MD_SPAN_WIKILINK: {
            auto* w = static_cast<MD_SPAN_WIKILINK_DETAIL*>(detail);
            d.a = attr(w->target);
            break;
        }
        case MD_SPAN_FOOTNOTE_REF: {
            auto* f = static_cast<MD_SPAN_FOOTNOTE_REF_DETAIL*>(detail);
            d.level = static_cast<int>(f->id);
            d.flag = static_cast<int>(f->ref_id);
            d.a = attr(f->label);
            break;
        }
        default:
            hasDetail = false;
            break;
        }
        if (type == MD_SPAN_A || type == MD_SPAN_IMG || type == MD_SPAN_WIKILINK) ++linkDepth_;
        spanStack_.push_back(doc_.events.size());
        push({Ev::EnterSpan, static_cast<unsigned char>(type), hasDetail ? addDetail(std::move(d)) : -1, -1,
              0});
        return 0;
    }

    int leaveSpan(MD_SPANTYPE type)
    {
        if (type == MD_SPAN_A || type == MD_SPAN_IMG || type == MD_SPAN_WIKILINK) --linkDepth_;
        const std::size_t enter = spanStack_.empty() ? doc_.events.size() : spanStack_.back();
        if (!spanStack_.empty()) spanStack_.pop_back();
        if (type == MD_SPAN_WIKILINK) fixTableWikilink(enter);
        if (type == MD_SPAN_LATEXMATH && demoteMath(enter)) return 0;
        push({Ev::LeaveSpan, static_cast<unsigned char>(type), -1, -1, 0});
        return 0;
    }

    int text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size)
    {
        if (!inSource(text) || (type != MD_TEXT_NORMAL && type != MD_TEXT_CODE && type != MD_TEXT_ENTITY &&
                                type != MD_TEXT_HTML && type != MD_TEXT_LATEXMATH)) {
            doc_.extraText.emplace_back(text, static_cast<qsizetype>(size));
            push({Ev::Text, static_cast<unsigned char>(type), static_cast<int>(doc_.extraText.size() - 1), -1,
                  static_cast<qsizetype>(size)});
            return 0;
        }
        const qsizetype off = text - doc_.source.data();
        if (type != MD_TEXT_NORMAL) {
            push({Ev::Text, static_cast<unsigned char>(type), -1, off, static_cast<qsizetype>(size)});
            return 0;
        }
        splitNormal(off, off + static_cast<qsizetype>(size));
        return 0;
    }

private:
    void pushNormal(qsizetype from, qsizetype to)
    {
        if (to > from) push({Ev::Text, MD_TEXT_NORMAL, -1, from, to - from});
    }

    bool isTagBoundary(qsizetype pos) const
    {
        if (pos <= doc_.bodyOffset) return true;
        const char p = doc_.source[pos - 1];
        return p == ' ' || p == '\t' || p == '\n' || p == '\r' || p == '(' || p == '[' || p == '{';
    }

    // Returns the end offset of a tag starting at '#' (pos), or -1.
    qsizetype tagEnd(qsizetype pos, qsizetype limit) const
    {
        const char* s = doc_.source.data();
        qsizetype i = pos + 1;
        bool nonDigit = false;
        while (i < limit) {
            int len = 0;
            const char32_t cp = decodeUtf8(s + i, s + limit, len);
            if (len == 0 || !isTagCodePoint(cp)) break;
            if (cp < '0' || cp > '9') nonDigit = true;
            i += len;
        }
        if (i == pos + 1 || !nonDigit) return -1;
        return i;
    }

    void splitNormal(qsizetype from, qsizetype to)
    {
        if (from < consumedUntil_) from = std::min(consumedUntil_, to);
        const char* s = doc_.source.data();
        qsizetype runStart = from;
        qsizetype i = from;
        while (i < to) {
            const char c = s[i];
            if (c == '%' && i + 1 < to && s[i + 1] == '%') {
                pushNormal(runStart, i);
                push({Ev::Comment, 0, -1, i, 2});
                i += 2;
                runStart = i;
                continue;
            }
            if (c == '!' && linkDepth_ == 0) {
                const qsizetype close = embedClose(i, to);
                if (close > 0) {
                    pushNormal(runStart, i);
                    emitEmbed(i, close);
                    const qsizetype end = close + 2;
                    if (end > to) consumedUntil_ = end; // md4c may have split the text
                    i = std::min(end, to);
                    runStart = i;
                    continue;
                }
            }
            if (c == '#' && linkDepth_ == 0 && isTagBoundary(i)) {
                // A tag may continue past this text piece (md4c splits text at '_' etc.),
                // so scan the source up to the end of the line.
                qsizetype lineEnd = i;
                while (lineEnd < doc_.source.size() && s[lineEnd] != '\n' && s[lineEnd] != '\r')
                    ++lineEnd;
                const qsizetype end = tagEnd(i, lineEnd);
                if (end > 0) {
                    pushNormal(runStart, i);
                    push({Ev::Tag, 0, -1, i, end - i});
                    if (end > to) consumedUntil_ = end;
                    i = std::min(end, to);
                    runStart = i;
                    continue;
                }
            }
            ++i;
        }
        pushNormal(runStart, to);
    }

    // Inside tables md4c resolves "[[a\\|b]]" with a constructed target "a|b".
    // Re-anchor the target in the source and keep only the alias as the label.
    void fixTableWikilink(std::size_t enter)
    {
        if (enter >= doc_.events.size()) return;
        Detail& d = doc_.details[static_cast<std::size_t>(doc_.events[enter].detail)];
        if (d.a.offset >= 0) return;
        std::size_t first = enter + 1;
        while (first < doc_.events.size() && doc_.events[first].offset < 0)
            ++first;
        if (first >= doc_.events.size()) return;
        const char* s = doc_.source.data();
        const qsizetype start = doc_.events[first].offset;
        qsizetype i = start;
        while (i < doc_.source.size() && s[i] != '\n' &&
               !(s[i] == '\\' && i + 1 < doc_.source.size() && s[i + 1] == '|') && s[i] != '|' &&
               !(s[i] == ']' && i + 1 < doc_.source.size() && s[i + 1] == ']')) {
            ++i;
        }
        d.a.offset = start;
        d.a.rawLength = i - start;
        d.a.text = QByteArray(s + start, i - start);
        // Drop label events up to and including the separating '|'.
        qsizetype pipe = -1;
        if (i < doc_.source.size() && s[i] == '\\')
            pipe = i + 1;
        else if (i < doc_.source.size() && s[i] == '|')
            pipe = i;
        if (pipe < 0) return;
        std::size_t cut = enter + 1;
        for (std::size_t k = enter + 1; k < doc_.events.size(); ++k) {
            const Event& e = doc_.events[k];
            if (e.offset >= 0 && e.offset <= pipe && pipe < e.offset + e.length) {
                Event& ev = doc_.events[k];
                const qsizetype keepFrom = pipe + 1;
                if (keepFrom < ev.offset + ev.length) {
                    ev.length -= keepFrom - ev.offset;
                    ev.offset = keepFrom;
                    cut = k;
                } else {
                    cut = k + 1;
                }
                break;
            }
        }
        doc_.events.erase(doc_.events.begin() + static_cast<std::ptrdiff_t>(enter + 1),
                          doc_.events.begin() + static_cast<std::ptrdiff_t>(cut));
    }

    // "$ x$": a '$' followed by whitespace doesn't open inline math (compat spec §2).
    // Turns the span back into normal text.
    bool demoteMath(std::size_t enter)
    {
        if (enter >= doc_.events.size()) return false;
        std::size_t firstText = enter + 1;
        if (firstText >= doc_.events.size()) return false;
        const Event& t = doc_.events[firstText];
        if (t.type != Ev::Text || t.offset < 0 || t.length == 0) return false;
        auto isWs = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; };
        // "$ x$" (space after the opening $) and "$x $" (space before the closing $) aren't math.
        const Event& lastText = doc_.events.back();
        const bool closeWs = lastText.type == Ev::Text && lastText.offset >= 0 && lastText.length > 0 &&
                             isWs(doc_.source[lastText.offset + lastText.length - 1]);
        if (!isWs(doc_.source[t.offset]) && !closeWs) return false;
        std::vector<Event> inner(doc_.events.begin() + static_cast<std::ptrdiff_t>(enter + 1),
                                 doc_.events.end());
        doc_.events.resize(enter);
        const qsizetype open = t.offset - 1;
        splitNormal(open, open + 1);
        qsizetype end = open + 1;
        for (const Event& e : inner) {
            if (e.offset >= 0) {
                splitNormal(e.offset, e.offset + e.length);
                end = e.offset + e.length;
            } else {
                push(e);
            }
        }
        if (end < doc_.source.size() && doc_.source[end] == '$') splitNormal(end, end + 1);
        return true;
    }

    // Obsidian embeds "![[target|alias]]": md4c reads "![" as an image opener and
    // leaves the whole construct as text, so it's recognised here.
    // Returns the offset of the closing "]]" of an embed starting at i, or -1.
    qsizetype embedClose(qsizetype i, qsizetype to) const
    {
        const char* s = doc_.source.data();
        const qsizetype n = doc_.source.size();
        if (i + 3 > to || s[i + 1] != '[' || s[i + 2] != '[') return -1;
        if (i > 0 && s[i - 1] == '\\') return -1;
        qsizetype close = i + 3;
        while (close + 1 < n && s[close] != '\n' && s[close] != '\r' &&
               !(s[close] == ']' && s[close + 1] == ']'))
            ++close;
        if (close + 1 >= n || s[close] != ']' || s[close + 1] != ']') return -1;
        qsizetype tEnd = i + 3;
        while (tEnd < close && s[tEnd] != '|' && !(s[tEnd] == '\\' && tEnd + 1 < close && s[tEnd + 1] == '|'))
            ++tEnd;
        return tEnd == i + 3 ? -1 : close;
    }

    void emitEmbed(qsizetype i, qsizetype close)
    {
        const char* s = doc_.source.data();
        const qsizetype tStart = i + 3;
        qsizetype tEnd = tStart;
        while (tEnd < close && s[tEnd] != '|' && !(s[tEnd] == '\\' && tEnd + 1 < close && s[tEnd + 1] == '|'))
            ++tEnd;
        Detail d;
        d.embed = true;
        d.a.text = QByteArray(s + tStart, tEnd - tStart);
        d.a.offset = tStart;
        d.a.rawLength = tEnd - tStart;
        const int det = addDetail(std::move(d));
        push({Ev::EnterSpan, MD_SPAN_WIKILINK, det, -1, 0});
        qsizetype labelStart = tStart;
        qsizetype labelEnd = tEnd;
        if (tEnd < close) {
            labelStart = s[tEnd] == '\\' ? tEnd + 2 : tEnd + 1;
            labelEnd = close;
        }
        if (labelEnd > labelStart) push({Ev::Text, MD_TEXT_NORMAL, -1, labelStart, labelEnd - labelStart});
        push({Ev::LeaveSpan, MD_SPAN_WIKILINK, -1, -1, 0});
    }

    // "^id" at the very end of a block's text becomes a BlockId event.
    void detectBlockId(MD_BLOCKTYPE type)
    {
        if (stack_.empty()) return;
        const std::size_t enter = stack_.back().enterIndex;
        // Find the last text event of this block, ignoring trailing span closings.
        std::size_t idx = doc_.events.size();
        while (idx > enter + 1) {
            const Event& e = doc_.events[idx - 1];
            if (e.type == Ev::LeaveSpan) {
                --idx;
                continue;
            }
            break;
        }
        if (idx <= enter + 1) return;
        Event& last = doc_.events[idx - 1];
        if (last.type != Ev::Text || last.kind != MD_TEXT_NORMAL || last.offset < 0) return;
        if (idx != doc_.events.size()) return; // the id must not be inside a span
        const char* s = doc_.source.data();
        qsizetype end = last.offset + last.length;
        while (end > last.offset && (s[end - 1] == ' ' || s[end - 1] == '\t'))
            --end;
        qsizetype j = end;
        while (j > last.offset && isBlockIdChar(s[j - 1]))
            --j;
        if (j == end || j <= last.offset || s[j - 1] != '^') return;
        const qsizetype caret = j - 1;
        // The caret must follow whitespace or start the block's text.
        const bool firstInBlock =
            caret == last.offset && idx >= 2 && doc_.events[idx - 2].type == Ev::EnterBlock;
        const char before = caret > 0 ? s[caret - 1] : '\n';
        if (!firstInBlock && before != ' ' && before != '\t' && before != '\n' && before != '\r') return;

        Detail d;
        // A paragraph that is only "^id" applies to the previous block.
        d.standalone = type == MD_BLOCK_P && firstInBlock;
        const int detail = addDetail(std::move(d));
        Event& lastRef = doc_.events[idx - 1];
        lastRef.length = caret - lastRef.offset;
        // Trim whitespace before the caret.
        while (lastRef.length > 0 && (s[lastRef.offset + lastRef.length - 1] == ' ' ||
                                      s[lastRef.offset + lastRef.length - 1] == '\t')) {
            --lastRef.length;
        }
        const Event idEvent{Ev::BlockId, 0, detail, caret + 1, end - caret - 1};
        if (lastRef.length == 0) {
            doc_.events[idx - 1] = idEvent;
        } else {
            doc_.events.push_back(idEvent);
        }
    }

    Document& doc_;
    std::vector<OpenBlock> stack_;
    std::vector<std::size_t> spanStack_;
    int linkDepth_ = 0;
    qsizetype consumedUntil_ = 0;
};

int cbEnterBlock(MD_BLOCKTYPE t, void* d, void* u)
{
    return static_cast<Builder*>(u)->enterBlock(t, d);
}
int cbLeaveBlock(MD_BLOCKTYPE t, void*, void* u)
{
    return static_cast<Builder*>(u)->leaveBlock(t);
}
int cbEnterSpan(MD_SPANTYPE t, void* d, void* u)
{
    return static_cast<Builder*>(u)->enterSpan(t, d);
}
int cbLeaveSpan(MD_SPANTYPE t, void*, void* u)
{
    return static_cast<Builder*>(u)->leaveSpan(t);
}
int cbText(MD_TEXTTYPE t, const MD_CHAR* text, MD_SIZE size, void* u)
{
    return static_cast<Builder*>(u)->text(t, text, size);
}

} // namespace

bool isTagCodePoint(char32_t c)
{
    if (c < 0x80) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
               c == '-' || c == '/';
    }
    const QChar::Category cat = QChar::category(c);
    switch (cat) {
    case QChar::Letter_Uppercase:
    case QChar::Letter_Lowercase:
    case QChar::Letter_Titlecase:
    case QChar::Letter_Modifier:
    case QChar::Letter_Other:
    case QChar::Number_DecimalDigit:
    case QChar::Number_Letter:
    case QChar::Number_Other:
    case QChar::Mark_NonSpacing:
    case QChar::Mark_SpacingCombining:
    case QChar::Mark_Enclosing:
        return true;
    default:
        return false;
    }
}

int Document::lineOf(qsizetype offset) const
{
    const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
    return static_cast<int>(it - lineStarts_.begin()) - 1;
}

QByteArrayView Document::textOf(const Event& e) const
{
    if (e.offset >= 0) return source.sliced(e.offset, e.length);
    if (e.type == Ev::Text && e.detail >= 0) return extraText[static_cast<std::size_t>(e.detail)];
    return {};
}

Document parse(QByteArrayView source, qsizetype bodyOffset)
{
    Document doc;
    doc.source = source;
    doc.bodyOffset = bodyOffset;
    doc.lineStarts_.push_back(0);
    for (qsizetype i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (c == '\n') {
            doc.lineStarts_.push_back(i + 1);
        } else if (c == '\r' && (i + 1 >= source.size() || source[i + 1] != '\n')) {
            doc.lineStarts_.push_back(i + 1);
        }
    }
    doc.events.reserve(static_cast<std::size_t>(source.size() / 8 + 16));

    Builder builder(doc);
    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = kFlags;
    parser.enter_block = cbEnterBlock;
    parser.leave_block = cbLeaveBlock;
    parser.enter_span = cbEnterSpan;
    parser.leave_span = cbLeaveSpan;
    parser.text = cbText;
    const QByteArrayView body = source.sliced(std::min(bodyOffset, source.size()));
    md_parse(body.data(), static_cast<MD_SIZE>(body.size()), &parser, &builder);
    return doc;
}

} // namespace mn::md
