#pragma once

#include "core/index/IndexTypes.h"

#include <QStyledItemDelegate>
#include <QWidget>

#include <functional>
#include <vector>

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
    // Adds a "Meaning" mode: passages found by keywords and by meaning (the assistant's
    // semantic search). The callback's error, if any, is shown (keyword results still come).
    using PassageSearch =
        std::function<void(const QString& query, std::function<void(std::vector<Passage>, QString error)>)>;
    void setSemanticSearch(PassageSearch search);

Q_SIGNALS:
    void openRequested(const QString& path, int line, const QString& findText);

private:
    void run();
    void showPassages();
    VaultService* vault_;
    QLineEdit* edit_;
    QComboBox* mode_; // whole notes, or passages (the part of a note about the query)
    QComboBox* sort_; // passages: best match, newest or oldest first
    QTreeWidget* results_;
    QLabel* status_;
    QTimer* debounce_;
    std::vector<Passage> passages_;
    PassageSearch semantic_;
    int request_ = 0;
};

// Backlinks and unlinked mentions for the open note.
class BacklinksPanel : public QWidget {
    Q_OBJECT
public:
    explicit BacklinksPanel(VaultService* vault, QWidget* parent = nullptr);
    void setNote(const QString& path);
    void refresh();
    // Looks for passages in other notes about the passage at `line` of the open note.
    void setCursorPassage(const QString& noteText, int line);

Q_SIGNALS:
    void openRequested(const QString& path, int line, const QString& findText);

private:
    void loadMentions();
    void showRelatedPassages();
    VaultService* vault_;
    QString path_;
    QTreeWidget* tree_;
    QTreeWidgetItem* linked_ = nullptr;
    QTreeWidgetItem* related_ = nullptr;
    QTreeWidgetItem* passages_ = nullptr;
    std::vector<Passage> relatedPassages_; // for path_
    int passageRequest_ = 0;
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
    // Pinned tags are listed first, in the order they were pinned (saved per vault).
    void setPinned(const QString& tag, bool pinned);
    void movePinned(const QString& tag, int delta);
    // Shows only tags containing `text` (and their parents); empty shows all.
    void setFilter(const QString& text);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

Q_SIGNALS:
    void openRequested(const QString& path, int line, const QString& findText);

private:
    void showMenu(const QPoint& pos);
    void savePinned(const QStringList& pinned);
    void showFiles(const QString& tag);
    void fillNotes();
    void applySort();
    void applyFilter();
    VaultService* vault_;
    QLineEdit* filter_;
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
