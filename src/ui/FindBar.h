#pragma once

#include <QWidget>

class QLineEdit;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QWidget;

namespace mn::ui {

// Find / replace within the open note (Ctrl+F, Ctrl+H).
class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(QPlainTextEdit* editor, QWidget* parent = nullptr);
    void showFind(bool withReplace);

public Q_SLOTS:
    void findNext();
    void findPrevious();
    void replaceOne();
    void replaceAll();

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    bool find(bool backward);
    QPlainTextEdit* editor_;
    QLineEdit* findEdit_;
    QLineEdit* replaceEdit_;
    QWidget* replaceRow_;
    QCheckBox* caseBox_;
    QLabel* status_;
};

} // namespace mn::ui
