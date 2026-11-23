#include "HtmlRenderer.h"

#include "core/links/FileIndex.h"
#include "core/parse/Frontmatter.h"
#include "core/parse/MarkdownDoc.h"
#include "core/parse/NoteMetadata.h"
#include "core/vault/Paths.h"

#include <QRegularExpression>
#include <QUrl>

#include <md4c.h>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

constexpr int kMaxEmbedDepth = 3;

QString colorCss(const QColor& c)
{
    return c.name(QColor::HexRgb);
}

QString encodeTarget(const QString& t)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(t, "/#^"));
}

struct CalloutStyle {
    QColor color;
    QString icon;
};

CalloutStyle calloutStyle(const QString& family)
{
    if (family == u"abstract") return {QColor(0x00, 0xb0, 0xd0), u"📋"_s};
    if (family == u"info") return {QColor(0x2e, 0x7d, 0xd8), u"ℹ"_s};
    if (family == u"todo") return {QColor(0x2e, 0x7d, 0xd8), u"☑"_s};
    if (family == u"tip") return {QColor(0x00, 0xa8, 0x8f), u"🔥"_s};
    if (family == u"success") return {QColor(0x2f, 0xa8, 0x4f), u"✔"_s};
    if (family == u"question") return {QColor(0xe0, 0x8a, 0x00), u"?"_s};
    if (family == u"warning") return {QColor(0xe0, 0x8a, 0x00), u"⚠"_s};
    if (family == u"failure") return {QColor(0xd9, 0x3f, 0x3f), u"✘"_s};
    if (family == u"danger") return {QColor(0xd9, 0x3f, 0x3f), u"⚡"_s};
    if (family == u"bug") return {QColor(0xd9, 0x3f, 0x3f), u"🐞"_s};
    if (family == u"example") return {QColor(0x7c, 0x4d, 0xdb), u"☰"_s};
    if (family == u"quote") return {QColor(0x88, 0x88, 0x88), u"❝"_s};
    return {QColor(0x2e, 0x7d, 0xd8), u"✎"_s};
}

