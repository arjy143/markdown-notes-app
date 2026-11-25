#include "ReadingView.h"

#include "HtmlRenderer.h"
#include "MathRenderer.h"
#include "core/index/VaultService.h"
#include "core/parse/NoteMetadata.h"
#include "core/vault/Paths.h"

#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFile>
#include <QImageReader>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace mn::ui {

ReadingView::ReadingView(QWidget* parent) : QTextBrowser(parent)
{
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    connect(this, &QTextBrowser::anchorClicked, this, &ReadingView::onAnchor);
}

void ReadingView::showNote(const QString& path, const QByteArray& raw)
{
    if (path != path_) toggled_.clear();
    path_ = path;
    raw_ = raw;
    rerender();
}

QTextDocument* ReadingView::renderDocument(const QString& path, const QByteArray& raw, const Theme& theme,
                                           int maxImageWidth, qreal dpr, QObject* parent)
{
    auto* doc = new QTextDocument(parent);
    doc->setDefaultFont(font());
    doc->setDocumentMargin(16);
    const QString mono =
        monoFamily_.isEmpty() ? QString() : u"pre, code { font-family: '%1'; }"_s.arg(monoFamily_);
    doc->setDefaultStyleSheet(
        mono +
        u"a { color: %1; text-decoration: none; } h1, h2, h3 { margin-top: 14px; }"_s.arg(theme.link.name()));

    RenderContext ctx;
    ctx.files = vault_ ? &vault_->files() : nullptr;
    ctx.notePath = path;
    ctx.theme = theme;
    ctx.fontPointSize = font().pointSizeF();
    ctx.maxImageWidth = maxImageWidth;
    ctx.strictLineBreaks = vault_ != nullptr && vault_->settings().strictLineBreaks;
    if (path == path_) ctx.toggledCallouts = toggled_;
    ctx.imageHtml = [this, doc, maxImageWidth, dpr](const QString& p, int w, int h) {
        return imageHtml(doc, p, w, h, maxImageWidth, dpr);
    };
    const QColor colour = theme.text;
    ctx.mathHtml = [this, doc, colour, dpr](const QString& latex, bool display) {
        return mathHtml(doc, latex, display, colour, dpr);
    };
    ctx.loadNote = [this](const QString& p) -> std::optional<QByteArray> {
        if (vault_ == nullptr) return std::nullopt;
        QFile f(vault_->absolutePath(p));
        if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
        return f.readAll();
    };
    doc->setHtml(renderNote(raw, ctx));
    return doc;
}

void ReadingView::rerender()
{
    QElapsedTimer t;
    t.start();
    const int scroll = verticalScrollBar()->value();
    QTextDocument* doc = renderDocument(path_, raw_, Theme::fromPalette(palette()),
                                        std::max(100, viewport()->width() - 48), devicePixelRatioF(), this);
    // QTextEdit deletes its own default document; documents we created are ours to free.
    QTextDocument* previous = ownDoc_;
    setDocument(doc);
    ownDoc_ = doc;
    if (previous != nullptr) previous->deleteLater();
    verticalScrollBar()->setValue(scroll);
    renderedWidth_ = viewport()->width();
    lastRenderMs_ = t.elapsed();
}

void ReadingView::setMonoFamily(const QString& family)
{
    monoFamily_ = family;
}

void ReadingView::setMaxLineChars(int chars)
{
    maxLineChars_ = chars;
    updateLineMargins();
}

void ReadingView::updateLineMargins()
{
    int side = 0;
    if (maxLineChars_ > 0) {
        const int column = fontMetrics().horizontalAdvance(u'n') * maxLineChars_ + 32;
        const int available = width() - 2 * frameWidth() - verticalScrollBar()->sizeHint().width();
        side = std::max(0, (available - column) / 2);
    }
    setViewportMargins(side, 0, side, 0);
}

