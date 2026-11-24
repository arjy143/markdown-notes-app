#pragma once

#include "Drawing.h"
#include "core/capture/Capture.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QMimeData;
class QPlainTextEdit;

namespace mn::ui {

// The quick-capture box: type, press Enter, done. Shift+Enter adds a line,
// Esc cancels. Pasted or dropped images and drawings (Ctrl+D) are listed under
// the text; double-click one to draw over it. The owner does the write (and
// calls showError on failure, which keeps everything so nothing is lost).
class CaptureWindow : public QDialog {
    Q_OBJECT
public:
    explicit CaptureWindow(const QString& target, QWidget* parent = nullptr);
    QString text() const;
    void showError(const QString& message);
    QPlainTextEdit* editor() const { return edit_; }

    // The images and drawings, encoded, named as of `now`. An image that wasn't
    // drawn on keeps its original bytes when it came from a file.
    QList<CaptureFile> files(const QDateTime& now) const;
    qsizetype imageCount() const { return items_.size(); }
    const Sketch& image(qsizetype i) const { return items_[i].sketch; }
    QListWidget* strip() const { return strip_; }

    void addImage(const QImage& image, const QString& fileName = {}, const QByteArray& original = {});
    void removeImage(qsizetype i);
    void draw();                // a new blank drawing
    void annotate(qsizetype i); // draw over image i
    void reject() override;     // asks first when images would be lost

Q_SIGNALS:
    void submitted(const QString& text);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    struct Item {
        QString name;         // base name; empty for "Pasted image <time>"
        bool drawing = false; // "Drawing <time>"
        QString suffix;       // of the original file
        QByteArray original;  // a dropped file's bytes, saved as-is unless drawn on
        Sketch sketch;
    };
    bool insertMime(const QMimeData* mime);
    bool editSketch(Sketch& sketch);
    void refreshStrip();

    QPlainTextEdit* edit_;
    QListWidget* strip_;
    QLabel* hint_;
    QLabel* error_;
    QList<Item> items_;
};

} // namespace mn::ui
