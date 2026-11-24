#pragma once

#include "core/index/IndexTypes.h"

#include <QWidget>

#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;
class QTreeWidget;

namespace mn::ai {
class Assistant;
}

namespace mn::ui {

// "Ask" tab: a question about the vault, answered by the local model from the
// passages that match it (by keywords and, with a semantic index, by meaning).
class AiAskPanel : public QWidget {
    Q_OBJECT
public:
    explicit AiAskPanel(ai::Assistant* assistant, QWidget* parent = nullptr);
    void focusQuestion();

Q_SIGNALS:
    void openRequested(const QString& path, int line, const QString& findText);

private:
    void ask();
    void stop();
    ai::Assistant* assistant_;
    QLineEdit* question_;
    QPushButton* askButton_;
    QLabel* status_;
    QTextBrowser* answer_;
    QTreeWidget* sources_;
    std::vector<Passage> shown_;
    int request_ = 0;
};

} // namespace mn::ui
