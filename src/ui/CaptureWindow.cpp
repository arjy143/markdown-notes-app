#include "CaptureWindow.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

constexpr int kThumbHeight = 64;
constexpr int kThumbWidth = 96;

// Local image files in a paste or drop.
QStringList imageFiles(const QMimeData* mime)
{
    QStringList out;
    if (!mime->hasUrls()) return out;
    for (const QUrl& u : mime->urls()) {
        if (!u.isLocalFile()) continue;
        const QString f = u.toLocalFile();
        if (QFileInfo(f).isFile() && !QImageReader::imageFormat(f).isEmpty()) out.append(f);
    }
    return out;
}

// A text box that hands images to the capture window instead of inserting them.
class CaptureEdit : public QPlainTextEdit {
public:
    std::function<bool(const QMimeData*)> onMime;
    using QPlainTextEdit::QPlainTextEdit;

protected:
    bool canInsertFromMimeData(const QMimeData* m) const override
    {
        return m->hasImage() || !imageFiles(m).isEmpty() || QPlainTextEdit::canInsertFromMimeData(m);
    }
    void insertFromMimeData(const QMimeData* m) override
    {
        if (onMime && onMime(m)) return;
        QPlainTextEdit::insertFromMimeData(m);
    }
};

} // namespace

CaptureWindow::CaptureWindow(const QString& target, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Quick capture"));
    setWindowFlag(Qt::WindowStaysOnTopHint);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 8);
    auto* edit = new CaptureEdit(this);
    edit->onMime = [this](const QMimeData* m) { return insertMime(m); };
    edit_ = edit;
    edit_->setPlaceholderText(tr("Text to capture, or paste an image"));
    edit_->setTabChangesFocus(true);
    edit_->setFixedHeight(edit_->fontMetrics().lineSpacing() * 3 + 16);
    edit_->installEventFilter(this);

    strip_ = new QListWidget(this);
    strip_->setViewMode(QListView::IconMode);
    strip_->setFlow(QListView::LeftToRight);
    strip_->setWrapping(false);
    strip_->setMovement(QListView::Static);
    strip_->setIconSize(QSize(kThumbWidth, kThumbHeight));
    strip_->setFixedHeight(kThumbHeight + 24);
    strip_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    strip_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    strip_->setContextMenuPolicy(Qt::CustomContextMenu);
    strip_->installEventFilter(this);
    strip_->hide();
    connect(strip_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* it) { annotate(strip_->row(it)); });
    connect(strip_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int row = strip_->row(strip_->itemAt(pos));
        if (row < 0) return;
        QMenu menu(this);
        menu.addAction(tr("Draw on image"), this, [this, row] { annotate(row); });
        menu.addAction(tr("Remove"), this, [this, row] { removeImage(row); });
        menu.exec(strip_->viewport()->mapToGlobal(pos));
    });

    hint_ = new QLabel(tr("↵ save to %1   ⇧↵ new line   ctrl+D draw   esc cancel").arg(target), this);
    hint_->setEnabled(false);
    auto* drawButton = new QToolButton(this);
    drawButton->setText(tr("Draw"));
    drawButton->setToolTip(tr("New drawing (Ctrl+D)"));
    drawButton->setAutoRaise(true);
    connect(drawButton, &QToolButton::clicked, this, &CaptureWindow::draw);
    auto* drawKey = new QShortcut(QKeySequence(u"Ctrl+D"_s), this);
    connect(drawKey, &QShortcut::activated, this, &CaptureWindow::draw);

    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->setStyleSheet(u"color: #c0392b"_s);
    error_->hide();
    lay->addWidget(edit_);
    lay->addWidget(strip_);
    lay->addWidget(error_);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(hint_, 1);
    bottom->addWidget(drawButton);
    lay->addLayout(bottom);
    resize(560, sizeHint().height());
}

QString CaptureWindow::text() const
{
    return edit_->toPlainText();
}

void CaptureWindow::showError(const QString& message)
{
    error_->setText(message);
    error_->show();
    edit_->setFocus();
}

bool CaptureWindow::insertMime(const QMimeData* mime)
{
    // Files keep their own name and bytes; a bare image (a screenshot) is a PNG.
    const QStringList files = imageFiles(mime);
    bool added = false;
    for (const QString& f : files) {
        QFile in(f);
        const QImage img(f);
        if (img.isNull() || !in.open(QIODevice::ReadOnly)) continue;
        addImage(img, QFileInfo(f).fileName(), in.readAll());
        added = true;
    }
    if (added) return true;
    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (img.isNull()) return false;
        addImage(img);
        return true;
    }
    return false;
}

