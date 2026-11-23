#pragma once

#include "core/index/IndexTypes.h"

#include <QDialog>

#include <vector>

class QLineEdit;
class QListView;
class QLabel;

namespace mn::ui {

// Fuzzy matching used by the quick switcher. Returns a score (higher is
// better) or -1 if `query` (case-folded) isn't a subsequence of `candidate`.
int fuzzyScore(QStringView queryFolded, QStringView candidateFolded, int nameStart);

class SwitcherModel;

// Ctrl+O: jump to a note by name, alias or path. Enter opens; Shift+Enter
// creates a note with the typed name.
class QuickSwitcher : public QDialog {
    Q_OBJECT
public:
    explicit QuickSwitcher(QWidget* parent = nullptr);

    // Marks the file list stale; it's rebuilt from `snapshot` the next time the switcher opens.
    void setFiles(const VaultSnapshot& snapshot);
    void popup();
    // Exposed for benchmarks: filters and returns the number of matches.
    int filterFor(const QString& query);

Q_SIGNALS:
    void openRequested(const QString& path);
    void createRequested(const QString& name);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void accept(bool create);
    void rebuild();
    const VaultSnapshot* snapshot_ = nullptr;
    bool stale_ = true;

    QLineEdit* edit_;
    QListView* list_;
    QLabel* hint_;
    SwitcherModel* model_;
};

} // namespace mn::ui
