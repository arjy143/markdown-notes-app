#pragma once

#include "core/index/IndexTypes.h"

#include <QStyledItemDelegate>
#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QLabel;
class QTimer;
class QComboBox;

namespace mn {
class VaultService;
}

namespace mn::ui {

// Paints item text as rich text (for search snippets and backlink context).
class HtmlDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
};

// Full-text search (Ctrl+Shift+F), FTS5 on the worker thread.
class SearchPanel : public QWidget {
    Q_OBJECT
public:
    explicit SearchPanel(VaultService* vault, QWidget* parent = nullptr);
    void focusSearch(const QString& text = {});

Q_SIGNALS:
    void openRequested(const QString& path, int line, const QString& findText);

private:
    void run();
    VaultService* vault_;
    QLineEdit* edit_;
    QTreeWidget* results_;
    QLabel* status_;
    QTimer* debounce_;
    int request_ = 0;
};

// Backlinks and unlinked mentions for the open note.
class BacklinksPanel : public QWidget {
    Q_OBJECT
public:
    explicit BacklinksPanel(VaultService* vault, QWidget* parent = nullptr);
    void setNote(const QString& path);
    void refresh();

Q_SIGNALS:
    void openRequested(const QString& path, int line, const QString& findText);

private:
    void loadMentions();
    VaultService* vault_;
    QString path_;
    QTreeWidget* tree_;
    QTreeWidgetItem* linked_ = nullptr;
    QTreeWidgetItem* related_ = nullptr;
    QTreeWidgetItem* unlinked_ = nullptr;
    int request_ = 0;
    bool mentionsLoaded_ = false;
};

// Tag pane: nested tags with note counts; selecting a tag lists its notes.
class TagPanel : public QWidget {
    Q_OBJECT
public:
    explicit TagPanel(VaultService* vault, QWidget* parent = nullptr);
    void refresh();
    void selectTag(const QString& tag);

Q_SIGNALS:
    void openRequested(const QString& path, int line, const QString& findText);

private:
    void showFiles(const QString& tag);
    void fillNotes();
    void applySort();
    VaultService* vault_;
    QComboBox* sort_;
    QComboBox* grouping_;
    QTreeWidget* tree_;
    QTreeWidget* notes_; // notes under the selected tag: grouped by other tags, or a plain list
    QLabel* label_;
    QString current_;
    std::vector<NoteTags> noteTags_;
    bool noteTagsLoaded_ = false;
    int request_ = 0;
};

} // namespace mn::ui
