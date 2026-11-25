#include "HtmlToMarkdown.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

bool isMono(const QTextCharFormat& f)
{
    if (f.fontFixedPitch()) return true;
    const QStringList families = f.fontFamilies().toStringList();
    if (families.isEmpty()) return false;
    static const QString systemFixed = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    for (const QString& fam : families) {
        const QString l = fam.toLower();
        if (l.contains(u"mono"_s) || l.contains(u"courier"_s) || l.contains(u"consolas"_s) ||
            fam.compare(systemFixed, Qt::CaseInsensitive) == 0)
            return true;
    }
    // Whatever it's called, the font it resolves to is fixed-width.
    return QFontInfo(f.font()).fixedPitch();
}

// Wraps `text` in `mark` on both sides, keeping spaces outside the markers.
QString wrap(const QString& text, const QString& mark)
{
    const qsizetype start = text.size() - QString(text).remove(QRegularExpression(u"^\\s+"_s)).size();
    const qsizetype end = QString(text).remove(QRegularExpression(u"\\s+$"_s)).size();
    if (end <= start) return text;
    return text.left(start) + mark + text.mid(start, end - start) + mark + text.mid(end);
}

// The block's text with inline formatting.
QString inlineText(const QTextBlock& block, bool inHeading, bool inCode)
{
    QString out;
    QString linkHref;
    QString linkText;
    auto flushLink = [&] {
        if (linkHref.isEmpty()) return;
        const QString t = linkText.trimmed();
        if (t.isEmpty() || t == linkHref)
            out += u'<' + linkHref + u'>';
        else
            out += u'[' + t + u"]("_s + linkHref + u')';
        linkHref.clear();
        linkText.clear();
    };
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        if (!frag.isValid()) continue;
        const QTextCharFormat f = frag.charFormat();
        QString piece;
        if (f.isImageFormat()) {
            const QTextImageFormat img = f.toImageFormat();
            const QString alt = img.property(QTextFormat::ImageAltText).toString();
            piece = u"!["_s + alt + u"]("_s + img.name() + u')';
        } else {
            piece = frag.text();
            piece.replace(QChar::LineSeparator, u'\n');
            piece.replace(QChar::Nbsp, u' ');
            if (!inCode) {
                if (isMono(f)) {
                    piece = wrap(piece, u"`"_s);
                } else {
                    if (f.fontStrikeOut()) piece = wrap(piece, u"~~"_s);
                    if (f.fontItalic()) piece = wrap(piece, u"*"_s);
                    if (f.fontWeight() >= QFont::DemiBold && !inHeading) piece = wrap(piece, u"**"_s);
                }
            }
        }
        const QString href = f.isAnchor() ? f.anchorHref() : QString();
        if (href != linkHref) flushLink();
        if (!href.isEmpty() && !href.startsWith(u'#') && !inCode) {
            linkHref = href;
            linkText += piece;
        } else {
            out += piece;
        }
    }
    flushLink();
    // Markers that meet ("**a****b**") are merged.
    out.replace(u"****"_s, QString());
    return out;
}

QString cellText(const QTextTableCell& cell, bool header)
{
    QStringList parts;
    for (auto it = cell.begin(); !it.atEnd(); ++it) {
        // Header cells are bold anyway: no ** around them.
        if (it.currentBlock().isValid()) parts.append(inlineText(it.currentBlock(), header, false).trimmed());
    }
    QString t = parts.join(u' ').simplified();
    t.replace(u'|', u"\\|"_s);
    return t;
}

QString tableText(QTextTable* table)
{
    QStringList lines;
    for (int r = 0; r < table->rows(); ++r) {
        QStringList cells;
        for (int c = 0; c < table->columns(); ++c)
            cells.append(cellText(table->cellAt(r, c), r == 0));
        lines.append(u"| "_s + cells.join(u" | "_s) + u" |"_s);
        if (r == 0) {
            QStringList sep;
            for (int c = 0; c < table->columns(); ++c)
                sep.append(u"---"_s);
            lines.append(u"| "_s + sep.join(u" | "_s) + u" |"_s);
        }
    }
    return lines.join(u'\n');
}