void CaptureWindow::addImage(const QImage& image, const QString& fileName, const QByteArray& original)
{
    Item it;
    if (!fileName.isEmpty()) {
        const QFileInfo fi(fileName);
        it.name = fi.completeBaseName();
        it.suffix = fi.suffix();
        it.original = original;
    }
    it.sketch = Sketch::over(image);
    items_.append(std::move(it));
    refreshStrip();
}

void CaptureWindow::removeImage(qsizetype i)
{
    if (i < 0 || i >= items_.size()) return;
    items_.removeAt(i);
    refreshStrip();
}

bool CaptureWindow::editSketch(Sketch& sketch)
{
    DrawingDialog dlg(sketch, this);
    if (dlg.exec() != QDialog::Accepted) return false;
    sketch = dlg.sketch();
    return true;
}

void CaptureWindow::draw()
{
    Sketch s = Sketch::blank();
    if (!editSketch(s) || s.strokes.isEmpty()) return;
    Item it;
    it.drawing = true;
    it.sketch = std::move(s);
    items_.append(std::move(it));
    refreshStrip();
}

void CaptureWindow::annotate(qsizetype i)
{
    if (i < 0 || i >= items_.size()) return;
    Sketch s = items_[i].sketch;
    if (!editSketch(s)) return;
    items_[i].sketch = std::move(s);
    refreshStrip();
}

void CaptureWindow::refreshStrip()
{
    const int current = strip_->currentRow();
    strip_->clear();
    const qreal dpr = devicePixelRatioF();
    for (const Item& it : items_) {
        const QSize sz = it.sketch.size;
        const qreal scale =
            std::min({qreal(kThumbWidth) / sz.width(), qreal(kThumbHeight) / sz.height(), 1.0}) * dpr;
        QPixmap thumb = QPixmap::fromImage(it.sketch.render(scale));
        thumb.setDevicePixelRatio(dpr);
        auto* row = new QListWidgetItem(QIcon(thumb), QString(), strip_);
        row->setToolTip(tr("Double-click to draw on it. Delete removes it."));
    }
    strip_->setCurrentRow(std::min(current, static_cast<int>(items_.size()) - 1));
    const bool show = !items_.isEmpty();
    if (strip_->isVisible() != show) {
        strip_->setVisible(show);
        resize(width(), sizeHint().height());
    }
}

QList<CaptureFile> CaptureWindow::files(const QDateTime& now) const
{
    const QString stamp = now.toString(u"yyyyMMddHHmmss"_s);
    QList<CaptureFile> out;
    for (const Item& it : items_) {
        CaptureFile f;
        f.baseName = !it.name.isEmpty() ? it.name
                     : it.drawing       ? u"Drawing "_s + stamp
                                        : u"Pasted image "_s + stamp;
        if (it.sketch.strokes.isEmpty() && !it.original.isEmpty()) {
            f.suffix = it.suffix;
            f.bytes = it.original;
        } else {
            f.suffix = u"png"_s;
            QBuffer buf(&f.bytes);
            buf.open(QIODevice::WriteOnly);
            it.sketch.render().save(&buf, "PNG");
        }
        out.append(std::move(f));
    }
    return out;
}

void CaptureWindow::reject()
{
    if (!items_.isEmpty()) {
        const auto r = QMessageBox::question(this, windowTitle(), tr("Discard this capture and its images?"),
                                             QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (r != QMessageBox::Discard) return;
    }
    QDialog::reject();
}

bool CaptureWindow::eventFilter(QObject* o, QEvent* e)
{
    if (o == strip_ && e->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(e)->key();
        if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
            removeImage(strip_->currentRow());
            if (items_.isEmpty()) edit_->setFocus();
            return true;
        }
    }
    if (o == edit_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)) {
            if (k->modifiers() & Qt::ShiftModifier) {
                edit_->textCursor().insertText(u"\n"_s);
            } else if (!text().trimmed().isEmpty() || !items_.isEmpty()) {
                Q_EMIT submitted(text());
            }
            return true;
        }
        if (k->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
    }
    return QDialog::eventFilter(o, e);
}

} // namespace mn::ui
