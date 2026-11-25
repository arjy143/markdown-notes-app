#pragma once

#include <QDialog>

class QLineEdit;
class QMenuBar;
class QTreeWidget;

namespace mn::ui {

// Help › Keyboard shortcuts: every menu shortcut (read from the menus, so it's
// always current) plus the keys of the editor, quick capture and drawing.
class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(const QMenuBar* menus, QWidget* parent = nullptr);
    QTreeWidget* list() const { return list_; }
    QLineEdit* filter() const { return filter_; }

private:
    void applyFilter();
    QTreeWidget* list_;
    QLineEdit* filter_;
};

} // namespace mn::ui
