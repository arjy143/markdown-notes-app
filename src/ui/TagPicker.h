#pragma once

#include "core/index/IndexTypes.h"

#include <QStringList>
#include <QWidget>

#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;

namespace mn::ui {

// Pick a note's tags without editing frontmatter: the chosen tags are listed
// (click one to remove it), and a search box finds the vault's tags fuzzily.
// Tags that look the same as the search ("Projects" for "project") come first,
// so existing tags get reused rather than duplicated; a new tag is offered last.
// Enter adds the highlighted tag; Enter on an empty search emits submitted().
class TagPicker : public QWidget {
    Q_OBJECT
public:
    explicit TagPicker(QWidget* parent = nullptr);
    void setVaultTags(const std::vector<TagCount>& tags);
    void setTags(const QStringList& tags);
    const QStringList& tags() const { return tags_; }

    void addTag(const QString& tag); // uses the vault's spelling when it has the tag
    void removeTag(const QString& tag);

    QLineEdit* searchBox() const { return search_; }
    QListWidget* results() const { return results_; }
    QListWidget* chosen() const { return chosen_; }

Q_SIGNALS:
    void changed();
    void submitted();

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void refreshResults();
    void refreshChosen();
    void addCurrent();

    std::vector<TagCount> vault_;
    QStringList tags_;
    QListWidget* chosen_;
    QLabel* none_;
    QLineEdit* search_;
    QListWidget* results_;
};

} // namespace mn::ui
