#include "Drawing.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

constexpr qreal kMargin = 8;
constexpr qreal kMaxZoom = 4;
constexpr int kHighlighterAlpha = 110;
constexpr qreal kHighlighterFactor = 4;

void paintSketch(QPainter& p, const Sketch& s)
{
    const QRectF area(QPointF(0, 0), QSizeF(s.size));
    p.fillRect(area, Qt::white);
    if (!s.background.isNull()) p.drawImage(area, s.background);
    for (const Stroke& st : s.strokes)
        Sketch::paintStroke(p, st);
}

int toolIndex(Stroke::Tool t)
{
    return static_cast<int>(t);
}

} // namespace

// --- Sketch ------------------------------------------------------------------------

Sketch Sketch::blank(QSize size)
{
    Sketch s;
    s.size = size;
    return s;
}

Sketch Sketch::over(const QImage& image)
{
    Sketch s;
    s.background = image;
    s.size = image.size();
    return s;
}

QImage Sketch::render(qreal scale) const
{
    const QSize px(std::max(1, qRound(size.width() * scale)), std::max(1, qRound(size.height() * scale)));
    QImage img(px, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    p.scale(qreal(px.width()) / size.width(), qreal(px.height()) / size.height());
    paintSketch(p, *this);
    return img;
}

void Sketch::paintStroke(QPainter& p, const Stroke& s)
{
    if (s.points.isEmpty()) return;
    p.save();
    p.setBrush(Qt::NoBrush);
    if (s.tool == Stroke::Tool::Rectangle) {
        p.setPen(QPen(s.color, s.width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.drawRect(QRectF(s.points.first(), s.points.last()).normalized());
        p.restore();
        return;
    }
    p.setPen(QPen(s.color, s.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (s.points.size() == 1) {
        p.drawPoint(s.points.first());
        p.restore();
        return;
    }
    // Smoothed through the midpoints, and drawn as one path so a highlighter
    // crossing itself doesn't get darker.
    QPainterPath path(s.points.first());
    for (qsizetype i = 1; i + 1 < s.points.size(); ++i)
        path.quadTo(s.points[i], (s.points[i] + s.points[i + 1]) / 2);
    path.lineTo(s.points.last());
    p.drawPath(path);
    p.restore();
}

// --- DrawingCanvas -----------------------------------------------------------------

DrawingCanvas::DrawingCanvas(Sketch sketch, QWidget* parent) : QWidget(parent), sketch_(std::move(sketch))
{
    if (sketch_.size.isEmpty()) sketch_.size = QSize(1600, 1000);
    for (qsizetype i = 0; i < sketch_.strokes.size(); ++i)
        undo_.append({sketch_.strokes.first(i), -1 - static_cast<int>(i)});
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::CrossCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize DrawingCanvas::sizeHint() const
{
    return {800, 500};
}

QRectF DrawingCanvas::viewRect() const
{
    const QRectF avail = QRectF(rect()).adjusted(kMargin, kMargin, -kMargin, -kMargin);
    if (avail.width() <= 0 || avail.height() <= 0) return {};
    const qreal scale =
        std::min({avail.width() / sketch_.size.width(), avail.height() / sketch_.size.height(), kMaxZoom});
    QRectF v(QPointF(), QSizeF(sketch_.size) * scale);
    v.moveCenter(avail.center());
    return v;
}

QPointF DrawingCanvas::toImage(QPointF widgetPos) const
{
    const QRectF v = viewRect();
    if (v.isEmpty()) return {};
    return (widgetPos - v.topLeft()) * (sketch_.size.width() / v.width());
}

void DrawingCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Mid));
    const QRectF v = viewRect();
    if (v.isEmpty()) return;
    const qreal dpr = devicePixelRatioF();
    const QSize px = (v.size() * dpr).toSize();
    if (cache_.size() != px) {
        cache_ = QPixmap(px);
        cache_.setDevicePixelRatio(dpr);
        QPainter cp(&cache_);
        cp.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        cp.scale(v.width() / sketch_.size.width(), v.height() / sketch_.size.height());
        paintSketch(cp, sketch_);
    }
    p.drawPixmap(v.topLeft(), cache_);
    if (drawing_) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setClipRect(v);
        p.translate(v.topLeft());
        p.scale(v.width() / sketch_.size.width(), v.height() / sketch_.size.height());
        Sketch::paintStroke(p, current_);
    }
}

void DrawingCanvas::resizeEvent(QResizeEvent* e)
{
    cache_ = QPixmap();
    QWidget::resizeEvent(e);
}

void DrawingCanvas::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || viewRect().isEmpty()) return;
    const qreal toImagePx = sketch_.size.width() / viewRect().width();
    current_ = Stroke{};
    current_.tool = tool_;
    current_.color = color_;
    current_.width = width_ * toImagePx;
    if (tool_ == Stroke::Tool::Highlighter) {
        current_.color.setAlpha(kHighlighterAlpha);
        current_.width *= kHighlighterFactor;
    }
    current_.points.append(toImage(e->position()));
    drawing_ = true;
    update();
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (!drawing_) return;
    const QPointF pt = toImage(e->position());
    if (current_.tool == Stroke::Tool::Rectangle) {
        if (current_.points.size() < 2)
            current_.points.append(pt);
        else
            current_.points.last() = pt;
    } else {
        // Skip points closer than half a screen pixel to the previous one.
        const qreal minStep = sketch_.size.width() / viewRect().width() / 2;
        const QPointF d = pt - current_.points.last();
        if (std::hypot(d.x(), d.y()) < minStep) return;
        current_.points.append(pt);
    }
    update();
}

void DrawingCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (!drawing_ || e->button() != Qt::LeftButton) return;
    drawing_ = false;
    const bool emptyRect = current_.tool == Stroke::Tool::Rectangle &&
                           (current_.points.size() < 2 || current_.points.first() == current_.points.last());
    if (emptyRect) {
        update();
        return;
    }
    QList<Stroke> next = sketch_.strokes;
    next.append(current_);
    commit(std::move(next));
}

void DrawingCanvas::commit(QList<Stroke> strokes)
{
    undo_.append({sketch_.strokes, state_});
    redo_.clear();
    sketch_.strokes = std::move(strokes);
    state_ = nextState_++;
    cache_ = QPixmap();
    update();
    Q_EMIT changed();
}

void DrawingCanvas::undo()
{
    if (undo_.isEmpty()) return;
    redo_.append({sketch_.strokes, state_});
    State s = undo_.takeLast();
    sketch_.strokes = std::move(s.strokes);
    state_ = s.id;
    cache_ = QPixmap();
    update();
    Q_EMIT changed();
}

void DrawingCanvas::redo()
{
    if (redo_.isEmpty()) return;
    undo_.append({sketch_.strokes, state_});
    State s = redo_.takeLast();
    sketch_.strokes = std::move(s.strokes);
    state_ = s.id;
    cache_ = QPixmap();
    update();
    Q_EMIT changed();
}

void DrawingCanvas::clear()
{
    if (sketch_.strokes.isEmpty()) return;
    commit({}); // undoable, like a stroke
}

// --- DrawingDialog -----------------------------------------------------------------

