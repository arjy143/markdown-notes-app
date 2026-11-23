#pragma once

#include <QDialog>

class QPlainTextEdit;
class QLabel;

namespace mn::ui {

// The quick-capture box: type, press Enter, done. Shift+Enter adds a line,
// Esc cancels. The owner does the write (and calls showError on failure, which
// keeps the text so nothing typed is lost).
class CaptureWindow : public QDialog {
    Q_OBJECT
public:
    explicit CaptureWindow(const QString& target, QWidget* parent = nullptr);
    QString text() const;
    void showError(const QString& message);
    QPlainTextEdit* editor() const { return edit_; }

Q_SIGNALS:
    void submitted(const QString& text);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QPlainTextEdit* edit_;
    QLabel* hint_;
    QLabel* error_;
};

} // namespace mn::ui