QString sanitizeInlineHtml(QStringView tag)
{
    // Allow a small set of formatting tags (without attributes other than style/color);
    // everything else is shown as text. Nothing is ever executed by QTextBrowser.
    static const QRegularExpression re(uR"(^<(/?)([a-zA-Z][a-zA-Z0-9]*)([^>]*)>$)"_s);
    static const QSet<QString> allowed{u"b"_s,      u"i"_s,   u"u"_s,    u"s"_s,    u"em"_s,   u"strong"_s,
                                       u"br"_s,     u"sub"_s, u"sup"_s,  u"span"_s, u"mark"_s, u"kbd"_s,
                                       u"small"_s,  u"big"_s, u"code"_s, u"font"_s, u"del"_s,  u"ins"_s,
                                       u"center"_s, u"p"_s,   u"div"_s};
    const auto m = re.matchView(tag.trimmed());
    if (!m.hasMatch()) return htmlEscape(tag);
    const QString name = m.captured(2).toLower();
    if (!allowed.contains(name)) return htmlEscape(tag);
    if (name == u"mark")
        return m.captured(1).isEmpty() ? u"<span style=\"background-color:#fff3a3\">"_s : u"</span>"_s;
    if (name == u"kbd") return m.captured(1).isEmpty() ? u"<code>"_s : u"</code>"_s;
    QString attrs;
    static const QRegularExpression attr(uR"((style|color|align)\s*=\s*("[^"]*"|'[^']*'))"_s);
    for (auto it = attr.globalMatch(m.captured(3)); it.hasNext();)
        attrs += u' ' + it.next().captured(0);
    return u'<' + m.captured(1) + name + attrs + u'>';
}

// Sanitises raw HTML tag by tag: allowed tags pass, everything else becomes text.
QString sanitizeHtml(QStringView html)
{
    QString out;
    qsizetype i = 0;
    while (i < html.size()) {
        const qsizetype lt = html.indexOf(u'<', i);
        if (lt < 0) {
            out += htmlEscape(html.mid(i));
            break;
        }
        out += htmlEscape(html.mid(i, lt - i));
        const qsizetype gt = html.indexOf(u'>', lt);
        if (gt < 0) {
            out += htmlEscape(html.mid(lt));
            break;
        }
        out += sanitizeInlineHtml(html.mid(lt, gt - lt + 1));
        i = gt + 1;
    }
    return out;
}

class Renderer {
public:
    Renderer(const md::Document& doc, RenderContext& ctx) : doc_(doc), ctx_(ctx) {}

    QString run()
    {
        const auto& ev = doc_.events;
        for (i_ = 0; i_ < ev.size(); ++i_)
            handle(ev[i_]);
        return out_;
    }

private:
    void text(const QString& s)
    {
        if (inComment_ || skipDepth_ > 0) return;
        if (mathOpen_) {
            math_ += s;
            return;
        }
        out_ += s;
    }

    void raw(const QString& s)
    {
        if (skipDepth_ > 0) return;
        out_ += s;
    }

    bool resolved(const QString& lp, int* id = nullptr) const
    {
        if (lp.isEmpty() || ctx_.files == nullptr) {
            if (id) *id = -1;
            return lp.isEmpty();
        }
        const Resolution r = ctx_.files->resolve(lp, ctx_.notePath);
        if (id) *id = r.file;
        return r.resolved();
    }

    QString linkOpen(const QString& target, bool ok) const
    {
        const QColor c = ok ? ctx_.theme.link : ctx_.theme.unresolved;
        return u"<a href=\"mn-link:"_s + encodeTarget(target) + u"\"><span style=\"color:"_s + colorCss(c) +
               (ok ? u"\">"_s : u";text-decoration:underline\">"_s);
    }

    void handle(const md::Event& e)
    {
        switch (e.type) {
        case md::Ev::EnterBlock:
            enterBlock(e);
            break;
        case md::Ev::LeaveBlock:
            leaveBlock(e);
            break;
        case md::Ev::EnterSpan:
            enterSpan(e);
            break;
        case md::Ev::LeaveSpan:
            leaveSpan(e);
            break;
        case md::Ev::Text:
            onText(e);
            break;
        case md::Ev::Tag: {
            if (inComment_ || skipDepth_ > 0 || linkDepth_ > 0) {
                if (linkDepth_ > 0) text(htmlEscape(doc_.stringOf(e)));
                break;
            }
            const QString t = doc_.stringOf(e);
            raw(u"<a href=\"mn-tag:"_s + encodeTarget(t.mid(1)) + u"\"><span style=\"color:"_s +
                colorCss(ctx_.theme.tag) + u"\">"_s + htmlEscape(t) + u"</span></a>"_s);
            break;
        }
        case md::Ev::Comment:
            inComment_ = !inComment_;
            break;
        case md::Ev::BlockId:
            if (!inComment_) raw(u"<a name=\"^"_s + htmlEscape(doc_.stringOf(e)) + u"\"></a>"_s);
            break;
        }
    }

    // --- blocks -------------------------------------------------------------

    void enterBlock(const md::Event& e)
    {
        const auto type = static_cast<MD_BLOCKTYPE>(e.kind);
        if (skipDepth_ > 0) {
            ++skipDepth_;
            return;
        }
        switch (type) {
        case MD_BLOCK_DOC:
            break;
        case MD_BLOCK_QUOTE:
            enterQuote();
            break;
        case MD_BLOCK_UL:
            raw(u"<ul>"_s);
            break;
        case MD_BLOCK_OL: {
            const int start = doc_.detailOf(e).level;
            raw(start != 1 ? u"<ol start=\"%1\">"_s.arg(start) : u"<ol>"_s);
            break;
        }
        case MD_BLOCK_LI: {
            raw(u"<li>"_s);
            const md::Detail& d = doc_.detailOf(e);
            if (d.flag) raw(d.mark == u' ' ? u"☐ "_s : u"☑ "_s);
            break;
        }
        case MD_BLOCK_HR:
            raw(u"<hr/>"_s);
            break;
        case MD_BLOCK_H: {
            const int level = doc_.detailOf(e).level;
            raw(u"<h%1>"_s.arg(level));
            break;
        }
        case MD_BLOCK_CODE:
            raw(u"<table width=\"100%\" cellspacing=\"0\" cellpadding=\"8\" style=\"background-color:"_s +
                (ctx_.theme.dark ? u"#2a2a2e"_s : u"#f3f3f5"_s) + u"\"><tr><td><pre>"_s);
            break;
        case MD_BLOCK_HTML:
            break;
        case MD_BLOCK_P:
            paragraphStart_ = out_.size();
            raw(u"<p>"_s);
            break;
        case MD_BLOCK_TABLE:
            raw(u"<table cellspacing=\"0\" cellpadding=\"5\" style=\"border-collapse:collapse;margin-top:6px;margin-bottom:6px\">"_s);
            break;
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break;
        case MD_BLOCK_TR:
            raw(u"<tr>"_s);
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            const int align = doc_.detailOf(e).flag;
            const QString tag = type == MD_BLOCK_TH ? u"th"_s : u"td"_s;
            QString a;
            if (align == MD_ALIGN_LEFT) a = u" align=\"left\""_s;
            if (align == MD_ALIGN_CENTER) a = u" align=\"center\""_s;
            if (align == MD_ALIGN_RIGHT) a = u" align=\"right\""_s;
            raw(u'<' + tag + a + u" style=\"border:1px solid "_s + colorCss(ctx_.theme.muted) + u"\">"_s);
            break;
        }
        case MD_BLOCK_FOOTNOTE_DEF_SECTION:
            raw(u"<hr/><ol>"_s);
            break;
        case MD_BLOCK_FOOTNOTE_DEF: {
            const md::Detail& d = doc_.detailOf(e);
            raw(u"<li><a name=\"fn-%1\"></a>"_s.arg(d.level));
            break;
        }
        default:
            break;
        }
    }

    void leaveBlock(const md::Event& e)
    {
        const auto type = static_cast<MD_BLOCKTYPE>(e.kind);
        if (skipDepth_ > 0) {
            --skipDepth_;
            if (skipDepth_ == 0 && type == MD_BLOCK_QUOTE) closeQuote();
            return;
        }
        switch (type) {
        case MD_BLOCK_QUOTE:
            closeQuote();
            break;
        case MD_BLOCK_UL:
            raw(u"</ul>"_s);
            break;
        case MD_BLOCK_OL:
            raw(u"</ol>"_s);
            break;
        case MD_BLOCK_LI:
            raw(u"</li>"_s);
            break;
        case MD_BLOCK_H:
            raw(u"</h%1>"_s.arg(doc_.detailOf(enterOf(i_)).level));
            break;
        case MD_BLOCK_CODE:
            raw(u"</pre></td></tr></table>"_s);
            break;
        case MD_BLOCK_P:
            if (QStringView(out_).mid(paragraphStart_ + 3).trimmed().isEmpty())
                out_.truncate(paragraphStart_); // e.g. a paragraph that was only a comment
            else
                raw(u"</p>"_s);
            break;
        case MD_BLOCK_TABLE:
            raw(u"</table>"_s);
            break;
        case MD_BLOCK_TR:
            raw(u"</tr>"_s);
            break;
        case MD_BLOCK_TH:
            raw(u"</th>"_s);
            break;
        case MD_BLOCK_TD:
            raw(u"</td>"_s);
            break;
        case MD_BLOCK_FOOTNOTE_DEF_SECTION:
            raw(u"</ol>"_s);
            break;
        case MD_BLOCK_FOOTNOTE_DEF:
            raw(u"</li>"_s);
            break;
        default:
            break;
        }
    }

    // Finds the matching EnterBlock for the LeaveBlock at index i.
    const md::Event& enterOf(std::size_t i) const
    {
        int depth = 0;
        for (std::size_t k = i; k-- > 0;) {
            const md::Event& e = doc_.events[k];
            if (e.type == md::Ev::LeaveBlock) ++depth;
            if (e.type == md::Ev::EnterBlock) {
                if (depth == 0) return e;
                --depth;
            }
        }
        return doc_.events[i];
    }

    // > [!type]+/- Title
    void enterQuote()
    {
        quoteKinds_.push_back(false);
        // Look ahead to the first text of this quote.
        const auto& ev = doc_.events;
        std::size_t k = i_ + 1;
        while (k < ev.size() && !(ev[k].type == md::Ev::Text && ev[k].offset >= 0) &&
               ev[k].type != md::Ev::LeaveBlock)
            ++k;
        if (k < ev.size() && ev[k].type == md::Ev::Text && ev[k].offset >= 0) {
            const int line = doc_.lineOf(ev[k].offset);
            qsizetype ls = ev[k].offset;
            while (ls > 0 && doc_.source[ls - 1] != '\n' && doc_.source[ls - 1] != '\r')
                --ls;
            qsizetype le = ev[k].offset;
            while (le < doc_.source.size() && doc_.source[le] != '\n' && doc_.source[le] != '\r')
                ++le;
            const QString srcLine = QString::fromUtf8(doc_.source.sliced(ls, le - ls));
            static const QRegularExpression re(uR"(^\s*(?:>\s*)+\[!([^\]]+)\]([+-]?)\s*(.*)$)"_s);
            const auto m = re.match(srcLine);
            if (m.hasMatch()) {
                quoteKinds_.back() = true;
                const QString type = m.captured(1).trimmed();
                const QString family = calloutFamily(type);
                const CalloutStyle st = calloutStyle(family);
                QString title = m.captured(3).trimmed();
                if (title.isEmpty()) title = type.left(1).toUpper() + type.mid(1).toLower();
                const int index = ctx_.calloutCounter++;
                const bool foldable = !m.captured(2).isEmpty();
                bool folded = m.captured(2) == u"-";
                if (ctx_.toggledCallouts.contains(index)) folded = !folded;
                QColor tint = st.color;
                tint.setAlpha(ctx_.theme.dark ? 40 : 28);
                const QColor bg = blend(tint);
                raw(u"<table width=\"100%\" cellspacing=\"0\" cellpadding=\"8\" "
                    u"style=\"margin-top:6px;margin-bottom:6px;"
                    "background-color:"_s +
                    colorCss(bg) + u";border:1px solid "_s + colorCss(st.color) + u"\"><tr><td>"_s);
                QString head = u"<p><span style=\"color:"_s + colorCss(st.color) + u";font-weight:bold\">"_s +
                               st.icon + u" "_s + renderInline(title) + u"</span>"_s;
                if (foldable)
                    head += u" <a href=\"mn-callout:%1\">%2</a>"_s.arg(index).arg(folded ? u"▸"_s : u"▾"_s);
                head += u"</p>"_s;
                raw(head);
                calloutTitleLine_ = line;
                if (folded) skipDepth_ = 1;
                return;
            }
        }
        raw(u"<blockquote>"_s);
    }

    void closeQuote()
    {
        const bool callout = !quoteKinds_.empty() && quoteKinds_.back();
        if (!quoteKinds_.empty()) quoteKinds_.pop_back();
        out_ += callout ? u"</td></tr></table>"_s : u"</blockquote>"_s;
    }

    QColor blend(const QColor& tint) const
    {
        const QColor base = ctx_.theme.dark ? QColor(0x1e, 0x1e, 0x22) : QColor(0xff, 0xff, 0xff);
        const qreal a = tint.alphaF();
        return QColor::fromRgbF(base.redF() * (1 - a) + tint.redF() * a,
                                base.greenF() * (1 - a) + tint.greenF() * a,
                                base.blueF() * (1 - a) + tint.blueF() * a);
    }

    QString renderInline(const QString& md)
    {
        RenderContext sub = ctx_;
        const QByteArray utf8 = md.toUtf8();
        const md::Document d = md::parse(utf8);
        Renderer r(d, sub);
        QString html = r.run();
        // Strip the paragraph wrapper.
        if (html.startsWith(u"<p>"_s)) html = html.mid(3);
        if (html.endsWith(u"</p>"_s)) html.chop(4);
        return html;
    }

    // --- spans --------------------------------------------------------------

    void enterSpan(const md::Event& e)
    {
        const auto type = static_cast<MD_SPANTYPE>(e.kind);
        if (skipDepth_ > 0) return;
        switch (type) {
        case MD_SPAN_EM:
            raw(u"<i>"_s);
            break;
        case MD_SPAN_STRONG:
            raw(u"<b>"_s);
            break;
        case MD_SPAN_U:
            raw(u"<u>"_s);
            break;
        case MD_SPAN_DEL:
            raw(u"<s>"_s);
            break;
        case MD_SPAN_MARK:
            raw(u"<span style=\"background-color:"_s + colorCss(ctx_.theme.highlightBg) + u"\">"_s);
            break;
        case MD_SPAN_CODE:
            raw(u"<code style=\"background-color:"_s + (ctx_.theme.dark ? u"#2a2a2e"_s : u"#f0f0f2"_s) +
                u"\">"_s);
            break;
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
            mathOpen_ = true;
            math_.clear();
            break;
        case MD_SPAN_A: {
            const md::Detail& d = doc_.detailOf(e);
            const QString href = QString::fromUtf8(d.a.text);
            ++linkDepth_;
            if (d.flag || hasUrlScheme(href)) {
                QString url = href;
                if (d.flag && url.startsWith(u"www."_s)) url.prepend(u"https://"_s);
                raw(u"<a href=\""_s + htmlEscape(url) + u"\">"_s);
            } else {
                QString lp;
                QString sub;
                splitLinkTarget(percentDecode(href), lp, sub);
                const QString target = sub.isEmpty() ? lp : lp + u'#' + sub;
                raw(linkOpen(target, resolved(lp)));
                linkHasSpan_.push_back(true);
                break;
            }
            linkHasSpan_.push_back(false);
            break;
        }
        case MD_SPAN_IMG: {
            const md::Detail& d = doc_.detailOf(e);
            spanImage_ = QString::fromUtf8(d.a.text);
            imageAlt_.clear();
            inImage_ = true;
            break;
        }
        case MD_SPAN_WIKILINK: {
            const md::Detail& d = doc_.detailOf(e);
            QString target = QString::fromUtf8(d.a.text);
            if (target.endsWith(u'\\')) target.chop(1);
            wikiTarget_ = std::move(target);
            wikiEmbed_ = d.embed;
            wikiLabel_.clear();
            inWiki_ = true;
            break;
        }
        case MD_SPAN_FOOTNOTE_REF: {
            const md::Detail& d = doc_.detailOf(e);
            raw(u"<sup><a href=\"#fn-%1\">%1</a></sup>"_s.arg(d.level));
            break;
        }
        default:
            break;
        }
    }

    void leaveSpan(const md::Event& e)
    {
        const auto type = static_cast<MD_SPANTYPE>(e.kind);
        if (skipDepth_ > 0) return;
        switch (type) {
        case MD_SPAN_EM:
            raw(u"</i>"_s);
            break;
        case MD_SPAN_STRONG:
            raw(u"</b>"_s);
            break;
        case MD_SPAN_U:
            raw(u"</u>"_s);
            break;
        case MD_SPAN_DEL:
            raw(u"</s>"_s);
            break;
        case MD_SPAN_MARK:
            raw(u"</span>"_s);
            break;
        case MD_SPAN_CODE:
            raw(u"</code>"_s);
            break;
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY: {
            mathOpen_ = false;
            if (inComment_) break;
            const bool display = type == MD_SPAN_LATEXMATH_DISPLAY;
            const QString img = ctx_.mathHtml ? ctx_.mathHtml(math_.trimmed(), display) : QString();
            if (img.isEmpty()) {
                raw(u"<code>"_s +
                    htmlEscape(display ? QString(u"$$"_s + math_ + u"$$"_s)
                                       : QString(u"$"_s + math_ + u'$')) +
                    u"</code>"_s);
            } else if (display) {
                raw(u"</p><p align=\"center\">"_s + img + u"</p><p>"_s);
            } else {
                raw(img);
            }
            break;
        }
        case MD_SPAN_A:
            --linkDepth_;
            if (!linkHasSpan_.empty() && linkHasSpan_.back()) raw(u"</span>"_s);
            if (!linkHasSpan_.empty()) linkHasSpan_.pop_back();
            raw(u"</a>"_s);
            break;
        case MD_SPAN_IMG:
            inImage_ = false;
            if (!inComment_) raw(embedHtml(spanImage_, imageAlt_, true));
            break;
        case MD_SPAN_WIKILINK:
            inWiki_ = false;
            if (inComment_) break;
            if (wikiEmbed_) {
                raw(embedHtml(wikiTarget_, wikiLabel_, false));
            } else {
                QString lp;
                QString sub;
                splitLinkTarget(wikiTarget_, lp, sub);
                QString label = wikiLabel_;
                const bool hasAlias = !label.isEmpty() && label != wikiTarget_;
                if (!hasAlias) label = sub.isEmpty() ? lp : (lp.isEmpty() ? sub : lp + u" > "_s + sub);
                raw(linkOpen(wikiTarget_, resolved(lp)) + htmlEscape(label) + u"</span></a>"_s);
            }
            break;
        default:
            break;
        }
    }

    void onText(const md::Event& e)
    {
        const auto type = static_cast<MD_TEXTTYPE>(e.kind);
        if (calloutTitleLine_ >= 0 && e.offset >= 0) {
            if (doc_.lineOf(e.offset) == calloutTitleLine_) return; // the [!type] line is the header
            calloutTitleLine_ = -1;
        }
        if (calloutTitleLine_ >= 0 && (type == MD_TEXT_SOFTBR || type == MD_TEXT_BR)) {
            calloutTitleLine_ = -1;
            return;
        }
        if (inImage_) {
            if (type != MD_TEXT_SOFTBR && type != MD_TEXT_BR) imageAlt_ += doc_.stringOf(e);
            return;
        }
        if (inWiki_) {
            wikiLabel_ += doc_.stringOf(e);
            return;
        }
        switch (type) {
        case MD_TEXT_NORMAL:
            text(htmlEscape(doc_.stringOf(e)));
            break;
        case MD_TEXT_NULLCHAR:
            text(QString(QChar(0xFFFD)));
            break;
        case MD_TEXT_BR:
            text(u"<br/>"_s);
            break;
        case MD_TEXT_SOFTBR:
            text(mathOpen_ || ctx_.strictLineBreaks ? u" "_s : u"<br/>"_s);
            break;
        case MD_TEXT_ENTITY: {
            const QString ent = doc_.stringOf(e);
            static const QRegularExpression ok(
                uR"(^&(#[0-9]{1,7}|#[xX][0-9a-fA-F]{1,6}|[a-zA-Z][a-zA-Z0-9]{1,31});$)"_s);
            text(ok.match(ent).hasMatch() ? ent : htmlEscape(ent));
            break;
        }
        case MD_TEXT_CODE:
            text(htmlEscape(doc_.stringOf(e)));
            break;
        case MD_TEXT_HTML:
            text(sanitizeHtml(doc_.stringOf(e)));
            break;
        case MD_TEXT_LATEXMATH:
            if (mathOpen_)
                math_ += doc_.stringOf(e);
            else
                text(htmlEscape(doc_.stringOf(e)));
            break;
        default:
            break;
        }
    }

    // --- embeds -------------------------------------------------------------

    QString placeholder(const QString& msg) const
    {
        return u"<p><span style=\"color:"_s + colorCss(ctx_.theme.muted) + u"\">"_s + htmlEscape(msg) +
               u"</span></p>"_s;
    }

    QString embedHtml(const QString& targetIn, const QString& label, bool markdownImage)
    {
        QString target = targetIn;
        if (markdownImage && hasUrlScheme(target)) {
            // Remote images aren't fetched (no network access in v1).
            return u"<a href=\""_s + htmlEscape(target) + u"\">🖼 "_s +
                   htmlEscape(label.isEmpty() ? target : label) + u"</a>"_s;
        }
        if (markdownImage) target = percentDecode(target);
        QString lp;
        QString sub;
        splitLinkTarget(target, lp, sub);
        int id = -1;
        const bool ok = resolved(lp, &id);
        if (!ok || (id < 0 && !lp.isEmpty())) return placeholder(u"⚠ “%1” could not be found."_s.arg(target));
        const QString path = id >= 0 ? ctx_.files->file(id).path : ctx_.notePath;
        const FileKind kind = classifyPath(path);

        // Size: "|300" or "|300x200" (wikilinks) / "alt|300" (Markdown images).
        int w = 0;
        int h = 0;
        {
            static const QRegularExpression size(uR"((?:^|\|)(\d+)(?:x(\d+))?$)"_s);
            const auto m = size.match(label);
            if (m.hasMatch()) {
                w = m.captured(1).toInt();
                h = m.captured(2).toInt();
            }
        }
        if (kind == FileKind::Image) {
            const QString img = ctx_.imageHtml ? ctx_.imageHtml(path, w, h) : QString();
            if (!img.isEmpty()) return img;
        }
        if (kind != FileKind::Note) {
            const QString icon =
                kind == FileKind::Pdf ? u"📄"_s : (kind == FileKind::Media ? u"🎵"_s : u"📎"_s);
            return u"<table cellpadding=\"8\" style=\"border:1px solid "_s + colorCss(ctx_.theme.muted) +
                   u"\"><tr><td>"_s + icon + u" <a href=\"mn-open:"_s + encodeTarget(path) + u"\">"_s +
                   htmlEscape(paths::fileName(path)) + u"</a></td></tr></table>"_s;
        }
        // Note embed.
        if (ctx_.depth >= kMaxEmbedDepth)
            return placeholder(u"⟲ “%1” isn't shown: embeds are limited to 3 levels."_s.arg(target));
        const QString key = path + u'#' + sub;
        if (ctx_.embedStack.contains(key) || (path == ctx_.notePath && sub.isEmpty()))
            return placeholder(u"⟲ “%1” isn't shown because it embeds itself."_s.arg(target));
        const std::optional<QByteArray> rawNote = ctx_.loadNote ? ctx_.loadNote(path) : std::nullopt;
        if (!rawNote) return placeholder(u"⚠ “%1” couldn't be read."_s.arg(target));
        QByteArrayView body(*rawNote);
        if (body.startsWith("\xEF\xBB\xBF")) body = body.sliced(3);
        const NoteMetadata meta = parseNote(body);
        qsizetype from = meta.frontmatter.present ? meta.frontmatter.bodyOffset : 0;
        qsizetype to = body.size();
        auto lineStart = [&](int line) {
            qsizetype off = 0;
            for (int l = 0; l < line && off < body.size(); ++l) {
                const qsizetype nl = body.indexOf('\n', off);
                if (nl < 0) return body.size();
                off = nl + 1;
            }
            return off;
        };
        if (sub.startsWith(u'^')) {
            const QString bid = sub.mid(1);
            const BlockRef* found = nullptr;
            for (const BlockRef& b : meta.blocks) {
                if (b.id.compare(bid, Qt::CaseInsensitive) == 0) found = &b;
            }
            if (found == nullptr) return placeholder(u"⚠ Block “%1” not found in “%2”."_s.arg(bid, lp));
            from = lineStart(found->startLine);
            to = lineStart(found->endLine + 1);
        } else if (!sub.isEmpty()) {
            QStringList texts;
            for (const Heading& h : meta.headings)
                texts.append(h.text);
            const int idx = findHeading(texts, sub);
            if (idx < 0) return placeholder(u"⚠ Heading “%1” not found in “%2”."_s.arg(sub, lp));
            const Heading& hd = meta.headings[static_cast<std::size_t>(idx)];
            from = lineStart(hd.line);
            to = body.size();
            for (std::size_t k = static_cast<std::size_t>(idx) + 1; k < meta.headings.size(); ++k) {
                if (meta.headings[k].level <= hd.level) {
                    to = lineStart(meta.headings[k].line);
                    break;
                }
            }
        }
        RenderContext sub2 = ctx_;
        sub2.depth = ctx_.depth + 1;
        sub2.embedStack.append(key);
        sub2.notePath = path;
        sub2.toggledCallouts = ctx_.toggledCallouts;
        const QString inner = renderMarkdown(body.sliced(from, std::max<qsizetype>(0, to - from)), sub2);
        ctx_.calloutCounter = sub2.calloutCounter;
        return u"</p><table width=\"100%\" cellspacing=\"0\" cellpadding=\"8\" style=\"border-left:3px solid "_s +
               colorCss(ctx_.theme.accent) + u";border:1px solid "_s + colorCss(ctx_.theme.muted) +
               u"\"><tr><td><p><a href=\"mn-link:"_s + encodeTarget(target) + u"\"><span style=\"color:"_s +
               colorCss(ctx_.theme.muted) + u"\">"_s + htmlEscape(paths::linkName(path)) +
               (sub.isEmpty() ? QString() : u" › "_s + htmlEscape(sub)) + u"</span></a></p>"_s + inner +
               u"</td></tr></table><p>"_s;
    }

    const md::Document& doc_;
    RenderContext& ctx_;
    QString out_;
    std::size_t i_ = 0;
    bool inComment_ = false;
    int skipDepth_ = 0;
    int linkDepth_ = 0;
    std::vector<bool> linkHasSpan_;
    std::vector<bool> quoteKinds_;
    qsizetype paragraphStart_ = 0;
    bool mathOpen_ = false;
    QString math_;
    bool inImage_ = false;
    QString spanImage_;
    QString imageAlt_;
    bool inWiki_ = false;
    bool wikiEmbed_ = false;
    QString wikiTarget_;
    QString wikiLabel_;
    int calloutTitleLine_ = -1;
};

} // namespace

