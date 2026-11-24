#pragma once

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QList>
#include <QPixmap>
#include <QPolygonF>
#include <QWidget>

class QButtonGroup;
class QPainter;
class QToolButton;

namespace mn::ui {

// One pen, highlighter or rectangle stroke, in image pixels.
struct Stroke {
    enum class Tool : unsigned char { Pen, Highlighter, Rectangle };
    Tool tool = Tool::Pen;
    QColor color;
    qreal width = 4;
    QPolygonF points; // a rectangle uses the first and last point
};

// A drawing: strokes over a background image (or over white when there is none).
// The strokes stay separate until the drawing is saved, so it can be reopened
// and changed before then.
struct Sketch {
    QImage background; // null for a blank drawing
    QSize size;        // in pixels
    QList<Stroke> strokes;

    static Sketch blank(QSize size = {1600, 1000});
    static Sketch over(const QImage& image);
    // The drawing as an opaque image at `scale` times its size.
    QImage render(qreal scale = 1.0) const;
    static void paintStroke(QPainter& p, const Stroke& s);
};

// The surface you draw on: the sketch scaled to fit, with undo and redo.
class DrawingCanvas : public QWidget {
    Q_OBJECT
public:
    explicit DrawingCanvas(Sketch sketch, QWidget* parent = nullptr);
    const Sketch& sketch() const { return sketch_; }
    bool isModified() const { return state_ != 0; } // changed since it was opened

    void setTool(Stroke::Tool t) { tool_ = t; }
    Stroke::Tool tool() const { return tool_; }
    void setColor(const QColor& c) { color_ = c; }
    QColor color() const { return color_; }
    void setPenWidth(qreal screenPixels) { width_ = screenPixels; }

    bool canUndo() const { return !undo_.isEmpty(); }
    bool canRedo() const { return !redo_.isEmpty(); }
    void undo();
    void redo();
    void clear();

    QRectF viewRect() const; // where the sketch is drawn, in widget pixels
    QSize sizeHint() const override;

Q_SIGNALS:
    void changed();

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QPointF toImage(QPointF widgetPos) const;
    void commit(QList<Stroke> strokes);

    // Undo and redo hold earlier stroke lists; a reopened drawing can undo its
    // strokes one at a time. Each state has an id; 0 is the one it opened with.
    struct State {
        QList<Stroke> strokes;
        int id = 0;
    };
    Sketch sketch_;
    QList<State> undo_;
    QList<State> redo_;
    int state_ = 0;
    int nextState_ = 1;
    Stroke current_;
    bool drawing_ = false;
    Stroke::Tool tool_ = Stroke::Tool::Pen;
    QColor color_{0x1c, 0x1c, 0x1c};
    qreal width_ = 3;
    // The background and finished strokes at view size, so a stroke in progress
    // doesn't rescale a large screenshot on every mouse move.
    QPixmap cache_;
};

// Draw, or draw over an image: pen, highlighter and rectangle, a few colours and
// widths, undo, redo and clear. Done keeps the strokes; Cancel drops them.
class DrawingDialog : public QDialog {
    Q_OBJECT
public:
    explicit DrawingDialog(const Sketch& sketch, QWidget* parent = nullptr);
    Sketch sketch() const;
    DrawingCanvas* canvas() const { return canvas_; }
    void reject() override;

private:
    void selectTool(Stroke::Tool t);
    void selectColor(const QColor& c);
    void updateActions();

    DrawingCanvas* canvas_;
    QButtonGroup* colors_;
    QList<QColor> palette_;
    QColor toolColor_[3];
    QToolButton* undo_;
    QToolButton* redo_;
    QToolButton* clear_;
};

} // namespace mn::ui
