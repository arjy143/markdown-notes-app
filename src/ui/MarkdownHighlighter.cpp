#include "MarkdownHighlighter.h"

#include "core/parse/MarkdownDoc.h"
#include "core/parse/NoteMetadata.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTextDocument>

#include <vector>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

bool isSpace(QChar c)
{
    return c == u' ' || c == u'\t';
}

int leadingSpaces(const QString& t)
{
    int i = 0;
    while (i < t.size() && isSpace(t[i]))
        ++i;
    return i;
}

// "```lang" or "~~~": returns the fence length (>= 3) or 0.
int fenceOpen(const QString& t, bool& tilde)
{
    const int ind = leadingSpaces(t);
    if (ind > 3 || ind >= t.size()) return 0;
    const QChar c = t[ind];
    if (c != u'`' && c != u'~') return 0;
    int n = 0;
    while (ind + n < t.size() && t[ind + n] == c)
        ++n;
    if (n < 3) return 0;
    if (c == u'`' && t.indexOf(u'`', ind + n) >= 0) return 0;
    tilde = c == u'~';
    return n;
}

bool fenceCloses(const QString& t, int len, bool tilde)
{
    const int ind = leadingSpaces(t);
    if (ind > 3) return false;
    const QChar c = tilde ? u'~' : u'`';
    int n = 0;
    while (ind + n < t.size() && t[ind + n] == c)
        ++n;
    if (n < len) return false;
    for (int i = ind + n; i < t.size(); ++i) {
        if (!isSpace(t[i])) return false;
    }
    return true;
}

} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {}

double MarkdownHighlighter::headingScale(int level)
{
    static const double scales[] = {1.6, 1.4, 1.25, 1.12, 1.05, 1.0};
    return scales[std::clamp(level, 1, 6) - 1];
}

void MarkdownHighlighter::setTheme(const Theme& t, const QFont& baseFont, const QString& monoFamily)
{
    theme_ = t;
    baseFont_ = baseFont;
    for (int i = 0; i < 6; ++i) {
        QTextCharFormat& h = headingFmt_[i];
        h = QTextCharFormat();
        h.setFontWeight(QFont::Bold);
        h.setFontPointSize(baseFont.pointSizeF() * headingScale(i + 1));
        h.setForeground(t.heading);
    }
    // Size the monospace font so it never makes a line taller than body text. A
    // block whose height changes makes QPlainTextEdit repaint the whole viewport
    // (and relayout everything below), which costs ~10 ms per keystroke.
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!monoFamily.isEmpty()) mono.setFamilies({monoFamily});
    {
        const QFontMetricsF base(baseFont);
        qreal size = baseFont.pointSizeF() > 0 ? baseFont.pointSizeF() : 10.0;
        mono.setPointSizeF(size);
        for (int i = 0; i < 40; ++i) {
            const QFontMetricsF fm(mono);
            if (fm.ascent() <= base.ascent() && fm.descent() <= base.descent()) break;
            size -= 0.25;
            mono.setPointSizeF(size);
        }
    }
    monoPointSize_ = mono.pointSizeF();
    codeFmt_ = QTextCharFormat();
    codeFmt_.setFontFamilies(mono.families());
    codeFmt_.setFontPointSize(monoPointSize_);
    codeFmt_.setForeground(t.code);
    codeFmt_.setBackground(t.codeBg);
    fenceFmt_ = codeFmt_;
    linkFmt_ = QTextCharFormat();
    linkFmt_.setForeground(t.link);
    unresolvedFmt_ = QTextCharFormat();
    unresolvedFmt_.setForeground(t.unresolved);
    unresolvedFmt_.setFontUnderline(true);
    unresolvedFmt_.setUnderlineStyle(QTextCharFormat::DotLine);
    tagFmt_ = QTextCharFormat();
    tagFmt_.setForeground(t.tag);
    markupFmt_ = QTextCharFormat();
    markupFmt_.setForeground(t.muted);
    commentFmt_ = QTextCharFormat();
    commentFmt_.setForeground(t.comment);
    commentFmt_.setFontItalic(true);
    mathFmt_ = QTextCharFormat();
    mathFmt_.setForeground(t.math);
    boldFmt_ = QTextCharFormat();
    boldFmt_.setFontWeight(QFont::Bold);
    italicFmt_ = QTextCharFormat();
    italicFmt_.setFontItalic(true);
    strikeFmt_ = QTextCharFormat();
    strikeFmt_.setFontStrikeOut(true);
    highlightFmt_ = QTextCharFormat();
    highlightFmt_.setBackground(t.highlightBg);
    quoteFmt_ = QTextCharFormat();
    quoteFmt_.setForeground(t.quote);
    fmFmt_ = QTextCharFormat();
    fmFmt_.setForeground(t.muted);
    fmFmt_.setFontFamilies(mono.families());
    fmFmt_.setFontPointSize(monoPointSize_);
    fmKeyFmt_ = fmFmt_;
    fmKeyFmt_.setForeground(t.accent);
    blockIdFmt_ = markupFmt_;
    urlFmt_ = linkFmt_;
    urlFmt_.setFontUnderline(true);
    rehighlight();
}

