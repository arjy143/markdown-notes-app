#include "FindBar.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

FindBar::FindBar(QPlainTextEdit* editor, QWidget* parent) : QWidget(parent), editor_(editor)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 4, 6, 4);
    outer->setSpacing(4);

    auto* row = new QHBoxLayout;
    findEdit_ = new QLineEdit(this);
    findEdit_->setPlaceholderText(tr("Find"));
    findEdit_->setClearButtonEnabled(true);
    auto* prev = new QPushButton(tr("Previous"), this);
    auto* next = new QPushButton(tr("Next"), this);
    caseBox_ = new QCheckBox(tr("Match case"), this);
    status_ = new QLabel(this);
    auto* close = new QToolButton(this);
    close->setText(u"✕"_s);
    close->setAutoRaise(true);
    row->addWidget(findEdit_, 1);
    row->addWidget(prev);
    row->addWidget(next);
    row->addWidget(caseBox_);
    row->addWidget(status_);
    row->addWidget(close);
    outer->addLayout(row);

    replaceRow_ = new QWidget(this);
    auto* r2 = new QHBoxLayout(replaceRow_);
    r2->setContentsMargins(0, 0, 0, 0);
    replaceEdit_ = new QLineEdit(replaceRow_);
    replaceEdit_->setPlaceholderText(tr("Replace with"));
    auto* one = new QPushButton(tr("Replace"), replaceRow_);
    auto* all = new QPushButton(tr("Replace all"), replaceRow_);
    r2->addWidget(replaceEdit_, 1);
    r2->addWidget(one);
    r2->addWidget(all);
    outer->addWidget(replaceRow_);

    connect(findEdit_, &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(findEdit_, &QLineEdit::textEdited, this, [this] {
        QTextCursor c = editor_->textCursor();
        c.setPosition(c.selectionStart());
        editor_->setTextCursor(c);
        find(false);
    });
    connect(next, &QPushButton::clicked, this, &FindBar::findNext);
    connect(prev, &QPushButton::clicked, this, &FindBar::findPrevious);
    connect(one, &QPushButton::clicked, this, &FindBar::replaceOne);
    connect(all, &QPushButton::clicked, this, &FindBar::replaceAll);
    connect(replaceEdit_, &QLineEdit::returnPressed, this, &FindBar::replaceOne);
    connect(close, &QToolButton::clicked, this, [this] {
        hide();
        editor_->setFocus();
    });
    hide();
}

void FindBar::showFind(bool withReplace)
{
    replaceRow_->setVisible(withReplace);
    show();
    const QString sel = editor_->textCursor().selectedText();
    if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator)) findEdit_->setText(sel);
    findEdit_->setFocus();
    findEdit_->selectAll();
}

bool FindBar::find(bool backward)
{
    const QString needle = findEdit_->text();
    status_->clear();
    if (needle.isEmpty()) return false;
    QTextDocument::FindFlags flags;
    if (backward) flags |= QTextDocument::FindBackward;
    if (caseBox_->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    if (editor_->find(needle, flags)) return true;
    // Wrap around.
    QTextCursor c = editor_->textCursor();
    c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    editor_->setTextCursor(c);
    if (editor_->find(needle, flags)) return true;
    status_->setText(tr("No matches"));
    return false;
}

void FindBar::findNext()
{
    find(false);
}

void FindBar::findPrevious()
{
    find(true);
}

void FindBar::replaceOne()
{
    if (editor_->isReadOnly()) return;
    QTextCursor c = editor_->textCursor();
    const Qt::CaseSensitivity cs = caseBox_->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (c.hasSelection() && c.selectedText().compare(findEdit_->text(), cs) == 0)
        c.insertText(replaceEdit_->text());
    find(false);
}

void FindBar::replaceAll()
{
    if (editor_->isReadOnly() || findEdit_->text().isEmpty()) return;
    QTextDocument::FindFlags flags;
    if (caseBox_->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    QTextCursor edit(editor_->document());
    edit.beginEditBlock();
    QTextCursor c(editor_->document());
    int count = 0;
    while (true) {
        c = editor_->document()->find(findEdit_->text(), c, flags);
        if (c.isNull()) break;
        c.insertText(replaceEdit_->text());
        ++count;
    }
    edit.endEditBlock();
    status_->setText(tr("%n replaced", nullptr, count));
}

void FindBar::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        hide();
        editor_->setFocus();
        return;
    }
    QWidget::keyPressEvent(e);
}

} // namespace mn::ui
