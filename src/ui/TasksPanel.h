#pragma once

#include "core/index/IndexTypes.h"
#include "core/tasks/TaskEdit.h"

#include <QHash>
#include <QSet>
#include <QWidget>

class QLineEdit;
class QComboBox;
class QTreeWidget;
class QLabel;
class QTimer;
class QTreeWidgetItem;

namespace mn {
class VaultService;
}

namespace mn::ui {

// Every "- [ ]" task in the vault, with due dates, filtered by words, #tags
// (in the task or on its note) and folders. Ticking a task's box marks it done;
// the context menu and the Delete key remove it (the owner performs the edit).
// Double-click or Enter opens the task in its note.
class TasksPanel : public QWidget {
    Q_OBJECT
public:
    explicit TasksPanel(VaultService* vault, QWidget* parent = nullptr);
    // Re-reads the tasks from the index (cheap; done when the vault changes).
    void refresh();

Q_SIGNALS:
    void openRequested(const QString& path, int line, const QString& findText);
    void taskEditRequested(const mn::TaskInfo& task, mn::TaskAction action);

protected:
    void showEvent(QShowEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void applyFilter();
    TaskInfo taskOf(const QTreeWidgetItem* item) const;
    void requestDelete(QTreeWidgetItem* item);
    void contextMenu(const QPoint& pos);
    bool filling_ = false;
    QTreeWidgetItem* justToggled_ = nullptr;
    VaultService* vault_;
    QLineEdit* filter_;
    QComboBox* show_;
    QComboBox* sort_;
    QTreeWidget* list_;
    QLabel* status_;
    QTimer* debounce_;
    std::vector<TaskInfo> tasks_;
    QHash<QString, QSet<QString>> notesWithTag_; // tag -> notes having it (fetched on demand)
    bool stale_ = true;
    int request_ = 0;
};

} // namespace mn::ui