void MarkdownHighlighter::apply(int start, int len, const QTextCharFormat& f)
{
    if (len <= 0) return;
    QTextCharFormat merged = format(start);
    merged.merge(f);
    setFormat(start, len, merged);
}

void MarkdownHighlighter::highlightBlock(const QString& text)
{
    const int prev = previousBlockState();
    const Kind prevKind = kindOf(prev);
    const int n = static_cast<int>(text.size());
    setCurrentBlockState(encode(Normal));

    // --- multi-line states ---------------------------------------------
    if (currentBlock().blockNumber() == 0 && text == u"---") {
        setFormat(0, n, fmFmt_);
        setCurrentBlockState(encode(Frontmatter));
        return;
    }
    if (prevKind == Frontmatter) {
        if (text == u"---") {
            setFormat(0, n, fmFmt_);
            return;
        }
        setFormat(0, n, fmFmt_);
        const int colon = static_cast<int>(text.indexOf(u':'));
        if (colon > 0 && !isSpace(text[0]) && text[0] != u'-') setFormat(0, colon, fmKeyFmt_);
        setCurrentBlockState(encode(Frontmatter));
        return;
    }
    if (prevKind == Fence) {
        const int len = static_cast<int>((static_cast<unsigned>(prev) >> 4U) & 0xFFU);
        const bool tilde = (static_cast<unsigned>(prev) & (1U << 12U)) != 0;
        setFormat(0, n, fenceFmt_);
        if (!fenceCloses(text, len, tilde)) setCurrentBlockState(prev);
        return;
    }
    if (prevKind == MathBlock) {
        const int close = static_cast<int>(text.indexOf(u"$$"_s));
        if (close < 0) {
            setFormat(0, n, mathFmt_);
            setCurrentBlockState(encode(MathBlock));
            return;
        }
        setFormat(0, close + 2, mathFmt_);
        inlineSpans(text, close + 2, n, QTextCharFormat());
        return;
    }
    int start = 0;
    if (prevKind == Comment) {
        const int close = static_cast<int>(text.indexOf(u"%%"_s));
        if (close < 0) {
            setFormat(0, n, commentFmt_);
            setCurrentBlockState(encode(Comment));
            return;
        }
        setFormat(0, close + 2, commentFmt_);
        start = close + 2;
    }
    if (start == 0) {
        bool tilde = false;
        const int fl = fenceOpen(text, tilde);
        if (fl > 0) {
            setFormat(0, n, fenceFmt_);
            setCurrentBlockState(encode(Fence, std::min(fl, 255), tilde));
            return;
        }
        const QString trimmed = text.trimmed();
        if (trimmed.startsWith(u"$$"_s) && trimmed.indexOf(u"$$"_s, 2) < 0) {
            setFormat(0, n, mathFmt_);
            setCurrentBlockState(encode(MathBlock));
            return;
        }
    }

    // --- line-start constructs -------------------------------------------
    QTextCharFormat base;
    int pos = start;
    if (start == 0) {
        const int ind = leadingSpaces(text);
        int hashes = 0;
        while (ind + hashes < n && text[ind + hashes] == u'#')
            ++hashes;
        if (ind <= 3 && hashes >= 1 && hashes <= 6 && (ind + hashes == n || isSpace(text[ind + hashes]))) {
            base = headingFmt_[hashes - 1];
            setFormat(0, n, base);
            QTextCharFormat mk = base;
            mk.setForeground(theme_.muted);
            setFormat(0, ind + hashes, mk);
            pos = ind + hashes;
        } else if (ind < n && text[ind] == u'>') {
            int q = ind;
            while (q < n && (text[q] == u'>' || isSpace(text[q])))
                ++q;
            setFormat(0, n, quoteFmt_);
            setFormat(ind, q - ind, markupFmt_);
            base = quoteFmt_;
            static const QRegularExpression callout(uR"(^\[!([^\]]+)\]([+-]?))"_s);
            const auto m = callout.matchView(QStringView(text).mid(q));
            if (m.hasMatch()) {
                QTextCharFormat cf = markupFmt_;
                cf.setForeground(theme_.accent);
                cf.setFontWeight(QFont::Bold);
                setFormat(q, static_cast<int>(m.capturedLength()), cf);
            }
            pos = q;
        } else {
            static const QRegularExpression list(uR"(^(\s*)([-*+]|\d{1,9}[.)])(\s+)(\[[ xX]\]\s)?)"_s);
            static const QRegularExpression hr(uR"(^\s{0,3}((\*\s*){3,}|(-\s*){3,}|(_\s*){3,})$)"_s);
            if (hr.match(text).hasMatch()) {
                setFormat(0, n, markupFmt_);
                return;
            }
            const auto m = list.match(text);
            if (m.hasMatch()) {
                QTextCharFormat mk = markupFmt_;
                mk.setForeground(theme_.accent);
                setFormat(static_cast<int>(m.capturedStart(2)), static_cast<int>(m.capturedLength(2)), mk);
                if (m.capturedLength(4) > 0) {
                    setFormat(static_cast<int>(m.capturedStart(4)), 3, mk);
                    if (text[m.capturedStart(4) + 1] != u' ') {
                        QTextCharFormat done;
                        done.setForeground(theme_.muted);
                        done.setFontStrikeOut(true);
                        setFormat(static_cast<int>(m.capturedEnd(4)), n - static_cast<int>(m.capturedEnd(4)),
                                  done);
                    }
                }
                pos = static_cast<int>(m.capturedEnd(0));
            }
        }
    }
    inlineSpans(text, pos, n, base);
}

