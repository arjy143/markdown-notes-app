#pragma once

#include "core/index/IndexTypes.h"
#include "core/settings/VaultSettings.h"

#include <QByteArray>
#include <QDialog>

#include <vector>

class QComboBox;
class QLineEdit;

namespace mn::ui {

class PropertiesForm;
class TagPicker;

// New note (Ctrl+N): a name and the note's properties, as a form built from the
// vault's property list. Dates start as today and a link on the clipboard as the
// first source; blank fields are written empty when the vault settings say so.
class NewNoteDialog : public QDialog {
    Q_OBJECT
public:
    NewNoteDialog(const QString& suggestedName, const VaultSettings& settings,
                  const std::vector<TagCount>& vaultTags = {}, QWidget* parent = nullptr);
    QString name() const;
    QByteArray content() const; // the new note's text: frontmatter, or empty

    PropertiesForm* form() const { return form_; }
    QComboBox* combo(int i) const; // the i-th category dropdown
    TagPicker* tagPicker() const;

private:
    QLineEdit* name_;
    PropertiesForm* form_;
    bool writeEmpty_;
};

} // namespace mn::ui
