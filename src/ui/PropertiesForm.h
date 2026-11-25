#pragma once

#include "core/ai/FrontmatterMerge.h"
#include "core/index/IndexTypes.h"
#include "core/parse/Frontmatter.h"
#include "core/settings/VaultSettings.h"

#include <QDate>
#include <QDialog>
#include <QHash>
#include <QWidget>

#include <functional>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;

namespace mn::ui {

class TagPicker;

// The note's properties as a form, built from the vault's property list: dropdowns
// for categories and choices (status), a tag picker, text, one-per-line lists and
// dates. Only what changed is written back, and properties not in the list are
// never touched. A value written in a form the merge can't edit (a nested or
// multi-line value) shows read-only.
class PropertiesForm : public QWidget {
    Q_OBJECT
public:
    PropertiesForm(const VaultSettings& settings, const std::vector<TagCount>& vaultTags,
                   QWidget* parent = nullptr);

    void load(const Frontmatter& fm); // an existing note's values
    // A new note: dates are today, and a link on the clipboard becomes the first source.
    void prefillNew(const QDate& today, const QString& clipboardText);
    // Fills empty fields from the assistant (categories only from the known choices).
    void applySuggestion(const QList<QPair<QString, QString>>& properties, const QStringList& tags);

    // The edit for a note whose frontmatter is `current` (empty for a new note).
    // With `writeEmpty`, properties left blank are written empty.
    FrontmatterPatch patch(const Frontmatter& current, bool writeEmpty) const;
    bool hasChanges(const Frontmatter& current) const { return !isEmpty(patch(current, false)); }
    static bool isEmpty(const FrontmatterPatch& p);

    TagPicker* tagPicker() const { return tags_; }
    QComboBox* categoryCombo(int i) const { return categories_.value(i); }
    QWidget* field(const QString& key) const { return fields_.value(key); }
    QString text(const QString& key) const; // the field's value as it would be written (lists: one per line)
    void setText(const QString& key, const QString& value);

private:
    struct Field {
        PropertySpec spec;
        QWidget* widget = nullptr;
    };
    QStringList values(const Field& f) const;
    void syncCategoryTag(QComboBox* box, const QString& before);

    QList<Field> specs_;
    QHash<QString, QWidget*> fields_;
    QList<QComboBox*> categories_;
    TagPicker* tags_ = nullptr;
    QString tagsKey_;
    bool categoriesInTags_ = true;
    bool loading_ = false;
};

// Ctrl+T: the open note's properties. With an assistant, Suggest fills empty fields.
class PropertiesDialog : public QDialog {
    Q_OBJECT
public:
    using Suggest = std::function<void(std::function<void(const QList<QPair<QString, QString>>&,
                                                          const QStringList&, const QString& error)>)>;
    PropertiesDialog(const QString& noteName, const Frontmatter& fm, const VaultSettings& settings,
                     const std::vector<TagCount>& vaultTags, Suggest suggest = {}, QWidget* parent = nullptr);
    PropertiesForm* form() const { return form_; }
    void suggest(); // asks the assistant (when there is one)

private:
    PropertiesForm* form_;
    QPushButton* suggestButton_ = nullptr;
    QLabel* status_;
    Suggest suggest_;
};

} // namespace mn::ui