DrawingDialog::DrawingDialog(const Sketch& sketch, QWidget* parent)
    : QDialog(parent), palette_{QColor(0x1c, 0x1c, 0x1c), QColor(0xd9, 0x30, 0x25), QColor(0x1a, 0x73, 0xe8),
                                QColor(0x18, 0x80, 0x38), QColor(0xfb, 0xbc, 0x04)}
{
    setWindowTitle(sketch.background.isNull() ? tr("Drawing") : tr("Annotate image"));
    toolColor_[toolIndex(Stroke::Tool::Pen)] = palette_[0];
    toolColor_[toolIndex(Stroke::Tool::Highlighter)] = palette_[4];
    toolColor_[toolIndex(Stroke::Tool::Rectangle)] = palette_[1];

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    auto* bar = new QHBoxLayout;
    lay->addLayout(bar);

    auto* tools = new QButtonGroup(this);
    auto addTool = [&](const QString& text, const QString& key, Stroke::Tool t) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setCheckable(true);
        b->setShortcut(QKeySequence(key));
        b->setToolTip(tr("%1 (%2)").arg(text, key));
        tools->addButton(b, toolIndex(t));
        bar->addWidget(b);
    };
    addTool(tr("Pen"), u"P"_s, Stroke::Tool::Pen);
    addTool(tr("Highlighter"), u"H"_s, Stroke::Tool::Highlighter);
    addTool(tr("Rectangle"), u"R"_s, Stroke::Tool::Rectangle);
    connect(tools, &QButtonGroup::idClicked, this,
            [this](int id) { selectTool(static_cast<Stroke::Tool>(id)); });
    bar->addSpacing(12);

    colors_ = new QButtonGroup(this);
    const QStringList names{tr("Black"), tr("Red"), tr("Blue"), tr("Green"), tr("Yellow")};
    for (qsizetype i = 0; i < palette_.size(); ++i) {
        auto* b = new QToolButton(this);
        QPixmap swatch(16, 16);
        swatch.fill(Qt::transparent);
        {
            QPainter sp(&swatch);
            sp.setRenderHint(QPainter::Antialiasing);
            sp.setPen(QPen(palette().color(QPalette::WindowText), 1));
            sp.setBrush(palette_[i]);
            sp.drawEllipse(QRectF(1.5, 1.5, 13, 13));
        }
        b->setIcon(swatch);
        b->setCheckable(true);
        b->setToolTip(names.value(i));
        colors_->addButton(b, static_cast<int>(i));
        bar->addWidget(b);
    }
    connect(colors_, &QButtonGroup::idClicked, this, [this](int id) { selectColor(palette_.value(id)); });
    bar->addSpacing(12);

    auto* width = new QComboBox(this);
    width->addItem(tr("Thin"), 2.0);
    width->addItem(tr("Medium"), 4.0);
    width->addItem(tr("Thick"), 8.0);
    width->setToolTip(tr("Line width (1, 2, 3)"));
    bar->addWidget(width);
    for (int i = 0; i < 3; ++i) {
        auto* sc = new QShortcut(QKeySequence(QString::number(i + 1)), this);
        connect(sc, &QShortcut::activated, width, [width, i] { width->setCurrentIndex(i); });
    }
    bar->addStretch(1);

    auto addAction = [&](const QString& text, const QKeySequence& key) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setShortcut(key);
        b->setToolTip(key.isEmpty() ? text : tr("%1 (%2)").arg(text, key.toString(QKeySequence::NativeText)));
        bar->addWidget(b);
        return b;
    };
    undo_ = addAction(tr("Undo"), QKeySequence::Undo);
    redo_ = addAction(tr("Redo"), QKeySequence::Redo);
    clear_ = addAction(tr("Clear"), {});
    auto* redoAlt = new QShortcut(QKeySequence(u"Ctrl+Y"_s), this);

    canvas_ = new DrawingCanvas(sketch, this);
    lay->addWidget(canvas_, 1);
    connect(undo_, &QToolButton::clicked, canvas_, &DrawingCanvas::undo);
    connect(redo_, &QToolButton::clicked, canvas_, &DrawingCanvas::redo);
    connect(redoAlt, &QShortcut::activated, canvas_, &DrawingCanvas::redo);
    connect(clear_, &QToolButton::clicked, canvas_, &DrawingCanvas::clear);
    connect(canvas_, &DrawingCanvas::changed, this, &DrawingDialog::updateActions);
    connect(width, &QComboBox::currentIndexChanged, this,
            [this, width] { canvas_->setPenWidth(width->currentData().toReal()); });
    width->setCurrentIndex(1);
    canvas_->setPenWidth(width->currentData().toReal());

    auto* bottom = new QHBoxLayout;
    auto* hint = new QLabel(tr("Ctrl+↵ done   esc cancel"), this);
    hint->setEnabled(false);
    bottom->addWidget(hint, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Done"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bottom->addWidget(buttons);
    lay->addLayout(bottom);
    for (const char* key : {"Ctrl+Return", "Ctrl+Enter"}) {
        auto* sc = new QShortcut(QKeySequence(QString::fromLatin1(key)), this);
        connect(sc, &QShortcut::activated, this, &QDialog::accept);
    }

    tools->button(toolIndex(Stroke::Tool::Pen))->setChecked(true);
    selectTool(Stroke::Tool::Pen);
    updateActions();

    // Big enough to show the image at full size where the screen allows.
    QSize want(1000, 700);
    if (!sketch.background.isNull()) want = sketch.size + QSize(40, 110);
    const QScreen* screen = parent != nullptr ? parent->screen() : QGuiApplication::primaryScreen();
    if (screen != nullptr) {
        const QSize avail = screen->availableSize() * 0.85;
        want = want.boundedTo(avail).expandedTo(QSize(640, 420));
    }
    resize(want);
}

Sketch DrawingDialog::sketch() const
{
    return canvas_->sketch();
}

void DrawingDialog::selectTool(Stroke::Tool t)
{
    canvas_->setTool(t);
    selectColor(toolColor_[toolIndex(t)]);
}

void DrawingDialog::selectColor(const QColor& c)
{
    toolColor_[toolIndex(canvas_->tool())] = c;
    canvas_->setColor(c);
    const auto i = palette_.indexOf(c);
    if (QAbstractButton* b = colors_->button(static_cast<int>(i))) b->setChecked(true);
}

void DrawingDialog::updateActions()
{
    undo_->setEnabled(canvas_->canUndo());
    redo_->setEnabled(canvas_->canRedo());
    clear_->setEnabled(!canvas_->sketch().strokes.isEmpty());
}

void DrawingDialog::reject()
{
    if (canvas_->isModified()) {
        const auto r = QMessageBox::question(this, windowTitle(), tr("Discard changes?"),
                                             QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (r != QMessageBox::Discard) return;
    }
    QDialog::reject();
}

} // namespace mn::ui