void MarkdownHighlighter::inlineSpans(const QString& text, int from, int to, const QTextCharFormat& /*base*/)
{
    if (from >= to) return;
    const int len = to - from;
    std::vector<char> taken(static_cast<std::size_t>(len), 0);
    auto take = [&](int s, int e) {
        for (int i = std::max(s, from); i < std::min(e, to); ++i)
            taken[static_cast<std::size_t>(i - from)] = 1;
    };
    auto isTaken = [&](int i) {
        return i >= from && i < to && taken[static_cast<std::size_t>(i - from)] != 0;
    };

    // Code spans first: nothing inside them is interpreted.
    for (int i = from; i < to;) {
        if (text[i] != u'`') {
            ++i;
            continue;
        }
        int run = 0;
        while (i + run < to && text[i + run] == u'`')
            ++run;
        int j = i + run;
        int close = -1;
        while (j < to) {
            if (text[j] == u'`') {
                int r2 = 0;
                while (j + r2 < to && text[j + r2] == u'`')
                    ++r2;
                if (r2 == run) {
                    close = j;
                    break;
                }
                j += r2;
            } else {
                ++j;
            }
        }
        if (close < 0) {
            i += run;
            continue;
        }
        apply(i, close + run - i, codeFmt_);
        take(i, close + run);
        i = close + run;
    }

    // %% comments within the line; an unmatched one opens a multi-line comment.
    for (int i = from; i + 1 < to; ++i) {
        if (isTaken(i) || text[i] != u'%' || text[i + 1] != u'%') continue;
        int close = -1;
        for (int j = i + 2; j + 1 < to; ++j) {
            if (!isTaken(j) && text[j] == u'%' && text[j + 1] == u'%') {
                close = j;
                break;
            }
        }
        if (close < 0) {
            apply(i, to - i, commentFmt_);
            take(i, to);
            setCurrentBlockState(encode(Comment));
            break;
        }
        apply(i, close + 2 - i, commentFmt_);
        take(i, close + 2);
        i = close + 1;
    }

    // Wikilinks and embeds.
    for (int i = from; i + 1 < to; ++i) {
        if (isTaken(i) || text[i] != u'[' || text[i + 1] != u'[') continue;
        const int close = static_cast<int>(text.indexOf(u"]]"_s, i + 2));
        if (close < 0 || close >= to) break;
        const int s = (i > from && text[i - 1] == u'!') ? i - 1 : i;
        const QStringView inner = QStringView(text).mid(i + 2, close - i - 2);
        qsizetype bar = inner.indexOf(u'|');
        QStringView target = bar >= 0 ? inner.left(bar) : inner;
        if (target.endsWith(u'\\')) target.chop(1);
        QString lp;
        QString sub;
        splitLinkTarget(target, lp, sub);
        const bool ok = lp.isEmpty() || !checker_ || checker_(lp);
        apply(s, i + 2 - s, markupFmt_);
        apply(i + 2, close - i - 2, ok ? linkFmt_ : unresolvedFmt_);
        apply(close, 2, markupFmt_);
        take(s, close + 2);
        i = close + 1;
    }

    // Markdown links and images: [text](dest), ![alt](dest)
    static const QRegularExpression mdLink(uR"(!?\[([^\]\n]*)\]\((<[^>\n]*>|[^)\s]*)(\s+"[^"]*")?\))"_s);
    for (auto it = mdLink.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        const int s = static_cast<int>(m.capturedStart());
        const int e = static_cast<int>(m.capturedEnd());
        if (e > to || isTaken(s)) continue;
        QString dest = m.captured(2);
        if (dest.startsWith(u'<')) dest = dest.mid(1, dest.size() - 2);
        bool ok = true;
        if (!dest.isEmpty() && !hasUrlScheme(dest) && checker_) {
            QString lp;
            QString sub;
            splitLinkTarget(percentDecode(dest), lp, sub);
            ok = lp.isEmpty() || checker_(lp);
        }
        apply(s, e - s, markupFmt_);
        apply(static_cast<int>(m.capturedStart(1)), static_cast<int>(m.capturedLength(1)),
              ok ? linkFmt_ : unresolvedFmt_);
        take(s, e);
    }

    // Bare URLs.
    static const QRegularExpression url(uR"((https?://|www\.)[^\s<>()\[\]]+[^\s<>()\[\].,;:!?'"])"_s);
    for (auto it = url.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        const int s = static_cast<int>(m.capturedStart());
        if (isTaken(s) || m.capturedEnd() > to) continue;
        apply(s, static_cast<int>(m.capturedLength()), urlFmt_);
        take(s, static_cast<int>(m.capturedEnd()));
    }

    // Inline math $...$ (a '$' followed by whitespace doesn't open math).
    for (int i = from; i < to; ++i) {
        if (isTaken(i) || text[i] != u'$') continue;
        if (i + 1 < to && text[i + 1] == u'$') {
            const int close = static_cast<int>(text.indexOf(u"$$"_s, i + 2));
            if (close >= 0 && close < to) {
                apply(i, close + 2 - i, mathFmt_);
                take(i, close + 2);
                i = close + 1;
            }
            continue;
        }
        if (i + 1 >= to || text[i + 1].isSpace()) continue;
        int close = -1;
        for (int j = i + 1; j < to; ++j) {
            if (text[j] == u'$' && !isTaken(j) && text[j - 1] != u'\\' && !text[j - 1].isSpace()) {
                close = j;
                break;
            }
        }
        if (close < 0) break;
        apply(i, close + 1 - i, mathFmt_);
        take(i, close + 1);
        i = close;
    }

    // Tags.
    for (int i = from; i < to; ++i) {
        if (isTaken(i) || text[i] != u'#') continue;
        if (i > 0 && !text[i - 1].isSpace() && text[i - 1] != u'(' && text[i - 1] != u'[' &&
            text[i - 1] != u'{')
            continue;
        int j = i + 1;
        bool nonDigit = false;
        while (j < to && !isTaken(j)) {
            const QChar c = text[j];
            char32_t cp = c.unicode();
            int step = 1;
            if (c.isHighSurrogate() && j + 1 < to) {
                cp = QChar::surrogateToUcs4(c, text[j + 1]);
                step = 2;
            }
            if (!md::isTagCodePoint(cp)) break;
            if (cp < '0' || cp > '9') nonDigit = true;
            j += step;
        }
        if (j > i + 1 && nonDigit) {
            apply(i, j - i, tagFmt_);
            take(i, j);
            i = j - 1;
        }
    }

    // Block id at the end of the line.
    {
        int e = to;
        while (e > from && isSpace(text[e - 1]))
            --e;
        int s = e;
        while (s > from && (text[s - 1].isLetterOrNumber() || text[s - 1] == u'-') &&
               text[s - 1].unicode() < 128)
            --s;
        if (s < e && s > from && text[s - 1] == u'^' && (s - 1 == from || isSpace(text[s - 2])) &&
            !isTaken(s - 1))
            apply(s - 1, e - s + 1, blockIdFmt_);
    }

    // Emphasis, strikethrough, highlight (simple, non-nested).
    struct Rule {
        const QRegularExpression* re;
        const QTextCharFormat* fmt;
        int marker;
    };
    static const QRegularExpression bold(uR"((\*\*|__)(?=\S)(.+?)(?<=\S)\1)"_s);
    static const QRegularExpression italic(
        uR"((?<![\*\w])(\*|_)(?=[^\s\*_])(.+?)(?<=[^\s\*_])\1(?![\*\w]))"_s);
    static const QRegularExpression strike(uR"(~~(?=\S)(.+?)(?<=\S)~~)"_s);
    static const QRegularExpression mark(uR"(==(?=\S)(.+?)(?<=\S)==)"_s);
    const Rule rules[] = {{&bold, &boldFmt_, 2},
                          {&italic, &italicFmt_, 1},
                          {&strike, &strikeFmt_, 2},
                          {&mark, &highlightFmt_, 2}};
    for (const Rule& r : rules) {
        for (auto it = r.re->globalMatch(text, from); it.hasNext();) {
            const auto m = it.next();
            const int s = static_cast<int>(m.capturedStart());
            const int e = static_cast<int>(m.capturedEnd());
            if (e > to || isTaken(s) || isTaken(e - 1)) continue;
            apply(s, e - s, *r.fmt);
            apply(s, r.marker, markupFmt_);
            apply(e - r.marker, r.marker, markupFmt_);
        }
    }
}

} // namespace mn::ui
