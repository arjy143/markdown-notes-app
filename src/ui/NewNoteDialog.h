#pragma once

#include <QByteArray>
#include <QDialog>
#include <QList>
#include <QPair>
#include <QStringList>

class QComboBox;
class QLineEdit;

namespace mn::ui {

// New note, for vaults with category properties (vault settings: tag properties such as
// primary / secondary, with the category tags as their values): a name plus a dropdown
// per property, written as the note's frontmatter.
class NewNoteDialog : public QDialog {
    Q_OBJECT
public:
    NewNoteDialog(const QString& suggestedName, const QStringList& properties, const QStringList& categories,
                  QWidget* parent = nullptr);
    QString name() const;
    QList<QPair<QString, QString>> values() const; // property, category; unset ones left out
    QByteArray content() const;                    // the new note's text: frontmatter, or empty

    QComboBox* combo(int i) const { return combos_.value(i); }

private:
    QLineEdit* name_;
    QStringList properties_;
    QList<QComboBox*> combos_;
};

} // namespace mn::ui
