#include "NewNoteDialog.h"

#include "PropertiesForm.h"
#include "TagPicker.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

NewNoteDialog::NewNoteDialog(const QString& suggestedName, const VaultSettings& settings,
                             const std::vector<TagCount>& vaultTags, QWidget* parent)
    : QDialog(parent), writeEmpty_(settings.writeEmptyProperties)
{
    setWindowTitle(tr("New note"));
    auto* lay = new QVBoxLayout(this);
    auto* top = new QFormLayout;
    name_ = new QLineEdit(suggestedName, this);
    name_->selectAll();
    top->addRow(tr("Name:"), name_);
    lay->addLayout(top);
    form_ = new PropertiesForm(settings, vaultTags, this);
    form_->prefillNew(QDate::currentDate(), QGuiApplication::clipboard()->text());
    lay->addWidget(form_, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    auto update = [this, buttons] { buttons->button(QDialogButtonBox::Ok)->setEnabled(!name().isEmpty()); };
    connect(name_, &QLineEdit::textChanged, this, update);
    update();
    // Enter on an empty tag search creates the note, as Enter in the name does.
    if (TagPicker* tags = form_->tagPicker()) {
        connect(tags, &TagPicker::submitted, this, [this] {
            if (!name().isEmpty()) accept();
        });
    }
    resize(520, sizeHint().height());
}

QString NewNoteDialog::name() const
{
    QString n = name_->text().trimmed();
    if (n.endsWith(u".md"_s, Qt::CaseInsensitive)) n.chop(3);
    return n;
}

QComboBox* NewNoteDialog::combo(int i) const
{
    return form_->categoryCombo(i);
}

TagPicker* NewNoteDialog::tagPicker() const
{
    return form_->tagPicker();
}

QByteArray NewNoteDialog::content() const
{
    const FrontmatterPatch patch = form_->patch(Frontmatter{}, writeEmpty_);
    const FrontmatterEdit e = planFrontmatterEdit(QString(), patch);
    return e.changed ? e.text.toUtf8() : QByteArray();
}

} // namespace mn::ui
