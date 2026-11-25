#pragma once

#include "core/index/IndexTypes.h"

#include <QByteArray>
#include <QDialog>
#include <QList>
#include <QPair>
#include <QStringList>

#include <vector>

class QComboBox;
class QLineEdit;

namespace mn::ui {

class TagPicker;

// New note (Ctrl+N): a name, the note's tags (picked from the vault's tags), and,
// for vaults with category properties (vault settings: tag properties such as
// primary / secondary, with the category tags as their values), a dropdown per
// property. The choices are written as the note's frontmatter.
class NewNoteDialog : public QDialog {
    Q_OBJECT
public:
    NewNoteDialog(const QString& suggestedName, const QStringList& properties, const QStringList& categories,
                  const std::vector<TagCount>& vaultTags = {}, QWidget* parent = nullptr);
    QString name() const;
    QList<QPair<QString, QString>> values() const; // property, category; unset ones left out
    QByteArray content() const;                    // the new note's text: frontmatter, or empty

    QComboBox* combo(int i) const { return combos_.value(i); }
    TagPicker* tagPicker() const { return tags_; }

private:
    QLineEdit* name_;
    QStringList properties_;
    QList<QComboBox*> combos_;
    TagPicker* tags_;
};

} // namespace mn::ui
