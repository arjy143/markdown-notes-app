#include "CaptureWindow.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

CaptureWindow::CaptureWindow(const QString& target, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Quick capture"));
    setWindowFlag(Qt::WindowStaysOnTopHint);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 8);
    edit_ = new QPlainTextEdit(this);
    edit_->setPlaceholderText(tr("Text to capture"));
    edit_->setTabChangesFocus(true);
    edit_->setFixedHeight(edit_->fontMetrics().lineSpacing() * 3 + 16);
    edit_->installEventFilter(this);
    hint_ = new QLabel(tr("↵ save to %1   ⇧↵ new line   esc cancel").arg(target), this);
    hint_->setEnabled(false);
    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->setStyleSheet(u"color: #c0392b"_s);
    error_->hide();
    lay->addWidget(edit_);
    lay->addWidget(error_);
    lay->addWidget(hint_);
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

bool CaptureWindow::eventFilter(QObject* o, QEvent* e)
{
    if (o == edit_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)) {
            if (k->modifiers() & Qt::ShiftModifier) {
                edit_->textCursor().insertText(u"\n"_s);
            } else if (!text().trimmed().isEmpty()) {
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