QString htmlEscape(QStringView s)
{
    QString out;
    out.reserve(s.size() + 8);
    for (QChar c : s) {
        switch (c.unicode()) {
        case '<':
            out += u"&lt;"_s;
            break;
        case '>':
            out += u"&gt;"_s;
            break;
        case '&':
            out += u"&amp;"_s;
            break;
        case '"':
            out += u"&quot;"_s;
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

QString calloutFamily(QStringView typeIn)
{
    const QString t = typeIn.toString().toLower();
    static const QHash<QString, QString> map{
        {u"note"_s, u"note"_s},         {u"abstract"_s, u"abstract"_s}, {u"summary"_s, u"abstract"_s},
        {u"tldr"_s, u"abstract"_s},     {u"info"_s, u"info"_s},         {u"todo"_s, u"todo"_s},
        {u"tip"_s, u"tip"_s},           {u"hint"_s, u"tip"_s},          {u"important"_s, u"tip"_s},
        {u"success"_s, u"success"_s},   {u"check"_s, u"success"_s},     {u"done"_s, u"success"_s},
        {u"question"_s, u"question"_s}, {u"help"_s, u"question"_s},     {u"faq"_s, u"question"_s},
        {u"warning"_s, u"warning"_s},   {u"caution"_s, u"warning"_s},   {u"attention"_s, u"warning"_s},
        {u"failure"_s, u"failure"_s},   {u"fail"_s, u"failure"_s},      {u"missing"_s, u"failure"_s},
        {u"danger"_s, u"danger"_s},     {u"error"_s, u"danger"_s},      {u"bug"_s, u"bug"_s},
        {u"example"_s, u"example"_s},   {u"quote"_s, u"quote"_s},       {u"cite"_s, u"quote"_s},
    };
    return map.value(t, u"note"_s);
}

QString renderMarkdown(QByteArrayView body, RenderContext& ctx)
{
    const md::Document doc = md::parse(body);
    Renderer r(doc, ctx);
    return r.run();
}

QString renderNote(QByteArrayView rawIn, RenderContext& ctx)
{
    QByteArrayView raw = rawIn;
    if (raw.startsWith("\xEF\xBB\xBF")) raw = raw.sliced(3);
    const Frontmatter fm = parseFrontmatter(raw);
    QString html;
    if (fm.present) {
        if (!fm.valid) {
            html += u"<p><span style=\"color:"_s + ctx.theme.unresolved.name() + u"\">⚠ "_s +
                    htmlEscape(QString(u"These properties aren't valid YAML: "_s + fm.error)) +
                    u"</span></p>"_s;
        } else if (!fm.fields.isEmpty()) {
            html += u"<table cellspacing=\"0\" cellpadding=\"3\" style=\"margin-bottom:10px\">"_s;
            for (const FrontmatterField& f : fm.fields) {
                html += u"<tr><td><span style=\"color:"_s + ctx.theme.muted.name() + u"\">"_s +
                        htmlEscape(f.key) + u"</span></td><td>"_s + htmlEscape(f.value) + u"</td></tr>"_s;
            }
            html += u"</table>"_s;
        }
    }
    const md::Document doc = md::parse(raw, fm.present ? fm.bodyOffset : 0);
    Renderer r(doc, ctx);
    html += r.run();
    return html;
}

} // namespace mn::ui
