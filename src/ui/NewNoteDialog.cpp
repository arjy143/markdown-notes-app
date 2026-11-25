#include "NewNoteDialog.h"

#include "TagPicker.h"
#include "core/ai/FrontmatterMerge.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

NewNoteDialog::NewNoteDialog(const QString& suggestedName, const QStringList& properties,
                             const QStringList& categories, const std::vector<TagCount>& vaultTags,
                             QWidget* parent)
    : QDialog(parent), properties_(properties)
{
    setWindowTitle(tr("New note"));
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(suggestedName, this);
    name_->selectAll();
    form->addRow(tr("Name:"), name_);
    for (const QString& p : properties) {
        auto* box = new QComboBox(this);
        box->addItem(tr("(none)"), QString());
        for (const QString& c : categories)
            box->addItem(c, c);
        form->addRow(p + u':', box);
        combos_.append(box);
    }
    // The next property starts from a different category than the ones already picked.
    for (qsizetype i = 0; i < combos_.size(); ++i) {
        connect(combos_[i], &QComboBox::currentIndexChanged, this, [this, i] {
            const QString v = combos_[i]->currentData().toString();
            for (qsizetype j = i + 1; j < combos_.size(); ++j) {
                if (!v.isEmpty() && combos_[j]->currentData().toString() == v) combos_[j]->setCurrentIndex(0);
            }
        });
    }
    lay->addLayout(form);
    tags_ = new TagPicker(this);
    tags_->setVaultTags(vaultTags);
    lay->addWidget(tags_, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    auto update = [this, buttons] { buttons->button(QDialogButtonBox::Ok)->setEnabled(!name().isEmpty()); };
    connect(name_, &QLineEdit::textChanged, this, update);
    update();
    // Enter on an empty tag search creates the note, as Enter in the name does.
    connect(tags_, &TagPicker::submitted, this, [this] {
        if (!name().isEmpty()) accept();
    });
    resize(440, 460);
}

QString NewNoteDialog::name() const
{
    QString n = name_->text().trimmed();
    if (n.endsWith(u".md"_s, Qt::CaseInsensitive)) n.chop(3);
    return n;
}

QList<QPair<QString, QString>> NewNoteDialog::values() const
{
    QList<QPair<QString, QString>> out;
    for (qsizetype i = 0; i < combos_.size(); ++i) {
        const QString v = combos_[i]->currentData().toString();
        if (!v.isEmpty()) out.append({properties_.value(i), v});
    }
    return out;
}

QByteArray NewNoteDialog::content() const
{
    FrontmatterPatch patch;
    patch.addIfMissing = values();
    patch.addTags = tags_->tags();
    const FrontmatterEdit e = planFrontmatterEdit(QString(), patch);
    return e.changed ? e.text.toUtf8() : QByteArray();
}

} // namespace mn::ui