void ReadingView::scrollToHeading(const QString& subpath)
{
    if (subpath.isEmpty()) return;
    if (subpath.startsWith(u'^')) {
        scrollToAnchor(subpath);
        return;
    }
    // Find the heading text in the rendered document.
    QStringList texts;
    std::vector<QTextBlock> blocks;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().headingLevel() > 0) {
            texts.append(b.text());
            blocks.push_back(b);
        }
    }
    const int idx = findHeading(texts, subpath);
    if (idx < 0) return;
    QTextCursor c(blocks[static_cast<std::size_t>(idx)]);
    setTextCursor(c);
    const QRect r = cursorRect(c);
    verticalScrollBar()->setValue(verticalScrollBar()->value() + r.top());
}

QString ReadingView::imageHtml(QTextDocument* doc, const QString& path, int w, int h, int maxW, qreal dpr)
{
    if (vault_ == nullptr) return {};
    QImageReader reader(vault_->absolutePath(path));
    reader.setAutoTransform(true);
    const QSize natural = reader.size();
    if (!natural.isValid()) return {};
    int lw = w > 0 ? w : natural.width();
    int lh = h > 0 ? h
                   : (w > 0 ? qRound(static_cast<double>(natural.height()) * w / natural.width())
                            : natural.height());
    if (lw > maxW) {
        lh = qRound(static_cast<double>(lh) * maxW / lw);
        lw = maxW;
    }
    // Decode only at the displayed size, so large images don't sit in memory at full resolution.
    const QSize decode(std::max(1, qRound(lw * dpr)), std::max(1, qRound(lh * dpr)));
    if (decode.width() < natural.width()) reader.setScaledSize(decode);
    const QImage img = reader.read();
    if (img.isNull()) return {};
    const QUrl url(u"mn-img://%1"_s.arg(++resourceCounter_));
    doc->addResource(QTextDocument::ImageResource, url, img);
    return u"<img src=\"%1\" width=\"%2\" height=\"%3\"/>"_s.arg(url.toString()).arg(lw).arg(lh);
}

QString ReadingView::mathHtml(QTextDocument* doc, const QString& latex, bool display, const QColor& colour,
                              qreal dpr)
{
    const qreal size = font().pointSizeF() * (display ? 1.15 : 1.0);
    QString err;
    const QImage img = MathRenderer::instance().render(latex, display, size, dpr, colour, &err);
    if (img.isNull()) return {};
    const QUrl url(u"mn-math://%1"_s.arg(++resourceCounter_));
    doc->addResource(QTextDocument::ImageResource, url, img);
    const int lw = qRound(img.width() / dpr);
    const int lh = qRound(img.height() / dpr);
    return u"<img src=\"%1\" width=\"%2\" height=\"%3\" style=\"vertical-align: middle\"/>"_s
        .arg(url.toString())
        .arg(lw)
        .arg(lh);
}

void ReadingView::onAnchor(const QUrl& url)
{
    const QString scheme = url.scheme();
    const QString payload =
        QUrl::fromPercentEncoding(url.toString(QUrl::FullyEncoded).section(u':', 1).toUtf8());
    if (scheme == u"mn-link") {
        Q_EMIT linkActivated(payload, path_);
    } else if (scheme == u"mn-tag") {
        Q_EMIT tagActivated(payload);
    } else if (scheme == u"mn-open") {
        Q_EMIT openFileRequested(payload);
    } else if (scheme == u"mn-callout") {
        const int idx = payload.toInt();
        if (toggled_.contains(idx))
            toggled_.remove(idx);
        else
            toggled_.insert(idx);
        rerender();
    } else if (url.toString().startsWith(u'#')) {
        scrollToAnchor(url.fragment());
    } else if (!scheme.isEmpty()) {
        QDesktopServices::openUrl(url);
    }
}

void ReadingView::changeEvent(QEvent* e)
{
    QTextBrowser::changeEvent(e);
    if (e->type() == QEvent::FontChange) updateLineMargins();
    if ((e->type() == QEvent::PaletteChange || e->type() == QEvent::FontChange) && !path_.isEmpty())
        rerender();
}

void ReadingView::resizeEvent(QResizeEvent* e)
{
    QTextBrowser::resizeEvent(e);
    updateLineMargins();
    // Images are sized to the view: re-render after a significant width change.
    if (!path_.isEmpty() && isVisible() && std::abs(viewport()->width() - renderedWidth_) > 80) rerender();
}

} // namespace mn::ui