class Writer {
public:
    void frame(QTextFrame* f)
    {
        for (auto it = f->begin(); !it.atEnd(); ++it) {
            if (QTextFrame* child = it.currentFrame()) {
                if (auto* table = qobject_cast<QTextTable*>(child)) {
                    closeCode();
                    emit(tableText(table), Kind::Block);
                } else {
                    frame(child);
                }
            } else if (it.currentBlock().isValid()) {
                block(it.currentBlock());
            }
        }
        closeCode();
    }

    QString result() const
    {
        QString t = out_;
        t.replace(QRegularExpression(u"\n{3,}"_s), u"\n\n"_s);
        return t.trimmed();
    }

private:
    enum class Kind { Block, ListItem, Code };

    void emit(const QString& text, Kind kind, const QTextList* list = nullptr)
    {
        if (!out_.isEmpty()) {
            // Items of one list and code lines follow each other; everything else is a paragraph.
            const bool sameList =
                list == nullptr || list_ == nullptr || list == list_ || list->format().indent() > 1;
            const bool tight =
                (kind == Kind::ListItem && last_ == Kind::ListItem && sameList) || kind == Kind::Code;
            out_ += tight ? u"\n"_s : u"\n\n"_s;
        }
        out_ += text;
        last_ = kind;
        if (list != nullptr && list->format().indent() <= 1) list_ = list;
    }

    void closeCode()
    {
        if (!inFence_) return;
        out_ += u"\n```"_s;
        inFence_ = false;
        last_ = Kind::Block;
    }

    void block(const QTextBlock& b)
    {
        const QTextBlockFormat bf = b.blockFormat();
        // <pre> blocks are non-breaking in Qt's import.
        const bool code = bf.nonBreakableLines() || bf.hasProperty(QTextFormat::BlockCodeFence);
        if (code) {
            if (!inFence_) {
                const QString lang = bf.stringProperty(QTextFormat::BlockCodeLanguage);
                emit(u"```"_s + lang, Kind::Block);
                inFence_ = true;
            }
            QString t = b.text();
            t.replace(QChar::LineSeparator, u'\n');
            emit(t, Kind::Code);
            return;
        }
        closeCode();
        if (bf.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
            emit(u"---"_s, Kind::Block);
            return;
        }
        const int level = bf.headingLevel();
        QString text = inlineText(b, level > 0, false);
        if (text.trimmed().isEmpty() && b.textList() == nullptr) return;
        if (level > 0) {
            emit(QString(level, u'#') + u' ' + text.trimmed(), Kind::Block);
            return;
        }
        if (QTextList* list = b.textList()) {
            const QTextListFormat lf = list->format();
            const int depth = std::max(0, lf.indent() - 1);
            QString marker;
            switch (lf.style()) {
            case QTextListFormat::ListDecimal:
                marker = QString::number(list->itemNumber(b) + 1) + u". "_s;
                break;
            default:
                marker = u"- "_s;
                break;
            }
            if (bf.marker() == QTextBlockFormat::MarkerType::Checked) marker += u"[x] "_s;
            if (bf.marker() == QTextBlockFormat::MarkerType::Unchecked) marker += u"[ ] "_s;
            text = text.trimmed();
            text.replace(u'\n', u'\n' + QString(depth + 1, u'\t'));
            emit(QString(depth, u'\t') + marker + text, Kind::ListItem, list);
            return;
        }
        const int quote = bf.property(QTextFormat::BlockQuoteLevel).toInt();
        text = text.trimmed();
        if (quote > 0) {
            const QString prefix = QString(quote, u'>') + u' ';
            text = prefix + QString(text).replace(u'\n', u'\n' + prefix);
        }
        emit(text, Kind::Block);
    }

    QString out_;
    Kind last_ = Kind::Block;
    const QTextList* list_ = nullptr; // the top-level list being written
    bool inFence_ = false;
};

} // namespace

bool htmlHasFormatting(const QString& html)
{
    static const QRegularExpression tags(
        uR"(<(a\s[^>]*href|b|strong|i|em|u|s|del|strike|h[1-6]|ul|ol|li|table|img|blockquote|hr|code)[\s>/])"_s,
        QRegularExpression::CaseInsensitiveOption);
    return tags.match(html).hasMatch();
}

QString htmlToMarkdown(const QString& html)
{
    QTextDocument doc;
    doc.setHtml(html);
    Writer w;
    w.frame(doc.rootFrame());
    return w.result();
}

} // namespace mn::ui
